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

#include "sensors.h"
#include "prefs.h"
#include "config.h"
#include "logging.h"
#include <Arduino.h>

#ifdef HAS_HTU21D
static HTU21D htu21(HTU21D_RES_RH12_TEMP14);
static bool htu21Ready = false;
#endif

#ifdef HAS_DHT122
static DHTesp dhtSensor;
static bool dhtReady = false;
#endif

#if defined(US_TRIGGER_PIN) && defined(US_ECHO_PIN)
static Ultrasonic hcrs04(US_TRIGGER_PIN, US_ECHO_PIN);
#endif
static uint16_t moistureMA[NUM_MOISTURE_SENSORS][MOISTURE_MA_WINDOW_SIZE];
sensorReadings_t sensors;

void initSensors()
{
/**
 * @brief
 * Initialize sensors and log their status. For the DHT22 sensor, we read the temperature and humidity values to verify that the sensor is working correctly and to log the initial values at startup. If the sensor is not working, we log an error message with the type of error. For the HC-SR04 ultrasonic sensor, we read the water level once at startup to initialize the moving average and log the initial value. For the moisture sensors, we read them once at startup to initialize the moving average and log the initial values.
 */
#ifdef HAS_HTU21D
    Serial.print(millis());
    if (!htu21.begin())
    {
        Serial.println(F(": Sensor htu21D not found!"));
    }
    else
    {
        Serial.printf(": Sensor htu21D v%d found\n", htu21.readFirmwareVersion());
        htu21Ready = true;
    }
#endif

#ifdef HAS_DHT122
    Serial.print(millis()); // print timestamp for logging
    Serial.print(F(": Starting DHT22  in GPIO "));
    Serial.println(DHT22_PIN);
    delay(5000);                               // wait for sensor to stabilize, especially important if sensor is powered on at startup, also gives time to user to see the startup message in serial monitor before potential error messages from sensor reading are printed
    dhtSensor.setup(DHT22_PIN, DHTesp::DHT22); // setup DHT22 sensor, needs to be called before any other function that relies on DHT22 sensor data

    TempAndHumidity data = dhtSensor.getTempAndHumidity(); // read the temperature and humidity to verify that the sensor is working correctly and to log the initial values at startup, also initializes the sensor for future readings, needs to be called before any other function that relies on DHT22 sensor data
    delay(2000);                                           // wait a bit before reading again to avoid potential issues with first reading after sensor setup, also gives time to user to see the initial values in serial monitor before potential error messages from sensor reading are printed
    if (isnan(data.temperature) || isnan(data.humidity))
    { // if the reading is not valid, log an error message with the type of error
        Serial.print(F("Error reading DHT22 sensor: "));
        Serial.println("Initial outdoor temperature: " + String(data.temperature, 2) + "°C");
        Serial.println("Initial outdoor humidity: " + String(data.humidity, 1) + "%");
        Serial.println(dhtSensor.getStatusString());
    }
    else
    {
        Serial.println(F("OK! Sensor DHT22 ready."));
        Serial.println("Initial outdoor temperature: " + String(data.temperature, 2) + "°C");
        Serial.println("Initial outdoor humidity: " + String(data.humidity, 1) + "%");
        dhtReady = true;
    }
#endif
    Serial.print(millis());
    for (uint8_t i = 0; i < sizeof(switchesPrefs.pinMoisture); i++)
    {
        if (switchesPrefs.pinMoisture[i] > 0)
        {
            Serial.printf(":Starting Moisture sensor in GPIO %d \n", switchesPrefs.pinMoisture[i]);
        }
    }
    Serial.println();
    delay(750);
    readMoisture(true, false, false);
#if defined(US_TRIGGER_PIN) && defined(US_ECHO_PIN)
    sensors.waterLevel = -1;
#endif
}

// read temperature/humidity from I2C sensor htu21D
void readTemp(bool verbose, bool log)
/**
 * @brief  Read temperature and humidity from the HTU21D sensor. If the sensor is ready, we read the temperature and humidity values and log them if the verbose or log parameters are true. The temperature is logged with one decimal place and the humidity is logged as an integer percentage. If the sensor is not ready, we do nothing.
 * @param verbose If true, the temperature and humidity values are printed to the serial monitor with a timestamp. The temperature is printed with one decimal place and the humidity is printed as an integer percentage.
 * @param log If true, the temperature and humidity values are logged using the logMsg function. The temperature is logged with one decimal place and the humidity is logged as an integer percentage.
 */
{
#ifdef HAS_HTU21D
    char logmsg[32], temp[8];
    if (htu21Ready)
    {
        sensors.temperature = htu21.readTemperature();
        sensors.humidity = (uint8_t)htu21.readCompensatedHumidity();
        if (verbose)
        {
            Serial.print(millis());
            Serial.print(F(": Temperature: "));
            Serial.print(sensors.temperature, 1);
            Serial.printf(" °C, relative humidity %d %%", sensors.humidity);
        }
        if (log)
        {
            dtostrf(sensors.temperature, 4, 1, temp);
            sprintf(logmsg, "temp %sC, hum %d%%", temp, sensors.humidity);
            logMsg(logmsg);
        }
    }
#endif
#ifdef HAS_DHT122
    char logmsg[32], temp[8];
    if (dhtReady)
    {
        TempAndHumidity data = dhtSensor.getTempAndHumidity();
        data = dhtSensor.getTempAndHumidity();
        sensors.temperature = data.temperature;
        sensors.humidity = (uint8_t)data.humidity;
        if (verbose)
        {
            Serial.print(millis());
            Serial.print(F(": Temperature: "));
            Serial.print(sensors.temperature, 1);
            Serial.printf(" °C, relative humidity %d %%", sensors.humidity);
        }
        if (log)
        {
            dtostrf(sensors.temperature, 4, 1, temp);
            sprintf(logmsg, "temp %sC, hum %d%%", temp, sensors.humidity);
            logMsg(logmsg);
        }
    }
#endif
}

// read water level using HC-SR04 ultrasonic sensor
void readWaterLevel(bool verbose, bool log)
/**
 * @brief Read water level using the HC-SR04 ultrasonic sensor. If the sensor is enabled, we read the distance to the water surface and calculate the water level based on the configured height of the water reservoir. We also try to avoid jumpy water level values due to invalid readings by checking if the distance reading is within a reasonable range and if it doesn't deviate too much from the previous reading. If the reading is valid, we update the water level in the sensors struct. If the reading is invalid, we increment an error counter and if there are too many consecutive errors, we set the water level to -1 to indicate that it's unknown. Finally, we log the water level value if the verbose or log parameters are true.
 * @param verbose If true, the water level value is printed to the serial monitor with a timestamp. If the water level is valid, it's printed in centimeters. If the water level is unknown, a warning message is printed instead.
 * @param log If true, the water level value is logged using the logMsg function. If the water level is valid, it's logged in centimeters. If the water level is unknown, it's logged as "n/a".
 */
{
#if defined(US_TRIGGER_PIN) && defined(US_ECHO_PIN)
    static int16_t prevDistance = 0;
    static uint8_t errors = 0;
    uint16_t MoistureValueThresholdTemp = 0;
    int16_t distance = int(hcrs04.read());
    char logmsg[32];
    // first run at system startup
    if (!prevDistance)
    {
        delay(100);
        distance = int(hcrs04.read());
        prevDistance = distance;
    }

    // try to avoid jumpy water level values due to invalid readings
    if (distance <= 0 || (abs(prevDistance - distance) * 100 / distance) > 25 ||
        distance > (WATER_RESERVOIR_HEIGHT * 1.1))
    {
        if (errors++ >= 3)
        {
            sensors.waterLevel = -1;
        }
    }
    else
    {
        sensors.waterLevel = switchesPrefs.waterReservoirHeight - distance;
        errors = 0;
    }
    prevDistance = distance;

    if (verbose)
    {
        Serial.print(millis());
        if (sensors.waterLevel > 0)
            Serial.printf(": Water level: %d cm\n", sensors.waterLevel);
        else
            Serial.println(": WARNING: water level unknown!");
        Serial.print("Distance : ");
        Serial.println(distance);
        Serial.print("Prev Distance: ");
        Serial.println(prevDistance);
        Serial.print("waterlevel: ");
        Serial.println(sensors.waterLevel);
        Serial.print("Altura tanque: ");
        Serial.println(switchesPrefs.waterReservoirHeight);
    }
    if (log)
    {
        sprintf(logmsg, "water %dcm", sensors.waterLevel);
        logMsg(logmsg);
    }
#endif
}

void readMoisture(bool verbose, bool log, bool reset)
/**
 * @brief // Read soil moisture sensor values using the ADC. For each configured moisture sensor, we read the raw ADC value and optionally calculate a moving average to smooth out the readings. We also check if the reading is valid by comparing it to a threshold based on the configured maximum moisture value. If the reading is valid, we update the moisture value in the sensors struct, either as a percentage or as a raw value depending on the configuration. If the reading is invalid, we set the moisture value to -1 to indicate that it's unknown. Finally, we log the moisture values if the verbose or log parameters are true.
 * @param verbose If true, the moisture values are printed to the serial monitor with a timestamp. For each sensor, if the reading is valid, it's printed as a percentage or as a raw value depending on the configuration. If the reading is invalid, "n/a" is printed instead. If the reading is valid and the raw value is also available, the raw value is printed in parentheses.
 * @param log If true, the moisture values are logged using the logMsg function.
 * For each sensor, if the reading is valid, it's logged as a percentage or as a raw value depending on the configuration. If the reading is invalid, it's logged as "n/a".
 * @param reset If true, the moving average readings are reset. This is useful to call at startup to initialize the moving average and to avoid using old readings that may be stored in the array
 */
{
    int16_t reading = 0;
    int16_t readingtemp = 0;
    static char logmsg[64], buf[16];
    uint8_t len = 0;
    static uint16_t moistureMAReadings[NUM_MOISTURE_SENSORS][MOISTURE_MA_WINDOW_SIZE];
    static uint16_t moistureMASum[NUM_MOISTURE_SENSORS];
    static uint8_t mindex = 0;
    static bool mvgAvgReady = false;

    if (reset && switchesPrefs.moistureMovingAvg) // if reset is true and moving average is enabled, reset the moving average readings to -1 and the sums to 0, also reset the moving average index and ready flag, this is useful to call at startup to initialize the moving average and to avoid using old readings that may be stored in the array
    {
        memset(moistureMAReadings, -1,
               sizeof(moistureMAReadings[0][0]) * NUM_MOISTURE_SENSORS * MOISTURE_MA_WINDOW_SIZE);
        memset(moistureMASum, 0, sizeof(moistureMASum[0]) * NUM_MOISTURE_SENSORS);
        mvgAvgReady = false;
        mindex = 0;
    }

    memset(logmsg, 0, sizeof(logmsg));
    for (uint8_t i = 0; i < NUM_MOISTURE_SENSORS; i++)
    {
        reading = 0;
        if (switchesPrefs.pinMoisture[i] > 0) // if the moisture sensor is configured, read the raw ADC value and optionally calculate a moving average to smooth out the readings, also check if the reading is valid by comparing it to a threshold based on the configured maximum moisture value, if the reading is valid, update the moisture value in the sensors struct, either as a percentage or as a raw value depending on the configuration, if the reading is invalid, set the moisture value to -1 to indicate that it's unknown
        {
            for (uint8_t j = 0; j < 10; j++)
            {
                readingtemp = analogRead(switchesPrefs.pinMoisture[i]);
                reading += readingtemp;
                delay(5);
            }
            reading /= 10;
            if (reading < switchesPrefs.moistureMax / 2)
            {
                reading = -1;
                moistureMASum[i] = 0;
            }

            if (switchesPrefs.moistureMovingAvg && reading >= 0) // if moving average is enabled and the reading is valid, update the moving average readings and sums, and calculate the moving average value if the array is filled with readings, this helps to smooth out the readings and reduce noise, especially for soil moisture sensors that can have jumpy values
            {
                moistureMASum[i] -= moistureMAReadings[i][mindex];
                moistureMAReadings[i][mindex] = reading;
                moistureMASum[i] += reading;
                if (moistureMAReadings[i][MOISTURE_MA_WINDOW_SIZE - 1] > 0 && mvgAvgReady)
                {
                    reading = moistureMASum[i] / MOISTURE_MA_WINDOW_SIZE;
                }
            }

            if (!switchesPrefs.moistureRaw && reading >= 0)
            {
                sensors.moisture[i] = map(reading, switchesPrefs.moistureMin,
                                          switchesPrefs.moistureMax, 0, 100);
                if (sensors.moisture[i] < 0)
                    sensors.moisture[i] = 0;
                if (sensors.moisture[i] > 100)
                    sensors.moisture[i] = 100;
            }
            else
            {
                sensors.moisture[i] = reading;
            }

            if (verbose)
            {
                Serial.print(millis());
                Serial.printf(": Soil moisture  %s : ", switchesPrefs.labelMoisture[i]);
                if (switchesPrefs.moistureRaw && sensors.moisture[i] >= 0) // raw sensor values
                    Serial.printf("%d", sensors.moisture[i]);
                else if (!switchesPrefs.moistureRaw && sensors.moisture[i] >= 0)
                    Serial.printf("%d%%", sensors.moisture[i]);
                else
                    Serial.print("n/a");
                if (!switchesPrefs.moistureRaw && reading >= 0)
                    Serial.printf(" (raw %d)\n", reading);
                else
                    Serial.println();
            }
            if (log)
            {
                if (!switchesPrefs.moistureRaw && sensors.moisture[i] >= 0)
                    sprintf(buf, "moist%d %d%%, ", i + 1, sensors.moisture[i]);
                else
                    sprintf(buf, "moist%d %d, ", i + 1, sensors.moisture[i]);
                strcat(logmsg, buf);
            }
        }
    }

    if (switchesPrefs.moistureMovingAvg) // if moving average is enabled, increment the moving average index to update the readings in a rolling window manner, this helps to smooth out the readings and reduce noise, especially for soil moisture sensors that can have jumpy values
    {
        mindex = (mindex + 1) % MOISTURE_MA_WINDOW_SIZE;
        if ((mindex + 1) == MOISTURE_MA_WINDOW_SIZE)
            mvgAvgReady = true;
    }

    len = strlen(logmsg);
    if (len > 2)
    {
        logmsg[len - 2] = '\0';
        logMsg(logmsg);
    }
}