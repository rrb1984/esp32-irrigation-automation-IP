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

#ifndef _WEBSERVER_H
#define _WEBSERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_wifi.h>

// guard againt invalid remote configuration values
#define WEBSERVER_TIMEOUT_MIN_SECS 60
#define WEBSERVER_TIMEOUT_MAX_SECS 300

extern WebServer webserver;

void webserver_start();
bool webserver_stop();

#endif
