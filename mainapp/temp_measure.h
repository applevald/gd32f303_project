/*
 * @file temp_measure.h
 * @brief 温度测量模块头文件
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

/* 全局 ADC 互斥锁，供多个模块共享 */
extern rt_mutex_t adc_mutex;

#ifdef __cplusplus
}
#endif

#endif /* __TEMP_MEASURE_H__ */
