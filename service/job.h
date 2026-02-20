#ifndef __JOB_H__
#define __JOB_H__

#include "task_base.h"

#define DEFAULT_JOB_INTERVAL 10      // default job interval in ms
typedef struct _JOP_ _job;
typedef void (*_job_entry)(_job *my_jop,void *arg);

struct _JOP_
{
	int count;
	int exp_count ; 
    int run_interval; // run interval in ms  
	void *private_para ;
    _task_base_t *owner; // father task
	// void (*enter_jop)(void *para);
	// void (*exit_jop)(void *para);
    _job_entry job_fuc;
	_job *next_jop;
};

typedef struct _JOB_LIST_
{
    char name[32];
    unsigned int base_interval; // base interval for job scheduling
    _job *head;
    rt_timer_t timer; // timer for job scheduling
    _task_base_t *owner; // owner task for job list
    void *next; // next job list in the same task
}_job_list;    

// void init_job_list(_job_list *job_list, unsigned int base_interval);
void init_job_list(_job_list *job_list, char *job_name,unsigned int base_interval,_task_base_t *owner);
_job *malloc_job(_job_entry job_fuc, void *private_para, int run_interval);
void add_job(_job_list *job_list,_job *new_job);
void run_job(_job_list *job_list);
void stop_job(_job_list *job_list);
void start_job(_job_list *job_list);

#endif
