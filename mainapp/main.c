#include <rtthread.h>
#include "board.h"
#include "includes.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_usart.h"

#include <rtdbg.h>

/* Function declarations */
int rtthread_startup(void);
extern int rt_hw_spi_init(void);  /* 添加SPI初始化声明 */
extern void ws2811_test_flow(void); /* 添加ws2811_test_flow声明 */
extern void system_clock_108m_config(void); /* 添加时钟配置声明 */
extern rt_err_t fan_init(void); /* 添加风扇初始化声明 */
extern void fan_test(void); /* 添加风扇测试声明 */
extern int fan_speed_measure_init(void); /* 添加风扇测速初始化声明 */
extern int temp_measure_init(void); /* 添加温度测量初始化声明 */
extern void heater_init(void);
 

int main(void)
{
    /* 打印系统时钟信息，用于调试 */
    rt_kprintf("\n===== System Clock Info =====\n");
    rt_kprintf("CK_SYS  = %d Hz\n", rcu_clock_freq_get(CK_SYS));
    rt_kprintf("CK_AHB  = %d Hz\n", rcu_clock_freq_get(CK_AHB));
    rt_kprintf("CK_APB1 = %d Hz\n", rcu_clock_freq_get(CK_APB1));
    rt_kprintf("CK_APB2 = %d Hz\n", rcu_clock_freq_get(CK_APB2));
    rt_kprintf("=============================\n\n");
    
    /* 1. 禁用 JTAG，保留 SWD（PA15 是 JTAG-TDI 引脚）*/
    rcu_periph_clock_enable(RCU_AF);
    gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);
    
    /* 2. 使能 GPIOA 时钟 */
    rcu_periph_clock_enable(RCU_GPIOA);
    
    /* 3. 配置 PA15 为推挽输出模式 */
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_15);
    
    /* 4. PA15 输出低电平 */
    gpio_bit_reset(GPIOA, GPIO_PIN_15);
    
    /* 5. 手动初始化SPI驱动 */
    rt_hw_spi_init();
    rt_kprintf("SPI driver initialized\n");

    // ws2811_test_flow();//test，以上电就亮绿灯，后续可以删除
    fan_init(); //风扇初始化
    fan_speed_measure_init(); //风扇测速初始化
    // temp_measure_init(); //温度测量初始化 - 已改为自动初始化
    
    while (1)
    {
        LOG_E("testing....\n");
        rt_thread_mdelay(5000);

    }
}
