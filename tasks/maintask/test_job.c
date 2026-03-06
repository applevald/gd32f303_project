#include "main_task.h"

static _job *input_monitor_job = RT_NULL ;
static _task_base_t *father_task = RT_NULL;
static _dlib_op *dlib_op = RT_NULL;

#define INPUT_MONITOR_JOB_INTERVAL 500 // 100ms
#define HC589_PORT_NUM_T 16 // Number of input ports

void input_monitor_job_fuc(_job *my_jop,void *arg);

#define INPUT_MONITOR_JOB_NAME "input_monitor_job"

int init_input_monitor_job(void)
{
    if(input_monitor_job != RT_NULL)
    {
        return -RT_ERROR;
    }
    father_task = get_task_by_name(MAIN_APP_NAME);
    if(father_task == RT_NULL)
    {
        return -RT_ERROR;
    }
    input_monitor_job = malloc_job(input_monitor_job_fuc, RT_NULL,INPUT_MONITOR_JOB_INTERVAL);
    if(input_monitor_job == RT_NULL)
    {
        return -RT_ERROR;
    }
    add_job_to_main_task(input_monitor_job);

    dlib_op = get_dlib_op();
    return 0;
}

INIT_SUB_MODULE_EXPORT(init_input_monitor_job);

void input_monitor_job_fuc(_job *my_jop,void *arg)
{
    if(my_jop == RT_NULL || my_jop->owner == RT_NULL || dlib_op == RT_NULL)
    {
        rt_kprintf("Invalid parameters in input_monitor_job_fuc\n");
        return;
    }
    static int counter = 0;
    counter++;
    /* do input port check */
    rt_kprintf("Input monitor job running... Counter: %d\n", counter);
    post_event_to_me(my_jop->owner, MAIN_TASK_FLUSH_SYSTEM_EVENT, RT_FALSE);  // Post flush system event
}


