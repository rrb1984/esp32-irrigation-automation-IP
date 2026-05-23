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

#ifndef _SCHEDULER_H
#define _SCHEDULER_H

#include <Arduino.h>
#include <sys/time.h>

#define MAX_JOBS 16

typedef struct valvejob_t valvejob_t;
typedef void (*jobfn_t)(uint8_t, bool); // setRelay()

extern valvejob_t valvejobs[MAX_JOBS];

struct valvejob_t
{
  struct valvejob_t *next;
  time_t time;
  jobfn_t func;
  uint8_t relay;
  bool state;
};

void schedule_job(valvejob_t *job, time_t time, jobfn_t func, uint8_t relay, bool state);
bool jobs_scheduled();
bool jobs_scheduled_relay(uint8_t relay);
void scheduler();

#endif
