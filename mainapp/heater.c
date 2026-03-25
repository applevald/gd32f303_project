//PB9引脚控制加热，高电平使能
#include <rtthread.h>
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"

/* 热腔风扇硬件组ID (对应协议ID 7-10) */
#define FAN_CHAMBER_GROUP_ID    2

uint32_t goal_temp = 0; // 目标温度，超过该温度时关闭加热器
static uint8_t g_heating_active = 0; // 加热是否激活（目标温度>0）
static rt_thread_t heater_thread = RT_NULL;

uint32_t get_goal_temp(void)
{
    return goal_temp;
}

/**
 * @brief 检查加热是否激活（目标温度>0）
 * @return 1: 加热中, 0: 未加热
 */
uint8_t is_heating_active(void)
{
    return g_heating_active;
}

/**
 * @brief 设置热腔风扇(硬件组ID=2，对应协议ID 7-10)为满速
 * @note  fan_set_speed 接受硬件组ID：0(协议1-2), 1(协议3-6), 2(协议7-10), 3(协议11-14)
 */
static void set_chamber_fans_full_speed(void)
{
    extern int fan_set_speed(uint8_t fan_id, uint8_t speed);
    /* 热腔风扇对应硬件组ID = 2，直接传组ID，无需循环 */
    fan_set_speed(2, 100);
}

void heater_set_state(uint8_t state)
{
    if (state)
    {
        /* 使能加热器（PB9高电平） */
        gpio_bit_set(GPIOB, GPIO_PIN_9);
    }
    else
    {
        /* 关闭加热器（PB9低电平） */
        gpio_bit_reset(GPIOB, GPIO_PIN_9);
    }
}

void heater_get_state(uint8_t *state)
{
    if (state == RT_NULL)
    {
        return;
    }
    
    /* 读取PB9状态 */
    *state = gpio_input_bit_get(GPIOB, GPIO_PIN_9);
}
extern rt_err_t temp_measure_get_temperature(float *temp);

uint8_t set_goal_temp(uint32_t temp)
{
    if(temp > 120){
        return 2; // 温度过高错误
    }
    
    uint8_t was_heating = g_heating_active;
    
    goal_temp = temp;
    
    if (temp > 0)
    {
        /* 开始加热：设置加热激活标志，热腔风扇满速 */
        g_heating_active = 1;
        set_chamber_fans_full_speed();
    }
    else
    {
        /* 停止加热：清除加热激活标志 */
        g_heating_active = 0;
    }
    
    return 0;
}

void heater_entry(void *parameter)
{
    static float current_temp = 0.0f;
    goal_temp = 0; // 目标温度，超过该温度时关闭加热器
    while (1)
    {
        uint8_t state;
        temp_measure_get_temperature(&current_temp);
        if(goal_temp > 0){
            if(current_temp >= goal_temp) // 温度超过30℃时开启加热器
            {
                state = 0;
            }
            else
            {
                state = 1;
            }
            heater_set_state(state);
        }
        rt_thread_mdelay(200); /* 每200毫秒检查一次温度 */
    }
}

int heater_init(void)
{
    /* 1. 使能GPIOB时钟 */
    rcu_periph_clock_enable(RCU_GPIOB);
    
    /* 2. 配置PB9为推挽输出 */
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);
    
    /* 3. 默认关闭加热器（PB9低电平） */
    gpio_bit_reset(GPIOB, GPIO_PIN_9);

    heater_thread = rt_thread_create("heater", heater_entry, RT_NULL, 512, 10, 20);
    if (heater_thread != RT_NULL)
    {
        rt_thread_startup(heater_thread);
        rt_kprintf("Heater control thread started\n");
    }
    else
    {
        rt_kprintf("Failed to create heater control thread\n");
    }

    return 0;
}
/* 自动初始化 */
INIT_APP_EXPORT(heater_init);

/* Shell command for testing */
static void heater_cmd(int argc, char **argv)
{
    if (argc >= 2 && rt_strcmp(argv[1], "on") == 0)
    {
        heater_set_state(1);
        rt_kprintf("Heater turned ON\n");
    }
    else if (argc >= 2 && rt_strcmp(argv[1], "off") == 0)
    {
        heater_set_state(0);
        rt_kprintf("Heater turned OFF\n");
    }
    else
    {
        rt_kprintf("Usage: heater [on|off]\n");
    }
}
/* Export to MSH (FinSH) shell */
MSH_CMD_EXPORT_ALIAS(heater_cmd, heater, Control the heater state);