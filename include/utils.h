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

#ifndef _UTILS_H
#define _UTILS_H

#include <Arduino.h>
#include "esp_task_wdt.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_ota_ops.h"

#ifdef WOKWKI_WEB

static const esp_task_wdt_config_t WDT_CONFIG = {
    .timeout_ms = 90000,                             // 90 segundos
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // núcleos activos
    .trigger_panic = true};

#endif // WOKWKI_WEB

enum rstcodes
{
    POWERUP,
    RESTART,
    SOFTRESET,
    EXCEPTION,
    WATCHDOG,
    BROWNOUT,
    OTHER
};

extern rstcodes runmode;
extern char runmodes[7][10];

String systemID();
rstcodes bootMsg();
char *checkFirmwareUpdate();
void restartSystem();
void resetSystem();
void array2string(const byte *arr, int len, char *buf, bool reverse);
void printHEX8bit(uint8_t *arr, uint8_t len, bool ln, bool reverse);
void free_heap();
#endif
