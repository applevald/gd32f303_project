#include "main_task.h"
#include "main_task_event.h"
#include "includes.h"

_main_task_t main_task = 
{
    .task = 
    {
        .task_name = MAIN_APP_NAME,
        .stack_size = MAIN_APP_TASK_STACK_SIZE,
        .priority = MAIN_APP_TASK_PRIORITY,
        .tick = MAIN_APP_TASK_TICKS,
        .private_data = RT_NULL,
        .msg_num = MAIN_APP_MSG_NUM,
        .msg_size = MAIN_APP_MSG_SIZE,
    },
};

static _task_base_t *this = RT_NULL;

static void main_task_normal_init(rt_base_t event, rt_uint8_t *msg, rt_base_t msg_len);
static void main_task_reset_system(rt_base_t event, rt_uint8_t *msg, rt_base_t msg_len);
static void main_task_jobs_handle(rt_base_t event, rt_uint8_t *msg, rt_base_t msg_len);

static _event_handle_t event_table[] = {
    {MAIN_TASK_EVENT_INIT,                      main_task_normal_init,                NULL},
    {MAIN_TASK_EVENT_RESET_SYSTEM,              main_task_reset_system,              NULL},
    {MAIN_TASK_JOB_START,                       main_task_jobs_handle,                NULL},
    {MAIN_TASK_JOB_STOP,                        main_task_jobs_handle,                NULL},
};

static void main_app_task_entry(void *parameter)
{
    _main_task_t *maintask = (_main_task_t *)parameter;
    _task_base_t *task = &maintask->task;
    int ret = 0;

    if (task == RT_NULL)
    {
        LOG_E("Main task parameter is NULL!");
        return;
    }

    while (RT_TRUE)
    {
        int process_ret = 0;
        ret = rt_mq_recv(task->msg_queue, task->recv_buf, TASK_MSG_BUFF_SIZE, RT_WAITING_FOREVER);
        if (ret < 0)
        {
            LOG_E("Failed to receive message in main task: %d", ret);
            continue;
        }
        // Process the received message here
        // ...
        /* process message */
        process_ret = msg_handle(task, task->recv_buf, ret);
        if (process_ret == -RT_MSG_NO_EVENT_MATCHING)
        {
            LOG_E("task %s no event matching, msg_len: %d", task->task_name, ret);
        }
    }
}
static int main_task_init(void)
{
    main_task.task.task_entry = main_app_task_entry;
    main_task.task.private_data = &main_task;

    this = &main_task.task;

    init_job_list(&main_task.main_job_list, "main_job_queue", MAIN_TASK_JOB_INTERVAL,this); // base interval 100ms

    register_event_table(this, event_table, sizeof(event_table) / sizeof(_event_handle_t));

    init_task_base(&main_task.task);
    
    start_task(this);
    POST_EVENT_TO_ME(MAIN_TASK_EVENT_INIT, RT_FALSE);  // Post initialization event
    return RT_EOK;
}
static void main_task_normal_init(rt_base_t event, rt_uint8_t *msg, rt_base_t msg_len)
{
    LOG_I("Main task initialized successfully.");
    // You can add more initialization logic here if needed
    // For example, you might want to start some jobs or tasks
    /* start dfs */

    // start_job(&main_task.main_job_list); // Start the job list for main task
    POST_EVENT_TO_ME(MAIN_TASK_JOB_START, RT_FALSE);  // Post initialization event
}
static void main_task_jobs_handle(rt_base_t event, rt_uint8_t *msg, rt_base_t msg_len)
{
    LOG_I("Main task job event received: %d", event);
    switch (event)
    {
    case MAIN_TASK_JOB_START:
        start_job(&main_task.main_job_list); // Start the job list for main task
        /* code */
        break;
    case MAIN_TASK_JOB_STOP:
        /* code */
        stop_job(&main_task.main_job_list); // Stop the job list for main task
        break;
    default:
        break;
    }
}
static void main_task_reset_system(rt_base_t event, rt_uint8_t *msg, rt_base_t msg_len)
{
    // Implement system reset logic here
    rt_kprintf("System reset requested.");
    // For example, you might want to call a function to reset the system
    // reset_system();

}
INIT_TASK_EXPORT(main_task_init);

void add_job_to_main_task(_job *new_job)
{
    if (new_job == RT_NULL)
    {
        LOG_E("New job is NULL, cannot add to main task job list.");
        return;
    }
    new_job->owner = this; // Set the owner of the job to the main task
    add_job(&main_task.main_job_list, new_job);
}


void mainapp(int argc,char **argv)
{
    rt_kprintf("Hello, this is the main task!\n");
    // You can add more functionality here as needed
    if(rt_strcmp(argv[1], "reset") == 0)
    {
        post_event_to_me(this, MAIN_TASK_EVENT_RESET_SYSTEM, RT_FALSE);  // Post reset event
        rt_kprintf("System reset event posted.\n");
    }
    else
    {
        rt_kprintf("Usage: mainapp reset\n");
    }
}

MSH_CMD_EXPORT(mainapp, main task command);  // Export the command, format: MSH_CMD_EXPORT(function_name, help_info)
