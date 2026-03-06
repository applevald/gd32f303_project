/* 风扇PWM控制
 * FAN1:  PA3  - TIMER1_CH3(默认映射) - 电气风扇
 * FAN3:  PA6  - TIMER2_CH0(默认映射) - 排风扇
 * FAN7:  PB1  - TIMER2_CH3(默认映射) - 腔体加热
 * FAN11: PB14 - TIMER0_CH1N(默认映射) - 鼓风扇
 * 
 * 注意: GD32F303RCT6(HD)没有TIMER11, PB14对应的硬件PWM通道是TIMER0_CH1N (通道1互补输出)
 */

#include <rtthread.h>
#include "board.h"
#include "gd32f30x.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_timer.h"
#include <stdlib.h>

/* PWM频率: 25kHz, 周期 = 108MHz / 25kHz = 4320 */
#define PWM_PERIOD      4320
#define PWM_FREQ_KHZ    25

/* 风扇枚举 */
typedef enum {
    FAN1 = 0,   // PA3  - TIMER1_CH3
    FAN3,       // PA6  - TIMER2_CH0
    FAN7,       // PB1  - TIMER2_CH3
    FAN11,      // PB14 - TIMER0_CH1N
    FAN_MAX
} fan_id_t;

/* 配置单个定时器PWM 
 * use_n_output: 是否使用互补输出(N通道), 主要用于FAN11(PB14/TIMER0_CH1N)
 */
static void timer_pwm_config(uint32_t timer_periph, uint16_t channel, uint16_t pulse, uint8_t use_n_output)
{
    timer_oc_parameter_struct timer_ocintpara;
    timer_parameter_struct timer_initpara;

    /* 只在定时器未使能时才初始化(避免重复初始化TIMER2) */
    if((TIMER_CTL0(timer_periph) & TIMER_CTL0_CEN) == 0) {
        timer_deinit(timer_periph);
        
        /* 定时器基本配置 */
        timer_initpara.prescaler         = 0;  // 不分频, 108MHz
        timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
        timer_initpara.counterdirection  = TIMER_COUNTER_UP;
        timer_initpara.period            = PWM_PERIOD - 1;
        timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
        timer_initpara.repetitioncounter = 0;
        timer_init(timer_periph, &timer_initpara);
    }

    /* PWM输出配置 */
    /* 普通通道输出使能 */
    timer_ocintpara.outputstate  = (use_n_output ? TIMER_CCX_DISABLE : TIMER_CCX_ENABLE);
    /* 互补通道输出使能 */
    timer_ocintpara.outputnstate = (use_n_output ? TIMER_CCXN_ENABLE : TIMER_CCXN_DISABLE);
    
    timer_ocintpara.ocpolarity   = TIMER_OC_POLARITY_HIGH;
    timer_ocintpara.ocnpolarity  = TIMER_OCN_POLARITY_HIGH;
    timer_ocintpara.ocidlestate  = TIMER_OC_IDLE_STATE_LOW;
    timer_ocintpara.ocnidlestate = TIMER_OCN_IDLE_STATE_LOW;

    timer_channel_output_config(timer_periph, channel, &timer_ocintpara);
    timer_channel_output_pulse_value_config(timer_periph, channel, pulse);
    timer_channel_output_mode_config(timer_periph, channel, TIMER_OC_MODE_PWM0);
    timer_channel_output_shadow_config(timer_periph, channel, TIMER_OC_SHADOW_DISABLE);
    
    /* 高级定时器(TIMER0/TIMER7)必须使能主输出才能工作 */
    if(timer_periph == TIMER0 || timer_periph == TIMER7) {
        timer_primary_output_config(timer_periph, ENABLE);
    }
    
    /* 使能定时器 */
    timer_enable(timer_periph);
}

/* 初始化风扇PWM */
rt_err_t fan_init(void)
{
    /* 使能时钟 */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_TIMER1);
    rcu_periph_clock_enable(RCU_TIMER2);
    rcu_periph_clock_enable(RCU_TIMER0); /* FAN11 - TIMER0 (TIM1) */
    rcu_periph_clock_enable(RCU_AF);

    /* 配置引脚重映射
     * FAN3(PA6)-TIMER2_CH0: 默认映射
     * FAN7(PB1)-TIMER2_CH3: 默认映射
     * FAN1(PA3)-TIMER1_CH3: 默认映射
     * FAN11(PB14)-TIMER0_CH1N: 默认映射 (注意是CH1N互补通道)
     */
    
    /* 确保重映射被禁用 */
    // gpio_pin_remap_config(GPIO_TIMER1_PARTIAL_REMAP1, DISABLE);
    // gpio_pin_remap_config(GPIO_TIMER2_PARTIAL_REMAP, DISABLE);

    /* 配置GPIO */
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_3);  // FAN1  - TIMER1_CH3
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_6);  // FAN3  - TIMER2_CH0
    gpio_init(GPIOB, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_1);  // FAN7  - TIMER2_CH3
    gpio_init(GPIOB, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_14); // FAN11 - TIMER0_CH1N

    /* 初始化硬件PWM, 默认100%占空比(全速) */
    timer_pwm_config(TIMER2, TIMER_CH_0, PWM_PERIOD, 0);  // FAN3 (普通)
    timer_pwm_config(TIMER2, TIMER_CH_3, PWM_PERIOD, 0);  // FAN7 (普通)
    timer_pwm_config(TIMER1, TIMER_CH_3, PWM_PERIOD, 0);  // FAN1 (普通)
    
    /* FAN11: TIMER0 channel 1, 使用互补输出(N) */
    timer_pwm_config(TIMER0, TIMER_CH_1, PWM_PERIOD, 1);  // FAN11 (互补输出)

    rt_kprintf("[FAN] PWM initialized: %d kHz (NO REMAP)\n", PWM_FREQ_KHZ);
    rt_kprintf("[FAN] PA3(T1_CH3), PA6(T2_CH0), PB1(T2_CH3), PB14(T0_CH1N)\n");
    
    return RT_EOK;
}

/* 设置风扇速度: 0-100% */
void fan_set_speed(fan_id_t fan, uint8_t percent)
{
    uint16_t pulse;
    
    if (percent > 100) percent = 100;
    pulse = (PWM_PERIOD * percent) / 100;

    switch(fan) {
        case FAN1:
            timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_3, pulse);
            rt_kprintf("[FAN1] Speed=%d%%, Pulse=%d/%d\n", percent, pulse, PWM_PERIOD);
            break;
        case FAN3:
            timer_channel_output_pulse_value_config(TIMER2, TIMER_CH_0, pulse);
            rt_kprintf("[FAN3] Speed=%d%%, Pulse=%d/%d\n", percent, pulse, PWM_PERIOD);
            break;
        case FAN7:
            timer_channel_output_pulse_value_config(TIMER2, TIMER_CH_3, pulse);
            rt_kprintf("[FAN7] Speed=%d%%, Pulse=%d/%d\n", percent, pulse, PWM_PERIOD);
            break;
        case FAN11:
            timer_channel_output_pulse_value_config(TIMER0, TIMER_CH_1, pulse);
            rt_kprintf("[FAN11] Speed=%d%%, Pulse=%d/%d\n", percent, pulse, PWM_PERIOD);
            break;
        default:
            break;
    }
}

/* 测试GPIO直接输出 - 用于验证硬件连接 */
void fan_gpio_test(int argc, char **argv)
{
    if (argc != 3) {
        rt_kprintf("Usage: fan_gpio_test <fan_id> <0|1>\n");
        rt_kprintf("  Test GPIO direct output (0=LOW, 1=HIGH)\n");
        return;
    }
    
    int fan_num = atoi(argv[1]);
    int level = atoi(argv[2]);
    
    /* 临时切换为普通GPIO输出模式测试 */
    switch(fan_num) {
        case 1:
            gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_3);
            gpio_bit_write(GPIOA, GPIO_PIN_3, level ? SET : RESET);
            rt_kprintf("[FAN1 GPIO TEST] PA3 = %s\n", level ? "HIGH" : "LOW");
            break;
        case 3:
            gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_6);
            gpio_bit_write(GPIOA, GPIO_PIN_6, level ? SET : RESET);
            rt_kprintf("[FAN3 GPIO TEST] PA6 = %s\n", level ? "HIGH" : "LOW");
            break;
        case 7:
            gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_1);
            gpio_bit_write(GPIOB, GPIO_PIN_1, level ? SET : RESET);
            rt_kprintf("[FAN7 GPIO TEST] PB1 = %s\n", level ? "HIGH" : "LOW");
            break;
        case 11:
            gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_14);
            gpio_bit_write(GPIOB, GPIO_PIN_14, level ? SET : RESET);
            rt_kprintf("[FAN11 GPIO TEST] PB14 = %s\n", level ? "HIGH" : "LOW");
            break;
        default:
            rt_kprintf("[ERROR] Invalid fan_id: %d\n", fan_num);
            return;
    }
    
    rt_kprintf(">>> Please check if fan runs. Use 'fan_init' to restore PWM mode.\n");
}
MSH_CMD_EXPORT(fan_gpio_test, Test fan GPIO direct output);

/* 设置风扇速度命令: fan_test <fan_id> <speed>
 * 例如: fan_test 1 50   设置FAN1为50%速度
 *       fan_test 11 100 设置FAN11为100%速度
 */
void fan_test(int argc, char **argv)
{
    int fan_num, speed;
    fan_id_t fan;
    
    if (argc != 3) {
        rt_kprintf("\nUsage: fan_test <fan_id> <speed>\n");
        rt_kprintf("  fan_id: 1, 3, 7, 11\n");
        rt_kprintf("  speed:  0-100 (%%)\n");
        rt_kprintf("\nExamples:\n");
        rt_kprintf("  fan_test 1 50    - Set FAN1 to 50%%\n");
        rt_kprintf("  fan_test 3 75    - Set FAN3 to 75%%\n");
        rt_kprintf("  fan_test 7 100   - Set FAN7 to 100%%\n");
        rt_kprintf("  fan_test 11 0    - Set FAN11 to 0%% (stop)\n");
        return;
    }
    
    fan_num = atoi(argv[1]);
    speed = atoi(argv[2]);
    
    /* 转换风扇编号 */
    switch(fan_num) {
        case 1:  fan = FAN1;  break;
        case 3:  fan = FAN3;  break;
        case 7:  fan = FAN7;  break;
        case 11: fan = FAN11; break;
        default:
            rt_kprintf("[ERROR] Invalid fan_id: %d (must be 1, 3, 7, or 11)\n", fan_num);
            return;
    }
    
    /* 检查速度范围 */
    if (speed < 0 || speed > 100) {
        rt_kprintf("[ERROR] Invalid speed: %d (must be 0-100)\n", speed);
        return;
    }
    
    /* 设置风扇速度 */
    fan_set_speed(fan, speed);
}
MSH_CMD_EXPORT(fan_test, Set fan speed: fan_test <fan_id> <speed>);