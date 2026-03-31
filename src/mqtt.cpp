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

AsyncMqttClient mqtt;
static char clientname[64];
static uint32_t lastConnectAttempt = 0;
static bool mqttInited = false;
static bool pendingPublish = false;
static bool tlsWarningIssued = false;
static bool callbacksRegistered = false;

static bool mqtt_publish_status();

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
    if (strstr(topic, generalPrefs.mqttTopicCmd) != NULL)
    {
        for (uint8_t i = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++)
        {
            if (strstr(topic, pinnames[i]))
            {
                bool turnOn = (length == 2 && payload[0] == 'o' && payload[1] == 'n');
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
        snprintf(buf, sizeof(buf) - 1, "%s/%s", generalPrefs.mqttTopicCmd, pinnames[i]);
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
    static char status[64], topic[64], buf[192], label[16];

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