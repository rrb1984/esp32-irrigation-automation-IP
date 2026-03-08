
/***************************************************************************
  Copyright (c) 2021-2022 Lars Wessels

  This file a part of the "ESP32-Irrigation-Automation" source code.
  https://github.com/lrswss/esp32-irrigation-automation

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

***************************************************************************/

#include "config.h"
#include "logging.h"
#include "wlan.h"
#include "utils.h"
#include "rtc.h"
#include "mqtt.h"
#include "prefs.h"
#include "sensors.h"
#include "relay.h"
#include "web.h"
#include "scheduler.h"

void setup()
{
    char logmsg[96]; // for logging startup info, needs to be large enough to hold firmware update info if available

#ifdef WOKWI_WEB
    /**
     * From this moment on, the task must periodically call `esp_task_wdt_reset()`
     * (sometimes referred to as "feed the dog") before `timeout_ms` expires.
     * If it fails to do so (due to blocking, a loop without yielding the CPU,
     * a deadlock, etc.), the WDT will expire and act according to the configuration
     * (`trigger_panic`, reset, log, etc.).
     */
    esp_task_wdt_init(&WDT_CONFIG); // init watchdog with 90 sec. timeout
    esp_task_wdt_add(NULL);         // add current task to WDT
#endif                              // WOKWI_WEB
#ifndef WOKWI_WEB

    esp_task_wdt_init(90, true); // init watchdog with 90 sec. timeout
    esp_task_wdt_add(NULL);      // add current task to WDT

#endif // WOKWI_WEB

    btStop();     // stop bluetooth to save power and avoid potential interference with wifi
    initPrefs();  // load preferences from flash, needs to be called before any other init function that relies on prefs
    initRelays(); // init relay pins, needs to be called before any other function that relies on relay states
    Serial.begin(115200);
    delay(500); // wait for serial to initialize before printing startup info

    Serial.println();
    Serial.printf("%s (v%d)\n", "ESP32-Irrigation-Automation", FIRMWARE_VERSION);
    runmode = bootMsg(); // determine reset reason and log it, also checks for firmware update and clears prefs if needed

    // normal power or full system reset
    if (runmode >= POWERUP)
    {
        sprintf(logmsg, "start,%s,v%d", runmodes[runmode], FIRMWARE_VERSION); // log reset reason and firmware version
        strcat(logmsg, checkFirmwareUpdate());                                // check for firmware update and append info to log message if available, also clears prefs if needed
        restorePrefs();                                                       // restore preferences from flash if not cleared by firmware update, needs to be called after checkFirmwareUpdate() because it may clear prefs if firmware was updated

        // soft restart triggered by external reset button
    }
    else if (runmode == RESTART)
    {
        sprintf(logmsg, "restart,%s,v%d", runmodes[runmode], FIRMWARE_VERSION);
    }

    initLogging();  // init logging (after prefs have been restored and potential firmware update has been checked)
    logMsg(logmsg); // log startup info, needs to be called after initLogging() and before any other function that relies on logging

    initSensors(); // init sensors, needs to be called before any other function that relies on sensor readings
#ifdef HAS_HTU21D
    readTemp(true, true);
#endif
#ifdef HAS_DHT122
    readTemp(true, true);
#endif
#if defined(US_TRIGGER_PIN) && defined(US_ECHO_PIN) // if ultrasonic sensor is enabled, read water level once at startup to initialize moving average and log initial value
    readWaterLevel(true, true);
#endif
    readMoisture(true, true, true); // read moisture sensors once at startup to initialize moving average and log initial value, also resets moisture sensor if enabled

    if (!wifi_uplink(true)) // try to connect to wifi, if it fails start local AP for configuration
        wifi_hotspot(true);
    else
        startNTPSync(); // sync time with NTP server, needs to be called after wifi is connected

    webserver_start(); // start webserver, needs to be called after wifi is connected and time is synced for correct timestamps in logs and mqtt messages

#ifdef DEBUG_MEMORY
    free_heap(); // print free heap at startup for debugging purposes
#endif
}

void loop()
{
    static uint32_t prevLoopTimer = 0;                      // for timing of tasks in loop
    static uint32_t prevMqttPublish = 0;                    // for timing of mqtt publish intervals
    static uint32_t wifiRetry = WIFI_STA_RECONNECT_TIMEOUT; // for timing of wifi reconnect attempts
    static uint16_t wifiOffline = 0;                        // for timing of wifi offline duration, used to trigger local AP if wifi is down for too long
    uint32_t scheduler_start = 0;                           // for timing of irrigation scheduler, needs to be initialized here to avoid blocking of loop if scheduler is running
    static uint32_t nextCheckTime[NUM_RELAY] = {0};         // for timing of soil moisture checks for irrigation scheduler, needs to be initialized here to avoid blocking of loop if scheduler is running
    static uint32_t lastDailyTriggerDay = 0;                // for timing of daily trigger scheduler, ensures that daily triggers are only triggered once per day
    static uint32_t lastMaintenanceDay = 0;                 // for timing of daily maintenance tasks, ensures that maintenance tasks are only triggered once per day
    // static uint32_t lastIrrigationDay = 0;  for timing of daily irrigation scheduler, ensures that irrigation scheduler is only triggered once per day

    uint32_t currentDay = getLocalTime() / 86400; // Get the current day as the number of days since the Unix epoch (January 1, 1970)

#ifdef DEBUG_MEMORY
    static char logmsg[32];
#endif
    /***
     * Slow tasks that don't need to run on every loop iteration are triggered based on timers to avoid blocking the loop and ensure responsiveness of the system.
     */
    if (millis() - prevLoopTimer >= 1000)
    {
        prevLoopTimer = millis();
        busyTime += 1; // increment busy time counter every second, used for timing of tasks in loop and to trigger certain actions after a certain amount of time has passed

        if (!wifi_uplink(false)) // check for wifi uplink, try to reconnect every 60 secs.
        {
            if (!wifiRetry || wifiRetry <= busyTime) // if wifi is down and retry timer has expired, try to reconnect
            {
                wifiRetry = busyTime + WIFI_STA_RECONNECT_TIMEOUT;
                if (wifi_uplink(true))
                    startNTPSync();
            }
            else if (!(busyTime % 10)) // if wifi is down, increment offline timer every 10 secs. and log it
            {
                wifiOffline += 10;
                Serial.print(millis());
                Serial.print(F(": WiFi: Uplink down, next retry in "));
                Serial.print(wifiRetry - busyTime);
                Serial.println(F(" seconds."));

                // Switch on local AP as fallback if wifi
                // is down for a longer period of time
                if (wifiOffline >= WIFI_AP_FALLBACK_TIMEOUT)
                    wifi_hotspot(true);
            }
        }
        else // if wifi is up, reset wifi retry and offline timers and perform tasks that require wifi connection
        {
            wifiRetry = 0;   // reset wifi retry timer if wifi is up
            wifiOffline = 0; // reset wifi offline timer if wifi is up

            // check for remote commands recieved via MQTT and trigger actions accordingly
            if (mqtt_connect(MQTT_TIMEOUT_MS))
                mqtt.loop();

            // publish current sensor readings
            if (millis() - prevMqttPublish >= (generalPrefs.mqttPushInterval * 1000))
            {
                prevMqttPublish = millis();
                mqtt_send(MQTT_TIMEOUT_MS);
            }

            // retry ntp sync every minute if time is not set
            if (getLocalTime() < 1609455600 && !(busyTime % 60)) // if time is not set (before 01.01.2021) and busyTime is a multiple of 60, try to sync time with NTP server
                startNTPSync();

            if (!(busyTime % 3600)) // log sensor readings every hour and update moving average for moisture sensors, also resets moisture sensor if enabled
            {
                readTemp(true, true);
                readWaterLevel(true, true);
                readMoisture(true, true, false);
            }

            // sync RTC, check for log rotation

            if (!strcmp(MAINTENANCE_TIME, getTimeString(false)) && currentDay != lastMaintenanceDay) // if it's time for daily maintenance tasks and they haven't been performed yet today, perform maintenance tasks
            {
                startNTPSync();                  // sync time with NTP server at maintenance time to ensure correct timestamps in logs and mqtt messages
                rotateLogs();                    // rotate logs at maintenance time to avoid doing it at startup when there might be a lot of log entries to rotate and to ensure that logs are rotated at least once a day
                lastMaintenanceDay = currentDay; // Update the last maintenance day to the current day
            }
        }

        if (!(busyTime % 5) && busyTime >= AP_TIMEOUT_SECS && wifi_uplink(false)) // stop local AP after timeout if connection to wifi is available
            wifi_hotspot(false);

        if (!(busyTime % MOISTURE_MA_WINDOW_TIME)) // update moving average for moisture sensors at defined intervals to ensure that the moving average is updated even if there are no changes in soil moisture that would trigger an update, also resets moisture sensor if enabled
        {
            readTemp(true, false);
            readWaterLevel(true, false);
            readMoisture(true, false, false); // updates moving avg if enabled
        }

        /**
         * @brief
         * SMART IRRIGATION
         * This is a simple implementation of a smart irrigation system that triggers irrigation based on soil moisture levels and a defined schedule. The system checks if auto irrigation is enabled and if there are no jobs currently scheduled. If the current time matches the defined auto irrigation time, it schedules irrigation jobs for each valve based on the configured runtime and pause hours to avoid overwatering. The system also checks the soil moisture levels for each valve and only schedules irrigation if the moisture level is below the defined threshold for that valve.
         */

        if (!switchesPrefs.enableAutoIrrigation && !strcmp(switchesPrefs.autoIrrigationTime, getTimeString(false)) && currentDay != lastDailyTriggerDay) // if auto irrigation is disabled, it's time for daily trigger and it hasn't been triggered yet today, check soil moisture levels and schedule irrigation jobs for valves that need watering based on configured runtime and pause hours to avoid overwatering
        {
            for (uint8_t i = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++) // loop through each valve and set next check time for soil moisture levels to current time to trigger immediate check and potential irrigation for valves that need watering based on configured runtime and pause hours to avoid overwatering, also ensures that daily trigger is only triggered once per day
            {
                nextCheckTime[i] = getLocalTime();
            }
            lastDailyTriggerDay = currentDay; // Update the last daily trigger day to the current day to ensure that the daily trigger is only triggered once per day
        }

        if (!switchesPrefs.enableAutoIrrigation && !jobs_scheduled()) // if auto irrigation is disabled, no jobs are currently scheduled and it's time for auto irrigation, check soil moisture levels and schedule irrigation jobs for valves that need watering based on configured runtime and pause hours to avoid overwatering
        {
            scheduler_start = millis();

            for (uint8_t i = 0, j = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++) // loop through each valve
            {
                if (nextCheckTime[i] > 0 && getLocalTime() >= nextCheckTime[i]) // if it's time to check soil moisture levels for valve 'i'
                {
                    if (sensors.moisture[i] < switchesPrefs.MoistureValueThreshold[i]) // if soil moisture level for valve 'i' is below defined threshold, schedule irrigation jobs for valve 'i' based on configured runtime and pause hours to avoid overwatering
                    {
                        if (switchesPrefs.autoIrrigationSecs[i] > 0) // if runtime is configured for valve 'i'
                        {
                            schedule_job(&valvejobs[j], (scheduler_start + ((i + 1) * 1000)), setRelay, i, true);
                            schedule_job(&valvejobs[j + 1], (scheduler_start + ((i + 1) + switchesPrefs.autoIrrigationSecs[i]) * 1000), setRelay, i, false);
                            scheduler_start = scheduler_start + (((i + 1) + switchesPrefs.autoIrrigationSecs[i]) * 1000) + 5000;
                            j = j + 2;
                        }
                    }
                    nextCheckTime[i] = getLocalTime() + (switchesPrefs.autoIrrigationPauseHours * 3600); // update next check time for valve 'i' to current time plus configured pause hours to avoid overwatering
                }
            }
        }

        unblockRelays();
        pumpAutoStop();

#ifdef DEBUG_MEMORY
        if (!(busyTime % 300))
            free_heap();
#endif
    }

    webserver.handleClient(); // handle webserver requests
    scheduler();              // trigger scheduled jobs
    esp_task_wdt_reset();     // feed the dog...
}
