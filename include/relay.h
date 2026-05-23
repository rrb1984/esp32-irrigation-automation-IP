
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

#ifndef _RELAY_H
#define _RELAY_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

extern uint16_t pinmap[NUM_RELAY][3];
extern char pinnames[NUM_RELAY][7];
extern uint32_t pintime[];
extern uint32_t pintimeOn[];

void initRelays();
void setRelay(uint8_t num, bool on);
void unblockRelays();
uint16_t relayStatus(char *buf, size_t s);
void pumpAutoStop();

#endif