/*
 * @file protocol.c
 * @brief 主板与副板串口通信协议实现
 */

#include "protocol.h"
#include "usart_Communication.h"
#include <string.h>

/* 接收缓冲区 */
#define RX_BUFFER_SIZE  512
static uint8_t rx_buffer[RX_BUFFER_SIZE];
static uint16_t rx_index = 0;

/* 命令处理回调函数 */
static protocol_cmd_handler_t g_cmd_handler = RT_NULL;

/* 协议接收线程 */
static rt_thread_t protocol_thread = RT_NULL;

/* 外部串口设备句柄 */
extern rt_device_t serial;
extern struct rt_semaphore rx_sem;

/**
 * @brief 计算校验和（从帧头到有效数据区的所有字节求和取低8位）
 */
uint8_t protocol_calculate_checksum(uint8_t *data, uint16_t len)
{
    uint32_t sum = 0;
    for (uint16_t i = 0; i < len; i++)
    {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

/**
 * @brief 发送协议帧
 */
rt_err_t protocol_send_frame(uint8_t cmd, uint8_t *data, uint16_t len)
{
    uint8_t tx_buffer[PROTOCOL_MAX_FRAME_LEN];
    uint16_t tx_len = 0;
    
    /* 检查数据长度 */
    if (len > PROTOCOL_MAX_DATA_LEN)
    {
        rt_kprintf("[Protocol] Error: Data length too long (%d > %d)\n", 
                   len, PROTOCOL_MAX_DATA_LEN);
        return -RT_ERROR;
    }
    
    /* 计算总长度（整个帧的长度，包含帧尾）*/
    uint16_t total_len = 1 + 2 + 1 + len + 1 + 1;  /* 帧头(1) + 长度(2) + 命令(1) + 数据(len) + 校验(1) + 帧尾(1) */
    
    /* 构建帧 */
    tx_buffer[tx_len++] = PROTOCOL_FRAME_HEAD;          /* 帧头 */
    tx_buffer[tx_len++] = (total_len >> 8) & 0xFF;      /* 长度高字节 */
    tx_buffer[tx_len++] = total_len & 0xFF;             /* 长度低字节 */
    tx_buffer[tx_len++] = cmd;                          /* 命令码 */
    
    /* 拷贝数据 */
    if (len > 0 && data != RT_NULL)
    {
        memcpy(&tx_buffer[tx_len], data, len);
        tx_len += len;
    }
    
    /* 计算校验和（从帧头到有效数据区）*/
    uint8_t checksum = protocol_calculate_checksum(tx_buffer, tx_len);
    tx_buffer[tx_len++] = checksum;                     /* 校验位 */
    tx_buffer[tx_len++] = PROTOCOL_FRAME_TAIL;          /* 帧尾 */
    
    /* 发送数据 */
    usart_send_data_direct(tx_buffer, tx_len);
    
    rt_kprintf("[Protocol] Sent: CMD=0x%02X, LEN=%d, CHK=0x%02X\n", 
               cmd, len, checksum);
    
    /* 打印发送的完整帧数据（用于调试）*/
    rt_kprintf("[Protocol] TX Frame (%d bytes): ", tx_len);
    for (uint16_t i = 0; i < tx_len && i < 32; i++)
    {
        rt_kprintf("%02X ", tx_buffer[i]);
    }
    rt_kprintf("\n");
    
    return RT_EOK;
}

/**
 * @brief 发送应答帧（成功）
 * @note 直接使用传入的命令码和数据发送，不添加额外的包装
 */
rt_err_t protocol_send_response_ok(uint8_t cmd, uint8_t *data, uint16_t len)
{
    /* 调试信息 */
    rt_kprintf("[Protocol] Response OK: CMD=0x%02X, Data (%d bytes): ", cmd, len);
    if (len > 0 && data != RT_NULL)
    {
        for (uint16_t i = 0; i < len && i < 16; i++)
        {
            rt_kprintf("%02X ", data[i]);
        }
    }
    rt_kprintf("\n");
    
    /* 直接发送，使用原命令码和原数据，不添加包装 */
    return protocol_send_frame(cmd, data, len);
}

/**
 * @brief 发送应答帧（失败）
 */
rt_err_t protocol_send_response_error(uint8_t cmd, uint8_t error_code)
{
    uint8_t response_data[2];
    response_data[0] = cmd;         /* 原命令码 */
    response_data[1] = error_code;  /* 错误码 */
    
    return protocol_send_frame(CMD_RESPONSE_ERROR, response_data, 2);
}

/**
 * @brief 验证并解析接收到的帧
 */
static rt_bool_t protocol_parse_frame(uint8_t *buffer, uint16_t len)
{
    /* 最小长度检查 */
    if (len < PROTOCOL_MIN_FRAME_LEN)
    {
        rt_kprintf("[Protocol] Error: Frame too short (%d < %d)\n", len, PROTOCOL_MIN_FRAME_LEN);
        return RT_FALSE;
    }
    
    /* 打印接收到的原始数据（用于调试）*/
    rt_kprintf("[Protocol] Raw data (%d bytes): ", len);
    for (uint16_t i = 0; i < len && i < 32; i++)
    {
        rt_kprintf("%02X ", buffer[i]);
    }
    rt_kprintf("\n");
    
    /* 检查帧头和帧尾 */
    if (buffer[0] != PROTOCOL_FRAME_HEAD || buffer[len - 1] != PROTOCOL_FRAME_TAIL)
    {
        rt_kprintf("[Protocol] Error: Invalid frame head (0x%02X) or tail (0x%02X)\n", 
                   buffer[0], buffer[len - 1]);
        return RT_FALSE;
    }
    
    /* 解析长度字段（表示从帧头到校验位的长度，不包含帧尾）*/
    uint16_t frame_len = ((uint16_t)buffer[1] << 8) | buffer[2];
    
    /* 验证长度（总长度 = frame_len，因为长度字段表示整个帧长度）*/
    if (frame_len != len)
    {
        rt_kprintf("[Protocol] Error: Length mismatch (frame_len=%d, actual_len=%d)\n", 
                   frame_len, len);
        return RT_FALSE;
    }
    
    /* 解析命令码 */
    uint8_t cmd = buffer[3];
    
    /* 计算数据区长度 */
    uint16_t data_len = len - PROTOCOL_MIN_FRAME_LEN;
    
    /* 验证校验和 */
    uint8_t received_checksum = buffer[len - 2];
    uint8_t calculated_checksum = protocol_calculate_checksum(buffer, len - 2);
    
    if (received_checksum != calculated_checksum)
    {
        rt_kprintf("[Protocol] Error: Checksum mismatch (recv=0x%02X, calc=0x%02X)\n", 
                   received_checksum, calculated_checksum);
        return RT_FALSE;
    }
    
    /* 调试输出 */
    rt_kprintf("[Protocol] Received: CMD=0x%02X, LEN=%d, CHK=0x%02X\n", 
               cmd, data_len, received_checksum);
    
    /* 调用命令处理回调 */
    if (g_cmd_handler != RT_NULL)
    {
        g_cmd_handler(cmd, &buffer[4], data_len);
    }
    
    return RT_TRUE;
}

/**
 * @brief 从接收缓冲区中查找并处理完整帧
 */
static void protocol_process_buffer(void)
{
    while (rx_index >= PROTOCOL_MIN_FRAME_LEN)
    {
        /* 查找帧头 */
        uint16_t head_pos = 0;
        rt_bool_t found_head = RT_FALSE;
        
        for (head_pos = 0; head_pos < rx_index; head_pos++)
        {
            if (rx_buffer[head_pos] == PROTOCOL_FRAME_HEAD)
            {
                found_head = RT_TRUE;
                break;
            }
        }
        
        /* 如果没有找到帧头，清空缓冲区 */
        if (!found_head)
        {
            rx_index = 0;
            break;
        }
        
        /* 如果帧头不在起始位置，移除前面的数据 */
        if (head_pos > 0)
        {
            memmove(rx_buffer, &rx_buffer[head_pos], rx_index - head_pos);
            rx_index -= head_pos;
        }
        
        /* 检查是否有足够的数据解析长度字段 */
        if (rx_index < 3)
        {
            break;  /* 等待更多数据 */
        }
        
        /* 解析长度字段（表示整个帧的长度，包含帧尾）*/
        uint16_t frame_len = ((uint16_t)rx_buffer[1] << 8) | rx_buffer[2];
        uint16_t total_len = frame_len;  /* 长度字段即为总长度 */
        
        /* 检查长度是否合法 */
        if (total_len > PROTOCOL_MAX_FRAME_LEN)
        {
            rt_kprintf("[Protocol] Error: Frame too long (%d)\n", total_len);
            /* 丢弃这个帧头，继续查找 */
            memmove(rx_buffer, &rx_buffer[1], rx_index - 1);
            rx_index--;
            continue;
        }
        
        /* 检查是否接收到完整帧 */
        if (rx_index < total_len)
        {
            break;  /* 等待更多数据 */
        }
        
        /* 解析并处理这一帧 */
        protocol_parse_frame(rx_buffer, total_len);
        
        /* 移除已处理的帧 */
        if (rx_index > total_len)
        {
            memmove(rx_buffer, &rx_buffer[total_len], rx_index - total_len);
            rx_index -= total_len;
        }
        else
        {
            rx_index = 0;
        }
    }
}

/**
 * @brief 协议接收线程
 */
static void protocol_receive_thread(void *parameter)
{
    uint8_t temp_buffer[64];
    
    rt_kprintf("[Protocol] Receive thread started\n");
    
    while (1)
    {
        /* 等待数据到达信号 */
        if (rt_sem_take(&rx_sem, RT_WAITING_FOREVER) == RT_EOK)
        {
            /* 读取数据 */
            rt_size_t recv_len = rt_device_read(serial, 0, temp_buffer, sizeof(temp_buffer));
            
            if (recv_len > 0)
            {
                /* 检查缓冲区是否溢出 */
                if (rx_index + recv_len > RX_BUFFER_SIZE)
                {
                    rt_kprintf("[Protocol] Warning: RX buffer overflow, resetting\n");
                    rx_index = 0;
                }
                
                /* 拷贝到接收缓冲区 */
                memcpy(&rx_buffer[rx_index], temp_buffer, recv_len);
                rx_index += recv_len;
                
                /* 处理接收缓冲区 */
                protocol_process_buffer();
            }
        }
    }
}

/**
 * @brief 启动协议接收线程
 */
void protocol_start_receive(void)
{
    if (protocol_thread != RT_NULL)
    {
        rt_kprintf("[Protocol] Receive thread already running\n");
        return;
    }
    
    protocol_thread = rt_thread_create("proto_rx",
                                       protocol_receive_thread,
                                       RT_NULL,
                                       2048,
                                       10,
                                       10);
    
    if (protocol_thread != RT_NULL)
    {
        rt_thread_startup(protocol_thread);
        rt_kprintf("[Protocol] Receive thread created\n");
    }
    else
    {
        rt_kprintf("[Protocol] Error: Failed to create receive thread\n");
    }
}

/**
 * @brief 初始化协议模块
 */
int protocol_init(protocol_cmd_handler_t handler)
{
    if (handler == RT_NULL)
    {
        rt_kprintf("[Protocol] Error: Handler cannot be NULL\n");
        return -RT_ERROR;
    }
    
    g_cmd_handler = handler;
    rx_index = 0;
    
    /* 启动接收线程 */
    protocol_start_receive();
    
    rt_kprintf("[Protocol] Initialized\n");
    
    return RT_EOK;
}
