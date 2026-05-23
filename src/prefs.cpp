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
#include "web.h"
#include "prefs.h"
#include "utils.h"
#include "sensors.h"

static const char *NVS_KEY_GENERAL = "general";
static const char *NVS_KEY_GENERAL_PREFS = "generalPrefs";
static const char *NVS_KEY_GENERAL_PREFS_VER = "generalPrefsV";
static const char *NVS_KEY_SWITCHES = "switches";
static const char *NVS_KEY_SWITCHES_PREFS = "switchesPrefs";
static const char *NVS_KEY_SWITCHES_PREFS_VER = "switchesPrefsV";

// instantiate general settings and set default values
RTC_DATA_ATTR generalPrefs_t generalPrefs = {
    AP_TIMEOUT_SECS,
    WIFI_AP_PASSWORD,
    WIFI_STA_SSID,
    WIFI_STA_PASSWORD,
#ifdef MQTT_ENABLE
    true,
#else
    false,
#endif
    MQTT_BROKER,
    MQTT_PORT,
    MQTT_USE_TLS,
    MQTT_TOPIC_CMD,
    MQTT_TOPIC_STATE,
    MQTT_PUSH_INTERVAL_SECS,
    MQTT_KEEPALIVE_SECS,
    MQTT_QOS,
    MQTT_CLEAN_SESSION,
#if defined(MQTT_USERNAME) && defined(MQTT_PASSWORD)
    MQTT_USERNAME,
    MQTT_PASSWORD,
    true,
#else
    "none",
    "none",
    false,
#endif
    MQTT_UBIDOTS_STEM_COMPAT,
    MQTT_UBIDOTS_DEVICE_LABEL,
#ifdef CLEAR_NVS_FWUPDATE
    true
#else
    false
#endif
};

RTC_DATA_ATTR switchesPrefs_t switchesPrefs = {
    {RELAY1_PIN, RELAY2_PIN, RELAY3_PIN, RELAY4_PIN},
    {RELAY1_LABEL, RELAY2_LABEL, RELAY3_LABEL, RELAY4_LABEL},
    {MOIST1_PIN, MOIST2_PIN, MOIST3_PIN, MOIST4_PIN},
    {MOIST1_LABEL, MOIST2_LABEL, MOIST3_LABEL, MOIST4_LABEL},
    PUMP_PIN,
    PUMP_AUTOSTOP_SECS,
    RELAY_BLOCK_MINS,
#ifdef ENABLE_AUTO_IRRIGRATION_SCHEDULER
    true,
#else
    false,
#endif
    AUTO_IRRIGRATION_TIME,
    {AUTO_IRRIGATION_SECS, AUTO_IRRIGATION_SECS, AUTO_IRRIGATION_SECS, AUTO_IRRIGATION_SECS},
    AUTO_IRRIGATION_PAUSE_HOURS,
#ifdef ENABLE_LOGGING
    true,
#else
    false,
#endif
    PUMP_MIN_WATERLEVEL_CM,
    false,
    WATER_RESERVOIR_HEIGHT,
    MOISTURE_VALUE_AIR,
    MOISTURE_VALUE_WATER,
    false,
    true,
    {MOISTURE1_VAL_THRESOLD, MOISTURE2_VAL_THRESOLD, MOISTURE3_VAL_THRESOLD, MOISTURE4_VAL_THRESOLD}};

// use NVS to store settings to survive
// a system reset (cold start) or reflash
Preferences nvs;

// initialize NVS to (permanently) store system settings
void initPrefs()
{
    nvs.begin("prefs", false);
}

// load system settings from NVS
void restorePrefs()
{
    size_t prefSize;

    if (nvs.getBool(NVS_KEY_GENERAL))
    {
        uint8_t storedVersion = nvs.getUChar(NVS_KEY_GENERAL_PREFS_VER, 0);
        prefSize = nvs.getBytesLength(NVS_KEY_GENERAL_PREFS);

        if (storedVersion == GENERAL_PREFS_SCHEMA_VERSION && prefSize == sizeof(generalPrefs))
        {
            byte bufGeneralPrefs[prefSize];
            nvs.getBytes(NVS_KEY_GENERAL_PREFS, bufGeneralPrefs, prefSize);
            memcpy(&generalPrefs, bufGeneralPrefs, prefSize);
            Serial.print("Restored general preferences (");
            Serial.print(prefSize);
            Serial.print(" bytes, schema ");
            Serial.print(storedVersion);
            Serial.println(").");
        }
        else
        {
            Serial.print("General preferences schema mismatch (stored version ");
            Serial.print(storedVersion);
            Serial.print(", stored size ");
            Serial.print(prefSize);
            Serial.print(", expected version ");
            Serial.print(GENERAL_PREFS_SCHEMA_VERSION);
            Serial.print(", expected size ");
            Serial.print(sizeof(generalPrefs));
            Serial.println("). Keeping defaults.");
            nvs.remove(NVS_KEY_GENERAL_PREFS);
            nvs.putUChar(NVS_KEY_GENERAL_PREFS_VER, GENERAL_PREFS_SCHEMA_VERSION);
        }
    }

    if (nvs.getBool(NVS_KEY_SWITCHES))
    {
        uint8_t storedVersion = nvs.getUChar(NVS_KEY_SWITCHES_PREFS_VER, 0);
        prefSize = nvs.getBytesLength(NVS_KEY_SWITCHES_PREFS);

        if (storedVersion == SWITCHES_PREFS_SCHEMA_VERSION && prefSize == sizeof(switchesPrefs))
        {
            byte bufSwitchesPrefs[prefSize];
            nvs.getBytes(NVS_KEY_SWITCHES_PREFS, bufSwitchesPrefs, prefSize);
            memcpy(&switchesPrefs, bufSwitchesPrefs, prefSize);
            Serial.print("Restored switches preferences (");
            Serial.print(prefSize);
            Serial.print(" bytes, schema ");
            Serial.print(storedVersion);
            Serial.println(").");
        }
        else
        {
            Serial.print("Switches preferences schema mismatch (stored version ");
            Serial.print(storedVersion);
            Serial.print(", stored size ");
            Serial.print(prefSize);
            Serial.print(", expected version ");
            Serial.print(SWITCHES_PREFS_SCHEMA_VERSION);
            Serial.print(", expected size ");
            Serial.print(sizeof(switchesPrefs));
            Serial.println("). Keeping defaults.");
            nvs.remove(NVS_KEY_SWITCHES_PREFS);
            nvs.putUChar(NVS_KEY_SWITCHES_PREFS_VER, SWITCHES_PREFS_SCHEMA_VERSION);
        }
    }
}
