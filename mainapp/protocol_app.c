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
 * 格式: 字符串，包含 \0 结尾符
 * 例如: "0.0326.1.Bate1"
 * 修改此处更新固件版本号
 * 版本号用 0.1.0409 格式 第一位是大版本号（硬件大变动，向前不兼容，自增1），第二位（功能较大变更或跨年自增1），第三是发布日期，测试版尾巴加Bate0-99 ，官方正式版不能有Bate信息
 */
#define FIRMWARE_VERSION            "0.1.0417.Bate1"

/* 设备状态结构体 */
typedef struct {
    uint8_t mode;           /* 工作模式 */
    uint8_t device_status;  /* 设备状态 */
    uint16_t data_value;    /* 数据值 */
} device_state_t;

static device_state_t g_device_state = {0};

/* 机型配置定义 */
#define MODEL_TYPE_T450D    0   /* T450D机型 */
#define MODEL_TYPE_T700     1   /* T700机型 */

/* 机型配置 - 默认为T450D */
static uint8_t g_model_type = MODEL_TYPE_T450D;

/* 配置结果码定义 */
#define CONFIG_RESULT_OK            0   /* 配置成功 */
#define CONFIG_RESULT_REJECT        1   /* 拒绝配置 */
#define CONFIG_RESULT_OTHER         2   /* 其他错误 */

extern int fan_set_speed(uint8_t fan_id, uint8_t speed);

/* IAP擦除线程 */
static rt_thread_t g_iap_erase_thread = RT_NULL;

/**
 * @brief IAP擦除线程入口 - 异步执行擦除，擦除完毕后回复上位机 ACCEPT
 */
static void iap_erase_thread_entry(void *parameter)
{
    extern int iap_start_erase(void);
    int ret = iap_start_erase();

    /* 擦除完成后再发送响应，通知上位机可以开始传数据 */
    iap_result_t result = (ret == 0) ? IAP_RESULT_ACCEPT : IAP_RESULT_ERASE_FAIL;
    protocol_send_response_ok(CMD_IAP_REQUEST, (uint8_t*)&result, 1);

    g_iap_erase_thread = RT_NULL;
}

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
                /* NTC温度(2) + 目标温度(1) + NTC状态(1) + 风扇状态(14) + 风扇速度(28) = 46字节 */
                uint8_t all_status_data[46] = {0};
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
                
                /* 添加14个风扇的实际转速(RPM) - 大端模式，每个风扇2字节 */
                extern uint16_t get_fan_rpm(uint8_t fan_id);
                for (int i = 0; i < 14; i++)
                {
                    uint16_t rpm = get_fan_rpm(i + 1);  /* 风扇ID: 1-14 */
                    /* 大端模式：高字节在前 */
                    all_status_data[18 + i * 2] = (uint8_t)(rpm >> 8);      /* RPM高字节 */
                    all_status_data[18 + i * 2 + 1] = (uint8_t)(rpm & 0xFF); /* RPM低字节 */
                }
                
                protocol_send_response_ok(CMD_ALL_STATUS, all_status_data, 46);
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
                extern iap_result_t iap_prepare_request(uint8_t *data, uint16_t len);
                
                /* 1. 验证请求参数 */
                iap_result_t result = iap_prepare_request(data, len);
                
                if (result != IAP_RESULT_ACCEPT)
                {
                    /* 参数校验失败，立即回复错误 */
                    protocol_send_response_ok(CMD_IAP_REQUEST, (uint8_t*)&result, 1);
                    break;
                }

                /* 2. 参数合法，异步启动擦除线程；擦除完毕后由线程发送 ACCEPT 响应 */
                if (g_iap_erase_thread == RT_NULL)
                {
                    g_iap_erase_thread = rt_thread_create("iap_erase",
                                                           iap_erase_thread_entry,
                                                           RT_NULL,
                                                           2048,
                                                           RT_THREAD_PRIORITY_MAX / 2,
                                                           10);
                    if (g_iap_erase_thread != RT_NULL)
                    {
                        rt_thread_startup(g_iap_erase_thread);
                    }
                }
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
                    /* 升级完成，返回0xAA命令结果码1 */
                    uint8_t response[1] = {IAP_RESULT_COMPLETE};
                    protocol_send_response_ok(CMD_IAP_REQUEST, response, 1);
                    
                    /* 延时确保响应发送完成，然后触发升级重启 */
                    rt_thread_mdelay(500);
                    extern void iap_trigger_upgrade(void);
                    iap_trigger_upgrade();
                }
                else if (result == IAP_RESULT_ACCEPT)
                {
                    /* 正常：返回下一包序列号（大端字节序） */
                    uint8_t response[2];
                    response[0] = (next_seq >> 8) & 0xFF;
                    response[1] = next_seq & 0xFF;
                    protocol_send_response_ok(CMD_IAP_PACKET, response, 2);
                }
                else
                {
                    /* 错误：写入失败/CRC失败/状态异常，通过0xAA命令返回错误码通知上位机终止 */
                    uint8_t response[1] = {(uint8_t)result};
                    protocol_send_response_ok(CMD_IAP_REQUEST, response, 1);
                }
            }
            break;

        case CMD_VERSION_QUERY:
            /* 版本号查询 (0xAC) */
            {
#if PROTOCOL_APP_DEBUG_VERBOSE
                rt_kprintf("[App] Version query: %s\n", FIRMWARE_VERSION);
#endif
                
                /* sizeof(FIRMWARE_VERSION) 会自动包含字符串末尾的 '\0' 字节 */
                protocol_send_response_ok(CMD_VERSION_QUERY, (uint8_t *)FIRMWARE_VERSION, sizeof(FIRMWARE_VERSION));
            }
            break;

        case CMD_CONFIG_REQUEST:
            /* 配置请求 (0xAD) */
            {
                
                uint8_t config_type = data[0];      /* 配置类型 */
                uint8_t config_content = data[1];   /* 配置内容 */
                uint8_t result = CONFIG_RESULT_OK;
                
#if PROTOCOL_APP_DEBUG_VERBOSE
                rt_kprintf("[App] Config request: type=%d, content=%d\n", config_type, config_content);
#endif
                
                if (config_type == 0)
                {
                    /* 配置机型 */
                    if (config_content == MODEL_TYPE_T450D || config_content == MODEL_TYPE_T700)
                    {
                        g_model_type = config_content;
                        result = CONFIG_RESULT_OK;
                        
#if PROTOCOL_APP_DEBUG_VERBOSE
                        rt_kprintf("[App] Model configured: %s\n", 
                                  config_content == MODEL_TYPE_T450D ? "T450D" : "T700");
#endif
                        
                        /* 根据机型配置灯珠和风扇 */
                        /* T450D: 50个灯珠, T700: 72个灯珠 */
                        extern void ws2812_bar_set_model(uint8_t model_type);
                        ws2812_bar_set_model(g_model_type);
                    }
                    else
                    {
                        /* 未知机型 */
                        result = CONFIG_RESULT_REJECT;
#if PROTOCOL_APP_DEBUG_VERBOSE
                        rt_kprintf("[App] Unknown model type: %d\n", config_content);
#endif
                    }
                }
                else
                {
                    /* 未知配置类型 */
                    result = CONFIG_RESULT_OTHER;
#if PROTOCOL_APP_DEBUG_VERBOSE
                    rt_kprintf("[App] Unknown config type: %d\n", config_type);
#endif
                }
                
                /* 发送配置结果 */
                protocol_send_response_ok(CMD_CONFIG_REQUEST, &result, 1);
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
 * @brief 获取当前机型配置
 * @return 0-T450D, 1-T700
 */
uint8_t protocol_get_model_type(void)
{
    return g_model_type;
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

/**
 * @brief Shell命令：配置机型
 * @note  测试CMD_CONFIG_REQUEST (0xAD)
 */
static void cmd_config_model(int argc, char **argv)
{
    if (argc >= 2)
    {
        uint8_t model_type = (uint8_t)atoi(argv[1]);  /* 0-T450D, 1-T700 */
        
        uint8_t data[2];
        data[0] = 0;           /* 配置类型：0-配置机型 */
        data[1] = model_type;  /* 配置内容：0-T450D, 1-T700 */
        
        protocol_send_frame(CMD_CONFIG_REQUEST, data, 2);
        rt_kprintf("Config request sent: model=%s\n", 
                  model_type == 0 ? "T450D (50 LEDs)" : "T700 (72 LEDs)");
    }
    else
    {
        rt_kprintf("Usage: proto_config <model_type>\n");
        rt_kprintf("Example: proto_config 0    (Configure as T450D - 50 LEDs)\n");
        rt_kprintf("         proto_config 1    (Configure as T700 - 72 LEDs)\n");
        rt_kprintf("\nCurrent model: %s\n", 
                  protocol_get_model_type() == 0 ? "T450D" : "T700");
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_config_model, proto_config, Send config request command);
