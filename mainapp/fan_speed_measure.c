/*
 * @file fan_speed_measure.c
 * @brief 风扇转速测量模块 - 通过FG引脚测量风扇转速
 * 
 * 测量原理：
 *   - FG信号：每转2个脉冲
 *   - 使用定时器捕获FG引脚的脉冲
 *   - 计算公式：RPM = (脉冲数 / 2) * 60 / 测量时间(秒)
 */

#include <rtthread.h>
#include "gd32f30x.h"

/* 风扇FG引脚定义 */
#define FAN_COUNT   14

typedef struct {
    uint32_t gpio_periph;   /* GPIO外设 */
    uint32_t pin;           /* GPIO引脚 */
    uint16_t rpm;           /* 当前转速(RPM) */
    uint32_t pulse_count;   /* 脉冲计数 */
    uint8_t last_state;     /* 上次引脚状态 */
} fan_fg_t;

/* 风扇FG引脚配置表 */
static fan_fg_t g_fan_fg[FAN_COUNT] = {
    {GPIOA, GPIO_PIN_5,  0, 0, 0},  /* FAN1  - PA5  */
    {GPIOA, GPIO_PIN_4,  0, 0, 0},  /* FAN2  - PA4  */
    {GPIOA, GPIO_PIN_7,  0, 0, 0},  /* FAN3  - PA7  */
    {GPIOC, GPIO_PIN_4,  0, 0, 0},  /* FAN4  - PC4  */
    {GPIOC, GPIO_PIN_5,  0, 0, 0},  /* FAN5  - PC5  */
    {GPIOB, GPIO_PIN_0,  0, 0, 0},  /* FAN6  - PB0  */
    {GPIOB, GPIO_PIN_10, 0, 0, 0},  /* FAN7  - PB10 */
    {GPIOB, GPIO_PIN_11, 0, 0, 0},  /* FAN8  - PB11 */
    {GPIOB, GPIO_PIN_12, 0, 0, 0},  /* FAN9  - PB12 */
    {GPIOB, GPIO_PIN_13, 0, 0, 0},  /* FAN10 - PB13 */
    {GPIOB, GPIO_PIN_15, 0, 0, 0},  /* FAN11 - PB15 */
    {GPIOC, GPIO_PIN_6,  0, 0, 0},  /* FAN12 - PC6  */
    {GPIOC, GPIO_PIN_7,  0, 0, 0},  /* FAN13 - PC7  */
    {GPIOC, GPIO_PIN_8,  0, 0, 0},  /* FAN14 - PC8  */
};

/* 测量线程 */
static rt_thread_t measure_thread = RT_NULL;
static rt_bool_t is_measuring = RT_FALSE;  /* 测量状态标志 */
static rt_sem_t measure_done_sem = RT_NULL;  /* 测量完成信号量 */

/**
 * @brief  初始化风扇FG引脚
 */
static void fan_fg_gpio_init(void)
{
    /* 使能GPIO时钟 */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    
    /* 配置所有FG引脚为输入上拉 */
    for (int i = 0; i < FAN_COUNT; i++)
    {
        gpio_init(g_fan_fg[i].gpio_periph, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, g_fan_fg[i].pin);
        g_fan_fg[i].last_state = gpio_input_bit_get(g_fan_fg[i].gpio_periph, g_fan_fg[i].pin);
    }
    
    rt_kprintf("[FanSpeed] FG pins initialized\n");
}

/**
 * @brief  读取FG引脚状态并计数脉冲
 */
static void fan_fg_pulse_count(void)
{
    for (int i = 0; i < FAN_COUNT; i++)
    {
        uint8_t current_state = gpio_input_bit_get(g_fan_fg[i].gpio_periph, g_fan_fg[i].pin);
        
        /* 检测下降沿（从高到低） */
        if (g_fan_fg[i].last_state == SET && current_state == RESET)
        {
            g_fan_fg[i].pulse_count++;
        }
        
        g_fan_fg[i].last_state = current_state;
    }
}

/**
 * @brief  风扇转速测量线程
 * @param  parameter: 线程参数
 * @note   触发后测量1秒，然后自动停止，释放CPU
 */
static void fan_speed_measure_entry(void *parameter)
{
    while (1)
    {
        /* 等待测量启动信号 */
        if (!is_measuring)
        {
            rt_thread_mdelay(100);  /* 空闲时休眠 */
            continue;
        }
        
        rt_kprintf("[FanSpeed] Starting measurement...\n");
        
        /* 清零脉冲计数 */
        for (int i = 0; i < FAN_COUNT; i++)
        {
            g_fan_fg[i].pulse_count = 0;
        }
        
        rt_uint32_t sample_count = 0;
        const rt_uint32_t sample_interval_ms = 1;   /* 采样间隔1ms (提高采样率) */
        const rt_uint32_t calc_interval = 1000;     /* 1000次采样后计算(1秒) */
        
        /* 测量1秒 */
        while (sample_count < calc_interval && is_measuring)
        {
            /* 脉冲计数 */
            fan_fg_pulse_count();
            
            sample_count++;
            rt_thread_mdelay(sample_interval_ms);
        }
        
        /* 计算转速 */
        for (int i = 0; i < FAN_COUNT; i++)
        {
            /* 计算RPM: (脉冲数/2) * 60 / 测量时间(秒) */
            rt_uint32_t pulse = g_fan_fg[i].pulse_count;
            g_fan_fg[i].rpm = (pulse * 60) / 2;  /* 1秒内的脉冲数/2 * 60 = 转速 */
        }
        
        rt_kprintf("[FanSpeed] Measurement completed\n");
        
        /* 停止测量 */
        is_measuring = RT_FALSE;
        
        /* 释放信号量，通知测量完成 */
        if (measure_done_sem != RT_NULL)
        {
            rt_sem_release(measure_done_sem);
        }
    }
}

/**
 * @brief  启动风扇转速测量（阻塞等待完成）
 * @retval RT_EOK: 成功, -RT_ERROR: 失败
 * @note   此函数会阻塞约1秒，等待测量完成
 */
rt_err_t start_fan_speed_measure(void)
{
    /* 检查是否正在测量 */
    if (is_measuring)
    {
        rt_kprintf("[FanSpeed] Already measuring, please wait...\n");
        /* 等待当前测量完成 */
        if (measure_done_sem != RT_NULL)
        {
            rt_sem_take(measure_done_sem, rt_tick_from_millisecond(2000));
        }
    }
    
    /* 启动测量 */
    is_measuring = RT_TRUE;
    
    /* 等待测量完成（最多等待2秒）*/
    if (measure_done_sem != RT_NULL)
    {
        rt_err_t ret = rt_sem_take(measure_done_sem, rt_tick_from_millisecond(2000));
        if (ret != RT_EOK)
        {
            rt_kprintf("[FanSpeed] Measurement timeout!\n");
            is_measuring = RT_FALSE;
            return -RT_ERROR;
        }
    }
    
    return RT_EOK;
}

/**
 * @brief  获取风扇转速数据（按协议格式）
 * @param  data: 输出缓冲区（至少28字节）
 * @param  len: 缓冲区长度
 * @retval 实际填充的字节数
 * 
 * @note   协议格式：
 *         每个风扇占2字节(高字节+低字节)
 *         数据格式：ID(高字节) + 转速(低字节)
 *         转速编码：0x01转速=1%, 0x64转速=100%
 *         
 *         例如：0x01 0x64 表示ID1转速100%
 */
int get_fan_speed(uint8_t *data, uint16_t len)
{
    if (data == RT_NULL || len < 28)
    {
        return 0;
    }
    
    rt_memset(data, 0, len);
    
    /* 填充风扇转速数据 */
    for (int i = 0; i < FAN_COUNT; i++)
    {
        uint8_t fan_id = i + 1;  /* 风扇ID: 1-14 */
        uint16_t rpm = g_fan_fg[i].rpm;
        uint8_t speed_percent;
        
        /* 根据风扇ID计算转速百分比 */
        if (fan_id <= 2)
        {
            /* FAN1和FAN2: 9000 RPM */
            speed_percent = (rpm > 9000) ? 100 : (rpm * 100 / 9000);
        }
        else
        {
            /* FAN3-FAN14: 5000 RPM */
            speed_percent = (rpm > 5000) ? 100 : (rpm * 100 / 5000);
        }
        
        /* 按协议格式填充：ID(高字节) + 转速%(低字节) */
        data[i * 2] = fan_id;           /* 高字节：风扇ID */
        data[i * 2 + 1] = speed_percent; /* 低字节：转速百分比 */
    }
    
    return 28;  /* 返回填充的字节数 */
}

/**
 * @brief  获取指定风扇的实际转速(RPM)
 * @param  fan_id: 风扇ID(1-14)
 * @retval 转速(RPM)
 */
uint16_t get_fan_rpm(uint8_t fan_id)
{
    if (fan_id < 1 || fan_id > FAN_COUNT)
    {
        return 0;
    }
    
    return g_fan_fg[fan_id - 1].rpm;
}

/**
 * @brief  风扇转速测量模块初始化
 */
int fan_speed_measure_init(void)
{
    rt_kprintf("[FanSpeed] Initializing fan speed measurement...\n");
    
    /* 初始化FG引脚 */
    fan_fg_gpio_init();
    
    /* 创建信号量 */
    measure_done_sem = rt_sem_create("fan_done", 0, RT_IPC_FLAG_FIFO);
    if (measure_done_sem == RT_NULL)
    {
        rt_kprintf("[FanSpeed] ERROR: Failed to create semaphore\n");
        return -RT_ERROR;
    }
    
    /* 创建测量线程（优先级较低，避免占用CPU）*/
    measure_thread = rt_thread_create("fan_meas",
                                     fan_speed_measure_entry,
                                     RT_NULL,
                                     2048,
                                     20,  /* 降低优先级 */
                                     10);
    if (measure_thread != RT_NULL)
    {
        rt_thread_startup(measure_thread);
        rt_kprintf("[FanSpeed] Measurement thread created (idle state)\n");
    }
    else
    {
        rt_kprintf("[FanSpeed] ERROR: Failed to create thread\n");
        return -RT_ERROR;
    }
    
    rt_kprintf("[FanSpeed] Module initialized successfully\n");
    return RT_EOK;
}

/* 自动初始化 */
// INIT_APP_EXPORT(fan_speed_measure_init);
