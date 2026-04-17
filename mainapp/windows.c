/*
 * @file windows.c
 * @brief 天窗控制模块 - 基于HRBB33电机驱动器（非阻塞模式）
 * 
 * 硬件配置：
 *   限位开关：
 *     - PA2: 天窗限位开关1 (关闭位置)
 *     - PA1: 天窗限位开关2 (打开位置)
 *   
 *   HRBB33驱动器：
 *     - PC9: AIN1 (电机A输入1)
 *     - PA8: AIN2 (电机A输入2)
 *     - PA9: BIN1 (电机B输入1)
 *     - PA10: BIN2 (电机B输入2)
 *     - PA0: AISEN (电机A电流检测 - ADC_CHANNEL_0)
 *     - PC2: BISEN (电机B电流检测 - ADC_CHANNEL_12)
 *     - PC13: NFAULT (故障输出，低电平有效)
 * 
 * 操作流程：
 *   - 天窗关闭：驱动电机正转，触发限位1停止
 *   - 天窗打开：驱动电机反转，触发限位2停止
 *   - 运行过程中监控电流和故障信号，异常时立即停止
 * 
 * 非阻塞模式：
 *   - window_open() / window_close() 启动电机后立即返回
 *   - 后台监控线程负责检测限位开关、电流、故障、超时
 *   - 限位开关状态变化时主动上报
 */

#include <rtthread.h>
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_adc.h"
#include "temp_measure.h"
#include "protocol.h"
#include "windows.h"

/* ==================== 引脚定义 ==================== */
/* 限位开关 (输入) */
#define LIMIT_SWITCH_1_PORT     GPIOA
#define LIMIT_SWITCH_1_PIN      GPIO_PIN_2      /* 关闭位置 */
#define LIMIT_SWITCH_2_PORT     GPIOA
#define LIMIT_SWITCH_2_PIN      GPIO_PIN_1      /* 打开位置 */

/* 电机驱动控制引脚 (输出) */
#define MOTOR_AIN1_PORT         GPIOC
#define MOTOR_AIN1_PIN          GPIO_PIN_9
#define MOTOR_AIN2_PORT         GPIOA
#define MOTOR_AIN2_PIN          GPIO_PIN_8
#define MOTOR_BIN1_PORT         GPIOA
#define MOTOR_BIN1_PIN          GPIO_PIN_9
#define MOTOR_BIN2_PORT         GPIOA
#define MOTOR_BIN2_PIN          GPIO_PIN_10

/* 电流检测引脚 (ADC输入) */
#define AISEN_PORT              GPIOA
#define AISEN_PIN               GPIO_PIN_0      /* ADC0_IN0 */
#define AISEN_ADC_CHANNEL       ADC_CHANNEL_0

#define BISEN_PORT              GPIOC
#define BISEN_PIN               GPIO_PIN_2      /* ADC0_IN12 */
#define BISEN_ADC_CHANNEL       ADC_CHANNEL_12

/* 故障检测引脚 (输入) */
#define NFAULT_PORT             GPIOC
#define NFAULT_PIN              GPIO_PIN_13

/* ==================== 参数配置 ==================== */
#define CURRENT_THRESHOLD_A     2048    /* 电机A电流阈值 (ADC值，对应约1.65V) */
#define CURRENT_THRESHOLD_B     2048    /* 电机B电流阈值 (ADC值，对应约1.65V) */
#define MOTOR_TIMEOUT_MS        30000   /* 电机运行超时时间 (30秒) */
#define CURRENT_CHECK_INTERVAL  50      /* 电流检测间隔 (毫秒) */
#define LIMIT_SWITCH_DEBOUNCE   3       /* 限位开关消抖次数 */

/* ==================== 前向声明 ==================== */
static void window_monitor_thread_entry(void *parameter);

/* ==================== 全局变量 ==================== */
static window_state_t g_window_state = WINDOW_IDLE;
static window_error_t g_window_error = WINDOW_OK;
static rt_mutex_t window_mutex = RT_NULL;
static rt_thread_t window_monitor_thread = RT_NULL;
static volatile rt_bool_t g_stop_requested = RT_FALSE;
static volatile rt_tick_t g_motion_start_time = 0;  /* 运动开始时间 */

/* 限位开关状态记录（用于检测变化）*/
static uint8_t g_last_limit_state = 0xFF;  /* 初始化为无效值，确保首次检测会上报 */

/* 状态变化回调函数指针 */
static window_status_callback_t g_status_callback = RT_NULL;

/* ==================== 状态变化上报 ==================== */

/**
 * @brief  注册状态变化回调函数
 * @param  callback: 回调函数指针
 */
void window_register_status_callback(window_status_callback_t callback)
{
    g_status_callback = callback;
}

/**
 * @brief  主动上报限位开关状态变化
 * @param  limit_state: 当前限位开关状态 (0=无触发, 1=关限位触发, 2=开限位触发)
 */
static void window_report_limit_change(uint8_t limit_state)
{
    /* 获取物理限位组合状态码 (0x10-0x13) */
    uint8_t protocol_status = window_get_limit_combined_status();
    
    rt_kprintf("[Window] Limit switch changed, reporting status: 0x%02X\n", protocol_status);
    
    /* 通过回调函数上报状态（如果已注册）*/
    if (g_status_callback != RT_NULL)
    {
        g_status_callback(protocol_status);
    }
}

/* ==================== 底层硬件初始化 ==================== */

/**
 * @brief  初始化GPIO（限位开关、电机控制、故障检测）
 */
static void windows_gpio_init(void)
{
    /* 使能时钟 */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOC);
    
    /* 配置限位开关为下拉输入 */
    gpio_init(LIMIT_SWITCH_1_PORT, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ, LIMIT_SWITCH_1_PIN);
    gpio_init(LIMIT_SWITCH_2_PORT, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ, LIMIT_SWITCH_2_PIN);
    
    /* 配置电机控制引脚为推挽输出，默认低电平 */
    gpio_init(MOTOR_AIN1_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, MOTOR_AIN1_PIN);
    gpio_init(MOTOR_AIN2_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, MOTOR_AIN2_PIN);
    gpio_init(MOTOR_BIN1_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, MOTOR_BIN1_PIN);
    gpio_init(MOTOR_BIN2_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, MOTOR_BIN2_PIN);
    
    gpio_bit_reset(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
    gpio_bit_reset(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);
    gpio_bit_reset(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN);
    gpio_bit_reset(MOTOR_BIN2_PORT, MOTOR_BIN2_PIN);
    
    /* 配置电流检测引脚为模拟输入 */
    gpio_init(AISEN_PORT, GPIO_MODE_AIN, GPIO_OSPEED_50MHZ, AISEN_PIN);
    gpio_init(BISEN_PORT, GPIO_MODE_AIN, GPIO_OSPEED_50MHZ, BISEN_PIN);
    
    /* 配置故障引脚为上拉输入 */
    gpio_init(NFAULT_PORT, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, NFAULT_PIN);
}

/**
 * @brief  初始化ADC（用于电流检测）
 */
static void windows_adc_init(void)
{
    /* 使能ADC0时钟 */
    rcu_periph_clock_enable(RCU_ADC0);
    
    /* 配置ADC时钟 */
    rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV6);
    
    /* 复位ADC0 */
    adc_deinit(ADC0);
    
    /* 配置ADC工作模式 */
    adc_mode_config(ADC_MODE_FREE);
    adc_special_function_config(ADC0, ADC_CONTINUOUS_MODE, DISABLE);
    adc_special_function_config(ADC0, ADC_SCAN_MODE, DISABLE);
    adc_data_alignment_config(ADC0, ADC_DATAALIGN_RIGHT);
    
    /* 配置ADC通道 */
    adc_channel_length_config(ADC0, ADC_REGULAR_CHANNEL, 1);
    
    /* 配置外部触发 */
    adc_external_trigger_source_config(ADC0, ADC_REGULAR_CHANNEL, ADC0_1_2_EXTTRIG_REGULAR_NONE);
    adc_external_trigger_config(ADC0, ADC_REGULAR_CHANNEL, ENABLE);
    
    /* 使能ADC */
    adc_enable(ADC0);
    rt_thread_mdelay(1);
    
    /* ADC校准 */
    adc_calibration_enable(ADC0);
}

/**
 * @brief  天窗模块初始化
 */
int windows_init(void)
{
    /* 初始化GPIO */
    windows_gpio_init();
    
    /* 初始化ADC */
    windows_adc_init();
    
    /* 创建互斥锁 */
    window_mutex = rt_mutex_create("win_mtx", RT_IPC_FLAG_PRIO);
    if (window_mutex == RT_NULL)
    {
        rt_kprintf("[Window] Failed to create mutex\n");
        return -RT_ERROR;
    }
    
    /* 创建后台监控线程 */
    window_monitor_thread = rt_thread_create("win_mon",
                                             window_monitor_thread_entry,
                                             RT_NULL,
                                             1024,
                                             10,
                                             10);
    if (window_monitor_thread == RT_NULL)
    {
        rt_kprintf("[Window] Failed to create monitor thread\n");
        return -RT_ERROR;
    }
    
    /* 启动监控线程 */
    rt_thread_startup(window_monitor_thread);
    
    rt_kprintf("[Window] Module initialized (non-blocking mode)\n");
    return RT_EOK;
}
INIT_APP_EXPORT(windows_init);

/* ==================== 底层电机控制函数 ==================== */

/**
 * @brief  停止电机
 */
static void motor_stop(void)
{
    gpio_bit_reset(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);
    gpio_bit_reset(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);
    gpio_bit_reset(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN);
    gpio_bit_reset(MOTOR_BIN2_PORT, MOTOR_BIN2_PIN);
}

/**
 * @brief  电机正转（天窗关闭方向）
 * HRBB33驱动器：AIN1=1, AIN2=0, BIN1=0, BIN2=1 为正转
 */
static void motor_forward(void)
{
    gpio_bit_set(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);      // PC9 = 1
    gpio_bit_reset(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);    // PA8 = 0
    gpio_bit_reset(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN);    // PA9 = 0
    gpio_bit_set(MOTOR_BIN2_PORT, MOTOR_BIN2_PIN);      // PA10 = 1
}

/**
 * @brief  电机反转（天窗打开方向）
 * HRBB33驱动器：AIN1=0, AIN2=1, BIN1=1, BIN2=0 为反转
 */
static void motor_reverse(void)
{
    gpio_bit_reset(MOTOR_AIN1_PORT, MOTOR_AIN1_PIN);    // PC9 = 0
    gpio_bit_set(MOTOR_AIN2_PORT, MOTOR_AIN2_PIN);      // PA8 = 1
    gpio_bit_set(MOTOR_BIN1_PORT, MOTOR_BIN1_PIN);      // PA9 = 1
    gpio_bit_reset(MOTOR_BIN2_PORT, MOTOR_BIN2_PIN);    // PA10 = 0
}

/**
 * @brief  读取 ADC 通道值
 * @param  channel: ADC 通道
 * @return ADC 值 (0-4095)
 */
static uint16_t adc_read_channel(uint8_t channel)
{
    uint16_t adc_value;
    
    /* 获取 ADC 互斥锁 */
    if (adc_mutex != RT_NULL)
    {
        rt_mutex_take(adc_mutex, RT_WAITING_FOREVER);
    }
    
    /* 配置要采样的通道 */
    adc_regular_channel_config(ADC0, 0, channel, ADC_SAMPLETIME_55POINT5);
    
    /* 启动转换 */
    adc_software_trigger_enable(ADC0, ADC_REGULAR_CHANNEL);
    
    /* 等待转换完成 */
    while (!adc_flag_get(ADC0, ADC_FLAG_EOC));
    
    /* 读取结果 */
    adc_value = adc_regular_data_read(ADC0);
    
    /* 释放 ADC 互斥锁 */
    if (adc_mutex != RT_NULL)
    {
        rt_mutex_release(adc_mutex);
    }
    
    return adc_value;
}

/**
 * @brief  检测限位开关状态
 * @return 0: 未触发, 1: 限位1触发(关闭到位), 2: 限位2触发(打开到位)
 */
static uint8_t check_limit_switch(void)
{
    uint8_t limit1 = gpio_input_bit_get(LIMIT_SWITCH_1_PORT, LIMIT_SWITCH_1_PIN);
    uint8_t limit2 = gpio_input_bit_get(LIMIT_SWITCH_2_PORT, LIMIT_SWITCH_2_PIN);
    
    /* 高电平有效 */
    if (limit1 == 1) return 1;  /* 关闭到位 */
    if (limit2 == 1) return 2;  /* 打开到位 */
    
    return 0;  /* 未触发 */
}

/**
 * @brief  检测电流是否超限
 * @return RT_TRUE: 过流, RT_FALSE: 正常
 */
static rt_bool_t check_overcurrent(void)
{
    uint16_t aisen_val = adc_read_channel(AISEN_ADC_CHANNEL);
    uint16_t bisen_val = adc_read_channel(BISEN_ADC_CHANNEL);
    
    if (aisen_val > CURRENT_THRESHOLD_A)
    {
        g_window_error = WINDOW_ERR_OVERCURRENT_A;
        rt_kprintf("[Window] Overcurrent on A: %d\n", aisen_val);
        return RT_TRUE;
    }
    
    if (bisen_val > CURRENT_THRESHOLD_B)
    {
        g_window_error = WINDOW_ERR_OVERCURRENT_B;
        rt_kprintf("[Window] Overcurrent on B: %d\n", bisen_val);
        return RT_TRUE;
    }
    
    return RT_FALSE;
}

/**
 * @brief  检测驱动器故障
 * @return RT_TRUE: 故障, RT_FALSE: 正常
 */
static rt_bool_t check_fault(void)
{
    /* NFAULT低电平表示故障 */
    if (gpio_input_bit_get(NFAULT_PORT, NFAULT_PIN) == 0)
    {
        g_window_error = WINDOW_ERR_FAULT;
        rt_kprintf("[Window] Driver fault detected!\n");
        return RT_TRUE;
    }
    return RT_FALSE;
}

/* ==================== 后台监控线程 ==================== */

/**
 * @brief  天窗监控线程 - 非阻塞模式下监控天窗状态
 */
static void window_monitor_thread_entry(void *parameter)
{
    uint8_t debounce_count = 0;
    uint8_t last_debounce_state = 0;
    
    rt_kprintf("[Window] Monitor thread started\n");
    
    while (1)
    {
        /* 读取当前限位开关状态 */
        uint8_t current_limit = check_limit_switch();
        
        /* ==================== 限位开关状态变化检测与上报 ==================== */
        /* 检测限位开关状态变化（无论是否在运动中都要检测）*/
        if (current_limit != g_last_limit_state)
        {
            /* 状态变化，进行消抖 */
            if (current_limit != last_debounce_state)
            {
                last_debounce_state = current_limit;
                debounce_count = 0;
            }
            else
            {
                debounce_count++;
                if (debounce_count >= LIMIT_SWITCH_DEBOUNCE)
                {
                    /* 消抖完成，确认状态变化 */
                    uint8_t old_state = g_last_limit_state;
                    g_last_limit_state = current_limit;
                    debounce_count = 0;
                    
                    rt_kprintf("[Window] Limit switch changed: %d -> %d\n", old_state, current_limit);
                    
                    /* 主动上报状态变化 */
                    window_report_limit_change(current_limit);
                }
            }
        }
        else
        {
            debounce_count = 0;
        }
        
        /* ==================== 运动状态监控 ==================== */
        if (g_window_state == WINDOW_OPENING || g_window_state == WINDOW_CLOSING)
        {
            /* 检查是否收到停止请求 */
            if (g_stop_requested)
            {
                motor_stop();
                g_window_state = WINDOW_IDLE;
                g_stop_requested = RT_FALSE;
                rt_kprintf("[Window] Stopped by request\n");
                
                /* 停止后上报当前状态，并同步消抖基准，防止重复上报 */
                {
                    uint8_t cur = check_limit_switch();
                    g_last_limit_state = cur;
                    debounce_count = 0;
                    window_report_limit_change(cur);
                }
                continue;
            }
            
            /* 检查是否到达目标限位 */
            if (g_window_state == WINDOW_CLOSING && current_limit == 1)
            {
                motor_stop();
                g_window_state = WINDOW_IDLE;
                rt_kprintf("[Window] Closed successfully\n");
                
                /* 到达限位，上报状态，同步消抖基准，防止重复上报 */
                g_last_limit_state = current_limit;
                debounce_count = 0;
                window_report_limit_change(current_limit);
                continue;
            }
            
            if (g_window_state == WINDOW_OPENING && current_limit == 2)
            {
                motor_stop();
                g_window_state = WINDOW_IDLE;
                rt_kprintf("[Window] Opened successfully\n");
                
                /* 到达限位，上报状态，同步消抖基准，防止重复上报 */
                g_last_limit_state = current_limit;
                debounce_count = 0;
                window_report_limit_change(current_limit);
                continue;
            }
            
            /* 检查故障 */
            if (check_fault())
            {
                motor_stop();
                g_window_state = WINDOW_ERROR;
                rt_kprintf("[Window] Driver fault detected!\n");
                
                /* 故障状态上报，同步消抖基准，防止重复上报 */
                {
                    uint8_t cur = check_limit_switch();
                    g_last_limit_state = cur;
                    debounce_count = 0;
                    window_report_limit_change(cur);
                }
                continue;
            }
            
            /* 检查电流 */
            if (check_overcurrent())
            {
                motor_stop();
                g_window_state = WINDOW_ERROR;
                rt_kprintf("[Window] Overcurrent detected!\n");
                
                /* 过流状态上报，同步消抖基准，防止重复上报 */
                {
                    uint8_t cur = check_limit_switch();
                    g_last_limit_state = cur;
                    debounce_count = 0;
                    window_report_limit_change(cur);
                }
                continue;
            }
            
            /* 检查超时 */
            if ((rt_tick_get() - g_motion_start_time) > rt_tick_from_millisecond(MOTOR_TIMEOUT_MS))
            {
                motor_stop();
                g_window_state = WINDOW_ERROR;
                g_window_error = WINDOW_ERR_TIMEOUT;
                rt_kprintf("[Window] Timeout\n");
                
                /* 超时状态上报，同步消抖基准，防止重复上报 */
                {
                    uint8_t cur = check_limit_switch();
                    g_last_limit_state = cur;
                    debounce_count = 0;
                    window_report_limit_change(cur);
                }
                continue;
            }
        }
        
        rt_thread_mdelay(CURRENT_CHECK_INTERVAL);
    }
}

/* ==================== 对外接口函数 ==================== */

/**
 * @brief  天窗关闭（非阻塞）
 * @return RT_EOK: 成功启动, -RT_ERROR: 失败
 */
rt_err_t window_close(void)
{
    uint8_t limit_state;
    
    if (window_mutex == RT_NULL)
    {
        rt_kprintf("[Window] Module not initialized\n");
        return -RT_ERROR;
    }
    
    /* 获取互斥锁 */
    if (rt_mutex_take(window_mutex, RT_WAITING_FOREVER) != RT_EOK)
    {
        return -RT_ERROR;
    }
    
    /* 检查是否已经在关闭位置 */
    limit_state = check_limit_switch();
    if (limit_state == 1)
    {
        rt_kprintf("[Window] Already closed\n");
        rt_mutex_release(window_mutex);
        return RT_EOK;
    }
    
    /* 检查是否正在执行其他操作 */
    if (g_window_state == WINDOW_OPENING || g_window_state == WINDOW_CLOSING)
    {
        rt_kprintf("[Window] Busy, current state: %d\n", g_window_state);
        rt_mutex_release(window_mutex);
        return -RT_ERROR;
    }
    
    rt_kprintf("[Window] Closing...\n");
    g_window_state = WINDOW_CLOSING;
    g_window_error = WINDOW_OK;
    g_stop_requested = RT_FALSE;
    g_motion_start_time = rt_tick_get();  /* 记录开始时间 */
    
    /* 启动电机正转 */
    motor_forward();
    
    rt_mutex_release(window_mutex);
    return RT_EOK;
}

/**
 * @brief  天窗打开（非阻塞）
 * @return RT_EOK: 成功启动, -RT_ERROR: 失败
 */
rt_err_t window_open(void)
{
    uint8_t limit_state;
    
    if (window_mutex == RT_NULL)
    {
        rt_kprintf("[Window] Module not initialized\n");
        return -RT_ERROR;
    }
    
    /* 获取互斥锁 */
    if (rt_mutex_take(window_mutex, RT_WAITING_FOREVER) != RT_EOK)
    {
        return -RT_ERROR;
    }
    
    /* 检查是否已经在打开位置 */
    limit_state = check_limit_switch();
    if (limit_state == 2)
    {
        rt_kprintf("[Window] Already opened\n");
        rt_mutex_release(window_mutex);
        return RT_EOK;
    }
    
    /* 检查是否正在执行其他操作 */
    if (g_window_state == WINDOW_OPENING || g_window_state == WINDOW_CLOSING)
    {
        rt_kprintf("[Window] Busy, current state: %d\n", g_window_state);
        rt_mutex_release(window_mutex);
        return -RT_ERROR;
    }
    
    rt_kprintf("[Window] Opening...\n");
    g_window_state = WINDOW_OPENING;
    g_window_error = WINDOW_OK;
    g_stop_requested = RT_FALSE;
    g_motion_start_time = rt_tick_get();  /* 记录开始时间 */
    
    /* 启动电机反转 */
    motor_reverse();
    
    rt_mutex_release(window_mutex);
    return RT_EOK;
}

/**
 * @brief  紧急停止（非阻塞）
 * @note   设置停止标志，由监控线程处理实际停止
 */
void window_emergency_stop(void)
{
    g_stop_requested = RT_TRUE;
    rt_kprintf("[Window] Emergency stop requested\n");
}

/**
 * @brief  获取天窗状态
 * @return 当前状态
 */
window_state_t window_get_state(void)
{
    return g_window_state;
}

/**
 * @brief  获取错误代码
 * @return 错误代码
 */
window_error_t window_get_error(void)
{
    return g_window_error;
}

/**
 * @brief  获取天窗协议状态码
 * @return 协议状态码
 *   0: 设定正常 关限位触发
 *   1: 设定正常 开限位触发
 *   2: 正在关窗
 *   3: 正在开窗
 *   4: 设定失败 超时未响应
 *   5: 未知状态（高4位为1，低2位表示限位开关状态）
 *      bit 0 - 关限位状态 0触发 1未触发
 *      bit 1 - 开限位状态 0触发 1未触发
 */
uint8_t window_get_protocol_status(void)
{
    uint8_t limit_state = check_limit_switch();
    
    /* 检查当前状态 */
    switch (g_window_state)
    {
        case WINDOW_CLOSING:
            return 2;  /* 正在关窗 */
            
        case WINDOW_OPENING:
            return 3;  /* 正在开窗 */
            
        case WINDOW_ERROR:
            /* 检查错误类型 */
            if (g_window_error == WINDOW_ERR_TIMEOUT)
            {
                return 4;  /* 超时未响应 */
            }
            else
            {
                /* 其他错误，返回未知状态 + 限位开关状态 *///高电平才是按下，所以要取反处理
                uint8_t close_limit = !gpio_input_bit_get(LIMIT_SWITCH_1_PORT, LIMIT_SWITCH_1_PIN);
                uint8_t open_limit = !gpio_input_bit_get(LIMIT_SWITCH_2_PORT, LIMIT_SWITCH_2_PIN);
                
                /* 高4位为1 (0x10 = 16)，低2位表示限位状态 */
                /* bit0: 关限位 (1=未触发, 0=触发) */
                /* bit1: 开限位 (1=未触发, 0=触发) */
                return (0x10 | (open_limit << 1) | close_limit);
            }
            
        case WINDOW_IDLE:
        default:
            /* 空闲状态，根据限位开关返回 */
            if (limit_state == 1)
            {
                return 0;  /* 关限位触发 */
            }
            else if (limit_state == 2)
            {
                return 1;  /* 开限位触发 */
            }
            else
            {
                /* 未知位置，返回状态5 + 限位开关状态 *///高电平才是按下，所以要取反处理
                uint8_t close_limit = !gpio_input_bit_get(LIMIT_SWITCH_1_PORT, LIMIT_SWITCH_1_PIN);
                uint8_t open_limit = !gpio_input_bit_get(LIMIT_SWITCH_2_PORT, LIMIT_SWITCH_2_PIN);
                
                /* 高4位为1 (0x10 = 16)，低2位表示限位状态 */
                return (0x10 | (open_limit << 1) | close_limit);
            }
    }
}

/**
 * @brief  获取天窗当前物理限位组合状态
 * @return 0x10-0x13:
 *         0x10: 两个限位都触发 (异常)
 *         0x11: 仅关限位触发
 *         0x12: 仅开限位触发
 *         0x13: 两个限位都未触发 (中间位置)
 */
uint8_t window_get_limit_combined_status(void)
{
    /* 高电平表示触发，逻辑要求：0表示触发，1表示未触发 */
    /* bit0: 关限位 (0=触发, 1=未触发) */
    /* bit1: 开限位 (0=触发, 1=未触发) */
    uint8_t close_limit = !gpio_input_bit_get(LIMIT_SWITCH_1_PORT, LIMIT_SWITCH_1_PIN);
    uint8_t open_limit = !gpio_input_bit_get(LIMIT_SWITCH_2_PORT, LIMIT_SWITCH_2_PIN);
    
    return (0x10 | (open_limit << 1) | close_limit);
}

/* ==================== Shell命令 ==================== */

//查询限位开关状态的Shell命令
static void windows_check(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("Usage: windows <get|open|close|stop|status>\n");
        return;
    }
    
    if (rt_strcmp(argv[1], "get") == 0)
    {
        uint8_t pa1_val = gpio_input_bit_get(GPIOA, GPIO_PIN_1);
        uint8_t pa2_val = gpio_input_bit_get(GPIOA, GPIO_PIN_2);
        
        rt_kprintf("Window limit switch: PA1(Open)=%d, PA2(Close)=%d\n", pa1_val, pa2_val);
    }
    else if (rt_strcmp(argv[1], "open") == 0)
    {
        if (window_open() == RT_EOK)
        {
            rt_kprintf("Window open command executed successfully\n");
        }
        else
        {
            rt_kprintf("Window open failed, error=%d\n", g_window_error);
        }
    }
    else if (rt_strcmp(argv[1], "close") == 0)
    {
        if (window_close() == RT_EOK)
        {
            rt_kprintf("Window close command executed successfully\n");
        }
        else
        {
            rt_kprintf("Window close failed, error=%d\n", g_window_error);
        }
    }
    else if (rt_strcmp(argv[1], "stop") == 0)
    {
        window_emergency_stop();
    }
    else if (rt_strcmp(argv[1], "status") == 0)
    {
        rt_kprintf("Window State: %d (0=Idle, 1=Opening, 2=Closing, 3=Error)\n", g_window_state);
        rt_kprintf("Window Error: %d (0=OK, 1=OC_A, 2=OC_B, 3=Fault, 4=Timeout)\n", g_window_error);
        
        uint16_t aisen = adc_read_channel(AISEN_ADC_CHANNEL);
        uint16_t bisen = adc_read_channel(BISEN_ADC_CHANNEL);
        uint8_t nfault = gpio_input_bit_get(NFAULT_PORT, NFAULT_PIN);
        
        rt_kprintf("AISEN=%d, BISEN=%d, NFAULT=%d\n", aisen, bisen, nfault);
    }
    else
    {
        rt_kprintf("Unknown command. Usage: windows <get|open|close|stop|status>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(windows_check, windows, Window control and status);