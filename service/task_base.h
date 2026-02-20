#ifndef _TASK_BASE_H_
#define _TASK_BASE_H_

#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "task_msg.h"
#include "rtdbg.h"
#include "myport.h"
#include "singlefsm.h"

#define USING_EVENT_TABLE 1

#define RT_MSG_BASE_ERROR            RT_ERROR + 500
#define RT_MSG_NO_EVENT_MATCHING     RT_MSG_BASE_ERROR + 1
#define RT_MSG_TASK_NOT_EXIST        RT_MSG_BASE_ERROR + 2
#define RT_MSG_TASK_CALLBK_NO_VALID  RT_MSG_BASE_ERROR + 3

#define RT_MSG_MAX_NUM          48
#define RT_MSG_MAX_SIZE         128

#define RT_MAX_RUN_COUNT_KICK   10000000


typedef void (*_task_entry)(void *arg);
typedef void (*_sync_event_cb)(rt_base_t event,void *data,rt_base_t msg);
typedef void (*_event_cb)(rt_base_t event,rt_uint8_t *msg,rt_base_t msg_len);

#define EMPTY_EVENT 0x00
#define EVENT_NONE  0x00

#define TASK_MSG_BUFF_SIZE 128

typedef struct EVENT_TABLE
{
    uint16_t event;
    _event_cb event_cb; // callback function
    // uint16_t expect_msg_len; // expect msg len
    // uint8_t is_log;
    void *next;
}_event_handle_t;

typedef struct TASK_BASE
{
    char task_name[32];
    rt_thread_t thread;
    rt_uint8_t task_id;
    /* stack */
    rt_uint16_t stack_size;
    rt_uint8_t priority;
    rt_uint16_t tick; // tick time

    rt_uint8_t msg_num; // msg num in queue
    rt_uint8_t msg_size; // msg len in queue

    /* queue */
    rt_mutex_t msg_queue_mutex;
    rt_mq_t msg_queue;
    _sync_event_cb sync_event_cb;

    /* send buff */
    uint8_t send_buf[TASK_MSG_BUFF_SIZE];
    uint8_t recv_buf[TASK_MSG_BUFF_SIZE];

    /* list */
    rt_list_t list;
#ifdef USING_EVENT_TABLE
    _event_handle_t *event_table;
    rt_uint16_t     table_size; // size of event table
#else
    _event_handle_t *event_list; // list of events
#endif
    _task_entry task_entry;
    void *private_data; // private data for task
    
    uint32_t task_run_tick; // task run tick

    _single_fsm_t *fsm_table; // fsm table for task
    rt_uint8_t fsm_table_num; // size of fsm table
    void (*task_monitor)(struct TASK_BASE *task); // task monitor function pointer
}_task_base_t;

int init_task_attr(_task_base_t *task,char *name,rt_uint16_t stack_size,rt_uint8_t priority,rt_uint8_t tick,
                   _task_entry task_entry,void *private_data);
void set_task_msg_size(_task_base_t *task, rt_uint8_t msg_size, rt_uint8_t msg_num);
int init_task_base(_task_base_t *task);
void register_task(_task_base_t *task);
void start_task(_task_base_t *task);
void register_event_table(_task_base_t *task, _event_handle_t *event_table , rt_uint8_t table_size);
rt_err_t send_task_msg(_task_base_t *task, uint8_t *msg,uint16_t msg_len,uint8_t is_from_irq);
rt_err_t send_task_msg_by_id(rt_uint8_t task_id, uint8_t *msg,uint16_t msg_len,uint8_t is_from_irq);
rt_err_t send_task_msg_by_name(const char *task_name, uint8_t *msg,uint16_t msg_len,uint8_t is_from_irq);
rt_err_t post_event_to_me(_task_base_t *task, rt_uint16_t msg_event,uint8_t is_from_irq);
rt_err_t post_msg_to_me(_task_base_t *task, rt_uint16_t msg_event, uint8_t *msg, uint16_t msg_len,uint8_t is_from_irq);
rt_err_t post_event_to_task_by_name(_task_base_t *me,char *target_name, rt_uint16_t msg_event,uint8_t is_from_irq);
rt_err_t post_msg_to_task(_task_base_t *me,char *target_name, rt_uint16_t msg_event, uint8_t *msg, uint16_t msg_len,uint8_t is_from_irq);
rt_err_t post_event_to_task(_task_base_t *me,_task_base_t *target, rt_uint16_t msg_event,uint8_t is_from_irq);
int msg_handle(_task_base_t *task, uint8_t *msg,uint16_t msg_len);

void unregister_task(_task_base_t *task);
void print_len_data_in_hex(char *head,uint8_t *data, uint16_t len);

_task_base_t *get_task_by_name(const char *task_name);

#define POST_EVENT_TO_ME(event ,is_from_irq)                                post_event_to_me(this, event, is_from_irq)
#define POST_EVENT_TO_TASK(target_name, event ,is_from_irq)                 post_event_to_task_by_name(this, target_name, event, is_from_irq)
#define POST_MSG_TO_ME(event, msg, msg_len, is_from_irq)                    post_msg_to_me(this, event, msg, msg_len, is_from_irq)
#define POST_MSG_TO_TASK(target_name, event, msg, msg_len, is_from_irq)     post_msg_to_task(this, target_name, event, msg, msg_len, is_from_irq)

void print_buf_in_hex(char *head,uint8_t *data, uint16_t len);
void nprintf(char *fmt, ...);

// #include "job.h"

#endif

