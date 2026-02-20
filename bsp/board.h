/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-02-16     Cline        Board support for GD32F303
 */

#ifndef __BOARD_H__
#define __BOARD_H__

#include <stdint.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include "gd32f30x.h"

#ifdef __cplusplus
extern "C" {
#endif

/* board configuration */
// <o> Internal SRAM memory size[Kbytes] <8-64>
//  <i>Default: 64
#define GD32_SRAM_SIZE         48
#ifdef __ICCARM__
// Use *.icf ram symbal, to avoid hardcode.
extern char __ICFEDIT_region_RAM_end__;
#define GD32_SRAM_END          &__ICFEDIT_region_RAM_end__
#else
#define GD32_SRAM_END          (0x20000000 + (GD32_SRAM_SIZE - 24) * 1024)
#endif

/* USART driver selection */
#define BSP_USING_UART0
#define BSP_UART0_TX_PIN       "PA9"
#define BSP_UART0_RX_PIN       "PA10"

/* LED configuration */
#define BSP_LED0_PIN           "PC13"

#ifdef RT_USING_SERIAL
    extern rt_err_t rt_hw_uart_init(void);
#endif

extern void rt_hw_board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_H__ */