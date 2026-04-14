/*
 * @file temp_measure.c
 * @brief 温度测量模块 - 通过ADC采集NTC温度传感器数据（查表法）
 * 
 * 硬件配置：
 *   - ADC引脚：PC0 (ADC012_IN10)
 *   - ADC外设：ADC0
 *   - 采样通道：ADC_CHANNEL_10
 * 
 * 温度计算：
 *   - 使用NTC100K热敏电阻 (B=3950)
 *   - 查表法获取温度，每5度一个小区间
 *   - 线性插值获取精确温度值
 * 
 * 参考：T450控制板软硬件接口文档V0.2
 *   - 电阻阻值：100KΩ
 *   - 阻值精度：±1%
 *   - 电阻B值：3950±1%
 *   - 测温范围：-50℃ - 125℃
 */

#include <rtthread.h>
#include "gd32f30x.h"
#include "gd32f30x_adc.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"

/* ADC配置参数 */
#define ADC_TEMP_CHANNEL        ADC_CHANNEL_10  /* PC0对应ADC0通道10 */
#define ADC_PERIPH              ADC0            /* 使用ADC0 */
#define ADC_SAMPLE_COUNT        10              /* 多次采样取平均 */
#define ADC_VREF                3.3f            /* 参考电压(V) */
#define ADC_RESOLUTION          4096            /* 12位ADC分辨率 */

/* 调试开关 - 设为1启用详细调试输出，0关闭以提高性能 */
#define TEMP_MEASURE_DEBUG_VERBOSE  1

/*
 * NTC传感器参数
 * 电路接法：VCC(3.3V) -- R_series -- ADC点 -- NTC -- GND
 * ADC测量的是NTC两端的电压
 * 
 * 根据实测校准（室温25℃，ADC ≈ 3906）：
 * V_adc = 3906 / 4096 * 3.3 ≈ 3.147V
 * R_ntc(25℃) = 100kΩ（标称值）
 * 反推 Rs = R_ntc * (Vcc - V_adc) / V_adc
 *         = 100000 * (3.3 - 3.147) / 3.147 ≈ 4865Ω
 * 实际硬件串联电阻为 4.7kΩ（标准值）
 * 
 * 计算：
 * V_adc = VCC * R_ntc / (R_ntc + R_s)
 * R_ntc = R_s * V_adc / (VCC - V_adc)
 */
#define TEMP_SERIES_R           4700.0f         /* 串联电阻阻值(Ω) - 4.7kΩ (实测校准) */
#define TEMP_SENSOR_R25         100000.0f       /* 25℃时的电阻值(Ω) NTC100K */
#define TEMP_SENSOR_B           3950.0f         /* NTC B值 */

/* 查表法参数 */
#define TEMP_TABLE_MIN          (-50)           /* 最低温度 (℃) */
#define TEMP_TABLE_MAX          (125)           /* 最高温度 (℃) */
#define TEMP_TABLE_STEP         5               /* 温度步进 (℃) */
#define TEMP_TABLE_SIZE         ((TEMP_TABLE_MAX - TEMP_TABLE_MIN) / TEMP_TABLE_STEP + 1)

/* NTC状态检测阈值
 * 电路：VCC -- R_s -- ADC -- NTC -- GND
 * - NTC短路(0Ω): ADC直接接地 → ADC低(接近0)
 * - NTC开路(∞Ω): ADC通过R_s接VCC → ADC高(接近4095)
 */
#define NTC_SHORT_THRESHOLD     100             /* ADC值低于此值判定为短路 */
#define NTC_OPEN_THRESHOLD      4000            /* ADC值高于此值判定为开路 */

/* 静态变量 */
static float g_current_temp = 0.0f;             /* 当前温度值 (℃) */
static uint8_t g_ntc_status = 0;                /* NTC状态: 0-正常, 1-短路, 2-开路 */
static rt_mutex_t temp_mutex = RT_NULL;         /* 温度数据互斥锁 */
rt_mutex_t adc_mutex = RT_NULL;                 /* ADC 访问互斥锁 - 全局变量 */

/*
 * NTC100K B=3950 温度-电阻查找表
 * 温度范围：-50℃ 到 125℃，每5度一个点
 * 电阻值单位：欧姆(Ω)
 * 
 * 计算公式：R = R25 * exp(B * (1/T - 1/T25))
 * 其中：R25 = 100000Ω (25℃时的标称电阻), B = 3950, T25 = 298.15K
 * 
 * 注意：表中电阻值从低温到高温递减（电阻随温度升高而减小）
 *       25℃时电阻 = 100000Ω (NTC100K的定义)
 */
static const float ntc_resistance_table[TEMP_TABLE_SIZE] = {
    /* -50℃ ~ -30℃ : 低温区，电阻极大 */
    8586128.0f,      /* -50℃ */
    5825362.0f,      /* -45℃ */
    4018597.0f,      /* -40℃ */
    2815768.0f,      /* -35℃ */
    2002039.0f,      /* -30℃ */
    
    /* -25℃ ~ 0℃ : 冷区 */
    1443169.0f,      /* -25℃ */
    1053847.0f,      /* -20℃ */
    778981.0f,       /* -15℃ */
    582457.0f,       /* -10℃ */
    440260.0f,       /* -5℃ */
    336206.0f,       /* 0℃ */
    
    /* 5℃ ~ 25℃ : 常温区 */
    259246.0f,       /* 5℃ */
    201746.0f,       /* 10℃ */
    158371.0f,       /* 15℃ */
    125353.0f,       /* 20℃ */
    100000.0f,       /* 25℃ - NTC100K标称值 */
    
    /* 30℃ ~ 60℃ : 温热区 */
    80371.0f,        /* 30℃ */
    65055.0f,        /* 35℃ */
    53015.0f,        /* 40℃ */
    43481.0f,        /* 45℃ */
    35882.0f,        /* 50℃ */
    29784.0f,        /* 55℃ */
    24862.0f,        /* 60℃ */
    
    /* 65℃ ~ 95℃ : 热区 */
    20864.0f,        /* 65℃ */
    17598.0f,        /* 70℃ */
    14917.0f,        /* 75℃ */
    12703.0f,        /* 80℃ */
    10867.0f,        /* 85℃ */
    9336.0f,         /* 90℃ */
    8054.0f,         /* 95℃ */
    
    /* 100℃ ~ 125℃ : 高温区 */
    6975.0f,         /* 100℃ */
    6064.0f,         /* 105℃ */
    5291.0f,         /* 110℃ */
    4633.0f,         /* 115℃ */
    4071.0f,         /* 120℃ */
    3588.0f          /* 125℃ */
};

/**
 * @brief  初始化ADC温度采集
 */
static void adc_temp_init(void)
{
    /* 1. 使能时钟 */
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_ADC0);
    
    /* 2. 配置ADC时钟：PCLK2/6 = 108MHz/6 = 18MHz */
    rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV6);
    
    /* 3. 配置PC0为模拟输入 */
    gpio_init(GPIOC, GPIO_MODE_AIN, GPIO_OSPEED_50MHZ, GPIO_PIN_0);
    
    /* 4. 复位ADC */
    adc_deinit(ADC_PERIPH);
    
    /* 5. 配置ADC工作模式 */
    adc_mode_config(ADC_MODE_FREE);                     /* 独立模式 */
    adc_special_function_config(ADC_PERIPH, ADC_CONTINUOUS_MODE, DISABLE);  /* 单次转换 */
    adc_special_function_config(ADC_PERIPH, ADC_SCAN_MODE, DISABLE);        /* 非扫描模式 */
    adc_data_alignment_config(ADC_PERIPH, ADC_DATAALIGN_RIGHT);            /* 右对齐 */
    
    /* 6. 配置ADC通道 */
    adc_channel_length_config(ADC_PERIPH, ADC_REGULAR_CHANNEL, 1);         /* 1个转换通道 */
    adc_regular_channel_config(ADC_PERIPH, 0, ADC_TEMP_CHANNEL, ADC_SAMPLETIME_55POINT5);  /* 采样时间55.5周期 */
    
    /* 7. 配置外部触发 */
    adc_external_trigger_source_config(ADC_PERIPH, ADC_REGULAR_CHANNEL, ADC0_1_2_EXTTRIG_REGULAR_NONE);
    adc_external_trigger_config(ADC_PERIPH, ADC_REGULAR_CHANNEL, ENABLE);
    
    /* 8. 使能ADC */
    adc_enable(ADC_PERIPH);
    rt_thread_mdelay(1);  /* 稳定时间 */
    
    /* 9. ADC校准 */
    adc_calibration_enable(ADC_PERIPH);
    
    rt_kprintf("[TempMeasure] ADC initialized (PC0, Channel 10)\n");
    rt_kprintf("[TempMeasure] Lookup table: %d entries, range %dC to %dC\n", 
               TEMP_TABLE_SIZE, TEMP_TABLE_MIN, TEMP_TABLE_MAX);
}

/**
 * @brief  读取 ADC 原始值
 * @return ADC 转换结果 (0-4095)
 */
static uint16_t adc_read_raw(void)
{
    uint16_t adc_value;
    
    /* 获取 ADC 互斥锁 */
    if (adc_mutex != RT_NULL)
    {
        rt_mutex_take(adc_mutex, RT_WAITING_FOREVER);
    }
    
    /* 重新配置通道，防止被其他模块覆盖 */
    adc_regular_channel_config(ADC_PERIPH, 0, ADC_TEMP_CHANNEL, ADC_SAMPLETIME_55POINT5);
    
    /* 启动转换 */
    adc_software_trigger_enable(ADC_PERIPH, ADC_REGULAR_CHANNEL);
    
    /* 等待转换完成 */
    while (!adc_flag_get(ADC_PERIPH, ADC_FLAG_EOC));
    
    /* 读取转换结果 */
    adc_value = adc_regular_data_read(ADC_PERIPH);
    
    /* 释放 ADC 互斥锁 */
    if (adc_mutex != RT_NULL)
    {
        rt_mutex_release(adc_mutex);
    }
    
    return adc_value;
}

/**
 * @brief  多次采样并取平均值
 * @return 平均ADC值
 */
static uint16_t adc_read_average(void)
{
    uint32_t sum = 0;
    int i;
    
    for (i = 0; i < ADC_SAMPLE_COUNT; i++)
    {
        sum += adc_read_raw();
        rt_thread_mdelay(5);  /* 采样间隔 */
    }
    
    return (uint16_t)(sum / ADC_SAMPLE_COUNT);
}

/**
 * @brief  ADC值转换为NTC电阻值
 * @param  adc_value: ADC采样值
 * @return NTC电阻值(Ω)
 * @note   分压电路：VCC -- R_series -- ADC -- NTC -- GND
 *         ADC测量NTC两端电压
 *         V_adc = VCC * R_ntc / (R_ntc + R_s)
 *         R_ntc = R_s * V_adc / (VCC - V_adc)
 */
static float adc_to_resistance(uint16_t adc_value)
{
    float v_adc;    /* NTC两端电压 */
    
    /* ADC值转换为电压 */
    v_adc = (float)adc_value * ADC_VREF / ADC_RESOLUTION;
    
    /* 防止除零错误 */
    if (v_adc >= ADC_VREF - 0.01f)
    {
        return 100000000.0f;  /* 返回极大值表示NTC开路（ADC接VCC） */
    }
    if (v_adc <= 0.01f)
    {
        return 0.0f;  /* 返回0表示NTC短路（ADC接GND） */
    }
    
    /* 计算NTC电阻：R_ntc = R_s * V_adc / (VCC - V_adc) */
    return TEMP_SERIES_R * v_adc / (ADC_VREF - v_adc);
}

/**
 * @brief  通过查表法将NTC电阻转换为温度
 * @param  resistance: NTC电阻值(Ω)
 * @return 温度值(℃)
 * @note   使用线性插值在查找表区间内获取精确温度
 */
static float resistance_to_temperature_lookup(float resistance)
{
    int i;
    float temp_low, temp_high;
    float r_low, r_high;
    float ratio;
    
    /* 边界检查 */
    if (resistance >= ntc_resistance_table[0])
    {
        /* 电阻超过表中最小温度对应的值，返回最低温度 */
        return (float)TEMP_TABLE_MIN;
    }
    
    if (resistance <= ntc_resistance_table[TEMP_TABLE_SIZE - 1])
    {
        /* 电阻低于表中最高温度对应的值，返回最高温度 */
        return (float)TEMP_TABLE_MAX;
    }
    
    /* 在表中查找电阻所在的区间 */
    /* 注意：表中电阻值从低温到高温递减，所以要用反向比较 */
    for (i = 0; i < TEMP_TABLE_SIZE - 1; i++)
    {
        if (resistance <= ntc_resistance_table[i] && 
            resistance >= ntc_resistance_table[i + 1])
        {
            /* 找到区间，进行线性插值 */
            temp_low = (float)(TEMP_TABLE_MIN + i * TEMP_TABLE_STEP);
            temp_high = (float)(TEMP_TABLE_MIN + (i + 1) * TEMP_TABLE_STEP);
            
            r_low = ntc_resistance_table[i];
            r_high = ntc_resistance_table[i + 1];
            
            /* 线性插值：温度与电阻在小区间内近似线性关系 */
            /* 由于电阻随温度升高而降低，插值公式需要考虑这个特性 */
            /* ratio = (r_low - resistance) / (r_low - r_high) */
            /* temperature = temp_low + ratio * (temp_high - temp_low) */
            
            /* 但实际上电阻与温度是对数关系，在小区间内用线性插值会有误差 */
            /* 更准确的方法是对温度进行插值，基于电阻的对数关系 */
            
            /* 简化的线性插值（对于5度小区间足够精确） */
            ratio = (r_low - resistance) / (r_low - r_high);
            return temp_low + ratio * (temp_high - temp_low);
        }
    }
    
    /* 未找到（不应该到达这里） */
    return 0.0f;
}

/**
 * @brief  温度测量线程
 * @param  parameter: 线程参数
 */
static void temp_measure_thread_entry(void *parameter)
{
    uint16_t adc_value;
    float resistance;
    float temperature;
    
    while (1)
    {
        /* 1. 读取ADC平均值 */
        adc_value = adc_read_average();
        
        /* 2. 检测NTC状态
         * 电路：VCC -- Rs -- ADC -- NTC -- GND
         * - NTC短路：ADC接GND，ADC值低
         * - NTC开路：ADC通过Rs接VCC，ADC值高
         */
        if (adc_value < NTC_SHORT_THRESHOLD)
        {
            /* ADC值过低，判定为短路 */
            g_ntc_status = 1;
        }
        else if (adc_value > NTC_OPEN_THRESHOLD)
        {
            /* ADC值过高，判定为开路 */
            g_ntc_status = 2;
        }
        else
        {
            /* 正常范围 */
            g_ntc_status = 0;
        }
        
        /* 3. ADC值转换为NTC电阻值 */
        resistance = adc_to_resistance(adc_value);
        
        /* 4. 查表法获取温度 */
        temperature = resistance_to_temperature_lookup(resistance);
        
        /* 5. 保存温度值 */
        if (temp_mutex)
        {
            rt_mutex_take(temp_mutex, RT_WAITING_FOREVER);
            g_current_temp = temperature;
            rt_mutex_release(temp_mutex);
        }
        
#if TEMP_MEASURE_DEBUG_VERBOSE
        /* 6. 调试输出 (兼容不支持%f的情况) */
        {
            int res_int = (int)(resistance / 1000);  /* kΩ */
            int res_frac = (int)((resistance / 1000 - res_int) * 10);
            
            int temp_int = (int)temperature;
            int temp_frac = (int)((temperature - temp_int) * 100);
            if (temp_frac < 0) temp_frac = -temp_frac;
            
            rt_kprintf("[TempMeasure] ADC=%d, R=%d.%dkΩ, Temp=%d.%02dC, Status=%d\n", 
                       adc_value, res_int, res_frac, temp_int, temp_frac, g_ntc_status);
        }
#endif
        
        /* 7. 延时 */
        rt_thread_mdelay(1000);  /* 每秒采集一次 */
    }
}

/**
 * @brief  获取当前温度值
 * @param  temp: 温度值指针
 * @return RT_EOK: 成功, -RT_ERROR: 失败
 */
rt_err_t temp_measure_get_temperature(float *temp)
{
    if (temp == RT_NULL)
    {
        return -RT_ERROR;
    }
    
    /* 直接读取全局变量，g_current_temp 是 float 类型，读取是原子的（32位）*/
    *temp = g_current_temp;
    
    return RT_EOK;
}

/**
 * @brief  获取NTC传感器状态
 * @return 0 - 正常, 1 - 短路异常, 2 - 开路异常
 */
uint8_t temp_measure_get_ntc_status(void)
{
    return g_ntc_status;
}

/**
 * @brief  获取温度测量详细数据（用于调试）
 * @param  adc_val: ADC原始值输出指针
 * @param  resistance: NTC电阻值输出指针(Ω)
 * @param  temp: 温度值输出指针(℃)
 * @return RT_EOK: 成功, -RT_ERROR: 失败
 */
rt_err_t temp_measure_get_detail(uint16_t *adc_val, float *resistance, float *temp)
{
    uint16_t adc_value;
    float res;
    
    /* 读取ADC值 */
    adc_value = adc_read_average();
    
    /* 转换为电阻 */
    res = adc_to_resistance(adc_value);
    
    /* 输出数据 */
    if (adc_val) *adc_val = adc_value;
    if (resistance) *resistance = res;
    if (temp) *temp = g_current_temp;
    
    return RT_EOK;
}

/**
 * @brief  温度测量模块初始化
 * @return RT_EOK: 成功, -RT_ERROR: 失败
 */
int temp_measure_init(void)
{
    rt_thread_t tid;
    
    /* 1. 创建互斥锁 */
    temp_mutex = rt_mutex_create("temp_mtx", RT_IPC_FLAG_PRIO);
    if (temp_mutex == RT_NULL)
    {
        rt_kprintf("[TempMeasure] Failed to create mutex\n");
        return -RT_ERROR;
    }
    
    /* 创建 ADC 访问互斥锁 */
    adc_mutex = rt_mutex_create("adc_mtx", RT_IPC_FLAG_PRIO);
    if (adc_mutex == RT_NULL)
    {
        rt_kprintf("[TempMeasure] Failed to create ADC mutex\n");
        rt_mutex_delete(temp_mutex);
        return -RT_ERROR;
    }
    
    /* 2. 初始化ADC */
    adc_temp_init();
    
    /* 3. 创建温度测量线程 */
    tid = rt_thread_create("temp_meas",
                          temp_measure_thread_entry,
                          RT_NULL,
                          1024,
                          15,
                          10);
    if (tid == RT_NULL)
    {
        rt_kprintf("[TempMeasure] Failed to create thread\n");
        rt_mutex_delete(temp_mutex);
        rt_mutex_delete(adc_mutex);
        return -RT_ERROR;
    }
    
    /* 4. 启动线程 */
    rt_thread_startup(tid);
    
    rt_kprintf("[TempMeasure] Module initialized (Lookup Table Method)\n");
    return RT_EOK;
}

/* 使用 INIT_APP_EXPORT 自动初始化 */
INIT_APP_EXPORT(temp_measure_init);

/* =============== Shell 调试命令 =============== */

/**
 * @brief Shell命令：读取温度传感器原始ADC值
 */
static void cmd_temp_adc(int argc, char **argv)
{
    uint16_t adc_value;
    float resistance;
    float voltage;
    
    /* 读取ADC平均值 */
    adc_value = adc_read_average();
    
    /* 转换为电压 */
    voltage = (float)adc_value * ADC_VREF / ADC_RESOLUTION;
    
    /* 转换为电阻 */
    resistance = adc_to_resistance(adc_value);
    
    rt_kprintf("=== Temperature Sensor Debug ===\n");
    rt_kprintf("ADC Raw:    %d\n", adc_value);
    rt_kprintf("Voltage:    %d.%03d V\n", (int)voltage, (int)((voltage - (int)voltage) * 1000));
    rt_kprintf("Resistance: %d kOhm\n", (int)(resistance / 1000));
    rt_kprintf("NTC Status: %d (%s)\n", g_ntc_status, 
               g_ntc_status == 0 ? "Normal" : 
               (g_ntc_status == 1 ? "Short" : "Open"));
    rt_kprintf("Temperature: %d.%d C\n", (int)g_current_temp, 
               (int)((g_current_temp - (int)g_current_temp) * 10));
    rt_kprintf("================================\n");
    rt_kprintf("Rs = %d ohm (calibrated)\n", (int)TEMP_SERIES_R);
    rt_kprintf("If 25C expected with Rs=6800, ADC~3839 is correct\n");
}
MSH_CMD_EXPORT_ALIAS(cmd_temp_adc, temp_adc, Read temperature ADC value);

/**
 * @brief Shell命令：设置串联电阻值（用于校准）
 * @note  用法: temp_set_r <resistance_in_ohm>
 */
static void cmd_temp_set_r(int argc, char **argv)
{
    if (argc >= 2)
    {
        /* 注意：这只是临时修改运行时的值，不会保存 */
        rt_kprintf("Note: Series resistance is fixed at compile time.\n");
        rt_kprintf("Current: %d ohm (22000)\n", (int)TEMP_SERIES_R);
        rt_kprintf("To change, modify TEMP_SERIES_R in source code.\n");
    }
    else
    {
        rt_kprintf("Current series resistance: %d ohm\n", (int)TEMP_SERIES_R);
        rt_kprintf("Usage: temp_set_r <resistance>\n");
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_temp_set_r, temp_set_r, Show series resistance value);

