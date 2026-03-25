/*
 * @file protocol_app.c
 * @brief 协议应用示例 - 演示如何使用协议模块与主板通信
 */

#include "protocol.h"
#include "iap.h"
#include "windows.h"
#include <rtthread.h>

/* 调试开关 - 设为1启用详细调试输出，0关闭以提高性能 */
#define PROTOCOL_APP_DEBUG_VERBOSE  0

/* 固件版本号定义
 * 格式: Vxxx = 高字节.低字节
 * 例如: V1.0 = 0x01 0x00 (0x0100)
 * 修改此处更新固件版本号
 */
#define FIRMWARE_VERSION            0x0100    /* V1.0 (0x0100 = 1.00) */

/* 设备状态结构体 */
typedef struct {
    uint8_t mode;           /* 工作模式 */
    uint8_t device_status;  /* 设备状态 */
    uint16_t data_value;    /* 数据值 */
} device_state_t;

static device_state_t g_device_state = {0};

extern int fan_set_speed(uint8_t fan_id, uint8_t speed);

/**
 * @brief 天窗状态变化回调函数
 * @param status 协议状态码
 * @note 当限位开关状态变化时，主动上报给上位机
 */
static void window_status_change_callback(uint8_t status)
{
#if PROTOCOL_APP_DEBUG_VERBOSE
    rt_kprintf("[App] Window status changed, reporting: 0x%02X\n", status);
#endif
    
    /* 主动上报天窗状态（命令码 0xA8）*/
    protocol_send_frame(CMD_WINDOWS_CONTROL, &status, 1);
}
/**
 * @brief 命令处理回调函数
 * @param cmd 命令码
 * @param data 数据指针
 * @param len 数据长度
 */
static void protocol_command_handler(uint8_t cmd, uint8_t *data, uint16_t len)
{
#if PROTOCOL_APP_DEBUG_VERBOSE
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
#endif
    
    switch (cmd)
    {
            
        case CMD_HEARTBEAT:
            /* 心跳包 - 直接回复，无调试输出以提高性能 */
            {
                uint8_t read_data = data[0];  /* 幻数 */
                protocol_send_response_ok(CMD_HEARTBEAT, &read_data, sizeof(read_data));
            }
            break;
        case CMD_SET_FAN:
            /* 设置风扇速度命令 */
            {
                uint8_t fan_id_high = (data[0] & 0xF0) >> 4; /* 高 4 位表示风扇 ID模式 */
                uint8_t fan_id = data[0] & 0x0F;
                uint8_t fan_speed = data[1]; 

#if PROTOCOL_APP_DEBUG_VERBOSE
                rt_kprintf("[App] Set fan: fan_id=%d, speed=%d\n", fan_id, fan_speed);
#endif

                /* 检查是否为热腔风扇且加热中 */
                /* 热腔风扇定义：单风扇模式ID 7-10，或组模式组编号2 */
                extern uint8_t is_heating_active(void);
                uint8_t is_chamber_fan = 0;
                
                if (fan_id_high == 0)
                {
                    /* 单风扇模式：ID 7-10为热腔风扇 */
                    is_chamber_fan = (fan_id >= 7 && fan_id <= 10);
                }
                else if (fan_id_high == 1)
                {
                    /* 组模式：组编号2为热腔风扇组(FAN_CHAMBER = [7,8,9,10]) */
                    is_chamber_fan = (fan_id == 2);
                }
                
                if (is_heating_active() && is_chamber_fan)
                {
                    /* 加热中，拒绝设置热腔风扇速度 */
                    data[1] = 0x02; /* 返回错误码2：拒绝设定 */
                    protocol_send_response_ok(CMD_SET_FAN, data, 2);
                    break;
                }

                if(fan_id_high == 0){
                    if(fan_id > 0 && fan_id <= 2){
                        fan_id = 0;
                    }else if(fan_id > 2 && fan_id <= 6){
                        fan_id = 1;
                    }else if(fan_id > 6 && fan_id <= 10){
                        fan_id = 2;
                    }else if(fan_id > 10 && fan_id <= 14){
                        fan_id = 3;
                    }
                }

                int result = fan_set_speed(fan_id, fan_speed);
                
                data[1] = (result == 0 ? 0x00 : 0x01); /* 成功返回 0x00，失败返回 0x01 */
                protocol_send_response_ok(CMD_SET_FAN, data, 2);
            }
            break;
            
        case CMD_FAN_GETSPEED:
            /* 获取风扇转速查询命令 */
            {
                /* 风扇转速已在后台定时器中持续更新，直接获取结果 */
                uint8_t fan_data[28] = {0};
                extern int get_fan_speed(uint8_t *data, uint16_t len);
                int data_len = get_fan_speed(fan_data, sizeof(fan_data));
    
#if PROTOCOL_APP_DEBUG_VERBOSE
                /* 打印调试信息 */
                rt_kprintf("[App] Fan speed data: ");
                for (int i = 0; i < data_len && i < 28; i++)
                {
                    rt_kprintf("%02X ", fan_data[i]);
                    if (i % 2 == 1) rt_kprintf(" ");
                }
                rt_kprintf("\n");
#endif
                    
                /* 发送应答（包含风扇转速数据）*/
                protocol_send_response_ok(CMD_FAN_GETSPEED, fan_data, data_len);
            }
            break;
            
        case CMD_FAN_STATUS:
            /* 获取风扇状态查询命令 */
            {
                /* 风扇状态已在后台定时器中持续更新，直接获取结果 */
                extern const uint8_t* fan_get_target_speed_array(void);
                const uint8_t *target_speed = fan_get_target_speed_array();
                
                /* 获取风扇状态 */
                uint8_t fan_status_data[14] = {0};
                extern int get_fan_status(uint8_t *data, uint16_t len, const uint8_t *fan_target_speed);
                int data_len = get_fan_status(fan_status_data, sizeof(fan_status_data), target_speed);
                
#if PROTOCOL_APP_DEBUG_VERBOSE
                /* 打印调试信息 */
                rt_kprintf("[App] Fan status data: ");
                for (int i = 0; i < data_len && i < 14; i++)
                {
                    rt_kprintf("%02X ", fan_status_data[i]);
                }
                rt_kprintf("\n");
#endif
                    
                /* 发送应答（包含风扇状态数据）*/
                protocol_send_response_ok(CMD_FAN_STATUS, fan_status_data, data_len);
            }
            break;
            
        case CMD_COLOR_LIGHT:
            /* 三色灯控制命令 */
            {
                uint8_t color_get = data[0];
                uint8_t breath_mode_get = data[1];
                
#if PROTOCOL_APP_DEBUG_VERBOSE
                rt_kprintf("[App] Color light command: color=0x%02X, breath_mode=0x%02X\n", color_get, breath_mode_get);
#endif
               
                /* 调用灯光控制函数 */
                extern rt_err_t ws2811_protocol_control(rt_uint8_t color, rt_uint8_t breath_mode);
                ws2811_protocol_control(color_get, breath_mode_get);
                /* 发送成功应答 */
                protocol_send_response_ok(cmd, data, 2);
            }
            break;

        case CMD_LIGHT_BAR:
            /* 进度灯条控制命令 */
            {
                uint8_t progress = data[0];           /* 字节0: 进度值(0-100) */
                uint8_t color_param = data[1];
                uint8_t breath_mode = data[2];        /* 字节3: 呼吸参数 */
                
#if PROTOCOL_APP_DEBUG_VERBOSE
                rt_kprintf("[App] Light bar command: progress=%d, color=0x%02X, breath=0x%02X\n", 
                          progress, color_param, breath_mode);
#endif
                
                /* 调用灯条控制函数 */
                extern rt_err_t ws2812_bar_protocol_control(rt_uint8_t progress, rt_uint8_t color_param, rt_uint8_t breath_mode);
                ws2812_bar_protocol_control(progress, color_param, breath_mode);
                /* 发送成功应答，回显原始数据 */
                protocol_send_response_ok(cmd, data, 3);
            }
            break;
        
        case CMD_ALL_STATUS:
            {
                float current_temp;
                uint8_t all_status_data[18] = {0}; /* NTC温度(2) + 目标温度(1) + NTC状态(1) + 风扇状态(14) = 18字节 */
                extern rt_err_t temp_measure_get_temperature(float *temp);
                temp_measure_get_temperature(&current_temp);
                uint16_t temp_data = (uint16_t)(current_temp * 10);
                all_status_data[0] = (uint8_t)(temp_data >> 8);   /* 温度高字节 */
                all_status_data[1] = (uint8_t)(temp_data & 0xFF);  /* 温度低字节 */
                
                extern uint32_t get_goal_temp(void);
                uint32_t goal_temp_value;
                goal_temp_value = get_goal_temp();
                all_status_data[2] = (uint8_t)goal_temp_value;   

                /* 获取NTC状态: 0-正常, 1-短路异常, 2-开路异常 */
                extern uint8_t temp_measure_get_ntc_status(void);
                all_status_data[3] = temp_measure_get_ntc_status();

                /* 风扇状态已在后台定时器中持续更新，直接获取结果 */
                extern const uint8_t* fan_get_target_speed_array(void);
                const uint8_t *target_speed = fan_get_target_speed_array();

                extern int get_fan_status(uint8_t *data, uint16_t len, const uint8_t *fan_target_speed);
                get_fan_status(all_status_data + 4, 14, target_speed);
                
                protocol_send_response_ok(CMD_ALL_STATUS, all_status_data, 18);
            }
            break;

        case CMD_TEMP_SET:
            {
                extern uint8_t set_goal_temp(uint32_t temp);
                uint8_t temp_value = data[0];
                uint8_t result = set_goal_temp(temp_value);
                if(result == 0){
                    protocol_send_response_ok(CMD_TEMP_SET, &result, 1);
                }else if(result == 1){
                    protocol_send_response_ok(CMD_TEMP_SET,  &result, 1);
                }else if(result == 2){
                    protocol_send_response_ok(CMD_TEMP_SET,  &result, 1);
                }
            }
            break;
        
        case CMD_WINDOWS_CONTROL:
            /* 天窗控制命令 */
            {
                uint8_t control_value = 0;
                
                if (data[0] == 0x00)
                {
                    /* 关闭天窗 */
                    window_close();
                }
                else if (data[0] == 0x01)
                {
                    /* 打开天窗 */
                    window_open();
                }
                else if (data[0] == 0x02)
                {
                    /* 查询状态 - 只返回状态，不执行任何操作 */
                    control_value = window_get_protocol_status();
                    /* 立即返回状态 */
                    protocol_send_response_ok(CMD_WINDOWS_CONTROL, &control_value, 1);
                    break;
                }
                else if (data[0] == 0x03)
                {
                    /* 强制停止天窗动作 */
                    window_emergency_stop();
                }
                else
                {
                    /* 未知命令 */
                    protocol_send_response_error(cmd, 0x01);
                    break;
                }

                /* 获取天窗协议状态并返回（用于关闭/打开/停止命令） */
                control_value = window_get_protocol_status();

                /* 发送成功应答 */
                protocol_send_response_ok(CMD_WINDOWS_CONTROL, &control_value, 1);
            }
            break;

        case CMD_IAP_REQUEST:
            /* IAP升级请求 (0xAA) */
            {
                extern iap_result_t iap_handle_request(uint8_t *data, uint16_t len);
                iap_result_t result = iap_handle_request(data, len);
                
                /* 返回结果码 */
                protocol_send_response_ok(CMD_IAP_REQUEST, (uint8_t*)&result, 1);
            }
            break;

        case CMD_IAP_PACKET:
            /* IAP数据包 (0xAB) */
            {
                extern iap_result_t iap_handle_packet(uint8_t *data, uint16_t len, uint16_t *next_seq);
                uint16_t next_seq = 0;
                iap_result_t result = iap_handle_packet(data, len, &next_seq);
                
                if (result == IAP_RESULT_COMPLETE)
                {
                    /* 升级完成，返回0xAA命令结果码2 */
                    uint8_t response[1] = {IAP_RESULT_COMPLETE};
                    protocol_send_response_ok(CMD_IAP_REQUEST, response, 1);
                }
                else
                {
                    /* 返回下一包序列号（小端字节序） */
                    uint8_t response[2];
                    response[0] = next_seq & 0xFF;
                    response[1] = (next_seq >> 8) & 0xFF;
                    protocol_send_response_ok(CMD_IAP_PACKET, response, 2);
                }
            }
            break;

        case CMD_VERSION_QUERY:
            /* 版本号查询 (0xAC) */
            {
                uint8_t version_data[2];
                version_data[0] = (FIRMWARE_VERSION >> 8) & 0xFF;   /* 版本号高字节 */
                version_data[1] = FIRMWARE_VERSION & 0xFF;          /* 版本号低字节 */
                
#if PROTOCOL_APP_DEBUG_VERBOSE
                rt_kprintf("[App] Version query: V%d (0x%04X)\n", FIRMWARE_VERSION, FIRMWARE_VERSION);
#endif
                
                protocol_send_response_ok(CMD_VERSION_QUERY, version_data, 2);
            }
            break;

        case CMD_RESPONSE_ERROR:
            /* 收到主板的应答（失败）*/
#if PROTOCOL_APP_DEBUG_VERBOSE
            if (len >= 1)
            {
                rt_kprintf("[App] Response ERROR received, error code: 0x%02X\n", data[0]);
            }
#endif
            break;
            
        default:
#if PROTOCOL_APP_DEBUG_VERBOSE
            rt_kprintf("[App] Unknown command: 0x%02X\n", cmd);
#endif
            /* 立即发送错误响应，不使用protocol_send_response_error避免额外开销 */
            {
                uint8_t error_data[2];
                error_data[0] = cmd;         /* 原命令码 */
                error_data[1] = 0xFF;        /* 错误码 */
                protocol_send_response_ok(CMD_RESPONSE_ERROR, error_data, 2);
            }
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
    
    /* 注册天窗状态变化回调函数 */
    window_register_status_callback(window_status_change_callback);
    
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
 * @brief Shell命令：发送心跳包
 */
static void cmd_heartbeat(int argc, char **argv)
{
    protocol_send_frame(CMD_HEARTBEAT, RT_NULL, 0);
    rt_kprintf("Heartbeat command sent\n");
}
MSH_CMD_EXPORT_ALIAS(cmd_heartbeat, proto_heart, Send heartbeat command);

/**
 * @brief Shell命令：发送进度灯条控制命令
 * @note  测试CMD_LIGHT_BAR (0xA5)
 */
static void cmd_light_bar(int argc, char **argv)
{
    if (argc >= 4)
    {
        uint8_t progress = (uint8_t)atoi(argv[1]);       /* 进度值 0-100 */
        uint16_t color = (uint16_t)strtol(argv[2], NULL, 16);  /* 颜色参数(hex) */
        uint8_t breath = (uint8_t)strtol(argv[3], NULL, 16);   /* 呼吸模式(hex) */
        
        uint8_t data[4];
        data[0] = progress;
        data[1] = (color >> 8) & 0xFF;  /* 颜色高字节 */
        data[2] = color & 0xFF;          /* 颜色低字节 */
        data[3] = breath;
        
        protocol_send_frame(CMD_LIGHT_BAR, data, 4);
        rt_kprintf("Light bar command sent: progress=%d%%, color=0x%04X, breath=0x%02X\n", 
                  progress, color, breath);
    }
    else
    {
        rt_kprintf("Usage: proto_lightbar <progress> <color_hex> <breath_hex>\n");
        rt_kprintf("Example: proto_lightbar 50 0001 00    (50%% progress, red, no breath)\n");
        rt_kprintf("         proto_lightbar 75 0004 01    (75%% progress, green, slow breath)\n");
        rt_kprintf("         proto_lightbar 100 0000 00   (100%% progress, white, no breath)\n");
        rt_kprintf("\nColor codes: 0x00=white, 0x01=red, 0x02=yellow, 0x03=blue, 0x04=green\n");
        rt_kprintf("Breath modes: 0x00=off, 0x01=slow, 0x02=medium, 0x03=fast\n");
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_light_bar, proto_lightbar, Send light bar control command);
