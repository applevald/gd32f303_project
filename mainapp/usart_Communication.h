#ifndef _USART_COMMUNICATION_H_
#define _USART_COMMUNICATION_H_

#include <stdint.h>
#include <rtthread.h>

/* 外部可访问的变量 */
extern rt_device_t serial;
extern struct rt_semaphore rx_sem;

int usart_communication_init(void);
void usart_send_data(uint8_t *data, uint16_t len);
void usart_send_data_direct(uint8_t *data, uint16_t len);
void usart_send_string(const char *str);

#endif
