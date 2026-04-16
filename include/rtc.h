
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

#ifndef _RTC_H
#define _RTC_H

#include <Arduino.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Timezone.h>
#include <rom/rtc.h>
#include <sys/time.h>

extern uint32_t busyTime;
extern uint32_t startupTime;

void setStartupTime(uint16_t diff_ms);
bool startNTPSync();
void stopNTPSync();
char *getRuntime(uint32_t runtimeSecs);
time_t getLocalTime();
char *getSystemTime();
char *getTimeString(bool showsecs);
char *getDateString();
char *getTimeZone();

#endif