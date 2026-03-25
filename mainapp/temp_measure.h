/*
 * @file temp_measure.h
 * @brief 温度测量模块头文件 - 查表法获取NTC温度
 * 
 * 硬件配置：
 *   - ADC引脚：PC0 (ADC012_IN10)
 *   - 温度传感器：NTC100K B=3950
 *   - 测温范围：-50℃ ~ 125℃
 *   - 查表精度：5度/区间，线性插值
 * 
 * 参考：T450控制板软硬件接口文档V0.2
 */

#ifndef __TEMP_MEASURE_H__
#define __TEMP_MEASURE_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  温度测量模块初始化
 * @return RT_EOK: 成功, -RT_ERROR: 失败
 */
int temp_measure_init(void);

/**
 * @brief  获取当前温度值
 * @param  temp: 温度值指针
 * @return RT_EOK: 成功，-RT_ERROR: 失败
 */
rt_err_t temp_measure_get_temperature(float *temp);

/**
 * @brief  获取NTC传感器状态
 * @return 0 - 正常, 1 - 短路异常, 2 - 开路异常
 */
uint8_t temp_measure_get_ntc_status(void);

/**
 * @brief  获取温度测量详细数据（用于调试）
 * @param  adc_val: ADC原始值输出指针 (可为NULL)
 * @param  resistance: NTC电阻值输出指针(Ω) (可为NULL)
 * @param  temp: 温度值输出指针(℃) (可为NULL)
 * @return RT_EOK: 成功, -RT_ERROR: 失败
 */
rt_err_t temp_measure_get_detail(uint16_t *adc_val, float *resistance, float *temp);

/* 全局 ADC 互斥锁，供多个模块共享 */
extern rt_mutex_t adc_mutex;

#ifdef __cplusplus
}
#endif

#endif /* __TEMP_MEASURE_H__ */
