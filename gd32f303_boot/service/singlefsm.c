#include "task_base.h"
#include "singlefsm.h"

void register_fsm_table(_task_base_t *task, _single_fsm_t *fsm_table , rt_uint8_t table_size)
{
    if(task == RT_NULL || fsm_table == RT_NULL)
    {
        return;
    }
    if(task->fsm_table == RT_NULL)
    {
        task->fsm_table = fsm_table;
    }
    task->fsm_table_num = table_size;
}

int run_single_fsm(void *task_t, int *state,uint8_t *msg, uint16_t len)
{
    _task_base_t *task = (_task_base_t *)task_t;
    if(task == RT_NULL || task->fsm_table == RT_NULL || task->fsm_table_num == 0)
    {
        rt_kprintf("Invalid parameters in run_single_fsm\n");
        return FSM_RET_FAIL;
    }
    uint16_t cur_state = *state;
    for(int i = 0; i < task->fsm_table_num; i++)
    {
        if(task->fsm_table[i].state == cur_state)
        {
            unsigned short is_custom_next_state = RT_FALSE;
            unsigned short custom_next_state = task->fsm_table[i].default_succ_state;
            int ret = task->fsm_table[i].handler(msg, len, &is_custom_next_state, &custom_next_state);
            if(ret == 0) // Success
            {
                if(is_custom_next_state)
                {
                    // task->fsm_table[i].state = custom_next_state;
                    *state = custom_next_state;
                }
                else
                {
                    *state  = task->fsm_table[i].default_succ_state;
                }
                if(task->fsm_table[i].succ_handler != NULL)
                {
                    task->fsm_table[i].succ_handler(cur_state, msg, len);
                }
            }
            else // Failure
            {
                if(is_custom_next_state)
                {
                    *state = custom_next_state;
                }
                else
                {
                    *state = task->fsm_table[i].default_fail_state;
                }
                if(task->fsm_table[i].fail_handler != NULL)
                {
                    task->fsm_table[i].fail_handler(cur_state, msg, len);
                }
            }
            return ret; // Return the result of the FSM handler
        }
    }
    rt_kprintf("No matching FSM state found for state: %d\n", cur_state);
    return FSM_RET_FAIL; // No matching state found
}

