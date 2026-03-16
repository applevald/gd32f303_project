#ifndef _SINGLE_FSM_H_
#define _SINGLE_FSM_H_

// #include "task_base.h"
// typedef struct TASK_BASE _task_base_t;
// typedef struct _SINGLE_FSM _single_fsm_t;

typedef int (*_fsm_handler)(uint8_t *msg, uint16_t len, unsigned short *is_custom_next_state, unsigned short *custom_next_state);
typedef int (*_fsm_res_handler)(int cur_state, uint8_t *msg, uint16_t len);

#define FSM_RET_SUCC 0
#define FSM_RET_FAIL -1


typedef struct _SINGLE_FSM
{
    // char name[32]; // FSM name
    int state; // current state
    _fsm_handler handler; // FSM handler function
    void *private_data; // private data for the FSM
    unsigned short default_succ_state; // default state of the FSM
    unsigned short default_fail_state; // default fail state of the FSM
    _fsm_res_handler succ_handler; // success handler
    _fsm_res_handler fail_handler; // fail handler
} _single_fsm_t;



int run_single_fsm(void *task_t,int *state,uint8_t *msg,uint16_t len);

#endif /* _SINGLE_FSM_H_ */
