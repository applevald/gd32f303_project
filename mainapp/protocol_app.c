/*
 * @file protocol_app.c
 * @brief 协议应用示例 - 演示如何使用协议模块与主板通信
 */

#include "protocol.h"
#include <rtthread.h>

/* 设备状态结构体 */
typedef struct {
    uint8_t mode;           /* 工作模式 */
    uint8_t device_status;  /* 设备状态 */
    uint16_t data_value;    /* 数据值 */
} device_state_t;

static device_state_t g_device_state = {0};

extern void fan_set_speed(uint8_t fan_id, uint8_t speed);
/**
 * @brief 命令处理回调函数
 * @param cmd 命令码
 * @param data 数据指针
 * @param len 数据长度
 */
static void protocol_command_handler(uint8_t cmd, uint8_t *data, uint16_t len)
{
    rt_kprintf("[App] Received CMD: 0x%02X, Data len: %d\n", cmd, len);
    
    /* 打印接收到的数据（用于调试）*/
    if (len > 0)
    {
        rt_kprintf("[App] Data: ");
        for (uint16_t i = 0; i < len && i < 16; i++)
        {
            rt_kprintf("%02X ", data[i]);
        }
        rt_kprintf("\n");
    }
    
    switch (cmd)
    {
        case CMD_QUERY_STATUS:
            /* 查询状态命令 */
            rt_kprintf("[App] Query status command received\n");
            {
                uint8_t status_data[4];
                status_data[0] = g_device_state.mode;
                status_data[1] = g_device_state.device_status;
                status_data[2] = (g_device_state.data_value >> 8) & 0xFF;
                status_data[3] = g_device_state.data_value & 0xFF;
                
                /* 发送应答 */
                protocol_send_response_ok(cmd, status_data, sizeof(status_data));
            }
            break;
            
        case CMD_SET_FAN:
            /* 设置风扇速度命令 */
            if (len >= 2)
            {
                uint8_t fan_id = data[0];
                uint8_t fan_speed = data[1]; 

                rt_kprintf("[App] Set fan: fan_id=%d, speed=%d\n", fan_id, fan_speed);

                if(fan_id > 0 && fan_id <= 2){
                    fan_id = 0;
                }else if(fan_id > 2 && fan_id <= 6){
                    fan_id = 1;
                }else if(fan_id > 6 && fan_id <= 10){
                    fan_id = 2;
                }else if(fan_id > 10 && fan_id <= 14){
                    fan_id = 3;
                }

                fan_set_speed(fan_id, fan_speed);

                /* 回显响应：使用原命令码和原数据 */
                rt_kprintf("[App] Sending response: CMD=0x%02X, fan_id=%d, speed=%d\n", 
                          CMD_SET_FAN, data[0], data[1]);
                
                /* 使用 protocol_send_response_ok 发送响应 */
                protocol_send_response_ok(CMD_SET_FAN, data, 2);
            }
            else
            {
                /* 参数错误 */
                rt_kprintf("[App] Error: CMD_SET_FAN requires 2 bytes, got %d\n", len);
                protocol_send_response_error(CMD_SET_FAN, 0x01);
            }
            break;
            
        case CMD_CONTROL_DEVICE:
            /* 控制设备命令 */
            if (len >= 1)
            {
                uint8_t device_cmd = data[0];
                rt_kprintf("[App] Control device: 0x%02X\n", device_cmd);
                
                /* 这里添加实际的设备控制逻辑 */
                g_device_state.device_status = device_cmd;
                
                /* 发送应答 */
                protocol_send_response_ok(cmd, RT_NULL, 0);
            }
            else
            {
                protocol_send_response_error(cmd, 0x01);
            }
            break;
            
        case CMD_READ_DATA:
            /* 读取数据命令 */
            rt_kprintf("[App] Read data command received\n");
            {
                /* 模拟读取的数据 */
                uint8_t read_data[8];
                read_data[0] = 0x11;
                read_data[1] = 0x22;
                read_data[2] = 0x33;
                read_data[3] = 0x44;
                read_data[4] = (g_device_state.data_value >> 8) & 0xFF;
                read_data[5] = g_device_state.data_value & 0xFF;
                read_data[6] = g_device_state.mode;
                read_data[7] = g_device_state.device_status;
                
                /* 发送应答 */
                protocol_send_response_ok(cmd, read_data, sizeof(read_data));
            }
            break;
            
        case CMD_COLOR_LIGHT:
            /* 彩灯控制命令 */
            if (len >= 2)
            {
                uint8_t color = data[0];
                uint8_t breath_mode = data[1];
                rt_kprintf("[App] Color light command: color=0x%02X, breath_mode=0x%02X\n", color, breath_mode);
               
                /* 调用灯光控制函数 */
                extern rt_err_t ws2811_protocol_control(rt_uint8_t color, rt_uint8_t breath_mode);
                if (ws2811_protocol_control(color, breath_mode) == RT_EOK)
                {
                    /* 发送成功应答 */
                    protocol_send_response_ok(cmd, data, 2);
                }
                else
                {
                    /* 发送失败应答 */
                    protocol_send_response_error(cmd, 0x02);
                }
            }
            else
            {
                protocol_send_response_error(cmd, 0x01);
            }
            break;
            
        case CMD_HEARTBEAT:
            /* 心跳包 */
            rt_kprintf("[App] Heartbeat received\n");
            uint8_t read_data[7];
            read_data[0] = 0xAE;
            read_data[1] = 0x00;
            read_data[2] = 0x07;
            read_data[3] = CMD_HEARTBEAT;
            read_data[4] = data[0];//幻数，接收到的数据区
            read_data[5] = protocol_calculate_checksum(read_data, 5);//校验位
            read_data[6] = 0xFE;
            protocol_send_response_ok(cmd, read_data, sizeof(read_data));
            break;
            
        case CMD_RESPONSE_OK:
            /* 收到主板的应答（成功）*/
            rt_kprintf("[App] Response OK received\n");
            break;
            
        case CMD_RESPONSE_ERROR:
            /* 收到主板的应答（失败）*/
            if (len >= 1)
            {
                rt_kprintf("[App] Response ERROR received, error code: 0x%02X\n", data[0]);
            }
            break;
            
        default:
            rt_kprintf("[App] Unknown command: 0x%02X\n", cmd);
            protocol_send_response_error(cmd, 0xFF);
            break;
    }
}

/**
 * @brief 协议应用初始化
 */
int protocol_app_init(void)
{
    /* 初始化设备状态 */
    g_device_state.mode = 0;
    g_device_state.device_status = 0;
    g_device_state.data_value = 0;
    
    /* 初始化协议模块 */
    if (protocol_init(protocol_command_handler) != RT_EOK)
    {
        rt_kprintf("[App] Error: Protocol init failed\n");
        return -RT_ERROR;
    }
    
    rt_kprintf("[App] Protocol application initialized\n");
    
    return RT_EOK;
}

/* 在串口初始化之后自动初始化协议应用 */
INIT_APP_EXPORT(protocol_app_init);

/* =============== Shell 测试命令 =============== */

/**
 * @brief Shell命令：发送查询状态命令
 */
static void cmd_query_status(int argc, char **argv)
{
    protocol_send_frame(CMD_QUERY_STATUS, RT_NULL, 0);
    rt_kprintf("Query status command sent\n");
}
MSH_CMD_EXPORT_ALIAS(cmd_query_status, proto_query, Send query status command);

/**
 * @brief Shell命令：发送设置模式命令
 */
static void cmd_set_mode(int argc, char **argv)
{
    if (argc >= 2)
    {
        uint8_t mode = (uint8_t)atoi(argv[1]);
        protocol_send_frame(CMD_SET_FAN, &mode, 1);
        rt_kprintf("Set mode command sent: %d\n", mode);
    }
    else
    {
        rt_kprintf("Usage: proto_setmode <mode>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_set_mode, proto_setmode, Send set mode command);

/**
 * @brief Shell命令：发送控制设备命令
 */
static void cmd_control_device(int argc, char **argv)
{
    if (argc >= 2)
    {
        uint8_t ctrl = (uint8_t)atoi(argv[1]);
        protocol_send_frame(CMD_CONTROL_DEVICE, &ctrl, 1);
        rt_kprintf("Control device command sent: 0x%02X\n", ctrl);
    }
    else
    {
        rt_kprintf("Usage: proto_ctrl <value>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_control_device, proto_ctrl, Send control device command);

/**
 * @brief Shell命令：发送读取数据命令
 */
static void cmd_read_data(int argc, char **argv)
{
    protocol_send_frame(CMD_READ_DATA, RT_NULL, 0);
    rt_kprintf("Read data command sent\n");
}
MSH_CMD_EXPORT_ALIAS(cmd_read_data, proto_read, Send read data command);

/**
 * @brief Shell命令：发送写入数据命令
 */
static void cmd_write_data(int argc, char **argv)
{
    if (argc >= 2)
    {
        uint16_t value = (uint16_t)atoi(argv[1]);
        uint8_t data[2];
        data[0] = (value >> 8) & 0xFF;
        data[1] = value & 0xFF;
        
        protocol_send_frame(CMD_COLOR_LIGHT, data, 2);
        rt_kprintf("Write data command sent: 0x%04X\n", value);
    }
    else
    {
        rt_kprintf("Usage: proto_write <value>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_write_data, proto_write, Send write data command);

/**
 * @brief Shell命令：发送心跳包
 */
static void cmd_heartbeat(int argc, char **argv)
{
    protocol_send_frame(CMD_HEARTBEAT, RT_NULL, 0);
    rt_kprintf("Heartbeat command sent\n");
}
MSH_CMD_EXPORT_ALIAS(cmd_heartbeat, proto_heart, Send heartbeat command);

/**
 * @brief Shell命令：发送自定义命令
 */
static void cmd_send_custom(int argc, char **argv)
{
    if (argc >= 2)
    {
        uint8_t cmd = (uint8_t)strtol(argv[1], NULL, 16);
        uint8_t data[32];
        uint16_t data_len = 0;
        
        /* 解析数据参数 */
        for (int i = 2; i < argc && data_len < 32; i++)
        {
            data[data_len++] = (uint8_t)strtol(argv[i], NULL, 16);
        }
        
        protocol_send_frame(cmd, data, data_len);
        rt_kprintf("Custom command sent: CMD=0x%02X, LEN=%d\n", cmd, data_len);
    }
    else
    {
        rt_kprintf("Usage: proto_custom <cmd_hex> [data1_hex] [data2_hex] ...\n");
        rt_kprintf("Example: proto_custom A1 01 02 03\n");
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_send_custom, proto_custom, Send custom protocol command);
