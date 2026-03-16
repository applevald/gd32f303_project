/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-02-17     Cline        Minimal rtdevice.h compatible with rtdef.h
 */

#ifndef __RTDEVICE_H__
#define __RTDEVICE_H__

#include <rtdef.h>
#include "dev_serial.h"

#ifdef RT_USING_DEVICE

// /* Forward declarations */
// struct rt_serial_device;
// struct rt_device;

// /* Serial configure */
// struct serial_configure
// {
//     rt_uint32_t baud_rate;
//     rt_uint32_t data_bits :4;
//     rt_uint32_t stop_bits :2;
//     rt_uint32_t parity    :2;
//     rt_uint32_t bit_order :1;
//     rt_uint32_t invert    :1;
//     rt_uint32_t rx_bufsz  :16;
//     rt_uint32_t tx_bufsz  :16;
// };

// /* Serial configure default values */
// #define BAUD_RATE_115200    115200
// #define DATA_BITS_8         8
// #define STOP_BITS_1         1
// #define PARITY_NONE         0
// #define BIT_ORDER_LSB       0
// #define NRZ_NORMAL          0
// #define RT_SERIAL_RB_BUFSZ  64

// /* Serial device */
// struct rt_serial_device
// {
//     struct rt_device parent;
//     const struct rt_uart_ops *ops;
//     struct serial_configure config;
//     void *serial_rx;
//     void *serial_tx;
// };
// typedef struct rt_serial_device rt_serial_t;

// struct rt_uart_ops
// {
//     rt_err_t (*configure)(struct rt_serial_device *serial, struct serial_configure *cfg);
//     rt_err_t (*control)(struct rt_serial_device *serial, int cmd, void *arg);
//     int (*putc)(struct rt_serial_device *serial, char c);
//     int (*getc)(struct rt_serial_device *serial);
//     rt_size_t (*dma_transmit)(struct rt_serial_device *serial, rt_uint8_t *buf, rt_size_t size, int direction);
// };

// extern const struct rt_uart_ops rt_uart_ops;

// rt_err_t rt_hw_serial_register(struct rt_serial_device *serial,
//                                const char *name,
//                                rt_uint32_t flag,
//                                void *data);

#endif /* RT_USING_DEVICE */

#endif /* __RTDEVICE_H__ */
