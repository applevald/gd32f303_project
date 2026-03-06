/*
 * @file usart_Communication.c
 * @brief USART Communication with Mainboard (PC10/PC11 - UART3)
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "usart_Communication.h"
#include "gd32f30x_usart.h"

// PC10 TX 引脚
// PC11 RX 引脚

/* UART3 Device Name */
#define UART_NAME       "uart3"

/* Serial device handle */
static rt_device_t serial;

/* Semaphore for sync */
static struct rt_semaphore rx_sem;

/* Receive callback function */
static rt_err_t uart_input(rt_device_t dev, rt_size_t size)
{
    /* 
     * Release semaphore to indicate data received in ISR context
     */
    rt_sem_release(&rx_sem);
    return RT_EOK;
}

/**
 * @brief Send raw data via USART (using RT-Thread device framework)
 * @param data Pointer to data buffer
 * @param len Length of data
 */
void usart_send_data(uint8_t *data, uint16_t len)
{
    if (serial)
    {
        rt_device_write(serial, 0, data, len);
    }
}

/**
 * @brief Send raw data via USART (direct hardware access)
 * @param data Pointer to data buffer
 * @param len Length of data
 */
void usart_send_data_direct(uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        /* Send one byte */
        usart_data_transmit(UART3, data[i]);
        /* Wait until transmit complete */
        while(RESET == usart_flag_get(UART3, USART_FLAG_TBE));
    }
}

/**
 * @brief Send a string via USART (using direct hardware access)
 * @param str Null-terminated string
 */
void usart_send_string(const char *str)
{
    usart_send_data_direct((uint8_t *)str, rt_strlen(str));
}

/**
 * @brief Initialize the USART communication (UART3)
 */
int usart_communication_init(void)
{
    /* Find the serial device */
    serial = rt_device_find(UART_NAME);
    
    if (!serial)
    {
        rt_kprintf("Error: Serial device %s not found!\n", UART_NAME);
        return -RT_ERROR;
    }

    /* Initialize semaphore */
    rt_sem_init(&rx_sem, "uart3_rx", 0, RT_IPC_FLAG_FIFO);

    /* Configure UART3 parameters explicitly */
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    config.baud_rate = BAUD_RATE_115200;
    config.data_bits = DATA_BITS_8;
    config.stop_bits = STOP_BITS_1;
    config.parity    = PARITY_NONE;

    if (rt_device_control(serial, RT_DEVICE_CTRL_CONFIG, &config) != RT_EOK)
    {
        rt_kprintf("Error: Failed to configure device %s\n", UART_NAME);
        return -RT_ERROR;
    }

    /* Open the device in interrupt RX mode */
    if (rt_device_open(serial, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        rt_kprintf("Error: Failed to open device %s\n", UART_NAME);
        return -RT_ERROR;
    }

    /* Set the receive callback function */
    rt_device_set_rx_indicate(serial, uart_input);
    
    rt_kprintf("USART communication initialized on %s (PC10/PC11)\n", UART_NAME);
    
    return RT_EOK;
}

/* Export to automatic initialization at app start */
INIT_APP_EXPORT(usart_communication_init);

/* Shell command for testing */
static void mainboard_send_cmd(int argc, char **argv)
{
    if (argc >= 2)
    {
        usart_send_string(argv[1]);
        usart_send_string("\r\n");
        rt_kprintf("Sent: %s\n", argv[1]);
    }
    else
    {
        rt_kprintf("Usage: mainboard_send <string>\n");
    }
}
/* Export to MSH (FinSH) shell */
MSH_CMD_EXPORT_ALIAS(mainboard_send_cmd, mainboard_send, Send data to mainboard via UART3);

