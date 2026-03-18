/*
 * WS2811 RGB LED驱动 - 使用硬件SPI2的MOSI引脚
 * 通过SPI编码模拟WS2811时序协议
 * 
 * 硬件连接：
 *   SPI2_MOSI (PB5) --> WS2811 DIN
 *   GND --> WS2811 GND
 *   5V --> WS2811 VCC
 * 
 * 工作原理：
 *   - SPI速率: 2.5MHz (每位0.4μs)
 *   - 0码: 0b11000000 (1.2μs高 + 2μs低)
 *   - 1码: 0b11111000 (2μs高 + 1.2μs低)
 *   - 每个颜色字节需要8个SPI字节编码
 */

#include <rtthread.h>
#include "board.h"
#include "drv_spi.h"
#include "gd32f30x_spi.h"

#define soft_spi_colorlight  /* 定义此宏以使用软件SPI实现WS2811驱动，注释掉则使用硬件SPI2 */

#ifndef soft_spi_colorlight
/* WS2811配置 */
#define WS2811_LED_NUM          3           /* LED数量 */
#define WS2811_COLOR_BITS       24          /* 每个LED的位数(GRB) */
#define WS2811_RESET_TIME_US    80          /* Reset时间(μs) */

/* SPI编码定义 */
/* 
 * SPI频率约6.75MHz时(每位约0.148μs)：
 * 0码 (T0H ~300ns): 2位高电平 -> 2*0.148us = 0.296us (符合WS2811要求)
 * 1码 (T1H ~600ns): 4位高电平 -> 4*0.148us = 0.592us (符合WS2811要求)
 */
#define WS2811_CODE_0           0xC0        /* 0码: 0b11000000 (2 high, 6 low) */
#define WS2811_CODE_1           0xF0        /* 1码: 0b11110000 (4 high, 4 low) */
#define WS2811_RESET_BYTE       0x00        /* Reset字节 */


/* 颜色缓冲区 */
static rt_uint8_t ws2811_buffer[WS2811_LED_NUM * WS2811_COLOR_BITS];  /* 编码后的数据 */
static struct gd32_spi_bus *spi_bus = RT_NULL;

/* 灯效线程句柄 */
static rt_thread_t light_thread = RT_NULL;

/* 灯效枚举 */
enum {
    LIGHT_EFFECT_NONE = 0,
    LIGHT_EFFECT_RGB_LOOP,
    LIGHT_EFFECT_FLOW,
    LIGHT_EFFECT_BREATH
};

/**
 * @brief  WS2811初始化
 * @retval RT_EOK: 成功, -RT_ERROR: 失败
 */
rt_err_t ws2811_init(void)
{
    rt_device_t spi_dev;
    
    rt_kprintf("WS2811: Initializing...\n");
    
    /* 查找SPI2设备 */
    spi_dev = rt_device_find("spi2");
    if (spi_dev == RT_NULL)
    {
        rt_kprintf("WS2811: ERROR - SPI2 device not found!\n");
        return -RT_ERROR;
    }
    
    /* 打开SPI2设备 */
    if (rt_device_open(spi_dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("WS2811: Failed to open SPI2!\n");
        return -RT_ERROR;
    }
    
    /* 获取SPI总线结构 */
    spi_bus = (struct gd32_spi_bus *)spi_dev;
    
    /* 清空所有LED */
    rt_memset(ws2811_buffer, 0, sizeof(ws2811_buffer));
    
    rt_kprintf("WS2811: Initialized successfully on SPI2 (PB5)\n");
    return RT_EOK;
}

/**
 * @brief  将一个字节编码为8个SPI字节
 * @param  byte: 要编码的字节
 * @param  output: 输出缓冲区(至少8字节)
 */
static void ws2811_encode_byte(rt_uint8_t byte, rt_uint8_t *output)
{
    rt_uint8_t i;
    
    for (i = 0; i < 8; i++)
    {
        /* 从高位到低位编码 */
        if (byte & (0x80 >> i))
        {
            output[i] = WS2811_CODE_1;  /* 1码 */
        }
        else
        {
            output[i] = WS2811_CODE_0;  /* 0码 */
        }
    }
}

/**
 * @brief  设置单个LED的颜色
 * @param  index: LED索引(0-2)
 * @param  red: 红色分量(0-255)
 * @param  green: 绿色分量(0-255)
 * @param  blue: 蓝色分量(0-255)
 */
void ws2811_set_color(rt_uint8_t index, rt_uint8_t red, rt_uint8_t green, rt_uint8_t blue)
{
    rt_uint8_t *buf;
    
    if (index >= WS2811_LED_NUM)
    {
        return;
    }
    
    /* WS2811的颜色顺序是GRB */
    buf = &ws2811_buffer[index * WS2811_COLOR_BITS];
    
    /* 编码绿色 */
    ws2811_encode_byte(green, buf);
    buf += 8;
    
    /* 编码红色 */
    ws2811_encode_byte(red, buf);
    buf += 8;
    
    /* 编码蓝色 */
    ws2811_encode_byte(blue, buf);
}

/**
 * @brief  设置所有LED的颜色
 * @param  red: 红色分量(0-255)
 * @param  green: 绿色分量(0-255)
 * @param  blue: 蓝色分量(0-255)
 */
void ws2811_set_all(rt_uint8_t red, rt_uint8_t green, rt_uint8_t blue)
{
    rt_uint8_t i;
    
    for (i = 0; i < WS2811_LED_NUM; i++)
    {
        ws2811_set_color(i, red, green, blue);
    }
}

/**
 * @brief  更新LED显示
 * @note   将缓冲区数据发送到LED灯带
 */
void ws2811_update(void)
{
    // rt_uint8_t reset_buf[20];  /* Reset信号缓冲区 */
    
    if (spi_bus == RT_NULL)
    {
        rt_kprintf("WS2811: Not initialized!\n");
        return;
    }
    
    /* 发送颜色数据 */
    gd32_spi_send(spi_bus, ws2811_buffer, sizeof(ws2811_buffer));
    
    // /* 发送Reset信号(>50μs低电平) */
    // rt_memset(reset_buf, WS2811_RESET_BYTE, sizeof(reset_buf));
    // gd32_spi_send(spi_bus, reset_buf, sizeof(reset_buf));
    // rt_thread_mdelay(2);//帧间隔
}

/**
 * @brief  关闭所有LED
 */
void ws2811_clear(void)
{
    ws2811_set_all(0, 0, 0);
    ws2811_update();
}

/**
 * @brief  停止当前灯效线程
 */
void ws2811_stop_effect(void)
{
    if (light_thread != RT_NULL)
    {
        rt_thread_delete(light_thread);
        light_thread = RT_NULL;
    }
    
    ws2811_clear();
}

/* ==================== 测试函数 ==================== */

/**
 * @brief  RGB循环灯效线程入口
 * @param  parameter: 线程参数(未使用)
 */
static void ws2811_rgb_entry(void *parameter)
{
    while (1)
    {
        ws2811_set_all(255, 0, 0);  /* RED */
        ws2811_update();
        rt_thread_mdelay(1000);
        
        ws2811_set_all(0, 255, 0);  /* GREEN */
        ws2811_update();
        rt_thread_mdelay(1000);
        
        ws2811_set_all(0, 0, 255);  /* BLUE */
        ws2811_update();
        rt_thread_mdelay(1000);
    }
}

/**
 * @brief  流水灯灯效线程入口
 * @param  parameter: 线程参数(未使用)
 */
static void ws2811_flow_entry(void *parameter)
{
    while (1)
    {
        /* LED0 红色 */
        ws2811_set_color(0, 255, 0, 0);
        ws2811_set_color(1, 0, 0, 0);
        ws2811_set_color(2, 0, 0, 0);
        ws2811_update();
        rt_thread_mdelay(300);
        
        // /* LED1 绿色 */
        // ws2811_set_color(0, 0, 0, 0);
        // ws2811_set_color(1, 0, 255, 0);
        // ws2811_set_color(2, 0, 0, 0);
        // ws2811_update();
        // rt_thread_mdelay(300);
        
        // /* LED2 蓝色 */
        // ws2811_set_color(0, 0, 0, 0);
        // ws2811_set_color(1, 0, 0, 0);
        // ws2811_set_color(2, 0, 0, 255);
        // ws2811_update();
        // rt_thread_mdelay(300);
    }
}

/**
 * @brief  呼吸灯灯效线程入口
 * @param  parameter: 线程参数(未使用)
 */
static void ws2811_breath_entry(void *parameter)
{
    rt_uint16_t brightness;
    
    while (1)
    {
        /* 渐亮 */
        for (brightness = 0; brightness <= 255; brightness += 5)
        {
            ws2811_set_all(brightness, brightness, brightness);
            ws2811_update();
            rt_thread_mdelay(20);
        }
        
        /* 渐暗 */
        for (brightness = 255; brightness > 0; brightness -= 5)
        {
            ws2811_set_all(brightness, brightness, brightness);
            ws2811_update();
            rt_thread_mdelay(20);
        }
    }
}

/**
 * @brief  测试函数：依次点亮红绿蓝 (循环运行)
 */
void ws2811_test_rgb(void)
{
    /* 检查是否已初始化 */
    if (spi_bus == RT_NULL)
    {
        if (ws2811_init() != RT_EOK)
        {
            return;
        }
    }
    
    ws2811_stop_effect();  /* 停止旧线程 */
    
    light_thread = rt_thread_create("led_rgb",
                             ws2811_rgb_entry, RT_NULL,
                             1024, 25, 10);
                             
    if (light_thread != RT_NULL)
    {
        rt_thread_startup(light_thread);
        rt_kprintf("WS2811: RGB loop started in background.\n");
    }
}

/**
 * @brief  测试函数：流水灯效果 (循环运行)
 */
void ws2811_test_flow(void)
{
    /* 检查是否已初始化 */
    if (spi_bus == RT_NULL)
    {
        if (ws2811_init() != RT_EOK)
        {
            return;
        }
    }
    
    ws2811_stop_effect();  /* 停止旧线程 */
    
    light_thread = rt_thread_create("led_flow",
                             ws2811_flow_entry, RT_NULL,
                             1024, 25, 10);
                             
    if (light_thread != RT_NULL)
    {
        rt_thread_startup(light_thread);
        rt_kprintf("WS2811: Flow effect started in background.\n");
    }
}

/**
 * @brief  测试函数：呼吸灯效果 (循环运行)
 */
void ws2811_test_breath(void)
{
    /* 检查是否已初始化 */
    if (spi_bus == RT_NULL)
    {
        if (ws2811_init() != RT_EOK)
        {
            return;
        }
    }
    
    ws2811_stop_effect();  /* 停止旧线程 */
    
    light_thread = rt_thread_create("led_breath",
                             ws2811_breath_entry, RT_NULL,
                             1024, 25, 10);
                             
    if (light_thread != RT_NULL)
    {
        rt_thread_startup(light_thread);
        rt_kprintf("WS2811: Breathing effect started in background.\n");
    }
}

/* MSH命令导出 */
#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT(ws2811_init, Initialize WS2811 LED driver);
MSH_CMD_EXPORT(ws2811_clear, Turn off all LEDs);
MSH_CMD_EXPORT(ws2811_stop_effect, Stop current lighting effect);
MSH_CMD_EXPORT(ws2811_test_rgb, Start RGB loop (background));
MSH_CMD_EXPORT(ws2811_test_flow, Start flow effect (background));
MSH_CMD_EXPORT(ws2811_test_breath, Start breathing effect (background));
#endif

#else

/* ==================== 软件SPI驱动WS2811实现 ==================== */

/* 软件SPI引脚定义 - 使用PB5作为数据引脚 */
#define WS2811_GPIO_PORT        GPIOB
#define WS2811_GPIO_PIN         GPIO_PIN_5
#define WS2811_GPIO_CLK         RCU_GPIOB

/* WS2811时序参数 (单位: 纳秒) */
#define WS2811_T0H_NS           300     /* 0码高电平时间: 300ns ±150ns */
#define WS2811_T0L_NS           900     /* 0码低电平时间: 900ns ±150ns */
#define WS2811_T1H_NS           600     /* 1码高电平时间: 600ns ±150ns */
#define WS2811_T1L_NS           600     /* 1码低电平时间: 600ns ±150ns */
#define WS2811_RESET_US         300      /* Reset时间: >50μs */

/* LED配置 */
#define WS2811_LED_NUM          3       /* LED数量 */

/* GPIO操作宏 */
#define WS2811_PIN_HIGH()       GPIO_BOP(WS2811_GPIO_PORT) = WS2811_GPIO_PIN
#define WS2811_PIN_LOW()        GPIO_BC(WS2811_GPIO_PORT) = WS2811_GPIO_PIN

/* 颜色缓冲区 */
static rt_uint8_t ws2811_color_buffer[WS2811_LED_NUM * 3];  /* GRB格式 */

/* 灯效线程句柄 */
static rt_thread_t light_thread = RT_NULL;

/**
 * @brief  微秒级延时 (精确延时,使用DWT)
 * @param  us: 延时时间(微秒)
 * @note   使用DWT(Data Watchpoint and Trace)单元实现精确延时
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
 *         WS2811标准: T0H=350ns±150ns (200-500ns有效范围)
 *         目标350ns → 38个NOP (保守值,易识别为0码)
 */
#define DELAY_T0H() do { \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
} while(0)

/**
 * @brief  精确内联延时宏 - T0L (800ns低电平)
 * @note   WS2811标准: T0L=800ns±150ns (650-950ns有效范围)
 *         目标800ns → 86个NOP (满足标准要求)
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

#define DELAY_T1H() do { \
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

#define DELAY_T1L() do { \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
    __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); __NOP(); \
} while(0)

/**
 * @brief  发送一个位到WS2811
 * @param  bit: 要发送的位 (0或1)
 * @note   使用内联宏实现精确时序
 */
static void ws2811_send_bit(rt_uint8_t bit)
{
    if (bit)
    {
        /* 发送1码: 600ns高 + 600ns低 */
        WS2811_PIN_HIGH();
        DELAY_T1H();
        WS2811_PIN_LOW();
        DELAY_T1L();
    }
    else
    {
        /* 发送0码: 300ns高 + 900ns低 */
        WS2811_PIN_HIGH();
        DELAY_T0H();
        WS2811_PIN_LOW();
        DELAY_T0L();
    }
}

/**
 * @brief  发送一个字节到WS2811
 * @param  byte: 要发送的字节
 * @note   高位在前
 */
static void ws2811_send_byte(rt_uint8_t byte)
{
    rt_uint8_t i;
    
    for (i = 0; i < 8; i++)
    {
        ws2811_send_bit((byte & 0x80) ? 1 : 0);
        byte <<= 1;
    }
}

/**
 * @brief  软件SPI WS2811初始化
 * @retval RT_EOK: 成功
 */
rt_err_t ws2811_init(void)
{
    rt_kprintf("WS2811: Initializing (Software SPI)...\n");
    
    /* 使能GPIO时钟 */
    rcu_periph_clock_enable(WS2811_GPIO_CLK);
    
    /* 配置GPIO为推挽输出,最高速度模式以获得最快的上升/下降沿 */
    gpio_init(WS2811_GPIO_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_MAX, WS2811_GPIO_PIN);
    
    /* 初始化为低电平 */
    WS2811_PIN_LOW();
    
    /* 清空颜色缓冲区 */
    rt_memset(ws2811_color_buffer, 0, sizeof(ws2811_color_buffer));
    
    rt_kprintf("WS2811: Initialized successfully on PB5 (Software SPI, MAX Speed)\n");
    return RT_EOK;
}

/**
 * @brief  设置单个LED的颜色
 * @param  index: LED索引(0-2)
 * @param  red: 红色分量(0-255)
 * @param  green: 绿色分量(0-255)
 * @param  blue: 蓝色分量(0-255)
 */
void ws2811_set_color(rt_uint8_t index, rt_uint8_t red, rt_uint8_t green, rt_uint8_t blue)
{
    if (index >= WS2811_LED_NUM)
    {
        return;
    }
    
    /* 根据实测，您的LED是RGB顺序(非标准GRB) */
    ws2811_color_buffer[index * 3 + 0] = red;    /* R */
    ws2811_color_buffer[index * 3 + 1] = green;  /* G */
    ws2811_color_buffer[index * 3 + 2] = blue;   /* B */
}

/**
 * @brief  设置所有LED的颜色
 * @param  red: 红色分量(0-255)
 * @param  green: 绿色分量(0-255)
 * @param  blue: 蓝色分量(0-255)
 */
void ws2811_set_all(rt_uint8_t red, rt_uint8_t green, rt_uint8_t blue)
{
    rt_uint8_t i;
    
    for (i = 0; i < WS2811_LED_NUM; i++)
    {
        ws2811_set_color(i, red, green, blue);
    }
}

/**
 * @brief  更新LED显示
 * @note   将缓冲区数据发送到LED灯带
 */
void ws2811_update(void)
{
    rt_uint16_t i;
    rt_base_t level;
    
    /* 关闭中断,保证时序准确性 */
    level = rt_hw_interrupt_disable();
    
    /* 发送所有LED的颜色数据 */
    for (i = 0; i < sizeof(ws2811_color_buffer); i++)
    {
        ws2811_send_byte(ws2811_color_buffer[i]);
    }
    
    /* Reset信号 (>280μs低电平) */
    WS2811_PIN_LOW();
    delay_us(300);  /* 300μs复位脉冲,足够让LED锁存数据 */
    
    /* 恢复中断 */
    rt_hw_interrupt_enable(level);
}

/**
 * @brief  关闭所有LED
 */
void ws2811_clear(void)
{
    ws2811_set_all(0, 0, 0);
    ws2811_update();
}

/**
 * @brief  强制复位WS2811 - 解除LED锁死状态
 * @note   发送超长复位脉冲清除LED内部状态机
 */
void ws2811_hard_reset(void)
{
    rt_kprintf("WS2811: Performing hard reset...\n");
    
    /* 发送1ms超长复位脉冲 */
    WS2811_PIN_LOW();
    delay_us(1000);
    
    /* 清空缓冲区 */
    rt_memset(ws2811_color_buffer, 0, sizeof(ws2811_color_buffer));
    
    /* 发送全0数据 */
    rt_base_t level = rt_hw_interrupt_disable();
    for (rt_uint16_t i = 0; i < sizeof(ws2811_color_buffer); i++)
    {
        ws2811_send_byte(0x00);
    }
    WS2811_PIN_LOW();
    delay_us(500);
    rt_hw_interrupt_enable(level);
    
    rt_kprintf("WS2811: Hard reset completed.\n");
}

/**
 * @brief  停止当前灯效线程
 */
void ws2811_stop_effect(void)
{
    if (light_thread != RT_NULL)
    {
        rt_thread_delete(light_thread);
        light_thread = RT_NULL;
    }
    
    ws2811_clear();
}

/* ==================== 灯效函数 ==================== */

/**
 * @brief  RGB循环灯效线程入口
 * @param  parameter: 线程参数(未使用)
 */
static void ws2811_rgb_entry(void *parameter)
{
    while (1)
    {
        ws2811_set_all(255, 0, 0);  /* RED */
        ws2811_update();
        rt_thread_mdelay(1000);
        
        ws2811_set_all(0, 255, 0);  /* GREEN */
        ws2811_update();
        rt_thread_mdelay(1000);
        
        ws2811_set_all(0, 0, 255);  /* BLUE */
        ws2811_update();
        rt_thread_mdelay(1000);
    }
}

/**
 * @brief  流水灯灯效线程入口
 * @param  parameter: 线程参数(未使用)
 */
static void ws2811_flow_entry(void *parameter)
{
    while (1)
    {
        /* LED0 红色 */
        ws2811_set_color(0, 255, 0, 0);
        ws2811_set_color(1, 0, 255, 0);
        ws2811_set_color(2, 0, 0, 255);
        ws2811_update();
        rt_thread_mdelay(300);
        
        // /* LED1 绿色 */
        // ws2811_set_color(0, 0, 0, 0);
        // ws2811_set_color(1, 0, 255, 0);
        // ws2811_set_color(2, 0, 0, 0);
        // ws2811_update();
        // rt_thread_mdelay(300);
        
        // /* LED2 蓝色 */
        // ws2811_set_color(0, 0, 0, 0);
        // ws2811_set_color(1, 0, 0, 0);
        // ws2811_set_color(2, 0, 0, 255);
        // ws2811_update();
        // rt_thread_mdelay(300);
    }
}

static void set_led_color(rt_uint8_t index, rt_uint8_t r, rt_uint8_t g, rt_uint8_t b)
{
    /* 初始化 */
    ws2811_init();
    
    ws2811_stop_effect();  /* 停止旧线程 */

    ws2811_set_color(index, r, g, b);
    ws2811_update();
}

/* ==================== 协议灯光控制 ==================== */

/* 呼吸模式参数 */
static rt_uint8_t g_breath_color_r = 0;
static rt_uint8_t g_breath_color_g = 0;
static rt_uint8_t g_breath_color_b = 0;
static rt_uint16_t g_breath_period = 0;  /* 呼吸周期(ms) */

/**
 * @brief  协议呼吸灯线程入口
 * @param  parameter: 线程参数(未使用)
 */
static void ws2811_protocol_breath_entry(void *parameter)
{
    rt_uint16_t brightness;
    rt_uint16_t step = 5;  /* 亮度步进值 */
    
    while (1)
    {
        /* 亮度从0到255 */
        for (brightness = 0; brightness <= 255; brightness += step)
        {
            ws2811_set_all(
                (g_breath_color_r * brightness) / 255,
                (g_breath_color_g * brightness) / 255,
                (g_breath_color_b * brightness) / 255
            );
            ws2811_update();
            rt_thread_mdelay(g_breath_period / (255 / step * 2));  /* 平滑过渡 */
        }
        
        /* 亮度从255到0 */
        for (brightness = 255; brightness > 0; brightness -= step)
        {
            ws2811_set_all(
                (g_breath_color_r * brightness) / 255,
                (g_breath_color_g * brightness) / 255,
                (g_breath_color_b * brightness) / 255
            );
            ws2811_update();
            rt_thread_mdelay(g_breath_period / (255 / step * 2));
        }
    }
}

/**
 * @brief  协议灯光控制函数
 * @param  color: 颜色值 (0x00=白色, 0x01=红色, 0x02=黄色, 0x03=蓝色, 0x04=绿色)
 * @param  breath_mode: 呼吸模式 (0x00=关闭, 0x01=慢速2s, 0x02=中速1s, 0x03=快速0.5s)
 * @retval RT_EOK: 成功, -RT_ERROR: 失败
 */
rt_err_t ws2811_protocol_control(rt_uint8_t color, rt_uint8_t breath_mode)
{
    rt_uint8_t r = 0, g = 0, b = 0;
    
    rt_kprintf("\n[WS2811 Protocol Control]\n");
    rt_kprintf("  Input: color=0x%02X, breath_mode=0x%02X\n", color, breath_mode);
    
    /* 初始化 */
    ws2811_init();
    
    /* 停止旧的灯效线程 */
    ws2811_stop_effect();
    
    /* 根据颜色值设置RGB */
    switch (color)
    {
        case 0x00:  /* 白色 */
            r = 255; g = 255; b = 255;
            rt_kprintf("  Color: White\n");
            break;
            
        case 0x01:  /* 红色 */
            r = 255; g = 0; b = 0;
            rt_kprintf("  Color: Red\n");
            break;
            
        case 0x02:  /* 黄色 */
            r = 255; g = 255; b = 0;
            rt_kprintf("  Color: Yellow\n");
            break;
            
        case 0x03:  /* 蓝色 */
            r = 0; g = 0; b = 255;
            rt_kprintf("  Color: Blue\n");
            break;
            
        case 0x04:  /* 绿色 */
            r = 0; g = 255; b = 0;
            rt_kprintf("  Color: Green\n");
            break;
            
        default:
            rt_kprintf("WS2811: Unknown color code 0x%02X\n", color);
            return -RT_ERROR;
    }
    
    rt_kprintf("  RGB values: R=%d, G=%d, B=%d\n", r, g, b);
    
    /* 根据呼吸模式设置 */
    if (breath_mode == 0x00)
    {
        /* 常亮模式 */
        rt_kprintf("  Mode: Steady (always on)\n");
        rt_kprintf("  Calling ws2811_set_all(%d, %d, %d)...\n", r, g, b);
        ws2811_set_all(r, g, b);
        rt_kprintf("  Calling ws2811_update()...\n");
        ws2811_update();
        rt_kprintf("  ✓ Color set successfully - RGB(%d,%d,%d), steady mode\n", r, g, b);
    }
    else
    {
        /* 呼吸模式 */
        g_breath_color_r = r;
        g_breath_color_g = g;
        g_breath_color_b = b;
        
        /* 设置呼吸周期 */
        switch (breath_mode)
        {
            case 0x01:  /* 慢速呼吸 - 2秒周期 */
                g_breath_period = 2000;
                break;
                
            case 0x02:  /* 中速呼吸 - 1秒周期 */
                g_breath_period = 1000;
                break;
                
            case 0x03:  /* 快速呼吸 - 0.5秒周期 */
                g_breath_period = 500;
                break;
                
            default:
                rt_kprintf("WS2811: Unknown breath mode 0x%02X\n", breath_mode);
                return -RT_ERROR;
        }
        
        /* 创建呼吸灯线程 */
        light_thread = rt_thread_create("led_proto_breath",
                                 ws2811_protocol_breath_entry,
                                 RT_NULL,
                                 512,
                                 10,
                                 10);
        
        if (light_thread != RT_NULL)
        {
            rt_thread_startup(light_thread);
            rt_kprintf("WS2811: Set color RGB(%d,%d,%d), breath mode=%d, period=%dms\n", 
                      r, g, b, breath_mode, g_breath_period);
        }
        else
        {
            rt_kprintf("WS2811: Failed to create breath thread\n");
            return -RT_ERROR;
        }
    }
    
    return RT_EOK;
}


/**
 * @brief  呼吸灯灯效线程入口
 * @param  parameter: 线程参数(未使用)
 */
static void ws2811_breath_entry(void *parameter)
{
    rt_uint16_t brightness;
    
    while (1)
    {
        /* 渐亮 */
        for (brightness = 0; brightness <= 255; brightness += 5)
        {
            ws2811_set_all(brightness, brightness, brightness);
            ws2811_update();
            rt_thread_mdelay(20);
        }
        
        /* 渐暗 */
        for (brightness = 255; brightness > 0; brightness -= 5)
        {
            ws2811_set_all(brightness, brightness, brightness);
            ws2811_update();
            rt_thread_mdelay(20);
        }
    }
}

/**
 * @brief  彩虹灯效线程入口
 * @param  parameter: 线程参数(未使用)
 */
static void ws2811_rainbow_entry(void *parameter)
{
    rt_uint16_t hue = 0;
    rt_uint8_t i;
    rt_uint8_t r, g, b;
    
    while (1)
    {
        for (i = 0; i < WS2811_LED_NUM; i++)
        {
            rt_uint16_t pixel_hue = (hue + (i * 65536 / WS2811_LED_NUM)) % 65536;
            
            /* HSV转RGB (简化版) */
            if (pixel_hue < 21845)  /* 红->绿 */
            {
                r = 255 - (pixel_hue * 255 / 21845);
                g = pixel_hue * 255 / 21845;
                b = 0;
            }
            else if (pixel_hue < 43690)  /* 绿->蓝 */
            {
                r = 0;
                g = 255 - ((pixel_hue - 21845) * 255 / 21845);
                b = (pixel_hue - 21845) * 255 / 21845;
            }
            else  /* 蓝->红 */
            {
                r = (pixel_hue - 43690) * 255 / 21846;
                g = 0;
                b = 255 - ((pixel_hue - 43690) * 255 / 21846);
            }
            
            ws2811_set_color(i, r, g, b);
        }
        
        ws2811_update();
        hue += 256;
        if (hue >= 65536) hue = 0;
        rt_thread_mdelay(10);
    }
}

/* ==================== 测试函数 ==================== */

/**
 * @brief  测试函数：依次点亮红绿蓝 (循环运行)
 */
void ws2811_test_rgb(void)
{
    /* 初始化 */
    ws2811_init();
    
    ws2811_stop_effect();  /* 停止旧线程 */
    
    light_thread = rt_thread_create("led_rgb",
                             ws2811_rgb_entry, RT_NULL,
                             1024, 25, 10);
                             
    if (light_thread != RT_NULL)
    {
        rt_thread_startup(light_thread);
        rt_kprintf("WS2811: RGB loop started in background.\n");
    }
}

/**
 * @brief  测试函数：流水灯效果 (循环运行)
 */
void ws2811_test_flow(void)
{
    /* 初始化 */
    ws2811_init();
    
    ws2811_stop_effect();  /* 停止旧线程 */
    
    light_thread = rt_thread_create("led_flow",
                             ws2811_flow_entry, RT_NULL,
                             1024, 25, 10);
                             
    if (light_thread != RT_NULL)
    {
        rt_thread_startup(light_thread);
        rt_kprintf("WS2811: Flow effect started in background.\n");
    }
}

/**
 * @brief  测试函数：呼吸灯效果 (循环运行)
 */
void ws2811_test_breath(void)
{
    /* 初始化 */
    ws2811_init();
    
    ws2811_stop_effect();  /* 停止旧线程 */
    
    light_thread = rt_thread_create("led_breath",
                             ws2811_breath_entry, RT_NULL,
                             1024, 25, 10);
                             
    if (light_thread != RT_NULL)
    {
        rt_thread_startup(light_thread);
        rt_kprintf("WS2811: Breathing effect started in background.\n");
    }
}

/**
 * @brief  测试函数：彩虹灯效果 (循环运行)
 */
void ws2811_test_rainbow(void)
{
    /* 初始化 */
    ws2811_init();
    
    ws2811_stop_effect();  /* 停止旧线程 */
    
    light_thread = rt_thread_create("led_rainbow",
                             ws2811_rainbow_entry, RT_NULL,
                             1024, 25, 10);
                             
    if (light_thread != RT_NULL)
    {
        rt_thread_startup(light_thread);
        rt_kprintf("WS2811: Rainbow effect started in background.\n");
    }
}

/**
 * @brief  测试函数：单色测试
 */
void ws2811_test_color(int r, int g, int b)
{
    /* 初始化 */
    ws2811_init();
    
    ws2811_stop_effect();  /* 停止旧线程 */
    
    ws2811_set_all(r, g, b);
    ws2811_update();
    rt_kprintf("WS2811: Set all LEDs to RGB(%d,%d,%d)\n", r, g, b);
}

/**
 * @brief  测试函数：单个LED测试
 * @param  index: LED索引 (0-2)
 * @param  r: 红色
 * @param  g: 绿色  
 * @param  b: 蓝色
 */
void ws2811_test_single(int index, int r, int g, int b)
{
    /* 初始化 */
    ws2811_init();
    
    ws2811_stop_effect();  /* 停止旧线程 */
    
    /* 打印调试信息 */
    rt_kprintf("WS2811: Parameters - index=%d, r=%d, g=%d, b=%d\n", index, r, g, b);
    
    /* 关闭所有LED */
    ws2811_set_all(0, 0, 0);
    
    /* 只点亮指定的LED */
    if (index >= 0 && index < (int)WS2811_LED_NUM)
    {
        ws2811_set_color((rt_uint8_t)index, (rt_uint8_t)r, (rt_uint8_t)g, (rt_uint8_t)b);
        ws2811_update();
        rt_kprintf("WS2811: Set LED[%d] to RGB(%d,%d,%d)\n", index, r, g, b);
    }
    else
    {
        rt_kprintf("WS2811: Invalid LED index %d (valid range: 0-%d)\n", index, WS2811_LED_NUM-1);
    }
}

/**
 * @brief  测试函数：单个LED测试 (LED0)
 */
static int ws2811_led0(int argc, char **argv)
{
    int r, g, b;
    
    if (argc != 4) {
        rt_kprintf("Usage: ws2811_led0 <r> <g> <b>\n");
        return -1;
    }
    
    r = atoi(argv[1]);
    g = atoi(argv[2]);
    b = atoi(argv[3]);
    
    ws2811_init();
    ws2811_stop_effect();
    ws2811_set_all(0, 0, 0);
    ws2811_set_color(0, (rt_uint8_t)r, (rt_uint8_t)g, (rt_uint8_t)b);
    ws2811_update();
    rt_kprintf("WS2811: LED0 RGB(%d,%d,%d)\n", r, g, b);
    return 0;
}
MSH_CMD_EXPORT(ws2811_led0, Test LED0: ws2811_led0 <r> <g> <b>);

/**
 * @brief  打印颜色缓冲区数据(调试用)
 */
static int ws2811_dump(int argc, char **argv)
{
    rt_uint8_t i;
    
    rt_kprintf("WS2811 Buffer (9 bytes for 3 LEDs in GRB format):\n");
    for (i = 0; i < sizeof(ws2811_color_buffer); i++)
    {
        rt_kprintf("Byte[%d]=0x%02X ", i, ws2811_color_buffer[i]);
        if ((i + 1) % 3 == 0) {
            rt_kprintf("  (LED%d: G=%d R=%d B=%d)\n", 
                i/3, 
                ws2811_color_buffer[(i/3)*3], 
                ws2811_color_buffer[(i/3)*3+1], 
                ws2811_color_buffer[(i/3)*3+2]);
        }
    }
    return 0;
}
MSH_CMD_EXPORT(ws2811_dump, Dump color buffer data);

/**
 * @brief  测试函数：单个LED测试 (LED1)
 */
static int ws2811_led1(int argc, char **argv)
{
    int r, g, b;
    rt_uint8_t i;
    
    if (argc != 4) {
        rt_kprintf("Usage: ws2811_led1 <r> <g> <b>\n");
        return -1;
    }
    
    r = atoi(argv[1]);
    g = atoi(argv[2]);
    b = atoi(argv[3]);
    
    ws2811_init();
    ws2811_stop_effect();
    ws2811_set_all(0, 0, 0);
    ws2811_set_color(1, (rt_uint8_t)r, (rt_uint8_t)g, (rt_uint8_t)b);
    
    /* 打印缓冲区内容 */
    rt_kprintf("Buffer content:\n");
    for (i = 0; i < sizeof(ws2811_color_buffer); i++) {
        rt_kprintf("0x%02X ", ws2811_color_buffer[i]);
    }
    rt_kprintf("\n");
    
    ws2811_update();
    rt_kprintf("WS2811: LED1 RGB(%d,%d,%d)\n", r, g, b);
    return 0;
}
MSH_CMD_EXPORT(ws2811_led1, Test LED1: ws2811_led1 <r> <g> <b>);

/**
 * @brief  测试函数：单个LED测试 (LED2)
 */
static int ws2811_led2(int argc, char **argv)
{
    int r, g, b;
    rt_uint8_t i;
    
    if (argc != 4) {
        rt_kprintf("Usage: ws2811_led2 <r> <g> <b>\n");
        return -1;
    }
    
    r = atoi(argv[1]);
    g = atoi(argv[2]);
    b = atoi(argv[3]);
    
    ws2811_init();
    ws2811_stop_effect();
    ws2811_set_all(0, 0, 0);
    ws2811_set_color(2, (rt_uint8_t)r, (rt_uint8_t)g, (rt_uint8_t)b);
    
    /* 打印缓冲区内容 */
    rt_kprintf("Buffer content:\n");
    for (i = 0; i < sizeof(ws2811_color_buffer); i++) {
        rt_kprintf("0x%02X ", ws2811_color_buffer[i]);
    }
    rt_kprintf("\n");
    
    ws2811_update();
    rt_kprintf("WS2811: LED2 RGB(%d,%d,%d)\n", r, g, b);
    return 0;
}
MSH_CMD_EXPORT(ws2811_led2, Test LED2: ws2811_led2 <r> <g> <b>);

static int color_light_all(int argc, char **argv)
{
    int r, g, b;
    
    if (argc != 4) {
        rt_kprintf("Usage: ws2811_test_all <r> <g> <b>\n");
        return -1;
    }

    ws2811_init();
    ws2811_stop_effect();
    
    r = atoi(argv[1]);
    g = atoi(argv[2]);
    b = atoi(argv[3]);
    
    ws2811_test_color(r, g, b);
    return 0;
}
MSH_CMD_EXPORT(color_light_all, Test all LEDs: color_light_all <r> <g> <b>);

/**
 * @brief  测试函数：协议灯光控制命令
 * @usage  ws2811_proto_test <color> <breath_mode>
 *         color: 0x00=绿色, 0x01=黄色, 0x02=红色
 *         breath_mode: 0x00=常亮, 0x01=慢呼吸(2s), 0x02=中速呼吸(1s), 0x03=快呼吸(0.5s)
 */
static int ws2811_proto_test(int argc, char **argv)
{
    int color, breath_mode;
    rt_err_t result;
    
    if (argc != 3) {
        rt_kprintf("Usage: ws2811_proto_test <color> <breath_mode>\n");
        rt_kprintf("  color: 0=Green, 1=Yellow, 2=Red\n");
        rt_kprintf("  breath_mode: 0=Steady, 1=Slow(2s), 2=Medium(1s), 3=Fast(0.5s)\n");
        return -1;
    }
    
    color = atoi(argv[1]);
    breath_mode = atoi(argv[2]);
    
    rt_kprintf("==> Calling ws2811_protocol_control(color=%d, breath_mode=%d)\n", color, breath_mode);
    
    extern rt_err_t ws2811_protocol_control(rt_uint8_t color, rt_uint8_t breath_mode);
    result = ws2811_protocol_control((rt_uint8_t)color, (rt_uint8_t)breath_mode);
    
    if (result == RT_EOK) {
        rt_kprintf("==> Command executed successfully!\n");
    } else {
        rt_kprintf("==> Command failed with error code: %d\n", result);
    }
    
    return 0;
}
MSH_CMD_EXPORT(ws2811_proto_test, Protocol light test: ws2811_proto_test <color> <breath>);

/**
 * @brief  测试函数：直接设置红色(调试用)
 */
static int test_red(int argc, char **argv)
{
    rt_kprintf("\n[Direct Red Test]\n");
    rt_kprintf("Step 1: Init...\n");
    ws2811_init();
    
    rt_kprintf("Step 2: Stop effects...\n");
    ws2811_stop_effect();
    
    rt_kprintf("Step 3: Set all to RED (255, 0, 0)...\n");
    ws2811_set_all(255, 0, 0);
    
    rt_kprintf("Step 4: Update display...\n");
    ws2811_update();
    
    rt_kprintf("Step 5: Dump buffer...\n");
    for (rt_uint8_t i = 0; i < sizeof(ws2811_color_buffer); i++) {
        rt_kprintf("0x%02X ", ws2811_color_buffer[i]);
    }
    rt_kprintf("\n✓ Red test completed!\n");
    return 0;
}
MSH_CMD_EXPORT(test_red, Direct red color test for debugging);

/* MSH命令导出 */
#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT(ws2811_init, Initialize WS2811 LED driver (Software SPI));
MSH_CMD_EXPORT(ws2811_clear, Turn off all LEDs);
MSH_CMD_EXPORT(ws2811_hard_reset, Hard reset WS2811 to clear locked state);
MSH_CMD_EXPORT(ws2811_stop_effect, Stop current lighting effect);
MSH_CMD_EXPORT(ws2811_test_rgb, Start RGB loop (background));
MSH_CMD_EXPORT(ws2811_test_flow, Start flow effect (background));
MSH_CMD_EXPORT(ws2811_test_breath, Start breathing effect (background));
MSH_CMD_EXPORT(ws2811_test_rainbow, Start rainbow effect (background));
#endif

#endif
