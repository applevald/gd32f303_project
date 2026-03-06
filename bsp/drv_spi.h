/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-04     Cline        first implementation for GD32F303
 */

#ifndef __DRV_SPI_H__
#define __DRV_SPI_H__

#include <rthw.h>
#include <rtthread.h>
#include <board.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SPI configuration structure */
struct gd32_spi_config
{
    uint32_t spi_periph;            /* SPI peripheral: SPI0, SPI1, SPI2 */
    rcu_periph_enum spi_clk;        /* SPI clock */
    rcu_periph_enum sck_gpio_clk;   /* SCK GPIO clock */
    rcu_periph_enum miso_gpio_clk;  /* MISO GPIO clock */
    rcu_periph_enum mosi_gpio_clk;  /* MOSI GPIO clock */
    
    uint32_t sck_port;              /* SCK GPIO port */
    uint16_t sck_pin;               /* SCK GPIO pin */
    
    uint32_t miso_port;             /* MISO GPIO port */
    uint16_t miso_pin;              /* MISO GPIO pin */
    
    uint32_t mosi_port;             /* MOSI GPIO port */
    uint16_t mosi_pin;              /* MOSI GPIO pin */
};

/* SPI bus device structure */
struct gd32_spi_bus
{
    struct rt_device parent;
    const struct gd32_spi_config *config;
    struct rt_mutex lock;           /* Bus lock for thread safety */
    const char *device_name;
};

/* SPI bus operations */
rt_err_t gd32_spi_bus_init(struct gd32_spi_bus *spi_bus);
rt_uint32_t gd32_spi_send_recv_byte(struct gd32_spi_bus *spi_bus, rt_uint8_t data);
rt_err_t gd32_spi_send(struct gd32_spi_bus *spi_bus, const void *buffer, rt_size_t length);
rt_err_t gd32_spi_recv(struct gd32_spi_bus *spi_bus, void *buffer, rt_size_t length);
rt_err_t gd32_spi_send_then_recv(struct gd32_spi_bus *spi_bus, 
                                  const void *send_buf, rt_size_t send_length,
                                  void *recv_buf, rt_size_t recv_length);

/* Initialize all SPI buses */
int rt_hw_spi_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_SPI_H__ */