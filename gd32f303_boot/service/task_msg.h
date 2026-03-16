#ifndef _TASK_MSG_H_
#define _TASK_MSG_H_

#include <rtthread.h>

typedef __packed struct TASK_MSG_HEAD{
    uint8_t      dst_task_id;
    uint8_t      src_task_id;
    uint16_t     msg_event;
    uint8_t      length;  // 包含消息头
    uint8_t      body[0]; // 
}_msg_head;

#define FILL_TASK_MSG_HEAD(msgHead, dst, src, event) \
    do { \
        msgHead->dst_task_id = dst; \
        msgHead->src_task_id = src; \
        msgHead->msg_event = event; \
    } while (0)

#define GET_TASK_MSG_BODY(msgHead) \
    (msgHead->body)

#define GET_TASK_MSG_EVET(msgHead) \
    (msgHead->msg_event)

#define GET_TASK_MSG_LEN(msgHead) \
    (msgHead->length)

#define GET_TASK_MSG_BODY_LEN(msgHead) \
    (msgHead->length - sizeof(_msg_head))

#define GET_TASK_MSG_SRC(msgHead) \
    (msgHead->src_task_id)

#define GET_TASK_MSG_DST(msgHead) \
    (msgHead->dst_task_id)

static inline void swap_task_msg_head(_msg_head *srcHead, _msg_head *dstHead)
{
    dstHead->dst_task_id = srcHead->src_task_id;
    dstHead->src_task_id = srcHead->dst_task_id;
}

#endif
