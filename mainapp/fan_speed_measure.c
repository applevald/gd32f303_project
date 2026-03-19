/*
 * @file fan_speed_measure.c
 * @brief 风扇转速测量模块 - 通过FG引脚测量风扇转速
 * 
 * 测量原理：
 *   - FG信号：每转2个脉冲
 *   - 使用GPIO轮询方式检测脉冲边沿
 *   - 后台线程持续测量，每100ms更新一次转速
 *   - 计算公式：RPM = (脉冲数 / 2) * 60 / 测量时间(秒)
 */

#include <rtthread.h>
#include "gd32f30x.h"

/* 风扇FG引脚定义 */
#define FAN_COUNT   14

/* 测量参数配置 */
/* 系统时钟: 108MHz, APB1 = 54MHz, TIMER3_CLK = 108MHz (APB1×2) */
#define TIMER_PRESCALER         107         /* 108MHz / 108 = 1MHz计数频率 (1us分辨率) */
#define TIMER_PERIOD            99          /* 100us 中断一次 (10kHz采样率) */
#define FAN_TIMEOUT_TICKS       20000       /* 2秒无脉冲视为停止(20000 * 100us) */
#define RPM_CALC_CONST          300000      /* RPM = 300,000 / period_ticks (100us单位, 2脉冲/转) */

typedef struct {
    uint32_t gpio_periph;   /* GPIO外设 */
    uint32_t pin;           /* GPIO引脚 */
    volatile uint32_t last_edge_tick; /* 上次下降沿的时间戳(100us单位) */
    volatile uint32_t period_ticks;   /* 脉冲周期(100us单位) */
    uint8_t last_state;     /* 上次引脚状态 */
    volatile uint16_t rpm;  /* 缓存的RPM值 */
} fan_fg_t;

/* 全局时间戳计数器 (100us单位) */
static volatile uint32_t g_timer_tick = 0;

/* 风扇FG引脚配置表 */
static fan_fg_t g_fan_fg[FAN_COUNT] = {
    {GPIOA, GPIO_PIN_5,  0, 0, 0, 0},  /* FAN1  - PA5  */
    {GPIOA, GPIO_PIN_4,  0, 0, 0, 0},  /* FAN2  - PA4  */
    {GPIOA, GPIO_PIN_7,  0, 0, 0, 0},  /* FAN3  - PA7  */
    {GPIOC, GPIO_PIN_4,  0, 0, 0, 0},  /* FAN4  - PC4  */
    {GPIOC, GPIO_PIN_5,  0, 0, 0, 0},  /* FAN5  - PC5  */
    {GPIOB, GPIO_PIN_0,  0, 0, 0, 0},  /* FAN6  - PB0  */
    {GPIOB, GPIO_PIN_10, 0, 0, 0, 0},  /* FAN7  - PB10 */
    {GPIOB, GPIO_PIN_11, 0, 0, 0, 0},  /* FAN8  - PB11 */
    {GPIOB, GPIO_PIN_12, 0, 0, 0, 0},  /* FAN9  - PB12 */
    {GPIOB, GPIO_PIN_13, 0, 0, 0, 0},  /* FAN10 - PB13 */
    {GPIOB, GPIO_PIN_15, 0, 0, 0, 0},  /* FAN11 - PB15 */
    {GPIOC, GPIO_PIN_6,  0, 0, 0, 0},  /* FAN12 - PC6  */
    {GPIOC, GPIO_PIN_7,  0, 0, 0, 0},  /* FAN13 - PC7  */
    {GPIOC, GPIO_PIN_8,  0, 0, 0, 0},  /* FAN14 - PC8  */
};

/**
 * @brief  初始化风扇FG引脚和定时器
 */
static void fan_fg_hardware_init(void)
{
    /* 1. GPIO 初始化 */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    
    for (int i = 0; i < FAN_COUNT; i++)
    {
        gpio_init(g_fan_fg[i].gpio_periph, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, g_fan_fg[i].pin);
        g_fan_fg[i].last_state = gpio_input_bit_get(g_fan_fg[i].gpio_periph, g_fan_fg[i].pin);
    }
    
    /* 2. 定时器 初始化 (TIMER3) - 100us中断 */
    rcu_periph_clock_enable(RCU_TIMER3);
    timer_deinit(TIMER3);
    
    timer_parameter_struct timer_initpara;
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler         = TIMER_PRESCALER;
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = TIMER_PERIOD;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_initpara.repetitioncounter = 0;
    timer_init(TIMER3, &timer_initpara);

    /* 清除更新中断标志并使能中断 */
    timer_interrupt_flag_clear(TIMER3, TIMER_INT_UP);
    timer_interrupt_enable(TIMER3, TIMER_INT_UP);
    
    /* 配置NVIC中断优先级 */
    nvic_irq_enable(TIMER3_IRQn, 1, 0); // 较高优先级
    
    /* 使能定时器 */
    timer_enable(TIMER3);
    
    rt_kprintf("[FanSpeed] Hardware initialized (Timer3 @ 10kHz)\n");
}

/**
 * @brief  TIMER3 中断服务函数 - 100us周期执行
 * @note   负责采样引脚状态并计算脉冲间隔
 */
void TIMER3_IRQHandler(void)
{
    if (timer_interrupt_flag_get(TIMER3, TIMER_INT_UP) != RESET)
    {
        timer_interrupt_flag_clear(TIMER3, TIMER_INT_UP);
        g_timer_tick++;
        
        /* 遍历所有风扇引脚进行采样 */
        for (int i = 0; i < FAN_COUNT; i++)
        {
            /* 读取当前引脚电平 (宏展开优化) */
            uint8_t current_state = gpio_input_bit_get(g_fan_fg[i].gpio_periph, g_fan_fg[i].pin);
            
            /* 检测下降沿 (1 -> 0) */
            if (g_fan_fg[i].last_state == SET && current_state == RESET)
            {
                uint32_t current_tick = g_timer_tick;
                uint32_t last_tick = g_fan_fg[i].last_edge_tick;
                
                /* 计算周期并更新时间戳 */
                if (current_tick > last_tick)
                {
                    g_fan_fg[i].period_ticks = current_tick - last_tick;
                }
                else
                {
                    /* 处理溢出回绕情况 (32位tick很久才会溢出) */
                     g_fan_fg[i].period_ticks = (0xFFFFFFFF - last_tick) + current_tick + 1;
                }
                
                g_fan_fg[i].last_edge_tick = current_tick;
            }
            
            g_fan_fg[i].last_state = current_state;
        }
    }
}

/**
 * @brief  启动风扇转速测量（兼容旧接口，实际上由硬件定时器驱动）
 */
rt_err_t start_fan_speed_measure(void)
{
    return RT_EOK;
}


/**
 * @brief  计算并更新指定风扇的RPM
 */
static void update_fan_rpm(int i)
{
    uint32_t current_tick = g_timer_tick;
    uint32_t last_tick = g_fan_fg[i].last_edge_tick;
    uint32_t elapsed;
    
    /* 处理 tick 计数器溢出回绕 */
    if (current_tick >= last_tick)
    {
        elapsed = current_tick - last_tick;
    }
    else
    {
        elapsed = (0xFFFFFFFF - last_tick) + current_tick + 1;
    }
    
    /* 判定超时：如果超过2秒没有脉冲，认为风扇已停转 */
    if (elapsed > FAN_TIMEOUT_TICKS)
    {
        g_fan_fg[i].rpm = 0;
    }
    else if (g_fan_fg[i].period_ticks > 0)
    {
        /* RPM = 常数 / 周期tick数 */
        /* CONST = 300,000 (100us单位) */
        uint32_t rpm = RPM_CALC_CONST / g_fan_fg[i].period_ticks;
        
        // /* 简单滤波或限幅 */
        // if (rpm > 20000) rpm = 0; /* 过滤异常大值 */
        
        g_fan_fg[i].rpm = (uint16_t)rpm;
    }
}

/**
 * @brief  获取风扇转速数据（按协议格式）
 * @param  data: 输出缓冲区（至少28字节）
 * @param  len: 缓冲区长度
 * @retval 实际填充的字节数
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
        /* 先更新RPM */
        update_fan_rpm(i);
        
        uint8_t fan_id = i + 1;  /* 风扇ID: 1-14 */
        uint16_t rpm = g_fan_fg[i].rpm;
        uint8_t speed_percent;
        
        /* 根据风扇ID计算转速百分比（浮点运算提高精度）*/
        if (fan_id <= 2)
        {
            /* FAN1和FAN2: 9000 RPM */
            if (rpm > 9000) {
                speed_percent = 100;
            } else {
                speed_percent = (uint8_t)((rpm * 100.0f + 0.5f) / 9000.0f);
            }
        }
        else
        {
            /* FAN3-FAN14: 5000 RPM */
            if (rpm > 5000) {
                speed_percent = 100;
            } else {
                speed_percent = (uint8_t)((rpm * 100.0f + 0.5f) / 5000.0f);
            }
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
    
    int idx = fan_id - 1;
    update_fan_rpm(idx);
    
    return g_fan_fg[idx].rpm;
}

/**
 * @brief  获取风扇状态数据（按协议格式）
 * @param  data: 输出缓冲区（至少28字节）
 * @param  len: 缓冲区长度
 * @param  fan_target_speed: 风扇目标速度数组（百分比 0-100，14个元素）
 * @retval 实际填充的字节数
 */
int get_fan_status(uint8_t *data, uint16_t len, const uint8_t *fan_target_speed)
{
    if (data == RT_NULL || len < 28 || fan_target_speed == RT_NULL)
    {
        return 0;
    }
    
    rt_memset(data, 0, len);
    
    /* 填充风扇状态数据 - 优化版本，减少浮点运算 */
    for (int i = 0; i < FAN_COUNT; i++)
    {
        /* 快速读取RPM，减少函数调用开销 */
        uint16_t rpm = g_fan_fg[i].rpm;
        uint8_t fan_id = i + 1;
        uint8_t target_speed = fan_target_speed[i];  /* 目标速度百分比 */
        uint8_t status = 0x00;  /* 默认正常 */
        uint16_t target_rpm;
        
        /* 根据风扇ID确定额定转速 - 使用整数运算 */
        if (fan_id <= 2)
        {
            /* FAN1和FAN2: 9000 RPM */
            target_rpm = (90 * target_speed);  /* 9000 * target_speed / 100 */
        }
        else
        {
            /* FAN3-FAN14: 5000 RPM */
            target_rpm = (50 * target_speed);  /* 5000 * target_speed / 100 */
        }
        
        /* 判断风扇状态 - 使用整数比较 */
        if (target_speed > 0)
        {
            /* 设置了速度 */
            if (rpm == 0)
            {
                /* 堵转：设置了速度但转速为0 */
                status = 0x01;
            }
            else if (rpm < (target_rpm * 7 / 10))  /* 目标转速的70% */
            {
                /* 转速过慢：实际转速 < 目标转速的70% */
                status = 0x02;
            }
            else
            {
                /* 正常 */
                status = 0x00;
            }
        }
        else
        {
            /* 未设置速度 */
            if (rpm > 0)
            {
                /* 异常转动：未设置速度但有转速 */
                status = 0x03;
            }
            else
            {
                /* 正常（未设置速度且无转速）*/
                status = 0x00;
            }
        }
        
        /* 按协议格式填充：ID(高字节) + 状态(低字节) */
        data[i * 2] = fan_id;
        data[i * 2 + 1] = status;
    }
    
    return 28;
}

/**
 * @brief  风扇转速测量模块初始化
 */
int fan_speed_measure_init(void)
{
    rt_kprintf("[FanSpeed] Initializing fan speed measurement (Timer3 Interrupt)...\n");
    
    /* 初始化硬件（GPIO + Timer3） */
    fan_fg_hardware_init();
    
    rt_kprintf("[FanSpeed] Module initialized successfully (Interrupt mode)\n");
    return RT_EOK;
}

/* 自动初始化 */
INIT_APP_EXPORT(fan_speed_measure_init);
