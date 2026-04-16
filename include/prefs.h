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

#ifndef _PREFS_H
#define _PREFS_H

#include <Arduino.h>
#include <Preferences.h> // use NVS instead of EEPROM (depreciated on ESP32)
#include "config.h"

#define GENERAL_PREFS_SCHEMA_VERSION 4
#define SWITCHES_PREFS_SCHEMA_VERSION 1

typedef struct
{
    uint16_t wifiAPT;
    char wifiApPassword[33];
    char wifiStaSSID[33];
    char wifiStaPassword[33];
    bool enableMQTT;
    char mqttBroker[65];
    uint16_t mqttPort;
    bool mqttUseTLS;
    char mqttTopicCmd[65];
    char mqttTopicState[65];
    uint16_t mqttPushInterval;
    uint16_t mqttKeepalive;
    uint8_t mqttQoS;
    bool mqttCleanSession;
    char mqttUsername[65];
    char mqttPassword[65];
    bool mqttEnableAuth;
    bool mqttUbidotsStemCompat;
    char mqttUbidotsDeviceLabel[33];
    bool clearNVSFwUpdate; // no switch in web ui
} generalPrefs_t;

typedef struct
{
    int8_t pinRelay[NUM_RELAY];
    char labelRelay[NUM_RELAY][25];
    int8_t pinMoisture[NUM_MOISTURE_SENSORS];
    char labelMoisture[NUM_MOISTURE_SENSORS][25];
    uint8_t pinPump;
    uint16_t pumpAutoStopSecs;
    uint32_t relaysBlockMins;
    bool enableAutoIrrigation;
    char autoIrrigationTime[8];
    uint16_t autoIrrigationSecs[4];
    uint8_t autoIrrigationPauseHours;
    bool enableLogging;
    uint8_t minWaterLevel;
    bool ignoreWaterLevel;
    uint16_t waterReservoirHeight; // change from 8 to 16 bit data to cover the distance of the container
    uint16_t moistureMin;
    uint16_t moistureMax;
    bool moistureRaw;
    bool moistureMovingAvg;
    uint16_t MoistureValueThreshold[NUM_MOISTURE_SENSORS];
} switchesPrefs_t;

extern Preferences nvs;
extern generalPrefs_t generalPrefs;
extern switchesPrefs_t switchesPrefs;

void initPrefs();
void restorePrefs();

#endif
