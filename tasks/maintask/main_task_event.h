#ifndef _MAIN_TASK_EVENT_H_
#define _MAIN_TASK_EVENT_H_

enum MAIN_TASK_EVENT_CMD
{
    MAIN_TASK_EVENT_NONE = 0,                  // 无事件
    MAIN_TASK_EVENT_INIT,                      // 初始化事件
    MAIN_TASK_EVENT_RESET_SYSTEM,              // 重置系统事件
    MAIN_TASK_JOB_START,                      // 启动任务事件
    MAIN_TASK_JOB_STOP,                       // 停止任务事件
    MAIN_TASK_FLUSH_SYSTEM_EVENT,              // 刷新系统事件
    MAIN_TASK_EVENT_MAX,                       // 事件最大值
};



#endif
