//PB9引脚控制加热，高电平使能
#include <rtthread.h>
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"

int heater_init(void)
{
    /* 1. 使能GPIOB时钟 */
    rcu_periph_clock_enable(RCU_GPIOB);
    
    /* 2. 配置PB9为推挽输出 */
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);
    
    /* 3. 默认关闭加热器（PB9低电平） */
    gpio_bit_reset(GPIOB, GPIO_PIN_9);

    return 0;
}
/* 自动初始化 */
INIT_APP_EXPORT(heater_init);

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