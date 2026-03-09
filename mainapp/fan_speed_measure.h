/*
 * @file fan_speed_measure.h
 * @brief 风扇转速测量模块头文件
 */

#ifndef __FAN_SPEED_MEASURE_H__
#define __FAN_SPEED_MEASURE_H__

#include <rtthread.h>

/**
 * @brief  启动风扇转速测量（阻塞等待完成）
 * @retval RT_EOK: 成功, -RT_ERROR: 失败
 * @note   此函数会阻塞约1秒，等待测量完成
 */
rt_err_t start_fan_speed_measure(void);

/**
 * @brief  获取风扇转速数据（按协议格式）
 * @param  data: 输出缓冲区（至少28字节）
 * @param  len: 缓冲区长度
 * @retval 实际填充的字节数
 */
int get_fan_speed(uint8_t *data, uint16_t len);

/**
 * @brief  获取指定风扇的实际转速(RPM)
 * @param  fan_id: 风扇ID(1-14)
 * @retval 转速(RPM)
 */
uint16_t get_fan_rpm(uint8_t fan_id);

/**
 * @brief  风扇转速测量模块初始化
 */
int fan_speed_measure_init(void);

#endif /* __FAN_SPEED_MEASURE_H__ */
