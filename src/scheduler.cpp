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

#include "scheduler.h"

valvejob_t *scheduledjobs = NULL; // pointer to the head of the linked list of scheduled jobs, initialized to NULL to indicate that no jobs are currently scheduled
valvejob_t valvejobs[MAX_JOBS];   // array of valvejob_t structs to hold the scheduled jobs, needs to be large enough to hold all potential scheduled jobs based on the configured irrigation schedule and runtime for each valve, also allows for easy management of job memory without dynamic allocation

/**
 * @brief Schedules a job to be executed at a specific time. The job is defined by a function pointer, relay number and state to set the relay to. The job is inserted into a linked list of scheduled jobs in order of execution time. If the scheduled time is in the past, it is adjusted to execute
 * as soon as possible. The function does not return any value, but the scheduled job will be executed by the scheduler() function when the scheduled time is reached.
 *
 * @param job pointer to the valvejob_t struct that defines the job to be scheduled, needs to be provided by the caller and should be taken from the valvejobs array to avoid dynamic memory allocation
 * @param time the time at which the job should be executed, specified as a time_t value (number of seconds since the Unix epoch), if the time is in the past it will be adjusted to execute as soon as possible
 * @param func the function pointer that defines the job to be executed, needs to match the jobfn_t typedef and should point to a function that takes a relay number and state as parameters, such as setRelay()
 * @param relay the relay number that should be passed to the job function when it is executed, needs to be a valid relay number based on the configured pinmap
 * @param state the state that should be passed to the job function when it is executed, typically true for turning the relay on and false for turning it off
 *
 * example usage:
 * valvejob_t myjob;
 * schedule_job(&myjob, (scheduler_start + 5000), setRelay, 0, true); // schedules a job to turn on relay 0 after 5 seconds from the current scheduler start time, the job will be executed by the scheduler() function when the scheduled time is reached, the job will be defined by the setRelay() function which takes the relay number and state as parameters
 *
 */
void schedule_job(valvejob_t *job, time_t time, jobfn_t func, uint8_t relay, bool state)
{
    valvejob_t **pnext;

    if (time <= 0)
        time = 1;

    // create job...
    job->next = NULL;
    job->time = time;
    job->func = func;
    job->relay = relay;
    job->state = state;

    // ...and insert it into schedule
    for (pnext = &scheduledjobs; *pnext; pnext = &((*pnext)->next))
    {
        if (((*pnext)->time - time) > 0)
        {
            job->next = *pnext;
            break;
        }
    }
    *pnext = job;
}

/**
 * @brief Checks the linked list of scheduled jobs and executes any jobs that are due to be executed based on the current time. The function iterates through the linked list of scheduled jobs and checks if the scheduled time for each job has been reached or passed. If a job is due to be executed, the function calls the job's function pointer with the specified relay number and state, and then removes the job from the linked list. The function continues to check for and execute any remaining jobs in the linked list until there are no more jobs that are due to be executed.
 *
 */
void scheduler()
{
    if (jobs_scheduled() && (scheduledjobs->time < millis()))
    {
        scheduledjobs->func(scheduledjobs->relay, scheduledjobs->state);
        scheduledjobs = scheduledjobs->next;
    }
}

/**
 * @brief // Checks if there are any jobs currently scheduled by checking if the head of the linked list of scheduled jobs is not NULL. If the head of the linked list is not NULL, it means that there are jobs currently scheduled and the function returns true. If the head of the linked list is NULL, it means that there are no jobs currently scheduled and the function returns false.
 *
 * @return true  if there are jobs currently scheduled, false if there are no jobs currently scheduled
 * @return false  if there are no jobs currently scheduled
 */
bool jobs_scheduled()
{
    return scheduledjobs != NULL;
}

bool jobs_scheduled_relay(uint8_t relay)
{
    for (valvejob_t *job = scheduledjobs; job != NULL; job = job->next)
    {
        if (job->relay == relay)
        {
            return true;
        }
    }

    return false;
}
