#include "sync_event.h"

static _sync_event_t *event_list = RT_NULL;

static void add_event_handle_to_group(_sync_event_t *event_group, _sync_event_handle_t *event_handle)
{
    if(event_group != NULL && event_handle != NULL)
    {
        _sync_event_handle_t *temp_handle = event_group->event_handle_head;
        if(temp_handle == NULL)
        {
            event_group->event_handle_head = event_handle;
            event_handle->next = RT_NULL;
        }
        else
        {
            while(temp_handle->next != NULL)
            {
                temp_handle = (_sync_event_handle_t *)temp_handle->next;
            }
            temp_handle->next = (void *)event_handle;
			event_handle->next = RT_NULL;
        }
    }
}
void insert_event_handle_to_group(_sync_event_t *event_t)
{
    if(event_list == NULL)
    {
        event_list = event_t;
		event_t->next = RT_NULL;
    }
    else
    {
        _sync_event_t *node = event_list;
        while (node->next != RT_NULL)
        {
            node = node->next;
        }
        node->next = event_t;
		event_t->next = RT_NULL;
    }
}
_sync_event_handle_t *register_event(uint16_t event, _task_base_t *task,_sync_event_cb event_cb,void *private_data)
{
    // check if event already exists
    _sync_event_t *node = event_list;
    while (node != RT_NULL)
    {
        _sync_event_t *event_p = node;
        if (event_p->event == event)
        {
            break;
        }
        node = node->next;
    }

    if(node == RT_NULL)
    {
        _sync_event_t *new_event = rt_malloc(sizeof(_sync_event_t));
        if (new_event == RT_NULL)
        {
            rt_kprintf("Failed to allocate memory for new event!!\n");
            return RT_NULL;
        }
        task->sync_event_cb = event_cb;
        new_event->event = event;
        new_event->event_handle_head = RT_NULL;
        new_event->next = RT_NULL;
        _sync_event_handle_t *new_event_handle = rt_malloc(sizeof(_sync_event_handle_t));
        if (new_event_handle == RT_NULL)
        {
            rt_kprintf("Failed to allocate memory for new event handle!!\n");
            rt_free(new_event);
            return RT_NULL;
        }
        new_event_handle->task = task;
        new_event_handle->private_data = private_data;
        add_event_handle_to_group(new_event, new_event_handle);
        insert_event_handle_to_group(new_event);
        return new_event_handle;
    }
    else
    {
        _sync_event_t *event_p = (_sync_event_t *)node;
        _sync_event_handle_t *new_event_handle = rt_malloc(sizeof(_sync_event_handle_t));
        if (new_event_handle == RT_NULL)
        {
            rt_kprintf("Failed to allocate memory for new event handle!!\n");
            return RT_NULL;
        }
        task->sync_event_cb = event_cb;
        new_event_handle->task = task;
        new_event_handle->private_data = private_data;
        add_event_handle_to_group(event_p, new_event_handle);
    }
    return RT_NULL;
}
void execute_event(uint16_t event,rt_base_t msg)
{
    _sync_event_t *node = event_list;
    while (node != RT_NULL)
    {
        _sync_event_t *event_p = node;
        if (event_p->event == event)
        {
            _sync_event_handle_t *handle = event_p->event_handle_head;
            while(handle != NULL)
            {
                if(handle->task->sync_event_cb != RT_NULL)
                    handle->task->sync_event_cb(event,handle->private_data,msg);
                handle = handle->next;
            }
            break;
        }
        node = node->next;
    }
}
