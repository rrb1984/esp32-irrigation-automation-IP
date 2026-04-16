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

#ifndef _MQTT_H
#define _MQTT_H

#include <Arduino.h>
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>
#include "config.h"

#define MQTT_TIMEOUT_MS 5000
#define MQTT_CONNECT_RETRY_SECS 30
#define MQTT_CLIENT_NAME "esp32-irrigation"

extern AsyncMqttClient mqttClient;

// Initialize MQTT client and register callbacks. Returns true if successfully initialized.
bool initMqttClient();
// Triggers non-blocking connect if needed; returns true only when already connected.
bool connectToMqtt(uint16_t timeoutMillis);
// Returns true when payload was queued for publish (not end-to-end delivery confirmation).
bool publishMqttStatus(uint16_t timeoutMillis);
// Forces MQTT client to reload settings from preferences (broker/port/tls/auth/topics).
void reconfigureMqttClient();

#endif