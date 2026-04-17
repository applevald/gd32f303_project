/*
 * @file windows.h
 * @brief 天窗控制模块头文件
 */

#ifndef __WINDOWS_H__
#define __WINDOWS_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 状态定义 */
typedef enum {
    WINDOW_IDLE = 0,        /* 空闲 */
    WINDOW_OPENING,         /* 正在打开 */
    WINDOW_CLOSING,         /* 正在关闭 */
    WINDOW_ERROR            /* 错误状态 */
} window_state_t;

typedef enum {
    WINDOW_OK = 0,          /* 正常 */
    WINDOW_ERR_OVERCURRENT_A,   /* 电机A过流 */
    WINDOW_ERR_OVERCURRENT_B,   /* 电机B过流 */
    WINDOW_ERR_FAULT,           /* 驱动器故障 */
    WINDOW_ERR_TIMEOUT          /* 超时 */
} window_error_t;

/**
 * @brief 天窗状态变化回调函数类型
 * @param status 协议状态码
 */
typedef void (*window_status_callback_t)(uint8_t status);

/**
 * @brief  天窗模块初始化
 * @return RT_EOK: 成功, -RT_ERROR: 失败
 */
int windows_init(void);

/**
 * @brief  天窗打开（非阻塞）
 * @return RT_EOK: 成功启动, -RT_ERROR: 失败
 */
rt_err_t window_open(void);

/**
 * @brief  天窗关闭（非阻塞）
 * @return RT_EOK: 成功启动, -RT_ERROR: 失败
 */
rt_err_t window_close(void);

/**
 * @brief  紧急停止（非阻塞）
 */
void window_emergency_stop(void);

/**
 * @brief  获取天窗状态
 * @return 当前状态
 */
window_state_t window_get_state(void);

/**
 * @brief  获取错误代码
 * @return 错误代码
 */
window_error_t window_get_error(void);

/**
 * @brief  获取天窗协议状态码
 * @return 协议状态码 (0-5)
 */
uint8_t window_get_protocol_status(void);

/**
 * @brief  获取天窗当前物理限位组合状态
 * @return 0x10-0x13:
 *         0x10: 两个限位都触发 (异常)
 *         0x11: 仅关限位触发
 *         0x12: 仅开限位触发
 *         0x13: 两个限位都未触发 (中间位置)
 */
uint8_t window_get_limit_combined_status(void);

/**
 * @brief  注册状态变化回调函数
 * @param  callback: 回调函数指针
 */
void window_register_status_callback(window_status_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif /* __WINDOWS_H__ */
