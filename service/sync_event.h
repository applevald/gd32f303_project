#ifndef _SYNC_EVENT_H_
#define _SYNC_EVENT_H_

#include <rtthread.h>
#include <rtdevice.h>
#include "task_base.h"

#define FAULT_EVENTTS_MAX_NUM  10
#define FAULT_HANDLING_MAX_NUM 50

typedef struct EVENT_HANDLE _sync_event_handle_t;

struct EVENT_HANDLE
{
    _task_base_t *task;
    void *private_data;
    _sync_event_handle_t *next;
};

typedef struct _EVENTS_GROUP _sync_event_t;

struct _EVENTS_GROUP
{
    rt_uint16_t event;
    _sync_event_handle_t* event_handle_head; // head of event handle list
    _sync_event_t* next;
};


_sync_event_handle_t *register_event(uint16_t event, _task_base_t *task,_sync_event_cb event_cb,void *private_data);
void execute_event(uint16_t event,rt_base_t msg);

// struct _FAULT_HANDLE *malloc_fault_handle(_fault_handle handle,void *private_data);

#endif // _SYNC_EVENT_H_

