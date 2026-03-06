#ifndef __FAN_TEST_H__
#define __FAN_TEST_H__

#include <rtthread.h>

/* 风扇ID定义 */
typedef enum {
    FAN1 = 0,   // PA3  - TIMER1_CH3 - 电气风扇
    FAN3,       // PA6  - TIMER2_CH0 - 排风扇
    FAN7,       // PB1  - TIMER2_CH3 - 腔体加热
    FAN11,      // PB14 - TIMER0_CH1 - 鼓风扇
    FAN_MAX
} fan_id_t;

/* 函数声明 */
rt_err_t fan_init(void);
void fan_set_speed(fan_id_t fan, uint8_t percent);
void fan_test(void);

#endif /* __FAN_TEST_H__ */
