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

#include "web.h"
#include "config.h"
#include "logging.h"
#include "utils.h"
#include "rtc.h"
#include "wlan.h"
#include "relay.h"
#include "mqtt.h"
#include "sensors.h"
#include "prefs.h"

static uint32_t webserverRequestMillis = 0;
static uint16_t webserverTimeout = 0;

WebServer webserver(80);

#ifdef LANG_DE
static const char *UI_LANG_SUFFIX = "_de";
#else
static const char *UI_LANG_SUFFIX = "_en";
#endif

static bool streamUiPage(const char *pageName)
{
    String path = "/";
    path += pageName;
    path += UI_LANG_SUFFIX;
    path += ".html";

    String gzPath = path + ".gz";
    if (LittleFS.exists(gzPath))
    {
        File file = LittleFS.open(gzPath, "r");
        if (file && !file.isDirectory())
        {
            // streamFile auto-adds Content-Encoding: gzip for .gz filenames
            webserver.sendHeader("Cache-Control", "no-cache");
            webserver.streamFile(file, "text/html");
            file.close();
            return true;
        }
    }

    File file = LittleFS.open(path, "r");
    if (file && !file.isDirectory())
    {
        webserver.sendHeader("Cache-Control", "no-cache");
        webserver.streamFile(file, "text/html");
        file.close();
        return true;
    }

    return false;
}

static void updateConfig()
{
    static char buf[2048], key[24];
    JsonDocument JSON;

    memset(buf, 0, sizeof(buf));
    JSON.clear();

    JSON["system_id"] = systemID();
    JSON["water_reservoir_height"] = switchesPrefs.waterReservoirHeight;
    JSON["min_water_level"] = switchesPrefs.minWaterLevel;

    for (uint8_t i = 1; i <= NUM_RELAY; i++)
    {
        sprintf(key, "relay%d_label", i);
        JSON[key] = switchesPrefs.labelRelay[i - 1];
        sprintf(key, "relay%d_pin", i);
        JSON[key] = switchesPrefs.pinRelay[i - 1];
        sprintf(key, "irrigation_relay%d_secs", i);
        JSON[key] = switchesPrefs.autoIrrigationSecs[i - 1];
    }

    for (uint8_t i = 1; i <= NUM_MOISTURE_SENSORS; i++)
    {
        sprintf(key, "moist%d_label", i);
        JSON[key] = switchesPrefs.labelMoisture[i - 1];
        sprintf(key, "moist%d_pin", i);
        JSON[key] = switchesPrefs.pinMoisture[i - 1];
    }

    JSON["relay_pins_csv"] = RELAY_PINS;
    JSON["moisture_pins_csv"] = MOISTURE_PINS;
    JSON["moisture_min"] = switchesPrefs.moistureMin;
    JSON["moisture_max"] = switchesPrefs.moistureMax;
    JSON["moisture_raw"] = switchesPrefs.moistureRaw ? 1 : 0;
    JSON["moisture_avg"] = switchesPrefs.moistureMovingAvg ? 1 : 0;

    JSON["auto_irrigation"] = switchesPrefs.enableAutoIrrigation ? 1 : 0;
    JSON["irrigation_time"] = switchesPrefs.autoIrrigationTime;
    JSON["irrigation_pause"] = switchesPrefs.autoIrrigationPauseHours;
    JSON["pump_autostop"] = switchesPrefs.pumpAutoStopSecs;
    JSON["pump_blocktime"] = switchesPrefs.relaysBlockMins;
    JSON["reservoir_height"] = switchesPrefs.waterReservoirHeight;
    JSON["ignore_water_level"] = switchesPrefs.ignoreWaterLevel ? 1 : 0;
    JSON["logging"] = switchesPrefs.enableLogging ? 1 : 0;

    JSON["stassid"] = generalPrefs.wifiStaSSID;
    JSON["stapassword"] = generalPrefs.wifiStaPassword;
    JSON["appassword"] = generalPrefs.wifiApPassword;
    JSON["mqtt"] = generalPrefs.enableMQTT ? 1 : 0;
    JSON["mqttbroker"] = generalPrefs.mqttBroker;
    JSON["mqttport"] = generalPrefs.mqttPort;
    JSON["mqtttopiccmd"] = generalPrefs.mqttTopicCmd;
    JSON["mqtttopicstate"] = generalPrefs.mqttTopicState;
    JSON["mqttinterval"] = generalPrefs.mqttPushInterval;
    JSON["mqttusetls"] = generalPrefs.mqttUseTLS ? 1 : 0;
    JSON["mqttauth"] = generalPrefs.mqttEnableAuth ? 1 : 0;
    JSON["mqttuser"] = generalPrefs.mqttUsername;
    JSON["mqttpassword"] = generalPrefs.mqttPassword;
    JSON["ubidots"] = generalPrefs.mqttUbidotsStemCompat ? 1 : 0;
    JSON["ubidots_label"] = generalPrefs.mqttUbidotsDeviceLabel;

    JSON["firmware"] = FIRMWARE_VERSION;
    JSON["build"] = String(__DATE__) + " " + String(__TIME__);

    if (serializeJson(JSON, buf) > 16)
        webserver.send(200, F("application/json"), buf);
    else
        webserver.send(500, "text/plain", "ERR");
}

// pass sensor readings, system status to web ui as JSON
static void updateUI()
{
    static char buf[192], label[16];
    JsonDocument JSON;

    memset(buf, 0, sizeof(buf));
    JSON.clear();
    JSON["logging"] = switchesPrefs.enableLogging ? 1 : 0;
    if (getLocalTime() > 1609455600)
    { // RTC already set?
        JSON["date"] = getDateString();
        JSON["time"] = getTimeString(false);
        JSON["tz"] = getTimeZone();
    }
    JSON["runtime"] = getRuntime(busyTime);
    JSON["wifi"] = wifi_uplink(false) ? 1 : 0;
#if defined(HAS_HTU21D) || defined(HAS_DHT122)
    char temp[8];
    if (sensors.humidity > 0)
    {
        dtostrf(sensors.temperature, 4, 1, temp);
        JSON["temp"] = temp;
        JSON["hum"] = sensors.humidity;
    }
#endif
#if defined(US_TRIGGER_PIN) && defined(US_ECHO_PIN)
    if (!switchesPrefs.ignoreWaterLevel)
        JSON["level"] = sensors.waterLevel;
    else
        JSON["level"] = -2;
#endif
    for (uint8_t i = 0; i < NUM_MOISTURE_SENSORS; i++)
    {
        if (switchesPrefs.pinMoisture[i] > 0)
        {
            sprintf(label, "moist%d", i + 1);
            JSON[label] = sensors.moisture[i];
        }
    }
#ifdef DEBUG_MEMORY
    JSON["heap"] = ESP.getFreeHeap();
#endif

    if (serializeJson(JSON, buf) > 16)
        webserver.send(200, F("application/json"), buf);
    else
        webserver.send(500, "text/plan", "ERR");
}

void webserver_start()
{
    // send main page
    webserver.on("/", HTTP_GET, []()
                 {
        if (!streamUiPage("index")) {
            webserver.send(500, "text/plain", "UI file not found");
        } });

    // static configuration for ui pages
    webserver.on("/cfg", HTTP_GET, updateConfig);

    // AJAX request from main page to update readings
    webserver.on("/ui", HTTP_GET, updateUI);

#ifndef ENABLE_AUTO_IRRIGRATION_SCHEDULER
    // set/check valves -1 value
    webserver.on("/valve", HTTP_GET, []()
                 {
                     char reply[256];
                     if (webserver.arg("on").toInt() >= 1 && webserver.arg("on").toInt() <= 4)
                     {
                         setRelay(webserver.arg("on").toInt() - 1, true);
                     }
                     else if (webserver.arg("off").toInt() >= 1 && webserver.arg("off").toInt() <= 4)
                     {
                         setRelay(webserver.arg("off").toInt() - 1, false);
                     }
                     if (relayStatus(reply, sizeof(reply)) > 0)
                     {
                         webserver.send(200, F("application/json"), reply);
                     }
                     else
                     {
                         webserver.send(500, "text/plain", "ERR");
                     }
                     if (webserver.arg("on").toInt() > 0 || webserver.arg("off").toInt() > 0)
                         publishMqttStatus(MQTT_TIMEOUT_MS); // publish changed relay settings
                 });
#else
    // set/check valves -1 value
    webserver.on("/valve", HTTP_GET, []()
                 {
                     char reply[256];
                     if (webserver.arg("on").toInt() >= 1 && webserver.arg("on").toInt() <= 4)
                     {
                         setRelay(webserver.arg("on").toInt(), true);
                     }
                     else if (webserver.arg("off").toInt() >= 1 && webserver.arg("off").toInt() <= 4)
                     {
                         setRelay(webserver.arg("off").toInt(), false);
                     }
                     if (relayStatus(reply, sizeof(reply)) > 0)
                     {
                         webserver.send(200, F("application/json"), reply);
                     }
                     else
                     {
                         webserver.send(500, "text/plain", "ERR");
                     }
                     if (webserver.arg("on").toInt() > 0 || webserver.arg("off").toInt() > 0)
                         publishMqttStatus(MQTT_TIMEOUT_MS); // publish changed relay settings
                 });
#endif
    // return log files as JSON for SPA
    webserver.on("/logs", HTTP_GET, []()
                 {
        if (webserver.arg("list") == "1") {
            static char buf[2048];
            JsonDocument JSON;
            memset(buf, 0, sizeof(buf));
            JSON.clear();

            uint32_t freeBytes = LittleFS.totalBytes() * 0.95 - LittleFS.usedBytes();
            JSON["free_kb"] = freeBytes / 1024;

            JsonArray files = JSON.createNestedArray("files");
            File dir = LittleFS.open("/");
            File file = dir.openNextFile();
            while (file)
            {
                if (!file.isDirectory() && file.name() != NULL)
                {
                    String fname = String(file.name());
                    bool isLog = fname.endsWith(".log") || fname.indexOf(".log.") >= 0;
                    if (isLog)
                    {
                        JsonObject f = files.createNestedObject();
                        f["name"] = fname;
                        f["size"] = file.size();
                    }
                }
                file = dir.openNextFile();
            }

            if (serializeJson(JSON, buf) > 16)
                webserver.send(200, F("application/json"), buf);
            else
                webserver.send(500, "text/plain", "ERR");
        } else {
            webserver.send(404, "text/plain", "Logs interface moved to SPA");
        } });

    // delete all log files
    webserver.on("/rmlogs", HTTP_GET, []()
                 {
        logMsg("remove logs");
        removeLogs();
        webserver.send(200, "text/plain", "OK"); });

    // delete single log file
    webserver.on("/dellogs", HTTP_GET, []()
                 {
        logMsg("delete log file");
        String fileName = webserver.arg("file");
        if (fileName.length() > 0 && fileName.indexOf("/") < 0 && fileName.indexOf("..") < 0)
        {
            String path = "/" + fileName;
            if (path.endsWith(".log") || path.indexOf(".log.") >= 0)
            {
                if (LittleFS.exists(path) && LittleFS.remove(path))
                {
                    webserver.send(200, "application/json", "{\"status\":\"deleted\"}");
                    return;
                }
            }
        }
        webserver.send(404, "application/json", "{\"status\":\"not found\"}"); });

    // firmware update interface moved to SPA
    webserver.on("/update", HTTP_GET, []()
                 { webserver.send(200, "application/json", "{\"status\":\"ready\"}"); });

    // handle firmware upload
    webserver.on("/update", HTTP_POST, []()
                 {
        if (Update.hasError()) {
            webserver.send(200, "application/json", "{\"status\":\"error\",\"message\":\"Update failed\"}");
            logMsg("ota failed");
        } else {
            webserver.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Update successful\"}");
            logMsg("ota successful");
            delay(2000);
            ESP.restart();
        } }, []()
                 {
        HTTPUpload& upload = webserver.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.println(F("Starting OTA update..."));
            uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
            if (!Update.begin(maxSketchSpace)) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            webserverRequestMillis = millis();
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.println(F("Update successful!"));
            } else {
                Update.printError(Serial);
            }
        }
        esp_task_wdt_reset(); });

    // show network settings
    webserver.on("/network", HTTP_GET, []()
                 {
        webserver.sendHeader("Location", "/", true);
        webserver.send(302, "text/plain", "");
        Serial.print(millis());
        Serial.println(F(": Redirect network route to SPA")); });

    // save network settings to NVS
    webserver.on("/network", HTTP_POST, []()
                 {
        logMsg("webui save network prefs");
        char oldStaSSID[33], oldStaPassword[33];
        bool staCredsChanged = false;

        strncpy(oldStaSSID, generalPrefs.wifiStaSSID, sizeof(oldStaSSID) - 1);
        oldStaSSID[sizeof(oldStaSSID) - 1] = '\0';
        strncpy(oldStaPassword, generalPrefs.wifiStaPassword, sizeof(oldStaPassword) - 1);
        oldStaPassword[sizeof(oldStaPassword) - 1] = '\0';

        if (webserver.arg("appassword").length() >= 8 && webserver.arg("appassword").length() <= 32)
        {
            memset(generalPrefs.wifiApPassword, 0, sizeof(generalPrefs.wifiApPassword));
            strncpy(generalPrefs.wifiApPassword, webserver.arg("appassword").c_str(), 32);
            generalPrefs.wifiApPassword[32] = '\0';
        }
        if (webserver.arg("stassid").length() >= 1 && webserver.arg("stassid").length() <= 32)
        {
            memset(generalPrefs.wifiStaSSID, 0, sizeof(generalPrefs.wifiStaSSID));
            strncpy(generalPrefs.wifiStaSSID, webserver.arg("stassid").c_str(), 32);
            generalPrefs.wifiStaSSID[32] = '\0';
        }
        if (webserver.arg("stapassword").length() <= 32)
        {
            memset(generalPrefs.wifiStaPassword, 0, sizeof(generalPrefs.wifiStaPassword));
            strncpy(generalPrefs.wifiStaPassword, webserver.arg("stapassword").c_str(), 32);
            generalPrefs.wifiStaPassword[32] = '\0';
        }

        staCredsChanged = (strncmp(oldStaSSID, generalPrefs.wifiStaSSID, 32) != 0) ||
                          (strncmp(oldStaPassword, generalPrefs.wifiStaPassword, 32) != 0);

        if (webserver.arg("mqtt") == "on") {
            generalPrefs.enableMQTT = true;
            if (webserver.arg("mqttbroker").length() >= 4 && webserver.arg("mqttbroker").length() <= 64)
            {
                memset(generalPrefs.mqttBroker, 0, sizeof(generalPrefs.mqttBroker));
                strncpy(generalPrefs.mqttBroker, webserver.arg("mqttbroker").c_str(), 64);
            }
            if (webserver.arg("mqttport").toInt() >= 1 && webserver.arg("mqttport").toInt() <= 65535)
                generalPrefs.mqttPort = webserver.arg("mqttport").toInt();
            if (webserver.arg("mqtttopiccmd").length() >= 4 && webserver.arg("mqtttopiccmd").length() <= 64)
            {
                memset(generalPrefs.mqttTopicCmd, 0, sizeof(generalPrefs.mqttTopicCmd));
                strncpy(generalPrefs.mqttTopicCmd, webserver.arg("mqtttopiccmd").c_str(), 64);
            }
            if (webserver.arg("mqtttopicstate").length() >= 4 && webserver.arg("mqtttopicstate").length() <= 64)
            {
                memset(generalPrefs.mqttTopicState, 0, sizeof(generalPrefs.mqttTopicState));
                strncpy(generalPrefs.mqttTopicState, webserver.arg("mqtttopicstate").c_str(), 64);
            }
            if (webserver.arg("mqttinterval").toInt() >= 10 && webserver.arg("mqttinterval").toInt() <= 600)
                generalPrefs.mqttPushInterval = webserver.arg("mqttinterval").toInt();
            generalPrefs.mqttUseTLS = (webserver.arg("mqttusetls") == "on");
        } else {
            generalPrefs.enableMQTT = false;
        }

        if (webserver.arg("mqttauth") == "on") {
            generalPrefs.mqttEnableAuth = true;
            if (webserver.arg("mqttuser").length() >= 1 && webserver.arg("mqttuser").length() <= 64)
            {
                memset(generalPrefs.mqttUsername, 0, sizeof(generalPrefs.mqttUsername));
                strncpy(generalPrefs.mqttUsername, webserver.arg("mqttuser").c_str(), 64);
                generalPrefs.mqttUsername[64] = '\0';
            }
            if (webserver.arg("mqttpassword").length() <= 64)
            {
                memset(generalPrefs.mqttPassword, 0, sizeof(generalPrefs.mqttPassword));
                strncpy(generalPrefs.mqttPassword, webserver.arg("mqttpassword").c_str(), 64);
                generalPrefs.mqttPassword[64] = '\0';
            }
        } else {
            generalPrefs.mqttEnableAuth = false;
        }

        generalPrefs.mqttUbidotsStemCompat = (webserver.arg("ubidots") == "on");
        if (webserver.arg("ubidots_label").length() >= 1 && webserver.arg("ubidots_label").length() <= 32)
        {
            memset(generalPrefs.mqttUbidotsDeviceLabel, 0, sizeof(generalPrefs.mqttUbidotsDeviceLabel));
            strncpy(generalPrefs.mqttUbidotsDeviceLabel, webserver.arg("ubidots_label").c_str(), 32);
            generalPrefs.mqttUbidotsDeviceLabel[32] = '\0';
        }

        nvs.putBool("general", true);
        nvs.putUChar("generalPrefsV", GENERAL_PREFS_SCHEMA_VERSION);
        nvs.putBytes("generalPrefs", &generalPrefs, sizeof(generalPrefs));  

        if (staCredsChanged)
        {
            Serial.print(millis());
            Serial.println(F(": WiFi: STA credentials changed, will reconnect in loop."));
            // Note: wifi_reconnect_sta() is NOT called here to avoid race conditions.
            // The loop() in main.cpp will detect the WiFi disconnection and reconnect.
        }

        // Apply MQTT changes immediately without requiring a full system restart.
        reconfigureMqttClient();
        connectToMqtt(MQTT_TIMEOUT_MS);

        webserver.send(200, "text/plain", "OK");
        Serial.print(millis());
        Serial.println(F(": Network settings saved")); });

    // show pin settings
    webserver.on("/pins", HTTP_GET, []()
                 {
        webserver.sendHeader("Location", "/", true);
        webserver.send(302, "text/plain", "");
        Serial.print(millis());
        Serial.println(F(": Redirect pins route to SPA")); });

    // save pin settings to NVS
    webserver.on("/pins", HTTP_POST, []()
                 {
        char buf[32];

        logMsg("webui save pin prefs");
        for (uint8_t i = 1; i <= NUM_RELAY; i++) {
            sprintf(buf, "relay%d_name", i);
            if (webserver.arg(buf).length() >= 3 && webserver.arg(buf).length() <= 24) {
                strncpy(switchesPrefs.labelRelay[i-1], webserver.arg(buf).c_str(), 24);
            }
            sprintf(buf, "relay%d_pin", i);
            if (webserver.arg(buf).toInt() >= -1 && webserver.arg(buf).toInt() <= 39) {
                switchesPrefs.pinRelay[i-1] = webserver.arg(buf).toInt();
            }
        }

       for (uint8_t i = 1; i <= NUM_MOISTURE_SENSORS; i++) {
            sprintf(buf, "moist%d_name", i);
            if (webserver.arg(buf).length() >= 3 && webserver.arg(buf).length() <= 24)
                strncpy(switchesPrefs.labelMoisture[i-1], webserver.arg(buf).c_str(), 24);
            sprintf(buf, "moist%d_pin", i);
            if (webserver.arg(buf).toInt() >= -1 && webserver.arg(buf).toInt() <= 39)
                switchesPrefs.pinMoisture[i-1] = webserver.arg(buf).toInt();
        }

        if (webserver.arg("moisture_min").toInt() >= 100 && webserver.arg("moisture_min").toInt() <= 1000)
            switchesPrefs.moistureMin = webserver.arg("moisture_min").toInt();
        if (webserver.arg("moisture_max").toInt() >= 100 && webserver.arg("moisture_max").toInt() <= 1000)
            switchesPrefs.moistureMax = webserver.arg("moisture_max").toInt();
        if (webserver.arg("moisture_raw") == "on")
            switchesPrefs.moistureRaw = true;
        else
            switchesPrefs.moistureRaw = false;
        if (webserver.arg("moisture_avg") == "on")
            switchesPrefs.moistureMovingAvg = true;
        else
            switchesPrefs.moistureMovingAvg = false;           

        // store settings in NVS     
        nvs.putBool("switches", true);
        nvs.putUChar("switchesPrefsV", SWITCHES_PREFS_SCHEMA_VERSION);
        nvs.putBytes("switchesPrefs", &switchesPrefs, sizeof(switchesPrefs));

        // reset moving average values
        if (switchesPrefs.moistureMovingAvg)
            readMoisture(true, false, true);     

        webserver.send(200, "text/plain", "OK");
        Serial.print(millis());
        Serial.println(F(": Pin settings saved")); });

    // show irrgation settings
    webserver.on("/config", HTTP_GET, []()
                 {
        webserver.sendHeader("Location", "/", true);
        webserver.send(302, "text/plain", "");
        Serial.print(millis());
        Serial.println(F(": Redirect config route to SPA")); });

    // save main settings to NVS
    webserver.on("/config", HTTP_POST, []()
                 {
        char buf[32];

        logMsg("webui save main prefs");
        if (webserver.arg("auto_irrigation") == "on")
            switchesPrefs.enableAutoIrrigation = true;
        else
            switchesPrefs.enableAutoIrrigation = false;

        if (webserver.arg("irrigation_time").length() == 5)
            strncpy(switchesPrefs.autoIrrigationTime, webserver.arg("irrigation_time").c_str(), 5); 
        if (webserver.arg("irrigation_pause").toInt() >= 1 && webserver.arg("irrigation_pause").toInt() <= 24)
            switchesPrefs.autoIrrigationPauseHours = webserver.arg("irrigation_pause").toInt();
        for (uint8_t i = 1; i <= NUM_RELAY; i++) {
            sprintf(buf, "irrigation_relay%d_secs", i);
            if (webserver.arg(buf).toInt() >= 0 && webserver.arg(buf).toInt() <= switchesPrefs.pumpAutoStopSecs)
                switchesPrefs.autoIrrigationSecs[i-1] = webserver.arg(buf).toInt();
        }

        if (webserver.arg("pump_autostop").toInt() >= 10 && webserver.arg("pump_autostop").toInt() <= 300)
            switchesPrefs.pumpAutoStopSecs = webserver.arg("pump_autostop").toInt();
        if (webserver.arg("pump_blocktime").toInt() >= 10 && webserver.arg("pump_blocktime").toInt() <= 480)
            switchesPrefs.relaysBlockMins = webserver.arg("pump_blocktime").toInt();
        if (webserver.arg("min_water_level").toInt() >= 4 && webserver.arg("min_water_level").toInt() <= 200)
            switchesPrefs.minWaterLevel = webserver.arg("min_water_level").toInt();    
        if (webserver.arg("reservoir_height").toInt() >= 10 && webserver.arg("reservoir_height").toInt() <= 200)
            switchesPrefs.waterReservoirHeight = webserver.arg("reservoir_height").toInt();
        
        if (webserver.arg("ignore_water_level") == "on")
            switchesPrefs.ignoreWaterLevel = true;
        else
            switchesPrefs.ignoreWaterLevel = false;

        if (webserver.arg("logging") == "on")
            switchesPrefs.enableLogging = true;
        else
            switchesPrefs.enableLogging = false;

        // store settings in NVS     
        nvs.putBool("switches", true);
        nvs.putUChar("switchesPrefsV", SWITCHES_PREFS_SCHEMA_VERSION);
        nvs.putBytes("switchesPrefs", &switchesPrefs, sizeof(switchesPrefs));       

        webserver.send(200, "text/plain", "OK");
        Serial.print(millis());
        Serial.println(F(": Main settings saved")); });

    webserver.on("/sendlogs", HTTP_GET, []()
                 {
        logMsg("send logs");
        if (webserver.arg("file").length() > 0)
        {
            String fileName = webserver.arg("file");
            if (fileName.indexOf("/") < 0 && fileName.indexOf("..") < 0)
            {
                String path = "/" + fileName;
                if ((path.endsWith(".log") || path.indexOf(".log.") >= 0) && handleSendFile(path))
                    return;
            }
            webserver.send(404, "text/plain", "File not found");
            return;
        }
        sendAllLogs(); });

    // soft reboot (short deep sleep, RTC memory is preserved)
    webserver.on("/restart", HTTP_GET, []()
                 {
        webserver.send(200, "text/plain", "OK");
        logMsg("webui restart");
        restartSystem(); });

    // triggers ESP.restart() thus RTC memory is lost
    webserver.on("/reset", HTTP_GET, []()
                 {
        webserver.send(200, "text/plain", "OK");
        logMsg("webui reset");
        resetSystem(); });

    webserver.on("/delnvs", HTTP_GET, []()
                 {
        logMsg("webui delnvs");
        nvs.clear();
        Serial.print(millis());
        Serial.println(F(": All settings in NVS removed. Rebooting..."));
        webserver.send(200, "text/plain", "OK");
        delay(1000);
        ESP.restart(); });

    webserver.onNotFound([]()
                         {
        // send main page
        if (webserver.uri().endsWith("/")) {
            if (!streamUiPage("index"))
                webserver.send(404, "text/plain", "Error 404: file not found");

        // send log file(s)
        } else if (!handleSendFile(webserver.uri()) && switchesPrefs.enableLogging) { 
            webserver.send(404, "text/plain", "Error 404: file not found");
        } else {
            webserver.send(404, "text/plain", "Error 404: file not found");
        } });

    webserver.begin();
    Serial.print(millis());
    Serial.println(F(": Webserver started."));
}

// change webserver timeout and reset its timer
void webserver_settimeout(uint16_t timeoutSecs)
{
    if (webserverTimeout != timeoutSecs)
    {
        Serial.printf("Set webserver timeout to %d secs.\n", timeoutSecs);
        webserverTimeout = timeoutSecs;
    }
}

// reset timeout countdown
void webserver_tickle()
{
    webserverRequestMillis = millis();
}

bool webserver_stop(bool force)
{
    if (!force && (millis() - webserverRequestMillis) < (webserverTimeout * 1000))
        return false;

    if (webserverRequestMillis > 0)
    {
        webserver.stop();
        webserverRequestMillis = 0;
        Serial.print(millis());
        Serial.println(F(": Webserver stopped"));
        logMsg("webserver off");
    }
    return true;
}