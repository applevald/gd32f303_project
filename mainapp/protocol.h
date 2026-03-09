/*
 * @file protocol.h
 * @brief 主板与副板串口通信协议
 * 
 * 通信格式：
 * +------+----------+----------+----------+--------+----------+------+
 * | 帧头 | 长度高位 | 长度低位 | 命令码   | 数据区 | 校验位   | 帧尾 |
 * +------+----------+----------+----------+--------+----------+------+
 * | 0xAE | 1 byte   | 1 byte   | 1 byte   | N byte | 1 byte   | 0xFE |
 * +------+----------+----------+----------+--------+----------+------+
 * 
 * 长度字段：表示从帧头到校验位的总长度（不含帧尾）
 * 校验位：从帧头到有效数据区的所有字节求和取低8位
 */

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#include <rtthread.h>

/* 协议常量定义 */
#define PROTOCOL_FRAME_HEAD     0xAE    /* 帧头标志 */
#define PROTOCOL_FRAME_TAIL     0xFE    /* 帧尾标志 */

/* 协议帧最小长度（帧头+长度高+长度低+命令码+校验+帧尾）*/
#define PROTOCOL_MIN_FRAME_LEN  6

/* 协议帧最大长度 */
#define PROTOCOL_MAX_FRAME_LEN  256

/* 有效数据区最大长度 */
#define PROTOCOL_MAX_DATA_LEN   (PROTOCOL_MAX_FRAME_LEN - PROTOCOL_MIN_FRAME_LEN)

/* 命令码定义（0xAx格式）*/
#define CMD_QUERY_STATUS        0xA5    /* 查询状态 */
#define CMD_SET_FAN            0xA1    /* 设置模式 */
#define CMD_CONTROL_DEVICE      0xA2    /* 控制设备 */
#define CMD_READ_DATA           0xA3    /* 读取数据 */
#define CMD_COLOR_LIGHT          0xA4    /* 三色灯控制 */
#define CMD_HEARTBEAT           0xA0    /* 心跳包 */
#define CMD_RESPONSE_OK         0xAA    /* 应答成功 */
#define CMD_RESPONSE_ERROR      0xAE    /* 应答失败 */

/* 协议帧结构体 */
typedef struct {
    uint8_t head;           /* 帧头 0xAE */
    uint8_t len_high;       /* 长度高字节 */
    uint8_t len_low;        /* 长度低字节 */
    uint8_t cmd;            /* 命令/应答码 */
    uint8_t data[PROTOCOL_MAX_DATA_LEN];  /* 有效数据区 */
    uint16_t data_len;      /* 有效数据长度 */
    uint8_t checksum;       /* 校验位 */
    uint8_t tail;           /* 帧尾 0xFE */
} protocol_frame_t;

/* 协议处理回调函数类型 */
typedef void (*protocol_cmd_handler_t)(uint8_t cmd, uint8_t *data, uint16_t len);

/* 协议接口函数 */

/**
 * @brief 初始化协议处理模块
 * @param handler 命令处理回调函数
 * @return RT_EOK: 成功, 其他: 失败
 */
int protocol_init(protocol_cmd_handler_t handler);

/**
 * @brief 发送协议帧
 * @param cmd 命令码
 * @param data 数据指针
 * @param len 数据长度
 * @return RT_EOK: 成功, 其他: 失败
 */
rt_err_t protocol_send_frame(uint8_t cmd, uint8_t *data, uint16_t len);

/**
 * @brief 发送应答帧（成功）
 * @param cmd 原命令码
 * @param data 应答数据
 * @param len 应答数据长度
 * @return RT_EOK: 成功, 其他: 失败
 */
rt_err_t protocol_send_response_ok(uint8_t cmd, uint8_t *data, uint16_t len);

/**
 * @brief 发送应答帧（失败）
 * @param cmd 原命令码
 * @param error_code 错误码
 * @return RT_EOK: 成功, 其他: 失败
 */
rt_err_t protocol_send_response_error(uint8_t cmd, uint8_t error_code);

/**
 * @brief 协议接收处理线程启动
 */
void protocol_start_receive(void);

/**
 * @brief 计算校验和
 * @param data 数据指针
 * @param len 数据长度
 * @return 校验和（低8位）
 */
uint8_t protocol_calculate_checksum(uint8_t *data, uint16_t len);

#endif /* __PROTOCOL_H__ */
