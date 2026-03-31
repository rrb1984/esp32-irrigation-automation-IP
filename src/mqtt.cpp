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
#include "mqtt.h"
#include "logging.h"
#include "prefs.h"
#include "wlan.h"
#include "sensors.h"
#include "relay.h"
#include "utils.h"
#include <stdlib.h>

AsyncMqttClient mqtt;
static char clientname[64];
static uint32_t lastConnectAttempt = 0;
static bool mqttInited = false;
static bool pendingPublish = false;
static bool tlsWarningIssued = false;
static bool callbacksRegistered = false;

static bool mqtt_publish_status();

static void mqtt_cmd_device_label(char *buf, size_t size)
{
    if (generalPrefs.mqttUbidotsStemCompat)
    {
        snprintf(buf, size - 1, "%s_cmd", generalPrefs.mqttUbidotsDeviceLabel);
        return;
    }

    snprintf(buf, size - 1, "%s", generalPrefs.mqttUbidotsDeviceLabel);
}

static bool mqtt_parse_bool(const char *payload, size_t length, bool *value)
{
    if (length == 2 && payload[0] == 'o' && payload[1] == 'n')
    {
        *value = true;
        return true;
    }
    if (length == 3 && payload[0] == 'o' && payload[1] == 'f' && payload[2] == 'f')
    {
        *value = false;
        return true;
    }
    if (length == 1 && payload[0] == '1')
    {
        *value = true;
        return true;
    }
    if (length == 1 && payload[0] == '0')
    {
        *value = false;
        return true;
    }

    if (length > 0 && length < 16)
    {
        char buf[16];
        memcpy(buf, payload, length);
        buf[length] = '\0';
        char *endptr = NULL;
        double v = strtod(buf, &endptr);
        if (endptr != buf)
        {
            *value = (v >= 0.5);
            return true;
        }
    }

    return false;
}

static uint8_t mqtt_qos()
{
    return generalPrefs.mqttQoS > 2 ? 2 : generalPrefs.mqttQoS;
}

static void mqtt_state_topic(char *topic, size_t size)
{
    if (generalPrefs.mqttUbidotsStemCompat)
    {
        snprintf(topic, size - 1, "/v1.6/devices/%s", generalPrefs.mqttUbidotsDeviceLabel);
        return;
    }

    snprintf(topic, size - 1, "%s", generalPrefs.mqttTopicState);
}

// called if mqtt messages arrive on topics we've subscribed
static void mqtt_callback(char *topic, const char *payload, size_t length)
{
    if (!generalPrefs.mqttUbidotsStemCompat && strstr(topic, generalPrefs.mqttTopicCmd) == NULL)
        return;

    if (generalPrefs.mqttUbidotsStemCompat)
    {
        static char prefix[96];
        char cmdDevice[48];
        mqtt_cmd_device_label(cmdDevice, sizeof(cmdDevice));
        snprintf(prefix, sizeof(prefix) - 1, "/v1.6/devices/%s/", cmdDevice);
        if (strstr(topic, prefix) == NULL)
            return;
    }

    for (uint8_t i = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++)
    {
        const char *label = switchesPrefs.labelRelay[i];
        if (strstr(topic, label) == NULL)
            continue;

        {
            bool turnOn = false;
            if (mqtt_parse_bool(payload, length, &turnOn))
            {
                setRelay(i, turnOn);
                mqtt_send(MQTT_TIMEOUT_MS);
            }
        }
    }
}

static void mqtt_on_connect(bool sessionPresent)
{
    static char buf[96], logmsg[96];
    uint8_t qos = mqtt_qos();
    (void)sessionPresent;

    Serial.print(millis());
    Serial.println(F(": MQTT: connected."));

    // subscribe to cmd topics for remote valve switching
    for (uint8_t i = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++)
    {
        if (generalPrefs.mqttUbidotsStemCompat)
        {
            char cmdDevice[48];
            mqtt_cmd_device_label(cmdDevice, sizeof(cmdDevice));
            snprintf(buf, sizeof(buf) - 1, "/v1.6/devices/%s/%s/lv",
                     cmdDevice, switchesPrefs.labelRelay[i]);
        }
        else
        {
            snprintf(buf, sizeof(buf) - 1, "%s/%s", generalPrefs.mqttTopicCmd, switchesPrefs.labelRelay[i]);
        }
        if (!mqtt.subscribe(buf, qos))
        {
            Serial.print(millis());
            Serial.printf(": MQTT: subscribe %s failed!\n", buf);
            sprintf(logmsg, "mqtt subscribe %s failed", buf);
            logMsg(logmsg);
        }
    }

    if (pendingPublish)
    {
        pendingPublish = false;
        mqtt_publish_status();
    }
}

static void mqtt_on_disconnect(AsyncMqttClientDisconnectReason reason)
{
    static char logmsg[96];
    Serial.print(millis());
    Serial.printf(": MQTT: disconnected (%d).\n", (int)reason);
    snprintf(logmsg, sizeof(logmsg), "mqtt disconnected %d", (int)reason);
    logMsg(logmsg);
}

static void mqtt_on_subscribe(uint16_t packetId, uint8_t qos)
{
    Serial.print(millis());
    Serial.printf(": MQTT: subscribed packet %u (qos %u).\n", packetId, qos);
}

static void mqtt_on_publish(uint16_t packetId)
{
    Serial.print(millis());
    Serial.printf(": MQTT: publish acknowledged packet %u.\n", packetId);
}

static void mqtt_on_message(char *topic, char *payload,
                            AsyncMqttClientMessageProperties properties,
                            size_t len, size_t index, size_t total)
{
    Serial.print(millis());
    Serial.printf(": MQTT: message on %s (len %u, qos %u, retain %u).\n",
                  topic,
                  (unsigned int)total,
                  properties.qos,
                  properties.retain ? 1 : 0);

    // We only process complete single-frame command payloads.
    if (index != 0 || len != total)
        return;

    mqtt_callback(topic, payload, len);
}

// set mqtt client name and callback function for subscribed topics
bool mqtt_init()
{
    String name;

    if (!mqttInited && generalPrefs.enableMQTT)
    {
        name = String(MQTT_CLIENT_NAME).substring(0, 48) + "-" + systemID() + "-" + String(random(0xffff), HEX);
        snprintf(clientname, sizeof(clientname), "%s", name.c_str());

        mqtt.setServer(generalPrefs.mqttBroker, generalPrefs.mqttPort);
        mqtt.setClientId(clientname);
        mqtt.setKeepAlive(generalPrefs.mqttKeepalive);
        mqtt.setCleanSession(generalPrefs.mqttCleanSession);
#if ASYNC_TCP_SSL_ENABLED
        mqtt.setSecure(generalPrefs.mqttUseTLS);
#else
        if (generalPrefs.mqttUseTLS && !tlsWarningIssued)
        {
            Serial.print(millis());
            Serial.println(F(": MQTT: TLS requested but ASYNC_TCP_SSL_ENABLED is not available in this build."));
            logMsg("mqtt tls unavailable in build");
            tlsWarningIssued = true;
        }
#endif
        if (!callbacksRegistered)
        {
            mqtt.onConnect(mqtt_on_connect);
            mqtt.onDisconnect(mqtt_on_disconnect);
            mqtt.onSubscribe(mqtt_on_subscribe);
            mqtt.onPublish(mqtt_on_publish);
            mqtt.onMessage(mqtt_on_message);
            callbacksRegistered = true;
        }

        if (generalPrefs.mqttEnableAuth)
            mqtt.setCredentials(generalPrefs.mqttUsername, generalPrefs.mqttPassword);

        mqttInited = true;
    }
    return mqttInited;
}

// connect to mqtt broker and subscribe to valve cmd topics
bool mqtt_connect(uint16_t timeoutMillis)
{
    (void)timeoutMillis;

    if (!mqtt_init())
        return false;

    if (mqtt.connected())
        return true;

    if (!wifi_uplink(false))
    {
        Serial.print(millis());
        Serial.println(F(": MQTT: cannot connect, no WiFi uplink."));
        return false;
    }

    // wait before reconnect attempt after disconnect/fail
    if (lastConnectAttempt > 0 && (millis() - lastConnectAttempt) < (MQTT_CONNECT_RETRY_SECS * 1000))
        return false;

    lastConnectAttempt = millis();

    Serial.print(millis());
    Serial.printf(": MQTT: connecting to broker %s:%u\n", generalPrefs.mqttBroker, generalPrefs.mqttPort);
    mqtt.connect();
    return false;
}

// try to publish sensor reedings with given timeout
// will implicitly call mqtt_init()
bool mqtt_send(uint16_t timeoutMillis)
{
    (void)timeoutMillis;

    if (mqtt_connect(MQTT_TIMEOUT_MS) && mqtt.connected())
        return mqtt_publish_status();

    pendingPublish = true;
    return false;
}

void mqtt_reconfigure()
{
    if (mqtt.connected())
        mqtt.disconnect();

    // Force full re-init so new network settings are applied on next connect.
    mqttInited = false;
    pendingPublish = false;
    lastConnectAttempt = 0;
    tlsWarningIssued = false;
}

static bool mqtt_publish_status()
{
    JsonDocument JSON;
    uint8_t qos = mqtt_qos();
    static char status[256], topic[64], buf[512], label[24], valveKey[8];

    if (!wifi_uplink(false))
    {
        Serial.print(millis());
        Serial.println(F(": MQTT: cannot send, no WiFi uplink."));
        logMsg("mqtt publish failed, no wifi");
        return false;
    }

    // create JSON with relay status and sensor readings
    relayStatus(status, sizeof(status));
    deserializeJson(JSON, status);
    for (uint8_t i = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++)
    {
        snprintf(valveKey, sizeof(valveKey), "valve%d", i + 1);
        if (JSON.containsKey(valveKey))
        {
            if (switchesPrefs.labelRelay[i][0] != '\0')
            {
                JSON[switchesPrefs.labelRelay[i]] = JSON[valveKey];
            }
            JSON.remove(valveKey);
        }
    }
#if defined(HAS_HTU21D) || defined(HAS_DHT122)
    readTemp(false, false);
    JSON["temp"] = sensors.temperature;
    JSON["hum"] = sensors.humidity;
#endif
#if defined(US_TRIGGER_PIN) && defined(US_ECHO_PIN)
    readWaterLevel(false, false);
    JSON["level"] = sensors.waterLevel;
#endif
    for (uint8_t i = 0; i < NUM_MOISTURE_SENSORS; i++)
    {
        // don't send raw sensor values
        if (switchesPrefs.pinMoisture[i] > 0 && sensors.moisture[i] <= 100)
        {
            sprintf(label, "moist%d", i + 1);
            JSON[label] = sensors.moisture[i];
        }
    }

    size_t s = serializeJson(JSON, buf);
    mqtt_state_topic(topic, sizeof(topic));

    uint16_t packetId = mqtt.publish(topic, qos, false, buf, s);
    if (packetId > 0)
    {
        Serial.print(millis());
        Serial.printf(": MQTT: queued %u bytes to %s on %s\n", (unsigned int)s,
                      topic, generalPrefs.mqttBroker);
        return true;
    }

    Serial.println(F(": MQTT: publish failed!"));
    logMsg("mqtt publish failed");
    return false;
}