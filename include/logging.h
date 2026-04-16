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

#ifndef _LOGGING_H
#define _LOGGING_H

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#define LOGFILE_MAX_SIZE 1024 * 50 // 50k
#define LOGFILE_MAX_FILES 24
#define LOGFILE_NAME "/irrigation.log"

void initLogging();
void logMsg(const char *msg);
void listDirectory(const char *dir);
void sendAllLogs();
void rotateLogs();
void removeLogs();
bool handleSendFile(String path);
String listDirHTML(const char *path);

#endif
