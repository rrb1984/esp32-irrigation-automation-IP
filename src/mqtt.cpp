/*
 * Based on "ESP32-Irrigation-Automation"
 * Copyright (c) 2021-2022 Lars Wessels
 * https://github.com/lrswss/esp32-irrigation-automation
 *
 * Modified for academic use in a Trabajo Fin de Grado (TFG)
 * Universidad Internacional de La Rioja (UNIR)
 *
 * Licensed under the Apache License, Version 2.0
 */

#include "config.h"
#include "mqtt.h"
#include "logging.h"
#include "prefs.h"
#include "wlan.h"
#include "sensors.h"
#include "relay.h"
#include "utils.h"
#include <stdlib.h>

AsyncMqttClient mqttClient;                     // AsyncMqttClient object for MQTT connection and communication
static char mqttClientId[64];                   // MQTT client ID generated from base name + system ID + random hex string
static uint32_t lastMqttConnectAttemptMs = 0;   // timestamp of last MQTT connect attempt, used for retry backoff
static bool mqttInitialized = false;            // flag to indicate if MQTT client has been initialized with settings and callbacks
static bool hasPendingPublish = false;          // flag to indicate if there is a pending publish operation that should be attempted on next successful connect
static bool tlsUnavailableWarningShown = false; // flag to track if a warning about TLS being unavailable has already been issued, to avoid spamming logs on repeated connect attempts when TLS is requested but not available in the build
static bool mqttCallbacksRegistered = false;    // flag to track if MQTT callbacks have already been registered, to avoid registering them multiple times if initMqttClient() is called more than once

static bool publishCurrentStatus();
/**
 * @brief forward declaration of the publishCurrentStatus function,
 * which is responsible for publishing the current status of the irrigation system to the MQTT broker.
 * This function gathers the relay status and sensor readings, formats them as a JSON document, and publishes it to the appropriate MQTT topic.
 * It is called when there is a pending publish operation after a successful MQTT connection,
 * and can also be called directly to attempt to publish the status immediately if already connected.
 * The function returns true if the publish operation was successfully queued, or false if it failed (e.g., due to lack of WiFi uplink).
 * @return true if the status was successfully published to the MQTT broker, false otherwise (e.g., if there is no WiFi uplink or if the publish operation failed).
 */

static void cmdDeviceLabel(char *buf, size_t size)
/**
 * @brief Generate the device label to be used in MQTT command topics when in Ubidots STEM compatibility mode.
 * This is based on the user-configurable device label, but may have a "_cmd" suffix if the compatibility mode is enabled,
 * to differentiate command topics from state topics which use the base device label.
 * The generated label is stored in the provided buffer, ensuring it does not exceed the specified size.
 *
 * @param buf The buffer where the generated device label will be stored.
 * @param size The size of the buffer, used to prevent overflow when writing the label.
 */
{
    if (generalPrefs.mqttUbidotsStemCompat) // In Ubidots STEM compatibility mode, command topics use the device label with a "_cmd" suffix, while state topics use the base device label. This allows for a clear separation of command and state topics in the Ubidots platform, which expects this naming convention for proper handling of incoming commands and outgoing state updates.
    {
        snprintf(buf, size - 1, "%s_cmd", generalPrefs.mqttUbidotsDeviceLabel);
        return;
    }

    snprintf(buf, size - 1, "%s", generalPrefs.mqttUbidotsDeviceLabel);
}

static bool parseBoolPayload(const char *payload, size_t length, bool *value)
/**
 * @brief Parse a boolean value from an MQTT command payload.
 * The function checks for common representations of boolean values, including "on"/"off", "1"/"0", and numeric values where any value >= 0.5
 * is considered true. The parsed boolean value is stored in the provided pointer, and the function returns true if parsing was successful,
 *  or false if the payload could not be interpreted as a valid boolean value.
 * This function is used to translate ubidots STEM command payloads into boolean values for controlling the relays,
 * allowing for flexible command formats while ensuring consistent interpretation of the intended on/off state.
 * @param payload The MQTT command payload to parse, as a byte array.
 * @param length The length of the payload in bytes.
 * @param value A pointer to a boolean variable where the parsed value will be stored if parsing is successful.
 * @return true if the payload was successfully parsed as a boolean value, false otherwise.
 */
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

static uint8_t getMqttQos()
/**
 * @brief Determine the MQTT Quality of Service (QoS) level to use for publishing messages.
 * The QoS level is determined based on the user-configurable setting in generalPrefs.mqttQoS,
 * but is capped at 2 since MQTT defines QoS levels 0, 1, and 2.
 * This function ensures that even if the user has set an invalid QoS level above 2,
 * the system will default to using QoS 2, which guarantees that messages are delivered exactly once.
 * The returned QoS level is used when subscribing to topics and publishing messages to ensure the desired level of delivery assurance.
 *
 */
{
    return generalPrefs.mqttQoS > 2 ? 2 : generalPrefs.mqttQoS;
}

static void buildStateTopic(char *topic, size_t size)
/**
 * @brief Generate the MQTT topic to which the irrigation system status will be published.
 * The topic is determined based on the user-configurable settings in generalPrefs,
 * and may differ depending on whether Ubidots STEM compatibility mode is enabled. In STEM compatibility mode,
 * the topic follows the format "/v1.6/devices/{device_label}", while in non-STEM mode it uses the user-defined mqttTopicState.
 * This function ensures that the generated topic fits within the provided buffer size and is properly null-terminated.
 * The generated topic is used when publishing the status of the irrigation system, allowing it to be correctly received
 *
 */
{
    if (generalPrefs.mqttUbidotsStemCompat)
    {
        snprintf(topic, size - 1, "/v1.6/devices/%s", generalPrefs.mqttUbidotsDeviceLabel);
        return;
    }

    snprintf(topic, size - 1, "%s", generalPrefs.mqttTopicState);
}

static void handleMqttCommand(char *topic, const char *payload, size_t length)
/**
 * @brief Callback function that is called when an MQTT message is received on a subscribed topic.
 * This function checks if the received topic matches any of the expected command topics for controlling the relays,
 * and if so, it attempts to parse the payload as a boolean value to determine whether to turn the corresponding relay on or off.
 * The function supports both the standard command topic format and the Ubidots STEM compatibility format,
 * allowing for flexible integration with different MQTT setups. If a valid command is parsed from the payload,
 * the function calls setRelay to change the state of the relay and then triggers an immediate
 * status publish to update the MQTT broker with the new state.
 *
 * @param topic The MQTT topic on which the message was received.
 * @param payload The payload of the MQTT message, as a byte array.
 * @param length The length of the payload in bytes.
 */
{
    if (!generalPrefs.mqttUbidotsStemCompat && strstr(topic, generalPrefs.mqttTopicCmd) == NULL)
        return;

    if (generalPrefs.mqttUbidotsStemCompat) /*In Ubidots STEM compatibility mode,
                                             * command topics are expected to follow the format "/v1.6/devices/{device_label}/{relay_label}/lv".
                                             * This block checks if the received topic matches this pattern for the configured device label,
                                             * ensuring that only relevant command topics are processed. If the topic does not contain the expected prefix,
                                             * the function returns early without attempting to parse the command.
                                             */
    {
        static char prefix[96];
        char cmdDevice[48];
        cmdDeviceLabel(cmdDevice, sizeof(cmdDevice));
        snprintf(prefix, sizeof(prefix) - 1, "/v1.6/devices/%s/", cmdDevice);
        if (strstr(topic, prefix) == NULL)
            return;
    }

    for (uint8_t i = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++) /* This loop iterates through the configured relays and checks
                                                                        * if the received MQTT topic contains the label of each relay.
                                                                        */
    {
        const char *label = switchesPrefs.labelRelay[i];
        if (strstr(topic, label) == NULL)
            continue;

        {
            bool turnOn = false;
            if (parseBoolPayload(payload, length, &turnOn))
            {
                setRelay(i, turnOn);
                publishMqttStatus(MQTT_TIMEOUT_MS);
            }
        }
    }
}

static void onMqttConnect(bool sessionPresent)
/**
 * @brief Callback function that is called when the MQTT client successfully connects to the broker.
 * This function is responsible for subscribing to the command topics for controlling the relays,
 * and if there is a pending publish operation (indicated by the hasPendingPublish flag),
 * it will attempt to publish the current status immediately after subscribing.
 * @param sessionPresent Indicates whether the MQTT session is present (i.e., if the broker has a persistent session for this client).
 * This parameter is not currently used in the function, but it could be used in the future to determine whether to
 * publish the status immediately or wait for a certain condition.
 */
{
    static char buf[96], logmsg[96];
    uint8_t qos = getMqttQos();
    (void)sessionPresent;

    Serial.print(millis());
    Serial.println(F(": MQTT: connected."));

    // subscribe to cmd topics for remote valve switching
    for (uint8_t i = 0; i < (sizeof(pinmap) / sizeof(pinmap[0])); i++)
    {
        if (generalPrefs.mqttUbidotsStemCompat)
        {
            char cmdDevice[48];
            cmdDeviceLabel(cmdDevice, sizeof(cmdDevice));
            snprintf(buf, sizeof(buf) - 1, "/v1.6/devices/%s/%s/lv",
                     cmdDevice, switchesPrefs.labelRelay[i]);
        }
        else
        {
            snprintf(buf, sizeof(buf) - 1, "%s/%s", generalPrefs.mqttTopicCmd, switchesPrefs.labelRelay[i]);
        }
        if (!mqttClient.subscribe(buf, qos))
        {
            Serial.print(millis());
            Serial.printf(": MQTT: subscribe %s failed!\n", buf);
            sprintf(logmsg, "mqtt subscribe %s failed", buf);
            logMsg(logmsg);
        }
    }

    if (hasPendingPublish)
    {
        hasPendingPublish = false;
        publishCurrentStatus();
    }
}

static void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
/**
 * @brief  Callback function that is called when the MQTT client disconnects from the broker.
 * @param reason The reason for the disconnection, provided as an AsyncMqttClientDisconnectReason enum value. This can indicate whether the disconnection was initiated by the client, the broker, or due to a network issue, among other reasons. The function logs the disconnection event along with the reason code for debugging and monitoring purposes.
 *
 */
{
    static char logmsg[96];
    Serial.print(millis());
    Serial.printf(": MQTT: disconnected (%d).\n", (int)reason);
    snprintf(logmsg, sizeof(logmsg), "mqtt disconnected %d", (int)reason);
    logMsg(logmsg);
}

static void onMqttSubscribe(uint16_t packetId, uint8_t qos)
/**
 * @brief Callback function that is called when the MQTT client successfully subscribes to a topic.
 * @param packetId The packet identifier for the subscribe operation,
 *  which can be used to track the subscription request and match it with the corresponding acknowledgment from the broker.
 *
 */
{
    Serial.print(millis());
    Serial.printf(": MQTT: subscribed packet %u (qos %u).\n", packetId, qos);
}

static void onMqttPublish(uint16_t packetId)
/**
 * @brief Callback function that is called when a published MQTT message has been acknowledged by the broker.
 * @packetId The packet identifier for the published message, which can be used to track the publish
 * operation and confirm that the message has been successfully received by the broker.
 * This function logs the acknowledgment event for debugging and monitoring purposes,
 * allowing the developer to verify that messages are being delivered as expected.
 *
 */
{
    Serial.print(millis());
    Serial.printf(": MQTT: publish acknowledged packet %u.\n", packetId);
}

static void onMqttMessage(char *topic, char *payload,
                          AsyncMqttClientMessageProperties properties,
                          size_t len, size_t index, size_t total)
/**
 * @brief Callback function that is called when an MQTT message is received on a subscribed topic.
 * This function processes incoming MQTT messages, checks if they match the expected command topics for controlling the relays,
 * and if so, parses the payload to determine the desired state of the relay (on/off).
 * It supports both standard command topics and Ubidots STEM compatibility format.
 * If a valid command is parsed, it updates the relay state accordingly and triggers an immediate status publish to reflect the change.
 * The function also logs the received message details for debugging purposes.
 *
 * @param topic The MQTT topic on which the message was received.
 * @param payload The payload of the MQTT message, as a byte array.
 * @param properties The properties of the MQTT message, including QoS and retain flag.
 * @param len The length of the payload in bytes.
 *  @param index The index of the current payload fragment, used for multi-part messages. For single-frame messages, this will be 0.
 * @param total The total length of the complete payload, which may be larger than len if
 * the message is received in multiple fragments. For single-frame messages, this will be equal to len.
 *
 */
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

    handleMqttCommand(topic, payload, len);
}

// set mqtt client name and callback function for subscribed topics
bool initMqttClient()
/**
 * @brief   // Initialize the MQTT client with the configured settings and register callback functions for connection events and incoming messages.
 * This function generates a unique client name based on a base name, the system ID, and a random hex string to ensure that multiple
 * devices can connect to the same broker without client ID conflicts.
 *
 */
{
    String name;

    if (!mqttInitialized && generalPrefs.enableMQTT)
    {
        name = String(MQTT_CLIENT_NAME).substring(0, 48) + "-" + systemID() + "-" + String(random(0xffff), HEX);
        snprintf(mqttClientId, sizeof(mqttClientId), "%s", name.c_str());

        mqttClient.setServer(generalPrefs.mqttBroker, generalPrefs.mqttPort);
        mqttClient.setClientId(mqttClientId);
        mqttClient.setKeepAlive(generalPrefs.mqttKeepalive);
        mqttClient.setCleanSession(generalPrefs.mqttCleanSession);
#if ASYNC_TCP_SSL_ENABLED
        mqttClient.setSecure(generalPrefs.mqttUseTLS);
#else
        if (generalPrefs.mqttUseTLS && !tlsUnavailableWarningShown)
        {
            Serial.print(millis());
            Serial.println(F(": MQTT: TLS requested but ASYNC_TCP_SSL_ENABLED is not available in this build."));
            logMsg("mqtt tls unavailable in build");
            tlsUnavailableWarningShown = true;
        }
#endif
        if (!mqttCallbacksRegistered)
        {
            mqttClient.onConnect(onMqttConnect);
            mqttClient.onDisconnect(onMqttDisconnect);
            mqttClient.onSubscribe(onMqttSubscribe);
            mqttClient.onPublish(onMqttPublish);
            mqttClient.onMessage(onMqttMessage);
            mqttCallbacksRegistered = true;
        }

        if (generalPrefs.mqttEnableAuth)
            mqttClient.setCredentials(generalPrefs.mqttUsername, generalPrefs.mqttPassword);

        mqttInitialized = true;
    }
    return mqttInitialized;
}

// connect to mqtt broker and subscribe to valve cmd topics
bool connectToMqtt(uint16_t timeoutMillis)
/**
 * @brief Attempt to connect to the MQTT broker and subscribe to the command topics for controlling the relays.
 * @param timeoutMillis The maximum time to wait for a successful connection before giving up and returning false.
 * This parameter is currently not used in the function, but it could be implemented in the future to add a timeout mechanism for the connection attempt.
 *
 */
{
    (void)timeoutMillis;

    if (!initMqttClient())
        return false;

    if (mqttClient.connected())
        return true;

    if (!wifi_uplink(false))
    {
        Serial.print(millis());
        Serial.println(F(": MQTT: cannot connect, no WiFi uplink."));
        return false;
    }

    // wait before reconnect attempt after disconnect/fail
    if (lastMqttConnectAttemptMs > 0 && (millis() - lastMqttConnectAttemptMs) < (MQTT_CONNECT_RETRY_SECS * 1000))
        return false;

    lastMqttConnectAttemptMs = millis();

    Serial.print(millis());
    Serial.printf(": MQTT: connecting to broker %s:%u\n", generalPrefs.mqttBroker, generalPrefs.mqttPort);
    mqttClient.connect();
    return false;
}

// try to publish sensor reedings with given timeout
// will implicitly call initMqttClient()
bool publishMqttStatus(uint16_t timeoutMillis)
/**
 * @brief Attempt to publish the current status of the irrigation system to the MQTT broker, with a specified timeout for the operation.
 * @param timeoutMillis The maximum time to wait for a successful publish operation before giving up and returning false.
 *
 */
{
    (void)timeoutMillis;

    if (connectToMqtt(MQTT_TIMEOUT_MS) && mqttClient.connected())
        return publishCurrentStatus();

    hasPendingPublish = true;
    return false;
}

void reconfigureMqttClient()
/**
 * @brief Reconfigure the MQTT client by disconnecting from the broker and resetting the initialization state.
 *
 */
{
    if (mqttClient.connected())
        mqttClient.disconnect();

    // Force full re-init so new network settings are applied on next connect.
    mqttInitialized = false;
    hasPendingPublish = false;
    lastMqttConnectAttemptMs = 0;
    tlsUnavailableWarningShown = false;
}

static bool publishCurrentStatus()
/**
 * @brief Publish the current status of the irrigation system to the MQTT broker.
 * This function gathers the relay status and sensor readings, formats them as a JSON document,
 * and publishes it to the appropriate MQTT topic. It checks for WiFi uplink before attempting to publish,
 * and logs the outcome of the publish operation for debugging purposes.
 * The function returns true if the publish operation was successfully queued, or false if it failed (e.g., due to lack of WiFi uplink).
 *
 */
{
    JsonDocument JSON;
    uint8_t qos = getMqttQos();
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
    buildStateTopic(topic, sizeof(topic));

    uint16_t packetId = mqttClient.publish(topic, qos, false, buf, s);
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