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

#ifndef _WIFI_H
#define _WIFI_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_wifi.h"
#include "config.h"

// #define MDNS_NAME "irrigation"

bool wifi_hotspot(bool activate);
bool wifi_uplink(bool reconnect);

#endif
