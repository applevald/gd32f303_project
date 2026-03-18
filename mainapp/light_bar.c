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
#define WS2812_LED_NUM          72      /* LED数量 */

/* GPIO操作宏 */
#define WS2812_PIN_HIGH()       GPIO_BOP(WS2812_GPIO_PORT) = WS2812_GPIO_PIN
#define WS2812_PIN_LOW()        GPIO_BC(WS2812_GPIO_PORT) = WS2812_GPIO_PIN

/* 颜色缓冲区 (GRB格式) */
static rt_uint8_t ws2812_color_buffer[WS2812_LED_NUM * 3];

/* 呼吸效果控制结构 */
typedef struct {
    rt_uint8_t enable;          /* 是否启用呼吸效果 */
    rt_uint8_t mode;            /* 呼吸模式 0x01=慢速 0x02=中速 0x03=快速 */
    rt_uint8_t progress;        /* 进度值 */
    rt_uint8_t r, g, b;         /* RGB颜色 */
} breath_ctrl_t;

static breath_ctrl_t g_breath_ctrl = {0};
static rt_thread_t breath_thread = RT_NULL;

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
    ws2812_color_buffer[index * 3 + 0] = red;  /* G */
    ws2812_color_buffer[index * 3 + 1] = green;    /* R */
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

/**
 * @brief  根据进度值点亮LED（进度条效果）
 * @param  progress: 进度值(0-100)
 * @param  red: 红色分量(0-255)
 * @param  green: 绿色分量(0-255)
 * @param  blue: 蓝色分量(0-255)
 * @note   根据进度点亮对应数量的LED
 */
void ws2812_bar_set_progress(rt_uint8_t progress, rt_uint8_t red, rt_uint8_t green, rt_uint8_t blue)
{
    rt_uint8_t i;
    rt_uint8_t leds_to_light;
    
    /* 限制进度值范围 */
    if (progress > 100)
    {
        progress = 100;
    }
    
    /* 计算需要点亮的LED数量 */
    leds_to_light = (progress * WS2812_LED_NUM) / 100;
    
    /* 清空所有LED */
    for (i = 0; i < WS2812_LED_NUM; i++)
    {
        ws2812_bar_set_color(i, 0, 0, 0);
    }
    
    /* 点亮对应数量的LED */
    for (i = 0; i < leds_to_light; i++)
    {
        ws2812_bar_set_color(i, red, green, blue);
    }
}

/**
 * @brief  呼吸效果线程
 * @param  parameter: 线程参数
 */
static void breath_thread_entry(void *parameter)
{
    rt_uint16_t delay_ms;
    
    while (1)
    {
        if (g_breath_ctrl.enable)
        {
            /* 根据呼吸模式设置延时 */
            switch (g_breath_ctrl.mode)
            {
                case 0x01:  /* 慢速：2秒周期 */
                    delay_ms = 1000;
                    break;
                case 0x02:  /* 中速：1秒周期 */
                    delay_ms = 500;
                    break;
                case 0x03:  /* 快速：0.5秒周期 */
                    delay_ms = 250;
                    break;
                default:
                    delay_ms = 1000;
                    break;
            }
            
            /* 亮 */
            ws2812_bar_set_progress(g_breath_ctrl.progress, 
                                   g_breath_ctrl.r, 
                                   g_breath_ctrl.g, 
                                   g_breath_ctrl.b);
            ws2812_bar_update();
            rt_thread_mdelay(delay_ms);
            
            /* 灭 */
            ws2812_bar_clear();
            rt_thread_mdelay(delay_ms);
        }
        else
        {
            /* 未启用呼吸效果，休眠 */
            rt_thread_mdelay(100);
        }
    }
}

/**
 * @brief  启动呼吸效果
 * @param  progress: 进度值
 * @param  r, g, b: RGB颜色
 * @param  mode: 呼吸模式
 */
static void start_breath_effect(rt_uint8_t progress, rt_uint8_t r, rt_uint8_t g, rt_uint8_t b, rt_uint8_t mode)
{
    /* 初始化灯带（如果还未初始化）*/
    static rt_bool_t initialized = RT_FALSE;
    if (!initialized)
    {
        ws2812_bar_init();
        initialized = RT_TRUE;
    }
    
    /* 如果线程已存在，先停止旧的呼吸效果 */
    if (breath_thread != RT_NULL)
    {
        g_breath_ctrl.enable = 0;
        rt_thread_mdelay(50);  /* 等待线程完成当前循环 */
    }
    
    /* 设置新的呼吸参数 */
    g_breath_ctrl.progress = progress;
    g_breath_ctrl.r = r;
    g_breath_ctrl.g = g;
    g_breath_ctrl.b = b;
    g_breath_ctrl.mode = mode;
    g_breath_ctrl.enable = 1;
    
    /* 创建呼吸线程（如果还没创建）*/
    if (breath_thread == RT_NULL)
    {
        breath_thread = rt_thread_create("breath",
                                        breath_thread_entry,
                                        RT_NULL,
                                        1024,
                                        15,
                                        10);
        if (breath_thread != RT_NULL)
        {
            rt_thread_startup(breath_thread);
            rt_kprintf("[LightBar] Breath thread created and started\n");
        }
        else
        {
            rt_kprintf("[LightBar] ERROR: Failed to create breath thread\n");
        }
    }
    else
    {
        rt_kprintf("[LightBar] Breath thread already exists, parameters updated\n");
    }
}

/**
 * @brief  停止呼吸效果
 */
static void stop_breath_effect(void)
{
    /* 初始化灯带（如果还未初始化）*/
    static rt_bool_t initialized = RT_FALSE;
    if (!initialized)
    {
        ws2812_bar_init();
        initialized = RT_TRUE;
    }
    
    /* 停止呼吸效果 */
    g_breath_ctrl.enable = 0;
    rt_kprintf("[LightBar] Breath effect stopped\n");
}

    /* 颜色映射函数 */
    static void map_color(rt_uint8_t index, rt_uint8_t *r, rt_uint8_t *g, rt_uint8_t *b)
    {
        switch (index)
        {
            case 0x00:  /* 全白 */
                *r = 255; *g = 255; *b = 255;
                break;
            case 0x01:  /* 红色 */
                *r = 255; *g = 0; *b = 0;
                break;
            case 0x02:  /* 黄色 */
                *r = 255; *g = 255; *b = 0;
                break;
            case 0x03:  /* 蓝色 */
                *r = 0; *g = 0; *b = 255;
                break;
            case 0x04:  /* 绿色 */
                *r = 0; *g = 255; *b = 0;
                break;
            default:    /* 其他值按白色处理 */
                *r = 255; *g = 255; *b = 255;
                break;
        }
    }
// color_param：颜色参数说明：
// 颜色参数为 8位数据（1字节），格式如下：
// ● 高 4位：进度条框底色,即为所有没显示进度的灯颜色
// ● 低 4位：进度显色，显示进度颜色
static void ws2812_parse_color(rt_uint8_t color_param, rt_uint8_t *bg_r, rt_uint8_t *bg_g, rt_uint8_t *bg_b,
                               rt_uint8_t *fg_r, rt_uint8_t *fg_g, rt_uint8_t *fg_b)
{
    rt_uint8_t bg_color_index = (color_param >> 4) & 0x0F;  /* 高4位：背景色 */
    rt_uint8_t fg_color_index = color_param & 0x0F;         /* 低4位：前景色 */
    
    
    /* 解析背景色（高4位）*/
    map_color(bg_color_index, bg_r, bg_g, bg_b);
    
    /* 解析前景色（低4位）*/
    map_color(fg_color_index, fg_r, fg_g, fg_b);
}

/**
 * @brief  协议控制函数 - 处理灯条控制命令
 * @param  progress: 进度值(0-100)
 * @param  color_param: 颜色参数(8位)
 *         高4位：背景色，低4位：前景色
 * @param  breath_mode: 呼吸模式(8位)
 * @retval RT_EOK: 成功
 * @note   根据协议0xA5命令格式处理进度灯显示
 */
rt_err_t ws2812_bar_protocol_control(rt_uint8_t progress, rt_uint8_t color_param, rt_uint8_t breath_mode)
{
    rt_uint8_t bg_r, bg_g, bg_b;  /* 背景色 */
    rt_uint8_t fg_r, fg_g, fg_b;  /* 前景色 */
    
    rt_kprintf("[LightBar] Protocol control: progress=%d, color=0x%02X, breath=0x%02X\n", 
               progress, color_param, breath_mode);
    
    /* 解析颜色参数 */
    ws2812_parse_color(color_param, &bg_r, &bg_g, &bg_b, &fg_r, &fg_g, &fg_b);
    
    rt_kprintf("[LightBar] BG Color RGB(%d,%d,%d), FG Color RGB(%d,%d,%d)\n",
               bg_r, bg_g, bg_b, fg_r, fg_g, fg_b);
    
    /* 根据呼吸模式调整显示 */
    if (breath_mode == 0x00)
    {
        /* 关闭呼吸效果，正常显示 */
        stop_breath_effect();
        
        /* 显示进度条：背景色 + 前景色 */
        rt_uint8_t i;
        rt_uint8_t leds_to_light = (progress * WS2812_LED_NUM) / 100;
        
        /* 点亮前景色LED */
        for (i = 0; i < leds_to_light; i++)
        {
            ws2812_bar_set_color(i, fg_r, fg_g, fg_b);
        }
        
        /* 点亮背景色LED */
        for (i = leds_to_light; i < WS2812_LED_NUM; i++)
        {
            ws2812_bar_set_color(i, bg_r, bg_g, bg_b);
        }
        
        ws2812_bar_update();
    }
    else
    {
        /* 启动持续呼吸效果（使用前景色）*/
        start_breath_effect(progress, fg_r, fg_g, fg_b, breath_mode);
    }
    
    rt_kprintf("[LightBar] Display updated - %d%% progress\n", progress);
    
    return RT_EOK;
}

/**
 * @brief  测试函数：设置进度条显示
 * @param  progress: 进度值(0-100)
 * @usage  ws2812_bar_progress 50  (50%进度，绿色)
 */
static int ws2812_bar_progress(int argc, char **argv)
{
    int progress;
    
    if (argc != 2) {
        rt_kprintf("Usage: ws2812_bar_progress <progress>\n");
        rt_kprintf("Example: ws2812_bar_progress 50  (50%% progress)\n");
        rt_kprintf("         ws2812_bar_progress 0   (0%% - all off)\n");
        rt_kprintf("         ws2812_bar_progress 100 (100%% - all on)\n");
        return -1;
    }
    
    progress = atoi(argv[1]);
    
    if (progress < 0 || progress > 100) {
        rt_kprintf("Error: Progress must be 0-100\n");
        return -1;
    }
    
    /* 初始化 */
    ws2812_bar_init();
    
    /* 设置进度显示（默认绿色）*/
    ws2812_bar_set_progress((rt_uint8_t)progress, 0, 255, 0);
    ws2812_bar_update();
    
    rt_kprintf("WS2812 Bar: Set progress to %d%% (%d LEDs lit)\n", 
               progress, (progress * WS2812_LED_NUM) / 100);
    
    return 0;
}
MSH_CMD_EXPORT(ws2812_bar_progress, Set LED bar progress: ws2812_bar_progress <0-100>);

/* MSH命令导出 */
#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT(ws2812_bar_init, Initialize WS2812 LED bar on PC12);
MSH_CMD_EXPORT(ws2812_bar_clear, Turn off all LEDs);
#endif