#include "job.h"

rt_uint8_t cur_max_job_id = 1;
_job_list *job_group = RT_NULL; // global job group


void add_job_list_to_group(_job_list *job_list)
{
    if(job_list == NULL)
    {
        return;
    }
    job_list->next = RT_NULL; // Ensure the next pointer is NULL
    if(job_group == RT_NULL)
    {
        job_group = job_list;
    }
    else
    {
        _job_list *temp = job_group;
        while(temp->next != NULL)
        {
            temp = (_job_list *)temp->next;
        }
        temp->next = job_list;
    }
}
void init_job_list(_job_list *job_list, char *job_name,unsigned int base_interval,_task_base_t *owner)
{
    if(job_list == NULL)
    {
        return;
    }
    job_list->base_interval = base_interval;
    job_list->head = NULL;
    job_list->timer = NULL;
    job_list->owner = owner;
    if(job_name != NULL)
    {
        rt_strncpy(job_list->name, job_name, sizeof(job_list->name) - 1);
        job_list->name[sizeof(job_list->name) - 1] = '\0'; // Ensure null termination
    }
    else
    {
        char default_name[32] = "job_list_";
        rt_snprintf(default_name + 9, sizeof(default_name) - 9, "%d", cur_max_job_id++);
        rt_strncpy(job_list->name, default_name, sizeof(job_list->name) - 1);
        job_list->name[sizeof(job_list->name) - 1] = '\0'; // Ensure null termination
    }
    add_job_list_to_group(job_list);
}

_job *malloc_job(_job_entry job_fuc, void *private_para, int run_interval)
{
    _job *new_job = (_job *)rt_malloc(sizeof(_job));
    if(new_job == RT_NULL)
    {
        return RT_NULL;
    }
    new_job->count = 0;
    new_job->run_interval = run_interval;
    new_job->private_para = private_para;
    new_job->job_fuc = job_fuc;
    new_job->next_jop = RT_NULL;
    return new_job;
}

void add_job(_job_list *job_list,_job *new_job)
{
    if(job_list == NULL || new_job == NULL)
    {
        return;
    }
    new_job->count = 0;
    if(new_job->run_interval <= 0 || new_job->run_interval < job_list->base_interval)
    {
        new_job->run_interval = job_list->base_interval; // set default interval if not specified
    }
    new_job->exp_count = new_job->run_interval / job_list->base_interval; // calculate
    if(job_list->head == NULL)
    {
        job_list->head = new_job;
        new_job->next_jop = NULL;
    }
    else
    {
        _job *temp = job_list->head;
        while(temp->next_jop != NULL)
        {
            temp = (_job *)temp->next_jop;
        }
        temp->next_jop = new_job;
        new_job->next_jop = NULL;
    }
}

void run_job(_job_list *job_list)
{
    if(job_list == NULL || job_list->head == NULL)
    {
        return;
    }
    _job *current_job = job_list->head;
    while(current_job != NULL)
    {
        if(current_job->exp_count > 0)
        {
            current_job->count++;
            if(current_job->count >= current_job->exp_count)
            {
                current_job->count = 0;
                if(current_job->job_fuc != NULL)
                {
                    current_job->job_fuc(current_job, current_job->private_para);
                }
            }
        }
        current_job = current_job->next_jop;
    }
}

void job_timer_callback(void *parameter)
{
    _job_list *job_list = (_job_list *)parameter;
    if(job_list == NULL)
    {
        return;
    }
    run_job(job_list);
}
void start_job(_job_list *job_list)
{
    if(job_list == NULL)
    {
        return;
    }
    if(job_list->timer == NULL)
    {
        job_list->timer = rt_timer_create(job_list->name, job_timer_callback, job_list, job_list->base_interval, RT_TIMER_FLAG_PERIODIC|RT_TIMER_FLAG_SOFT_TIMER);
        if(job_list->timer != NULL)
        {
            rt_timer_start(job_list->timer);
        }
    }
}
void stop_job(_job_list *job_list)
{
    if(job_list == NULL || job_list->timer == NULL)
    {
        return;
    }
    rt_timer_stop(job_list->timer);
}

