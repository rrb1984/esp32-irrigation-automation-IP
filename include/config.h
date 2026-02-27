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

#ifndef _CONFIG_H
#define _CONFIG_H

//wokwi_web simulator (optional)
//#define WOKWKI_WEB
#ifdef WOKWKI_WEB
	#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION 121
#endif
#ifndef CORE_DEBUG_LEVEL
  #define CORE_DEBUG_LEVEL 0
#endif

#ifndef LOG_LOCAL_LEVEL
  #define LOG_LOCAL_LEVEL 0
#endif

#endif //WOKWKI_WEB


// choose language for web interface
//#define LANG_DE
#define LANG_EN

// Valor por defecto si no viene de -DFIRMWARE_VERSION (p.ej. en Wokwi web)
#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION 121
#endif
#ifndef CORE_DEBUG_LEVEL
  #define CORE_DEBUG_LEVEL 0
#endif

#ifndef LOG_LOCAL_LEVEL
  #define LOG_LOCAL_LEVEL 0
#endif
//
// All settings except DEBUG options can
// also be changed in the web interface
//

// timeout for local access point
#define AP_TIMEOUT_SECS 120

// base name and credentials for local access point (min. 8 characters!)
#define WIFI_AP_SSID "Irrigation-System"
#define WIFI_AP_PASSWORD "__secret__"

// start local AP if wifi is not available for given number of seconds
#define WIFI_AP_FALLBACK_TIMEOUT 120

// credentials for internet connection (wifi uplink)
#define WIFI_STA_SSID "Wokwi-GUEST"
#define WIFI_STA_PASSWORD ""
#define WIFI_STA_CONNECT_TIMEOUT 5
#define WIFI_STA_RECONNECT_TIMEOUT 60

// peridocally publish readings with MQTT
// if a WiFi uplink is preset/available
//#define MQTT_ENABLE
#define MQTT_BROKER "node02.myqtthub.com"
#define MQTT_TOPIC_CMD "irrigation/cmd"
#define MQTT_TOPIC_STATE "irrigation/state"
#define MQTT_PUSH_INTERVAL_SECS 120
#define MQTT_USERNAME "irrigationESP32"
#define MQTT_PASSWORD "p@ssw0rds3cr3t@"

// relay pin for water pump
#define PUMP_PIN 4

// pump will stop if active for more then given
// number of settings; meant as a upper limit
// to avoid accidental overwatering
#define PUMP_AUTOSTOP_SECS 5

// if water level in reservoir falls below this
// level, the pump is switched of and blocked to
// avoid damaging the submersible pump
#define PUMP_MIN_WATERLEVEL_CM 50

// distance from ultra-sonice sensor to bottom of reservoir
// use to calculate the water level
#define WATER_RESERVOIR_HEIGHT 400

// relay names (max. 24 chars) and pin assigment
// set *_PIN to -1 to disable
#define NUM_RELAY 4
#define RELAY_PINS "16,17,4,2"
#define RELAY1_PIN 16
#define RELAY1_LABEL "pump16"
#define RELAY2_PIN 17
#define RELAY2_LABEL "pump17"
#define RELAY3_PIN 4
#define RELAY3_LABEL "pump4"
#define RELAY4_PIN 2
#define RELAY4_LABEL "pump2"



// optional capacitive moisture sensors
// pins and labels (max. 24 chars) for moisture sensor on ADC1
// set *_PIN to -1 to disable
#define NUM_MOISTURE_SENSORS 4
#define MOISTURE_PINS "33,34,32,35"
#define MOIST1_PIN 33
#define MOIST1_LABEL "moisture33"
#define MOIST2_PIN 34
#define MOIST2_LABEL "moisture34"
#define MOIST3_PIN 32
#define MOIST3_LABEL "moisture32"
#define MOIST4_PIN 35
#define MOIST4_LABEL "moisture35"


// ADC readings from capacitave moisture sensor v1.2 which mark the
// upper bound (sensor in air) and lower bound (sensor placed in water)
//upper values than thresold causes irrigation starts
#define MOISTURE_VALUE_AIR 4095
#define MOISTURE_VALUE_WATER 0
#define MOISTURE1_VAL_THRESOLD 2000
#define MOISTURE2_VAL_THRESOLD 2000
#define MOISTURE3_VAL_THRESOLD 2000
#define MOISTURE4_VAL_THRESOLD 2000

// wait a least given number of secs before triggering 
// relay again; meant to prevent accidental overwatering
#define RELAY_BLOCK_MINS 180

// I2C temperature and humidity sensor (optional)
//#define HAS_HTU21D

// I2C temperature and humidity sensor (DHT122)
#define HAS_DHT122
#define DHT22_PIN 13 

// pins for HC-SR04 ultrasonic sensor (water level)
#define US_TRIGGER_PIN 14
#define US_ECHO_PIN 12

// log sensor reading to flash
#define ENABLE_LOGGING

// if plants haven't been watered for AUTO_IRRIGATION_PAUSE_HOURS trigger 
// irrigation (all valves) for AUTO_IRRIGATION_SECS at AUTO_IRRIGRATION_TIME
// Note: AUTO_IRRIGATION_DURATION_SECS must be less than PUMP_AUTOSTOP_SECS
//#define ENABLE_AUTO_IRRIGRATION_SCHEDULER
#define AUTO_IRRIGRATION_TIME "08:00"   // HH:MM, 24h
#define AUTO_IRRIGATION_SECS 20
#define AUTO_IRRIGATION_PAUSE_HOURS 12

// time server
#define NTP_ADDRESS "de.pool.ntp.org"

// clear all preferences in NVS if firmware was updated
// must be set if structs for preferences were changed
#define CLEAR_NVS_FWUPDATE

// prints free heap in web ui
//#define DEBUG_MEMORY

#endif