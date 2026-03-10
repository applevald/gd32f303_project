/*
 * @file temp_measure.c
 * @brief 温度测量模块 - 通过ADC采集温度传感器数据
 * 
 * 硬件配置：
 *   - ADC引脚：PC0 (ADC012_IN10)
 *   - ADC外设：ADC12
 *   - 采样通道：ADC_CHANNEL_10
 * 
 * 温度计算：
 *   - 使用NTC热敏电阻或线性温度传感器
 *   - 根据具体传感器型号调整转换公式
 */

#include <rtthread.h>
#include <math.h>
#include "gd32f30x.h"
#include "gd32f30x_adc.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"

/* ADC配置参数 */
#define ADC_TEMP_CHANNEL        ADC_CHANNEL_10  /* PC0对应ADC0通道10 */
#define ADC_PERIPH              ADC0            /* 修正为ADC0 */
#define ADC_SAMPLE_COUNT        10              /* 多次采样取平均 */
#define ADC_VREF                3.3f            /* 参考电压(V) */
#define ADC_RESOLUTION          4096            /* 12位ADC分辨率 */

/* 温度传感器参数 (根据实际传感器调整) */
#define TEMP_SENSOR_R25         100000.0f       /* 25℃时的电阻值(Ω) NTC100K */
#define TEMP_SENSOR_B           3950.0f         /* NTC B值 (常见值, 需核对规格书) */
/* 
 * 修正说明：
 * 根据ADC读数3340 (2.69V) 推算，在室温(NTC约100k)下，分压电阻约为 22kΩ 
 * 计算推导：R_ntc = R_series * V / (Vcc - V) => 100k = R_series * 4.4 => R_series ≈ 22.7k
 * 这里暂时设定为 22k，如果仍有误差请检查硬件原理图的分压电阻值
 */
#define TEMP_SERIES_R           22000.0f        /* 串联电阻阻值(Ω) */

/* 静态变量 */
static float g_current_temp = 0.0f;             /* 当前温度值(℃) */
static rt_mutex_t temp_mutex = RT_NULL;         /* 温度数据互斥锁 */

/**
 * @brief  初始化ADC温度采集
 */
static void adc_temp_init(void)
{
    /* 1. 使能时钟 */
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_ADC0);
    
    /* 2. 配置ADC时钟：PCLK2/6 = 108MHz/6 = 18MHz (ADC最大14MHz，这里会被内部分频) */
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
}

/**
 * @brief  读取ADC原始值
 * @return ADC转换结果 (0-4095)
 */
static uint16_t adc_read_raw(void)
{
    /* 启动转换 */
    adc_software_trigger_enable(ADC_PERIPH, ADC_REGULAR_CHANNEL);
    
    /* 等待转换完成 */
    while (!adc_flag_get(ADC_PERIPH, ADC_FLAG_EOC));
    
    /* 读取转换结果 */
    return adc_regular_data_read(ADC_PERIPH);
}

/**
 * @brief  多次采样并取平均值
 * @return 平均ADC值
 */
static uint16_t adc_read_average(void)
{
    uint32_t sum = 0;
    
    for (int i = 0; i < ADC_SAMPLE_COUNT; i++)
    {
        sum += adc_read_raw();
        rt_thread_mdelay(5);  /* 采样间隔 */
    }
    
    return (uint16_t)(sum / ADC_SAMPLE_COUNT);
}

/**
 * @brief  ADC值转换为电压
 * @param  adc_value: ADC采样值
 * @return 电压值(V)
 */
static float adc_to_voltage(uint16_t adc_value)
{
    return (float)adc_value * ADC_VREF / ADC_RESOLUTION;
}

/**
 * @brief  电压值转换为温度 (NTC热敏电阻)
 * @param  voltage: 输入电压(V)
 * @return 温度值(℃)
 * @note   分压电路：VCC - R_series - NTC - GND，ADC测量NTC两端电压
 *         计算公式：
 *         1. R_ntc = R_series * voltage / (VCC - voltage)
 *         2. 1/T = 1/T0 + (1/B) * ln(R_ntc/R0)
 *         其中：T0 = 298.15K (25℃), R0 = R25
 */
static float voltage_to_temperature(float voltage)
{
    float r_ntc;        /* NTC当前电阻 */
    float temp_k;       /* 温度(开尔文) */
    float temp_c;       /* 温度(摄氏度) */
    
    /* 防止除零错误 */
    if (voltage >= ADC_VREF)
    {
        voltage = ADC_VREF - 0.01f;
    }
    
    /* 1. 计算NTC电阻值 */
    r_ntc = TEMP_SERIES_R * voltage / (ADC_VREF - voltage);
    
    /* 2. 使用B参数方程计算温度 */
    temp_k = 1.0f / (1.0f / 298.15f + (1.0f / TEMP_SENSOR_B) * logf(r_ntc / TEMP_SENSOR_R25));
    
    /* 3. 转换为摄氏度 */
    temp_c = temp_k - 273.15f;
    
    return temp_c;
}

/**
 * @brief  温度测量线程
 * @param  parameter: 线程参数
 */
static void temp_measure_thread_entry(void *parameter)
{
    uint16_t adc_value;
    float voltage;
    float temperature;
    
    while (1)
    {
        /* 1. 读取ADC值 */
        adc_value = adc_read_average();
        
        /* 2. 转换为电压 */
        voltage = adc_to_voltage(adc_value);
        
        /* 3. 转换为温度 */
        temperature = voltage_to_temperature(voltage);
        
        /* 4. 保存温度值 */
        if (temp_mutex)
        {
            rt_mutex_take(temp_mutex, RT_WAITING_FOREVER);
            g_current_temp = temperature;
            rt_mutex_release(temp_mutex);
        }
        
        /* 5. 调试输出 (兼容不支持%f的情况) */
        int val_int = (int)voltage;
        int val_frac = (int)((voltage - val_int) * 1000); // 3位小数
        
        int temp_int = (int)temperature;
        int temp_frac = (int)((temperature - temp_int) * 100); // 2位小数
        if (temp_frac < 0) temp_frac = -temp_frac;

        rt_kprintf("[TempMeasure] ADC=%d, Voltage=%d.%03dV, Temp=%d.%02dC\n", 
                   adc_value, val_int, val_frac, temp_int, temp_frac);
        
        /* 6. 延时 */
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
    if (temp == RT_NULL || temp_mutex == RT_NULL)
    {
        return -RT_ERROR;
    }
    
    rt_mutex_take(temp_mutex, RT_WAITING_FOREVER);
    *temp = g_current_temp;
    rt_mutex_release(temp_mutex);
    
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
        return -RT_ERROR;
    }
    
    /* 4. 启动线程 */
    rt_thread_startup(tid);
    
    rt_kprintf("[TempMeasure] Module initialized\n");
    return RT_EOK;
}

// /* 使用INIT_APP_EXPORT自动初始化 */
// INIT_APP_EXPORT(temp_measure_init);

