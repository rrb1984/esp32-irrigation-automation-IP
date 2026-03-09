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
#include "prefs.h"
#include "sensors.h"
#include "rtc.h"
#include "logging.h"
#include "mqtt.h"
#include "relay.h"

uint16_t pinstate = 0;
// store time pin was last triggered
/// used for auto-stop and blocking of relays
uint32_t pintime[] = {0, 0, 0, 0};

// define pins configure as relay control port as
// output set them high since relay are active low
// pinmap[0] is used for digitalWrite, pinmap[1] for checking if relay is on and pinmap[2] for checking if relay is blocked by previous petition
uint16_t pinmap[][3] = {
    {RELAY1_PIN, 0x0001, 0x0002},
    {RELAY2_PIN, 0x0004, 0x0008},
    {RELAY3_PIN, 0x0010, 0x0020},
    {RELAY4_PIN, 0x0040, 0x0080}};

char pinnames[NUM_RELAY][7] = {
    RELAY1_LABEL,
    RELAY2_LABEL,
    RELAY3_LABEL,
    RELAY4_LABEL};

/**
 * @brief
 * define pins configure as relay control port as
 * output set them high since relay are active low
 * needs to be called at startup before any other function that relies on relay states
 */
void initRelays()
{
    for (uint8_t i = 0; i < NUM_RELAY; i++)
    {
        pinMode(switchesPrefs.pinRelay[i], OUTPUT);
        digitalWrite(switchesPrefs.pinRelay[i], 1);
    }
}

/**
 * @brief Set the Relay object
 *  turn on or off relay depending on petition bool, also checks if relay is blocked by previous petition and logs action
 * @param num relay number, starting with 0 for first relay
 * @param on true to turn on relay, false to turn off relay
 *
 */
void setRelay(uint8_t num, bool on)
{
    char logmsg[32];

    if (on) //   turn on relay
    {
        if ((pinstate & pinmap[num][2]) == 0) // check if relay is blocked by previous petition, if not turn on relay, update pinstate and log action
        {
            if ((pinstate & pinmap[num][1]) == 0) // check if relay is already on, if not turn on relay, update pinstate and log action
            {
                digitalWrite(pinmap[num][0], LOW); // active -> low ( inverted logic)
                pintime[num] = getLocalTime();     // store time relay was turned on for auto-stop and blocking of relays
                pinstate |= pinmap[num][1];        // update pinstate to indicate relay is on
                pinstate &= ~pinmap[num][2];       // update pinstate to indicate relay is not blocked
                Serial.print(millis());
                Serial.printf(": Opened %s\n", pinnames[num]);
                sprintf(logmsg, "%s on, water %dcm", pinnames[num], sensors.waterLevel);
                logMsg(logmsg);
            }
        }
        else //
        {
            Serial.print(millis());
            Serial.printf(": Relay %s blocked!\n", pinnames[num]);
            return;
        }
    }
    else // turn off relay
    {
        if ((pinstate & pinmap[num][1]) != 0) // check if relay is on, if it is turn off relay, update pinstate and log action
        {
            digitalWrite(pinmap[num][0], HIGH); // inactive -> high ( inverted logic)
            pintime[num] = getLocalTime();
            pinstate &= ~pinmap[num][1];
            pinstate |= pinmap[num][2];
            Serial.print(millis());
            Serial.printf(": Closed %s\n", pinnames[num]);
            sprintf(logmsg, "%s off, water %dcm", pinnames[num], sensors.waterLevel);
            logMsg(logmsg);
        }
    }
}

/**
 * @brief
 * check if any relay can be unblocked based on configured block time ( switchesPrefs.relaysBlockMins ) to avoid accidental overwatering, needs to be called regularly in loop
 */
void unblockRelays()
{
    uint32_t blockTimeSecs;

    for (uint8_t i = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++)
    {
        blockTimeSecs = getLocalTime() - pintime[i];
        if ((pinstate & pinmap[i][2]) != 0 && blockTimeSecs > (switchesPrefs.relaysBlockMins * 60))
        {
            pinstate &= ~pinmap[i][2];
        }
    }
}

/**
 * @brief
 * prevent water pump from running dry, turn off pump automatically after configured auto stop timeout or if water level reaches lower limit, needs to be called regularly in loop
 */
void pumpAutoStop()
{
    static char logmsg[48];
    bool pumpoff = false;

#if defined(US_TRIGGER_PIN) && defined(US_ECHO_PIN) // only check water level if ultrasonic sensor is configured
    for (uint8_t i = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++)
    { // update level reading regularly if pump is running
        if ((pinstate & pinmap[i][1]) != 0)
        { // check if pump is on and update water level reading if it is
            readWaterLevel(false, false);
        }
        if ((sensors.waterLevel > switchesPrefs.minWaterLevel || switchesPrefs.ignoreWaterLevel) && (pinstate & pinmap[i][2]) != 0) // unblock pump if water level is known or deliberately ignored
        {
            pinstate &= ~pinmap[i][2];
            Serial.print(millis());
            Serial.printf(": Pump unblocked (%swater level %d cm)\n",
                          switchesPrefs.ignoreWaterLevel ? "ignoring " : "", sensors.waterLevel);
            sprintf(logmsg, "pump unblocked, %swater %dcm",
                    switchesPrefs.ignoreWaterLevel ? "ignoring " : "", sensors.waterLevel);
            logMsg(logmsg);
            // turn off and block pump and valves if water level is unknown due to sensor error
        }
        else if (sensors.waterLevel <= switchesPrefs.minWaterLevel &&
                 !switchesPrefs.ignoreWaterLevel && (pinstate & pinmap[i][2]) == 0) // check if water level is below configured minimum and not deliberately ignored and pump is not already blocked, if so turn off and block pump and valves to prevent running dry and log action
        {
            Serial.print(millis());
            if (sensors.waterLevel <= 0)
            {
                Serial.println(F(": WARNING: System blocked (unknown water level)"));
                logMsg("system blocked, unknown water level");
            }
            else
            {
                Serial.printf(": WARNING: Low water level %d cm\n", sensors.waterLevel);
                sprintf(logmsg, "low water, %dcm", sensors.waterLevel);
                logMsg(logmsg);
            }
            pumpoff = true;
            for (uint8_t j = 0; j < (sizeof(pinmap) / sizeof(pinmap[0])); j++)
                pinstate |= pinmap[j][2]; // block all relay
        }
    }
#endif

    // turn off pump if auto-stop time has been reached
    // time limit is checked to avoid accidental overwatering
    for (uint8_t i = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++)
    {
        if ((pinstate & pinmap[i][1]) != 0)
        {
            if ((getLocalTime() - pintime[i]) > switchesPrefs.pumpAutoStopSecs)
            {
                setRelay(i, false);
                Serial.print(millis());
                Serial.printf(": Pump autostop, %d secs\n", switchesPrefs.pumpAutoStopSecs);
                sprintf(logmsg, "pump autostop, %d secs", switchesPrefs.pumpAutoStopSecs);
                logMsg(logmsg);
                if (i == ((sizeof(pinmap) / sizeof(pinmap[0])) - 1))
                {
                    readMoisture(true, true, false);
                    mqtt_send(MQTT_TIMEOUT_MS);
                }
            }
        }
    }
}

// return current relay/pump status as json string
uint16_t relayStatus(char *buf, size_t s)
{
    JsonDocument JSON;
    char key[8];
    // applied changes to keep javascript correctly updated in the web ui
    for (uint8_t i = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++)
    {
        sprintf(key, "valve%d", i + 1);
        if (switchesPrefs.pinRelay[i] < 0)
            JSON[key] = -1; // disabled
        else if ((pinstate & pinmap[i][2]) != 0)
            JSON[key] = 2; // blocked
        else if ((pinstate & pinmap[i][1]) != 0)
            JSON[key] = 1; // on
        else
            JSON[key] = 0; // off
    }
    return serializeJson(JSON, buf, s);
}
