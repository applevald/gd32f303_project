/*
 * WS2812 RGB LED灯带驱动 - 使用PC12引脚软件模拟SPI
 * 驱动48个WS2812灯珠
 * 
 * 硬件连接：
 *   PC12 --> WS2812 DIN
 *   GND --> WS2812 GND
 *   5V --> WS2812 VCC
 * 
 * 工作原理：
 *   - 软件模拟时序协议
 *   - 0码: ~350ns高 + ~800ns低
 *   - 1码: ~700ns高 + ~600ns低
 *   - Reset: >50μs低电平
 * 
 * 注意事项：
 *   - GD32输出3.3V，WS2812需要5V供电，建议加电平转换电路
 *   - 或将WS2812供电降至3.3V测试（亮度会降低）
 */

#include <rtthread.h>
#include "board.h"
#include "gd32f30x.h"

/* WS2812引脚定义 - 使用PC12作为数据引脚 */
#define WS2812_GPIO_PORT        GPIOC
#define WS2812_GPIO_PIN         GPIO_PIN_12
#define WS2812_GPIO_CLK         RCU_GPIOC

/* LED配置 */
#define WS2812_LED_NUM          48      /* LED数量 */

/* GPIO操作宏 */
#define WS2812_PIN_HIGH()       GPIO_BOP(WS2812_GPIO_PORT) = WS2812_GPIO_PIN
#define WS2812_PIN_LOW()        GPIO_BC(WS2812_GPIO_PORT) = WS2812_GPIO_PIN

/* 颜色缓冲区 (GRB格式) */
static rt_uint8_t ws2812_color_buffer[WS2812_LED_NUM * 3];

/**
 * @brief  微秒级延时
 * @param  us: 延时时间(微秒)
 * @note   使用SysTick实现精确延时
 */
static void delay_us(rt_uint32_t us)
{
    rt_uint32_t ticks = us * (SystemCoreClock / 1000000);
    rt_uint32_t told = SysTick->VAL;
    rt_uint32_t tnow, tcnt = 0;
    
    while (tcnt < ticks)
    {
        tnow = SysTick->VAL;
        if (tnow != told)
        {
            if (tnow < told)
            {
                tcnt += told - tnow;
            }
            else
            {
                tcnt += SysTick->LOAD - tnow + told;
            }
            told = tnow;
        }
    }
}

/**
 * @brief  精确内联延时宏 - T0H (350ns高电平)
 * @note   108MHz = 9.26ns/cycle
 *         WS2812标准: T0H=350ns±150ns
 *         目标350ns → 38个NOP
 */
#define DELAY_T0H() do { \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
} while(0)

/**
 * @brief  精确内联延时宏 - T0L (800ns低电平)
 * @note   WS2812标准: T0L=800ns±150ns
 *         目标800ns → 86个NOP
 */
#define DELAY_T0L() do { \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
} while(0)

/**
 * @brief  精确内联延时宏 - T1H (700ns高电平)
 * @note   WS2812标准: T1H=700ns±150ns
 *         目标700ns → 76个NOP
 */
#define DELAY_T1H() do { \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
} while(0)

/**
 * @brief  精确内联延时宏 - T1L (600ns低电平)
 * @note   WS2812标准: T1L=600ns±150ns
 *         目标600ns → 65个NOP
 */
#define DELAY_T1L() do { \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
} while(0)

/**
 * @brief  发送一个位到WS2812
 * @param  bit: 要发送的位 (0或1)
 */
static void ws2812_send_bit(rt_uint8_t bit)
{
    if (bit)
    {
        /* 发送1码: 700ns高 + 600ns低 */
        WS2812_PIN_HIGH();
        DELAY_T1H();
        WS2812_PIN_LOW();
        DELAY_T1L();
    }
    else
    {
        /* 发送0码: 350ns高 + 800ns低 */
        WS2812_PIN_HIGH();
        DELAY_T0H();
        WS2812_PIN_LOW();
        DELAY_T0L();
    }
}

/**
 * @brief  发送一个字节到WS2812
 * @param  byte: 要发送的字节
 * @note   高位在前
 */
static void ws2812_send_byte(rt_uint8_t byte)
{
    rt_uint8_t i;
    
    for (i = 0; i < 8; i++)
    {
        ws2812_send_bit((byte & 0x80) ? 1 : 0);
        byte <<= 1;
    }
}

/**
 * @brief  WS2812灯带初始化
 * @retval RT_EOK: 成功
 */
rt_err_t ws2812_bar_init(void)
{
    rt_kprintf("WS2812 Bar: Initializing on PC12...\n");
    
    /* 使能GPIO时钟 */
    rcu_periph_clock_enable(WS2812_GPIO_CLK);
    
    /* 配置GPIO为推挽输出，最高速度模式 */
    gpio_init(WS2812_GPIO_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_MAX, WS2812_GPIO_PIN);
    
    /* 初始化为低电平 */
    WS2812_PIN_LOW();
    
    /* 清空颜色缓冲区 */
    rt_memset(ws2812_color_buffer, 0, sizeof(ws2812_color_buffer));
    
    rt_kprintf("WS2812 Bar: Initialized successfully (%d LEDs)\n", WS2812_LED_NUM);
    rt_kprintf("WS2812 Bar: Note - 3.3V output may need level shifter for 5V WS2812\n");
    
    return RT_EOK;
}

/**
 * @brief  设置单个LED的颜色
 * @param  index: LED索引(0-47)
 * @param  red: 红色分量(0-255)
 * @param  green: 绿色分量(0-255)
 * @param  blue: 蓝色分量(0-255)
 */
void ws2812_bar_set_color(rt_uint8_t index, rt_uint8_t red, rt_uint8_t green, rt_uint8_t blue)
{
    if (index >= WS2812_LED_NUM)
    {
        return;
    }
    
    /* WS2812的颜色顺序是GRB */
    ws2812_color_buffer[index * 3 + 0] = green;  /* G */
    ws2812_color_buffer[index * 3 + 1] = red;    /* R */
    ws2812_color_buffer[index * 3 + 2] = blue;   /* B */
}

/**
 * @brief  设置所有LED的颜色
 * @param  red: 红色分量(0-255)
 * @param  green: 绿色分量(0-255)
 * @param  blue: 蓝色分量(0-255)
 */
void ws2812_bar_set_all(rt_uint8_t red, rt_uint8_t green, rt_uint8_t blue)
{
    rt_uint8_t i;
    
    for (i = 0; i < WS2812_LED_NUM; i++)
    {
        ws2812_bar_set_color(i, red, green, blue);
    }
}

/**
 * @brief  更新LED显示
 * @note   将缓冲区数据发送到LED灯带
 */
void ws2812_bar_update(void)
{
    rt_uint16_t i;
    rt_base_t level;
    
    /* 关闭中断，保证时序准确性 */
    level = rt_hw_interrupt_disable();
    
    /* 发送所有LED的颜色数据 */
    for (i = 0; i < sizeof(ws2812_color_buffer); i++)
    {
        ws2812_send_byte(ws2812_color_buffer[i]);
    }
    
    /* Reset信号 (>50μs低电平) */
    WS2812_PIN_LOW();
    
    /* 恢复中断 */
    rt_hw_interrupt_enable(level);
    
    /* 延时以完成复位 */
    delay_us(300);
}

/**
 * @brief  关闭所有LED
 */
void ws2812_bar_clear(void)
{
    ws2812_bar_set_all(0, 0, 0);
    ws2812_bar_update();
}

/**
 * @brief  测试函数：设置所有LED为指定颜色
 * @param  r: 红色分量
 * @param  g: 绿色分量
 * @param  b: 蓝色分量
 * @usage  ws2812_bar_test 255 0 0  (全红色)
 */
static int ws2812_bar_test(int argc, char **argv)
{
    int r, g, b;
    
    if (argc != 4) {
        rt_kprintf("Usage: ws2812_bar_test <r> <g> <b>\n");
        rt_kprintf("Example: ws2812_bar_test 255 0 0  (all red)\n");
        rt_kprintf("         ws2812_bar_test 0 255 0  (all green)\n");
        rt_kprintf("         ws2812_bar_test 0 0 255  (all blue)\n");
        rt_kprintf("         ws2812_bar_test 255 255 255  (all white)\n");
        return -1;
    }
    
    r = atoi(argv[1]);
    g = atoi(argv[2]);
    b = atoi(argv[3]);
    
    /* 初始化 */
    ws2812_bar_init();
    
    /* 设置所有LED为指定颜色 */
    ws2812_bar_set_all((rt_uint8_t)r, (rt_uint8_t)g, (rt_uint8_t)b);
    ws2812_bar_update();
    
    rt_kprintf("WS2812 Bar: Set all %d LEDs to RGB(%d, %d, %d)\n", 
               WS2812_LED_NUM, r, g, b);
    
    return 0;
}
MSH_CMD_EXPORT(ws2812_bar_test, Set all LEDs color: ws2812_bar_test <r> <g> <b>);

/* MSH命令导出 */
#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT(ws2812_bar_init, Initialize WS2812 LED bar on PC12);
MSH_CMD_EXPORT(ws2812_bar_clear, Turn off all LEDs);
#endif