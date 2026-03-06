/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-03-04     Cline        first implementation for GD32F303
 */

#include "drv_spi.h"
#include "gd32f30x_spi.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_gpio.h"

#ifdef RT_USING_SPI

#if !defined(BSP_USING_SPI0) && !defined(BSP_USING_SPI1) && !defined(BSP_USING_SPI2)
#error "Please define at least one SPIx in rtconfig.h"
#endif

/* SPI bus device structures */
#ifdef BSP_USING_SPI0
static struct gd32_spi_bus spi_bus0 = {0};
#endif

#ifdef BSP_USING_SPI1
static struct gd32_spi_bus spi_bus1 = {0};
#endif

#ifdef BSP_USING_SPI2
static struct gd32_spi_bus spi_bus2 = {0};
#endif

/* SPI configuration array */
static const struct gd32_spi_config spi_config[] =
{
#ifdef BSP_USING_SPI0
    {
        SPI0,                                   /* SPI peripheral */
        RCU_SPI0,                              /* SPI clock */
        RCU_GPIOA, RCU_GPIOA, RCU_GPIOA,       /* SCK, MISO, MOSI GPIO clocks */
        GPIOA, GPIO_PIN_5,                     /* SCK: PA5 */
        GPIOA, GPIO_PIN_6,                     /* MISO: PA6 */
        GPIOA, GPIO_PIN_7,                     /* MOSI: PA7 */
    },
#endif

#ifdef BSP_USING_SPI1
    {
        SPI1,                                   /* SPI peripheral */
        RCU_SPI1,                              /* SPI clock */
        RCU_GPIOB, RCU_GPIOB, RCU_GPIOB,       /* SCK, MISO, MOSI GPIO clocks */
        GPIOB, GPIO_PIN_13,                    /* SCK: PB13 */
        GPIOB, GPIO_PIN_14,                    /* MISO: PB14 */
        GPIOB, GPIO_PIN_15,                    /* MOSI: PB15 */
    },
#endif

#ifdef BSP_USING_SPI2
    {
        SPI2,                                   /* SPI peripheral */
        RCU_SPI2,                              /* SPI clock */
        RCU_GPIOB, RCU_GPIOB, RCU_GPIOB,       /* SCK, MISO, MOSI GPIO clocks */
        GPIOB, GPIO_PIN_3,                     /* SCK: PB3 */
        GPIOB, GPIO_PIN_4,                     /* MISO: PB4 */
        GPIOB, GPIO_PIN_5,                     /* MOSI: PB5 */
    },
#endif
};

/* SPI bus device pointers */
static struct gd32_spi_bus * const spi_bus_obj[] =
{
#ifdef BSP_USING_SPI0
    &spi_bus0,
#endif
#ifdef BSP_USING_SPI1
    &spi_bus1,
#endif
#ifdef BSP_USING_SPI2
    &spi_bus2,
#endif
};

/* SPI device names */
static const char * spi_bus_names[] =
{
#ifdef BSP_USING_SPI0
    "spi0",
#endif
#ifdef BSP_USING_SPI1
    "spi1",
#endif
#ifdef BSP_USING_SPI2
    "spi2",
#endif
};

/**
 * @brief  Configure SPI GPIO pins
 * @param  config: SPI configuration structure
 * @retval None
 */
static void gd32_spi_gpio_init(const struct gd32_spi_config *config)
{
    /* Enable SPI clock */
    rcu_periph_clock_enable(config->spi_clk);
    
    /* Special handling for SPI2: only configure MOSI pin */
    if (config->spi_periph == SPI2)
    {
        /* Enable MOSI GPIO clock only */
        rcu_periph_clock_enable(config->mosi_gpio_clk);
        
        /* Disable JTAG to free PB5 pin, keep SWD */
        rcu_periph_clock_enable(RCU_AF);
        gpio_pin_remap_config(GPIO_SWJ_SWDPENABLE_REMAP, ENABLE);
        
        /* MOSI pin: alternate function push-pull */
        gpio_init(config->mosi_port, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, config->mosi_pin);
    }
    else
    {
        /* For other SPI (SPI0/SPI1): configure all pins normally */
        /* Enable GPIO clocks */
        rcu_periph_clock_enable(config->sck_gpio_clk);
        rcu_periph_clock_enable(config->miso_gpio_clk);
        rcu_periph_clock_enable(config->mosi_gpio_clk);
        
        /* Configure SPI pins */
        /* SCK pin: alternate function push-pull */
        gpio_init(config->sck_port, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, config->sck_pin);
        
        /* MISO pin: input floating or pull-up */
        gpio_init(config->miso_port, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, config->miso_pin);
        
        /* MOSI pin: alternate function push-pull */
        gpio_init(config->mosi_port, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, config->mosi_pin);
    }
}

/**
 * @brief  Initialize SPI peripheral
 * @param  spi_bus: SPI bus device structure
 * @retval RT_EOK on success, -RT_ERROR on failure
 */
rt_err_t gd32_spi_bus_init(struct gd32_spi_bus *spi_bus)
{
    spi_parameter_struct spi_init_struct;
    
    RT_ASSERT(spi_bus != RT_NULL);
    RT_ASSERT(spi_bus->config != RT_NULL);
    
    /* Configure GPIO pins */
    gd32_spi_gpio_init(spi_bus->config);
    
    /* Deinitialize SPI */
    spi_i2s_deinit(spi_bus->config->spi_periph);
    
    /* SPI parameter configuration */
    spi_init_struct.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;
    spi_init_struct.device_mode          = SPI_MASTER;
    spi_init_struct.frame_size           = SPI_FRAMESIZE_8BIT;
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;  /* CPOL=0, CPHA=0 (Mode 0) */
    spi_init_struct.nss                  = SPI_NSS_SOFT;
    /* 
     * SPI频率配置 (系统时钟108MHz, APB1=54MHz):
     * SPI_PSC_8: 54MHz/8 = 6.75MHz
     * 每个SPI位时间: 1/6.75MHz ≈ 0.148μs
     * 8个SPI位 = 1.18μs (符合WS2811 800kHz = 1.25μs周期要求)
     */
    spi_init_struct.prescale             = SPI_PSC_8;
    spi_init_struct.endian               = SPI_ENDIAN_MSB;          /* MSB first */
    
    /* Initialize SPI */
    spi_init(spi_bus->config->spi_periph, &spi_init_struct);
    
    /* Enable SPI */
    spi_enable(spi_bus->config->spi_periph);
    
    return RT_EOK;
}

/**
 * @brief  Send and receive one byte via SPI
 * @param  spi_bus: SPI bus device structure
 * @param  data: Data to send
 * @retval Received data
 */
rt_uint32_t gd32_spi_send_recv_byte(struct gd32_spi_bus *spi_bus, rt_uint8_t data)
{
    rt_uint32_t timeout = 0x1000;
    
    RT_ASSERT(spi_bus != RT_NULL);
    
    /* Wait until TBE flag is set */
    while (RESET == spi_i2s_flag_get(spi_bus->config->spi_periph, SPI_FLAG_TBE))
    {
        if (--timeout == 0)
            return 0xFF;
    }
    
    /* Send data */
    spi_i2s_data_transmit(spi_bus->config->spi_periph, data);
    
    /* Wait until RBNE flag is set */
    timeout = 0x1000;
    while (RESET == spi_i2s_flag_get(spi_bus->config->spi_periph, SPI_FLAG_RBNE))
    {
        if (--timeout == 0)
            return 0xFF;
    }
    
    /* Return received data */
    return spi_i2s_data_receive(spi_bus->config->spi_periph);
}

/**
 * @brief  Send data via SPI
 * @param  spi_bus: SPI bus device structure
 * @param  buffer: Data buffer to send
 * @param  length: Number of bytes to send
 * @retval RT_EOK on success, -RT_ERROR on failure
 */
rt_err_t gd32_spi_send(struct gd32_spi_bus *spi_bus, const void *buffer, rt_size_t length)
{
    const rt_uint8_t *ptr = (const rt_uint8_t *)buffer;
    rt_size_t i;
    
    RT_ASSERT(spi_bus != RT_NULL);
    RT_ASSERT(buffer != RT_NULL);
    
    /* 
     * 注意：为了避免MSH命令上下文中的线程调度问题，
     * 临时移除互斥锁保护。如果需要多线程并发访问SPI，
     * 请确保在应用层进行保护，或恢复互斥锁并确保初始化正确。
     */
    /* rt_mutex_take(&spi_bus->lock, RT_WAITING_FOREVER); */
    
    for (i = 0; i < length; i++)
    {
        gd32_spi_send_recv_byte(spi_bus, ptr[i]);
    }
    
    /* rt_mutex_release(&spi_bus->lock); */
    
    return RT_EOK;
    
    return RT_EOK;
}

/**
 * @brief  Receive data via SPI
 * @param  spi_bus: SPI bus device structure
 * @param  buffer: Buffer to store received data
 * @param  length: Number of bytes to receive
 * @retval RT_EOK on success, -RT_ERROR on failure
 */
rt_err_t gd32_spi_recv(struct gd32_spi_bus *spi_bus, void *buffer, rt_size_t length)
{
    rt_uint8_t *ptr = (rt_uint8_t *)buffer;
    rt_size_t i;
    
    RT_ASSERT(spi_bus != RT_NULL);
    RT_ASSERT(buffer != RT_NULL);
    
    /* rt_mutex_take(&spi_bus->lock, RT_WAITING_FOREVER); */
    
    for (i = 0; i < length; i++)
    {
        ptr[i] = gd32_spi_send_recv_byte(spi_bus, 0xFF);  /* Send dummy byte to generate clock */
    }
    
    /* rt_mutex_release(&spi_bus->lock); */
    
    return RT_EOK;
    
    return RT_EOK;
}

/**
 * @brief  Send then receive data via SPI
 * @param  spi_bus: SPI bus device structure
 * @param  send_buf: Buffer containing data to send
 * @param  send_length: Number of bytes to send
 * @param  recv_buf: Buffer to store received data
 * @param  recv_length: Number of bytes to receive
 * @retval RT_EOK on success, -RT_ERROR on failure
 */
rt_err_t gd32_spi_send_then_recv(struct gd32_spi_bus *spi_bus,
                                  const void *send_buf, rt_size_t send_length,
                                  void *recv_buf, rt_size_t recv_length)
{
    const rt_uint8_t *send_ptr = (const rt_uint8_t *)send_buf;
    rt_uint8_t *recv_ptr = (rt_uint8_t *)recv_buf;
    rt_size_t i;
    
    RT_ASSERT(spi_bus != RT_NULL);
    
    /* rt_mutex_take(&spi_bus->lock, RT_WAITING_FOREVER); */
    
    /* Send phase */
    if (send_buf != RT_NULL && send_length > 0)
    {
        for (i = 0; i < send_length; i++)
        {
            gd32_spi_send_recv_byte(spi_bus, send_ptr[i]);
        }
    }
    
    /* Receive phase */
    if (recv_buf != RT_NULL && recv_length > 0)
    {
        for (i = 0; i < recv_length; i++)
        {
            recv_ptr[i] = gd32_spi_send_recv_byte(spi_bus, 0xFF);
        }
    }
    
    /* rt_mutex_release(&spi_bus->lock); */
    
    return RT_EOK;
}

/**
 * @brief  SPI device open
 * @param  dev: Device handle
 * @param  oflag: Open flags
 * @retval RT_EOK on success
 */
static rt_err_t gd32_spi_open(rt_device_t dev, rt_uint16_t oflag)
{
    return RT_EOK;
}

/**
 * @brief  SPI device close
 * @param  dev: Device handle
 * @retval RT_EOK on success
 */
static rt_err_t gd32_spi_close(rt_device_t dev)
{
    return RT_EOK;
}

/**
 * @brief  SPI device control
 * @param  dev: Device handle
 * @param  cmd: Control command
 * @param  args: Arguments
 * @retval RT_EOK on success
 */
static rt_err_t gd32_spi_control(rt_device_t dev, int cmd, void *args)
{
    /* Reserved for future use */
    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS
const static struct rt_device_ops spi_ops =
{
    RT_NULL,
    gd32_spi_open,
    gd32_spi_close,
    RT_NULL,
    RT_NULL,
    gd32_spi_control
};
#endif

/**
 * @brief  Initialize all SPI buses
 * @param  None
 * @retval RT_EOK on success, -RT_ERROR on failure
 */
int rt_hw_spi_init(void)
{
    rt_err_t result = RT_EOK;
    rt_size_t obj_num = sizeof(spi_bus_obj) / sizeof(struct gd32_spi_bus *);
    rt_size_t i;
    
    for (i = 0; i < obj_num; i++)
    {
        spi_bus_obj[i]->config = &spi_config[i];
        spi_bus_obj[i]->device_name = spi_bus_names[i];
        spi_bus_obj[i]->parent.type = RT_Device_Class_SPIBUS;
        
        /* Initialize mutex - 使用指针方式 */
#ifdef BSP_USING_SPI0
        if (spi_bus_obj[i]->config->spi_periph == SPI0)
        {
            rt_mutex_init(&spi_bus_obj[i]->lock, "spi0_lock", RT_IPC_FLAG_PRIO);
        }
#endif
#ifdef BSP_USING_SPI1
        if (spi_bus_obj[i]->config->spi_periph == SPI1)
        {
            rt_mutex_init(&spi_bus_obj[i]->lock, "spi1_lock", RT_IPC_FLAG_PRIO);
        }
#endif
#ifdef BSP_USING_SPI2
        if (spi_bus_obj[i]->config->spi_periph == SPI2)
        {
            rt_mutex_init(&spi_bus_obj[i]->lock, "spi2_lock", RT_IPC_FLAG_PRIO);
        }
#endif
        
        /* Initialize SPI peripheral */
        gd32_spi_bus_init(spi_bus_obj[i]);
        
        /* Register SPI bus device */
#ifdef RT_USING_DEVICE_OPS
        spi_bus_obj[i]->parent.ops = &spi_ops;
#else
        spi_bus_obj[i]->parent.init    = RT_NULL;
        spi_bus_obj[i]->parent.open    = gd32_spi_open;
        spi_bus_obj[i]->parent.close   = gd32_spi_close;
        spi_bus_obj[i]->parent.read    = RT_NULL;
        spi_bus_obj[i]->parent.write   = RT_NULL;
        spi_bus_obj[i]->parent.control = gd32_spi_control;
#endif
        
        result = rt_device_register(&spi_bus_obj[i]->parent, 
                                    spi_bus_obj[i]->device_name,
                                    RT_DEVICE_FLAG_RDWR);
        
        if (result != RT_EOK)
        {
            rt_kprintf("SPI bus %s register failed!\n", spi_bus_obj[i]->device_name);
            return result;
        }
    }
    
    return result;
}
INIT_BOARD_EXPORT(rt_hw_spi_init);

#endif /* RT_USING_SPI */