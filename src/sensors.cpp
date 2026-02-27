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
  static HTU21D  htu21(HTU21D_RES_RH12_TEMP14);
  static bool htu21Ready = false;
#endif

#ifdef HAS_DHT122
  static DHTesp dhtSensor;
  static bool dhtReady = false;
  
#endif

#if defined(US_TRIGGER_PIN) && defined(US_ECHO_PIN)
  //static UltraSonicDistanceSensor hcrs04(US_TRIGGER_PIN, US_ECHO_PIN);
  static Ultrasonic hcrs04(US_TRIGGER_PIN, US_ECHO_PIN);
#endif
static uint16_t moistureMA[NUM_MOISTURE_SENSORS][MOISTURE_MA_WINDOW_SIZE];
sensorReadings_t sensors;


void initSensors() {
    
#ifdef HAS_HTU21D
    Serial.print(millis());
    if (!htu21.begin()) {
        Serial.println(F(": Sensor htu21D not found!"));
    } else {
        Serial.printf(": Sensor htu21D v%d found\n", htu21.readFirmwareVersion());
        htu21Ready = true;
    }
#endif

#ifdef HAS_DHT122
    Serial.print(millis()); // Imprime el tiempo actual
    Serial.print(F(": Starting DHT22  in GPIO "));
    Serial.println(DHT22_PIN); // Mensaje informativo
    delay(5000); //damos tiempo a que se inicialicen los sensores
    dhtSensor.setup(DHT22_PIN,DHTesp::DHT22); //configuramos el objeto

    // Lee la temperatura y humedad para verificar el estado del sensor
    TempAndHumidity data = dhtSensor.getTempAndHumidity();
    delay(2000);
    // Utiliza getStatus() para una verificación de error más robusta
    if (isnan(data.temperature) || isnan(data.humidity)){
        Serial.print(F("Error: No se pudo leer el sensor DHT22. "));
        Serial.println("Initial outdoor temperature: " + String(data.temperature, 2) + "°C");
        Serial.println("Initial outdoor humidity: " + String(data.humidity, 1) + "%");
        Serial.println(dhtSensor.getStatusString()); // Imprime el tipo de error
    } else {
        Serial.println(F("OK! Sensor DHT22 ready."));
        Serial.println("Initial outdoor temperature: " + String(data.temperature, 2) + "°C");
        Serial.println("Initial outdoor humidity: " + String(data.humidity, 1) + "%");
        dhtReady = true; // El sensor está listo
    }
#endif
    Serial.print(millis());
    for (uint8_t i = 0; i < sizeof(switchesPrefs.pinMoisture); i++) {
        if (switchesPrefs.pinMoisture[i] > 0) {
            Serial.printf(":Starting Moisture sensor in GPIO %d \n", switchesPrefs.pinMoisture[i]);
            //adcAttachPin(switchesPrefs.pinMoisture[i]); funcion no necesaria
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
void readTemp(bool verbose, bool log) {
#ifdef HAS_HTU21D
    char logmsg[32], temp[8];
    if (htu21Ready) {
        sensors.temperature = htu21.readTemperature();
        sensors.humidity = (uint8_t)htu21.readCompensatedHumidity();
        if (verbose) {
            Serial.print(millis());
            Serial.print(F(": Temperature: "));
            Serial.print(sensors.temperature, 1);
            Serial.printf(" °C, relative humidity %d %%", sensors.humidity);
        }
        if (log) {
            dtostrf(sensors.temperature, 4, 1, temp);
            sprintf(logmsg, "temp %sC, hum %d%%", temp, sensors.humidity);
            logMsg(logmsg);
        }
    }
#endif
#ifdef HAS_DHT122
    char logmsg[32], temp[8];
    if (dhtReady) {
        // Lee la temperatura y humedad para verificar el estado del sensor
        TempAndHumidity data = dhtSensor.getTempAndHumidity();
        data = dhtSensor.getTempAndHumidity();
        sensors.temperature = data.temperature;
        sensors.humidity = (uint8_t)data.humidity;
        if (verbose) {
            Serial.print(millis());
            Serial.print(F(": Temperature: "));
            Serial.print(sensors.temperature, 1);
            Serial.printf(" °C, relative humidity %d %%", sensors.humidity);
        }
        if (log) {
            dtostrf(sensors.temperature, 4, 1, temp);
            sprintf(logmsg, "temp %sC, hum %d%%", temp, sensors.humidity);
            logMsg(logmsg);
        }
    }
#endif


}


// read water level using HC-SR04 ultrasonic sensor
void readWaterLevel(bool verbose, bool log) {
#if defined(US_TRIGGER_PIN) && defined(US_ECHO_PIN)
    static int16_t prevDistance = 0;
    static uint8_t errors = 0;
    uint16_t MoistureValueThresholdTemp = 0;
    //int16_t distance = int(hcrs04.measureDistanceCm());
    int16_t distance = int(hcrs04.read());
    char logmsg[32];
    // first run at system startup
    if (!prevDistance) {
        delay(100);
        //distance = int(hcrs04.measureDistanceCm());
        distance = int(hcrs04.read());
        prevDistance = distance;
    }

    // try to avoid jumpy water level values due to invalid readings
    if (distance <= 0 || (abs(prevDistance-distance)*100/distance) > 25 || 
            distance > (WATER_RESERVOIR_HEIGHT * 1.1)) {
        if (errors++ >= 3) {
            sensors.waterLevel = -1;
        }
    } else {
        sensors.waterLevel = switchesPrefs.waterReservoirHeight - distance;
        errors = 0;       
    }
    prevDistance = distance;
    // if moisture data is % then moisturevaluethesold is mapped to %
    for (uint8_t i = 0; i < NUM_MOISTURE_SENSORS; i++) {
      if (!switchesPrefs.moistureRaw) {
                MoistureValueThresholdTemp = switchesPrefs.MoistureValueThreshold[i];
                switchesPrefs.MoistureValueThreshold[i] = map(MoistureValueThresholdTemp, switchesPrefs.moistureMin,switchesPrefs.moistureMax, 0, 100);
            }
    }

    if (verbose) {
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
    if (log) {
        sprintf(logmsg, "water %dcm", sensors.waterLevel);
        logMsg(logmsg);
    }
#endif
}


// read capacitive soil moisture sensor(s) v1.2 using ADC
void readMoisture(bool verbose, bool log, bool reset) {
    int16_t reading = 0;
    int16_t readingtemp =0;
    static char logmsg[64], buf[16];
    uint8_t len = 0;
    static uint16_t moistureMAReadings[NUM_MOISTURE_SENSORS][MOISTURE_MA_WINDOW_SIZE];
    static uint16_t moistureMASum[NUM_MOISTURE_SENSORS];
    static uint8_t mindex = 0;
    static bool mvgAvgReady = false;

    // reset moving average readings
    if (reset && switchesPrefs.moistureMovingAvg) {
        memset (moistureMAReadings, -1, 
            sizeof(moistureMAReadings[0][0]) * NUM_MOISTURE_SENSORS * MOISTURE_MA_WINDOW_SIZE);
        memset(moistureMASum, 0, sizeof(moistureMASum[0]) * NUM_MOISTURE_SENSORS);
        mvgAvgReady = false;
        mindex = 0;
    }

    //analogReadResolution(10);
    memset(logmsg, 0, sizeof(logmsg));
    for (uint8_t i = 0; i < NUM_MOISTURE_SENSORS; i++) {
        reading = 0;
        if (switchesPrefs.pinMoisture[i] > 0) {
            for (uint8_t j = 0; j < 10; j++) { // average readings
                readingtemp = analogRead(switchesPrefs.pinMoisture[i]);
                //Serial.printf("Lectura potenciomentro %d : %d \n",switchesPrefs.pinMoisture[i], readingtemp);
                reading += readingtemp;
                //Serial.printf("Valor de reading : %d \n",reading);
                delay(5);
            }
            reading /= 10;
             //Serial.printf("Valor de reading div 10 : %d \n",reading);

            // sensor not connected
            if (reading < switchesPrefs.moistureMax/2) {
                reading = -1;
                moistureMASum[i] = 0;
                //Serial.println("entra en 1");
            }

            // optionally calculate moving average to avoid jumpy readings
            if (switchesPrefs.moistureMovingAvg && reading >= 0) {
                moistureMASum[i] -= moistureMAReadings[i][mindex];
                moistureMAReadings[i][mindex] = reading;
                moistureMASum[i] += reading;
                //Serial.println("entra en 2");
                 //Serial.printf("Valor de readmoisturemasum : %d \n",moistureMASum[i]);
                // don't calc moving avg unless array is filled with readings
                if (moistureMAReadings[i][MOISTURE_MA_WINDOW_SIZE-1] > 0 && mvgAvgReady){
                    reading = moistureMASum[i] / MOISTURE_MA_WINDOW_SIZE;
                    //Serial.println("entra en 2b");
                    //Serial.printf("Valor de reading : %d \n",reading);
                }
                    
            }

            if (!switchesPrefs.moistureRaw && reading >= 0) {
                sensors.moisture[i] = map(reading, switchesPrefs.moistureMin, 
                    switchesPrefs.moistureMax, 0, 100);
                  //Serial.println("entra en 3");
                if (sensors.moisture[i] < 0)
                    sensors.moisture[i] = 0;
                if (sensors.moisture[i] > 100)
                    sensors.moisture[i] = 100;
            } else {
                sensors.moisture[i] = reading;
                //Serial.println("entra en 4");
            }

            if (verbose) {
                Serial.print(millis());
                Serial.printf(": Soil moisture  %s : ", switchesPrefs.labelMoisture[i]);
                if (switchesPrefs.moistureRaw && sensors.moisture[i] >= 0) // raw sensor values
                    Serial.printf("%d", sensors.moisture[i]);
                else if (!switchesPrefs.moistureRaw && sensors.moisture[i] >= 0)
                    Serial.printf("%d%%", sensors.moisture[i]);
                else
                    Serial.print("n/a");
                if (!switchesPrefs.moistureRaw && reading >=0)
                    Serial.printf(" (raw %d)\n", reading); 
                else
                    Serial.println();
            }
            if (log) {
                if (!switchesPrefs.moistureRaw && sensors.moisture[i] >= 0)
                    sprintf(buf, "moist%d %d%%, ", i+1, sensors.moisture[i]);
                else
                    sprintf(buf, "moist%d %d, ", i+1, sensors.moisture[i]);
                strcat(logmsg, buf);
            }
        }
    }
    // increment moving avg index, and wrap to 0 if it exceeds the window size
    if (switchesPrefs.moistureMovingAvg) {
        mindex = (mindex + 1) % MOISTURE_MA_WINDOW_SIZE;
        if ((mindex + 1) == MOISTURE_MA_WINDOW_SIZE)
            mvgAvgReady = true; // array filled, ready to use moving average values
    }

    len = strlen(logmsg);
    if (len > 2) {
        logmsg[len-2] = '\0'; // remove trailing ', '
        logMsg(logmsg);
    }
    
}