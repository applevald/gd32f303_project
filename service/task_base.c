#include "task_base.h"
#include "event.h"

static rt_list_t active_task_list = RT_LIST_OBJECT_INIT(active_task_list);

rt_uint16_t cur_max_task_id = 1;

_task_base_t *get_task_by_name(const char *task_name);

int init_task_attr(_task_base_t *task,char *name,rt_uint16_t stack_size,rt_uint8_t priority,rt_uint8_t tick,
                   _task_entry task_entry,void *private_data)
{
    if(task == RT_NULL || name == RT_NULL || task_entry == RT_NULL)
    {
        return -RT_ERROR;
    }
    rt_strncpy(task->task_name, name, sizeof(task->task_name) - 1);
    task->task_name[sizeof(task->task_name) - 1] = '\0'; // Ensure null termination
    task->stack_size = stack_size;
    task->priority = priority;
    task->tick = tick;
    task->task_entry = task_entry;
    task->private_data = private_data;
    task->thread = RT_NULL;
    task->msg_queue = RT_NULL;
    task->msg_queue_mutex = RT_NULL;
    if(task->msg_size == 0 || task->msg_num == 0)
    {
        rt_kprintf("task %s msg size or mag num is zero, set to default\n", task->task_name);
        task->msg_size = RT_MSG_MAX_SIZE;
        task->msg_num = RT_MSG_MAX_NUM;
    }
    rt_list_init(&task->list);
    return 0;
}

void set_task_msg_size(_task_base_t *task, rt_uint8_t msg_size, rt_uint8_t msg_num)
{
    if(task == RT_NULL)
    {
        return;
    }
    task->msg_size = msg_size;
    task->msg_num = msg_num;
}

void task_monitor_t(struct TASK_BASE *task)
{
    if(task == RT_NULL)
    {
        return;
    }
    extern void execute_event(rt_uint16_t event, void *arg);
    execute_event(SYNC_SYSTEM_KICKED,0);
}
int init_task_base(_task_base_t *task)
{
    if(task == RT_NULL)
    {
        return -RT_ERROR;
    }
    /* thread create */
    rt_kprintf("[DEBUG] Creating thread '%s': stack=%d, priority=%d, tick=%d\n",
               task->task_name, task->stack_size, task->priority, task->tick);
    task->thread = rt_thread_create(task->task_name, task->task_entry, task->private_data, task->stack_size, task->priority, task->tick);
    if (task->thread == RT_NULL)
    {
        return -RT_ERROR;
    }
    task->msg_queue = rt_mq_create(task->task_name, task->msg_size, task->msg_num, RT_IPC_FLAG_FIFO);
    if (task->msg_queue == RT_NULL)
    {
        return -RT_ERROR;
    }
    task->msg_queue_mutex = rt_mutex_create(task->task_name, RT_IPC_FLAG_FIFO);
    if (task->msg_queue_mutex == RT_NULL)
    {
        return -RT_ERROR;
    }
    register_task(task);
    return 0;
}


void register_task(_task_base_t *task)
{
    /* check if task exist */
    if (task == RT_NULL || task->thread == RT_NULL)
    {
        return;
    }
    if(get_task_by_name(task->task_name) != RT_NULL)
    {
        rt_kprintf("task %s already exist\n",task->task_name);
        return;
    }

    task->task_id = cur_max_task_id++;
    task->task_run_tick = 0;
    if(task->task_monitor == RT_NULL)
    {
        task->task_monitor = task_monitor_t;
    }
    rt_list_insert_after(&active_task_list, &task->list); 
}

void start_task(_task_base_t *task)
{
    if(task == RT_NULL || task->thread == RT_NULL)
    {
        return;
    }
    rt_thread_startup(task->thread);
}

void unregister_task(_task_base_t *task)
{
    rt_list_remove(&task->list);
}
void register_event_table(_task_base_t *task, _event_handle_t *event_table , rt_uint8_t table_size)
{
    if(task == RT_NULL || event_table == RT_NULL)
    {
        return;
    }
    if(task->event_table == RT_NULL)
    {
        task->event_table = event_table;
    }
    task->table_size = table_size;
}
_task_base_t *get_task_by_id(rt_uint8_t task_id)
{
    _task_base_t *task = RT_NULL;
    rt_list_t *node = active_task_list.next;
    while (node != &active_task_list)
    {
        task = rt_list_entry(node, _task_base_t, list);
        if (task->task_id == task_id)
        {
            return task;
        }
        node = node->next;
    }
    return RT_NULL;
}
_task_base_t *get_task_by_name(const char *task_name)
{
    _task_base_t *task = RT_NULL;
    rt_list_t *node = active_task_list.next;
    while (node != &active_task_list)
    {
        task = rt_list_entry(node, _task_base_t, list);
        if (rt_strcmp(task->task_name, task_name) == 0)
        {
            return task;
        }
        node = node->next;
    }
    return RT_NULL;
}
rt_err_t send_task_msg(_task_base_t *task, uint8_t *msg,uint16_t msg_len,uint8_t is_from_irq)
{
    rt_err_t ret = RT_EOK;
    if(task == RT_NULL || task->msg_queue == RT_NULL)
    {
        return -RT_ERROR;
    }
    if(is_from_irq == RT_TRUE)
    {
        ret = rt_mq_urgent(task->msg_queue, msg, msg_len);
    }
    else
    {
        rt_mutex_take(task->msg_queue_mutex, RT_WAITING_FOREVER);
        rt_err_t ret = rt_mq_send(task->msg_queue, msg, msg_len);
        rt_mutex_release(task->msg_queue_mutex);
    }
    return ret;
}


rt_err_t send_task_msg_by_id(rt_uint8_t task_id, uint8_t *msg,uint16_t msg_len,uint8_t is_from_irq)
{
    _task_base_t *task = get_task_by_id(task_id);
    if(task == RT_NULL || task->msg_queue == RT_NULL)
    {
        return -RT_ERROR;
    }
    return send_task_msg(task, msg, msg_len,is_from_irq);
}

rt_err_t send_task_msg_by_name(const char *task_name, uint8_t *msg,uint16_t msg_len,uint8_t is_from_irq)
{
    _task_base_t *task = RT_NULL;
    rt_list_t *node = active_task_list.next;
    while (node != &active_task_list)
    {
        task = rt_list_entry(node, _task_base_t, list);
        if (rt_strcmp(task->task_name, task_name) == 0)
        {
            return send_task_msg(task, msg, msg_len,is_from_irq);
        }
        node = node->next;
    }
    return -RT_ERROR;
}
/* send to me */
rt_err_t post_event_to_me(_task_base_t *task, rt_uint16_t msg_event,uint8_t is_from_irq)
{
    _msg_head msg = {0};
    if(task == RT_NULL || task->msg_queue == RT_NULL)
    {
        return -RT_ERROR;
    }
    msg.dst_task_id = task->task_id;
    msg.src_task_id = task->task_id;
    msg.msg_event = msg_event;
    msg.length = sizeof(_msg_head);
    return send_task_msg(task, (uint8_t *)&msg, sizeof(_msg_head),is_from_irq);
}
rt_err_t post_msg_to_me(_task_base_t *task, rt_uint16_t msg_event, uint8_t *msg, uint16_t msg_len,uint8_t is_from_irq)
{
    _msg_head *msg_head = (_msg_head *)task->send_buf;
    if(task == RT_NULL || task->msg_queue == RT_NULL)
    {
        return -RT_ERROR;
    }
    FILL_TASK_MSG_HEAD(msg_head, task->task_id, task->task_id, msg_event);
    msg_head->length = sizeof(_msg_head) + msg_len;
    rt_memcpy(msg_head->body, msg, msg_len);
    msg_head->body[msg_len] = '\0';
    return send_task_msg(task, (uint8_t *)task->send_buf, msg_head->length,is_from_irq);
}
rt_err_t post_event_to_task_by_name(_task_base_t *me,char *target_name, rt_uint16_t msg_event,uint8_t is_from_irq)
{
    _task_base_t *task = get_task_by_name(target_name);
    _msg_head msg = {0};
    if(task == RT_NULL || task->msg_queue == RT_NULL)
    {
        rt_kprintf("task %s not exist\n",target_name);
        return -RT_ERROR;
    }
    if(me == RT_NULL)
    {
        rt_kprintf("me task %s not could not be null\n",me->task_name);
        return -RT_ERROR;
    }
    msg.dst_task_id = task->task_id;
    msg.src_task_id = me->task_id;
    msg.msg_event = msg_event;
    msg.length = sizeof(_msg_head);
    return send_task_msg(task, (uint8_t *)&msg, sizeof(_msg_head),is_from_irq);
}
rt_err_t post_event_to_task(_task_base_t *me,_task_base_t *target, rt_uint16_t msg_event,uint8_t is_from_irq)
{
    if(target == RT_NULL || target->msg_queue == RT_NULL)
    {
        rt_kprintf("task %s not exist\n",target->task_name);
        return -RT_ERROR;
    }
    if(me == RT_NULL)
    {
        rt_kprintf("me task %s not could not be null\n",me->task_name);
        return -RT_ERROR;
    }
    _msg_head msg = {0};
    msg.dst_task_id = target->task_id;
    msg.src_task_id = me->task_id;
    msg.msg_event = msg_event;
    msg.length = sizeof(_msg_head);
    return send_task_msg(target, (uint8_t *)&msg, sizeof(_msg_head),is_from_irq);
}
rt_err_t post_msg_to_task(_task_base_t *me,char *target_name, rt_uint16_t msg_event, uint8_t *msg, uint16_t msg_len,uint8_t is_from_irq)
{
    _task_base_t *task = get_task_by_name(target_name);
    _msg_head *msg_head = (_msg_head *)me->send_buf;
    if(task == RT_NULL || task->msg_queue == RT_NULL)
    {
        rt_kprintf("task %s not exist\n",target_name);
        return -RT_ERROR;
    }
    if(me == RT_NULL)
    {
        rt_kprintf("me task %s not could not be null\n",me->task_name);
        return -RT_ERROR;
    }
    FILL_TASK_MSG_HEAD(msg_head, task->task_id, me->task_id, msg_event);
    msg_head->length = sizeof(_msg_head) + msg_len;
    rt_memcpy(msg_head->body, msg, msg_len);
    msg_head->body[msg_len] = '\0';
    return send_task_msg(task, (uint8_t *)me->send_buf, msg_head->length,is_from_irq);
}

/* send to me */
//rt_err_t post_event_to_me(_task_base_t *task, rt_uint8_t msg_event,uint8_t is_from_irq)
//{
//    _msg_head msg = {0};
//    if(task == RT_NULL || task->msg_queue == RT_NULL)
//    {
//        return -RT_ERROR;
//    }
//    msg.dst_task_id = task->task_id;
//    msg.src_task_id = task->task_id;
//    msg.msg_event = msg_event;
//    msg.length = sizeof(_msg_head);
//    return send_task_msg(task, (uint8_t *)&msg, sizeof(_msg_head),is_from_irq);
//}
//rt_err_t post_msg_to_me(_task_base_t *task, rt_uint8_t msg_event, uint8_t *msg, uint16_t msg_len,uint8_t is_from_irq)
//{
//    _msg_head *msg_head = (_msg_head *)task->send_buf;
//    if(task == RT_NULL || task->msg_queue == RT_NULL)
//    {
//        return -RT_ERROR;
//    }
//    FILL_TASK_MSG_HEAD(msg_head, task->task_id, task->task_id, msg_event);
//    msg_head->length = sizeof(_msg_head) + msg_len - 2;
//    rt_memcpy(msg_head->body, msg, msg_len);
//    msg_head->body[msg_len] = '\0';
//    return send_task_msg(task, (uint8_t *)task->send_buf, msg_head->length,is_from_irq);
//}
//rt_err_t post_event_to_task(_task_base_t *me,char *target_name, rt_uint8_t msg_event,uint8_t is_from_irq)
//{
//    _task_base_t *task = get_task_by_name(target_name);
//    _msg_head msg = {0};
//    if(task == RT_NULL || task->msg_queue == RT_NULL)
//    {
//        rt_kprintf("task %s not exist\n",target_name);
//        return -RT_ERROR;
//    }
//    if(me == RT_NULL)
//    {
//        rt_kprintf("me task %s not could not be null\n",me->task_name);
//        return -RT_ERROR;
//    }
//    msg.dst_task_id = task->task_id;
//    msg.src_task_id = me->task_id;
//    msg.msg_event = msg_event;
//    msg.length = sizeof(_msg_head);
//    return send_task_msg(task, (uint8_t *)&msg, sizeof(_msg_head),is_from_irq);
//}
//rt_err_t post_msg_to_task(_task_base_t *me,char *target_name, rt_uint8_t msg_event, uint8_t *msg, uint16_t msg_len,uint8_t is_from_irq)
//{
//    _task_base_t *task = get_task_by_name(target_name);
//    _msg_head *msg_head = (_msg_head *)me->send_buf;
//    if(task == RT_NULL || task->msg_queue == RT_NULL)
//    {
//        rt_kprintf("task %s not exist\n",target_name);
//        return -RT_ERROR;
//    }
//    if(me == RT_NULL)
//    {
//        rt_kprintf("me task %s not could not be null\n",me->task_name);
//        return -RT_ERROR;
//    }
//    FILL_TASK_MSG_HEAD(msg_head, task->task_id, me->task_id, msg_event);
//    msg_head->length = sizeof(_msg_head) + msg_len - 2;
//    rt_memcpy(msg_head->body, msg, msg_len);
//    msg_head->body[msg_len] = '\0';
//    return send_task_msg(task, (uint8_t *)me->send_buf, msg_head->length,is_from_irq);
//}

int msg_handle(_task_base_t *task, uint8_t *msg,uint16_t msg_len)
{
    uint8_t *msg_buff = (uint8_t *)msg;
    _msg_head *msg_head = (_msg_head *)msg_buff;
    if(task == RT_NULL)
    {
        return -RT_ERROR;
    }
    if(msg == RT_NULL || msg_len < sizeof(_msg_head))
    {
        return -RT_ERROR;
    }
    task->task_run_tick ++;
    if(task->task_run_tick > RT_MAX_RUN_COUNT_KICK)
    {
        task->task_run_tick = 0;
        if(task->task_monitor != RT_NULL)
        {
            task->task_monitor(task);
        }
    }
#if 0
    int leftLen = (int)msg_len - (int)msgHead->length;
    do
    {
        /* code */
        if(leftLen < 0)
        {
            rt_kprintf("msg len error, msg_len: %d, leftLen: %d\n",msg_len,leftLen);
            return -RT_ERROR;
        }
        if(msg_head->dst_task_id != task->task_id)
        {
            rt_kprintf("msg dst task id error, msg_head->dst_task_id: %d, task->task_id: %d\n",msg_head->dst_task_id,task->task_id);
            return -RT_ERROR;
        }
        else
        {
            for(int i = 0; i < task->table_size; i++)
            {
                if(task->event_table[i].msg_event == GET_TASK_MSG_EVET(msg_head))
                {
                    // if(task->event_table[i].expect_msg_len == GET_TASK_MSG_BODY_LEN(msg_head))
                    {
                        task->event_table[i].event_cb(task->event_table[i].msg_event ,msg,msg_len);
                        return 0;
                    }
                }
                else if(task->event_table[i].msg_event == EVENT_NONE)
                {
                    /* NO more msg_event */
                    break;
                }
                else if(task->event_table[i].msg_event == EMPTY_EVENT)
                {
                    break;
                }  
            }
        }
        if(leftLen > 0)
        {
            msg_buff =  msg_buff + (int)msgHead->length;
            leftLen = leftLen - (int)msgHead->length;
            msg_head = (_msg_head *)msg_buff;
        }
        else
        {
            break;
        }
        
    } while (leftLen > 0);
 #else
 for(int i = 0; i < task->table_size; i++)
 {
     if(task->event_table[i].event == GET_TASK_MSG_EVET(msg_head))
     {
         // if(task->event_table[i].expect_msg_len == GET_TASK_MSG_BODY_LEN(msg_head))
         if(task->event_table[i].event_cb != RT_NULL)
         {
             task->event_table[i].event_cb(task->event_table[i].event ,msg,msg_len);
             return 0;
         }
     }
     else if(task->event_table[i].event == EVENT_NONE || task->event_table[i].event == EMPTY_EVENT)
     {
         /* NO more msg_event */
         break;
     }
 }
 #endif   
    
    //rt_kprintf("task:%s msg no match msg_event: %d, expect msg len: %d\n",task->task_name,GET_TASK_MSG_EVET(msg_head),GET_TASK_MSG_BODY_LEN(msg_head));
	return -RT_MSG_NO_EVENT_MATCHING;
}

void print_len_data_in_hex(char *head,uint8_t *data, uint16_t len)
{
    if(data == RT_NULL || len == 0)
    {
        return;
    }
    rt_kprintf("%s: ", head);
    for(uint16_t i = 0; i < len; i++)
    {
        rt_kprintf("%02X ", data[i]);
    }
    rt_kprintf("\n");
}
static char printbuf[2048];
void nprintf(char *fmt, ...)
{
    int n = 0;
    // enter critical
    rt_memset(printbuf, 0, sizeof(printbuf));
    va_list args;
    va_start(args, fmt);
    n = vsprintf(printbuf, fmt, args);
    va_end(args);
    // leave critical
    rt_base_t level = rt_hw_interrupt_disable();
    rt_kprintf("%s", printbuf);
    rt_hw_interrupt_enable(level);
}
void print_buf_in_hex(char *head,uint8_t *data, uint16_t len)
{
    if(data == RT_NULL || len == 0)
    {
        return;
    }
    rt_base_t level;
    level = rt_hw_interrupt_disable();
    rt_kprintf("%s: ", head);
    for(uint16_t i = 0; i < len; i++)
    {
        rt_kprintf("%02X ", data[i]);
        if((i + 1) % 16 == 0)
        {
            rt_kprintf("\n");
        }
    }
    rt_kprintf("\n");
    rt_hw_interrupt_enable(level);
}

