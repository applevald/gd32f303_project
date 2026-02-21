/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-02-16     Cline        Board initialization for GD32F303
 */

#include "board.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_usart.h"
#include <rthw.h>
#include <rtthread.h>

/* Note: UART serial device initialization has been moved to drv_serial.c */
/* This file only contains board-level initialization and console output */

/* systick configuration */
static void systick_configuration(void)
{
    /* setup systick timer for 1000Hz interrupts */
    if (SysTick_Config(SystemCoreClock / RT_TICK_PER_SECOND))
    {
        /* capture error */
        while (1);
    }
    /* configure the systick handler priority */
    NVIC_SetPriority(SysTick_IRQn, 0xFF);
}

/**
  * @brief  This function is used to initialize USART0
  * @param  None
  * @retval None
  */
static void uart0_init(void)
{
#ifdef BSP_USING_UART0
    /* enable GPIO clock */
    rcu_periph_clock_enable(RCU_GPIOA);
    /* enable USART clock */
    rcu_periph_clock_enable(RCU_USART0);
    
    /* connect port to USARTx_Tx */
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);
    /* connect port to USARTx_Rx */
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_10);
    
    /* USART configure */
    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_hardware_flow_rts_config(USART0, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(USART0, USART_CTS_DISABLE);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_enable(USART0);
#endif
}

/**
  * @brief  This function is used to initialize GPIO
  * @param  None
  * @retval None
  */
static void gpio_init_config(void)
{
#ifdef BSP_LED0_PIN
    /* enable GPIO clock */
    rcu_periph_clock_enable(RCU_GPIOC);
    /* configure LED GPIO pin */
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_13);
    gpio_bit_reset(GPIOC, GPIO_PIN_13);
#endif
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  None
  * @retval None
  */
void _Error_Handler(char *file, int line)
{
    /* User can add his own implementation to report the error */
    while (1)
    {
    }
}

/**
 * This is the timer interrupt service routine.
 */
void SysTick_Handler(void)
{
    /* enter interrupt */
    rt_interrupt_enter();

    rt_tick_increase();

    /* leave interrupt */
    rt_interrupt_leave();
}

/**
 * This function will initial GD32 board.
 */
void rt_hw_board_init(void)
{
    /* NVIC Configuration */
#define NVIC_VTOR_MASK              0x3FFFFF80
#ifdef  VECT_TAB_RAM
    /* Set the Vector Table base location at 0x20000000 */
    SCB->VTOR  = (0x20000000 & NVIC_VTOR_MASK);
#else  /* VECT_TAB_FLASH  */
    /* Set the Vector Table base location at 0x08000000 */
    SCB->VTOR  = (0x08000000 & NVIC_VTOR_MASK);
#endif

    /* Configure the SysTick */
    systick_configuration();

    /* init gpio */
    gpio_init_config();

    /* init uart */
    uart0_init();

#ifdef RT_USING_HEAP
    /* initialize memory system */
    /* Use linker symbols for proper heap boundaries */
    /* CSTACK$$Base and CSTACK$$Limit are defined by IAR linker */
    extern uint32_t CSTACK$$Base;
    extern uint32_t CSTACK$$Limit;
    
    /* Heap starts after all static data and stack */
    /* In IAR, stack grows downward from CSTACK$$Limit toward CSTACK$$Base */
    /* Heap should use memory after the stack (CSTACK$$Limit) up to SRAM end */
    rt_system_heap_init((void *)&CSTACK$$Limit, (void *)GD32_SRAM_END);
#endif

#ifdef RT_USING_SERIAL
    /* init uart device */
    rt_hw_uart_init();
#endif

#ifdef RT_USING_CONSOLE
    /* set console device */
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif

    /* show version */
    rt_show_version();
}

#ifdef RT_USING_SERIAL
/* UART initialization function - now implemented in drv_serial.c */
/* This is a stub to maintain compatibility */
rt_err_t rt_hw_uart_init(void)
{
    /* Call the actual implementation from drv_serial.c */
    extern int rt_hw_usart_init(void);
    return rt_hw_usart_init();
}
#endif


#ifdef RT_USING_CONSOLE
/* retarget the C library printf function to the USART */
void rt_hw_console_output(const char *str)
{
    while (*str)
    {
        if (*str == '\n')
        {
            usart_data_transmit(USART0, '\r');
            while (RESET == usart_flag_get(USART0, USART_FLAG_TBE));
        }
        usart_data_transmit(USART0, *str);
        while (RESET == usart_flag_get(USART0, USART_FLAG_TBE));
        str++;
    }
}
#endif
