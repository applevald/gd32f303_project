//PB9引脚控制加热，高电平使能
#include <rtthread.h>
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"

static uint32_t goal_temp = 0; // 目标温度，超过该温度时关闭加热器
static rt_thread_t heater_thread = RT_NULL;


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
    goal_temp = temp;
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