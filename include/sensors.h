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

#ifndef _SENSORS_H
#define _SENSORS_H

#include <Arduino.h>
#include <HTU21D.h>
#include <HCSR04.h>
#include <driver/adc.h>
#include <DHTesp.h>

#define MOISTURE_MA_WINDOW_SIZE 5   // size of moving average window for moisture sensor readings, needs to be large enough to allow for stable readings but small enough to react to changes in soil moisture in a timely manner
#define MOISTURE_MA_WINDOW_TIME 10 // time in seconds to wait before updating moving average of moisture sensor readings, needs to be long enough to allow for stable readings but short enough to react to changes in soil moisture in a timely manner

typedef struct
{
  float temperature;
  uint8_t humidity;
  int16_t waterLevel;
  int16_t moisture[4];
} sensorReadings_t;

extern sensorReadings_t sensors;

void initSensors();
void readTemp(bool verbose, bool log);
void readWaterLevel(bool verbose, bool log);
void readMoisture(bool verbose, bool log, bool reset);

#endif