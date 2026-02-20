#ifndef _MAIN_TASK_H_
#define _MAIN_TASK_H_

#include "includes.h"
#include "task_base.h"

#define MAIN_APP_NAME "main_app"
#define MAIN_APP_TASK_STACK_SIZE    8192
#define MAIN_APP_TASK_PRIORITY      8
#define MAIN_APP_TASK_TICKS         10
#define MAIN_APP_MSG_SIZE           16
#define MAIN_APP_MSG_NUM            32

#define MAIN_TASK_JOB_INTERVAL      10  // job interval in ms



typedef struct MAIN_APP_TASK
{
    _task_base_t task;
    _job_list main_job_list; // job list for main task
}_main_task_t;


void add_job_to_main_task(_job *new_job);


#endif
