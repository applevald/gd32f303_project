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
#include "gd32f30x_pmu.h"
#include <rthw.h>
#include <rtthread.h>

/* Note: UART serial device initialization has been moved to drv_serial.c */
/* This file only contains board-level initialization and console output */

/**
 * @brief  手动配置系统时钟到 108MHz (使用 IRC8M + PLL)
 * @note   解决 system_gd32f30x.c 中 PLL 倍频配置不正确的问题
 */
void system_clock_108m_config(void)
{
    uint32_t timeout = 0U;
    
    /* 1. 使能 IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;
    while ((RCU_CTL & RCU_CTL_IRC8MSTB) == 0U)
    {
        if (++timeout > 0x100000) return;
    }
    
    /* 2. 切换到 IRC8M 作为临时系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_IRC8M;
    timeout = 0;
    while ((RCU_CFG0 & RCU_CFG0_SCSS) != RCU_SCSS_IRC8M)
    {
        if (++timeout > 0x100000) return;
    }
    
    /* 3. 关闭 PLL */
    RCU_CTL &= ~RCU_CTL_PLLEN;
    timeout = 0;
    while ((RCU_CTL & RCU_CTL_PLLSTB) != 0U)
    {
        if (++timeout > 0x100000) break;
    }
    
    /* 4. 使能 PMU 时钟，配置 LDO 高压模式 */
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;
    
    /* 5. 配置 AHB/APB 分频 */
    RCU_CFG0 &= ~(RCU_CFG0_AHBPSC | RCU_CFG0_APB1PSC | RCU_CFG0_APB2PSC);
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;   /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;  /* APB2 = AHB */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;  /* APB1 = AHB/2 */
    
    /* 6. 配置 PLL: IRC8M/2 * 27 = 4MHz * 27 = 108MHz */
    /* 清除 PLL 配置位 */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5 | RCU_CFG0_PLLSEL);
    /* 设置 PLL 倍频为 27，PLL 源默认为 IRC8M/2(PLLSEL=0) */
    RCU_CFG0 |= RCU_PLL_MUL27;
    
    /* 7. 使能 PLL 并等待锁定 */
    RCU_CTL |= RCU_CTL_PLLEN;
    timeout = 0;
    while ((RCU_CTL & RCU_CTL_PLLSTB) == 0U)
    {
        if (++timeout > 0x100000) return;
    }
    
    /* 8. 使能高驱动模式 (108MHz 需要) */
    PMU_CTL |= PMU_CTL_HDEN;
    timeout = 0;
    while ((PMU_CS & PMU_CS_HDRF) == 0U)
    {
        if (++timeout > 0x100000) break;
    }
    
    /* 9. 切换到高驱动模式 */
    PMU_CTL |= PMU_CTL_HDS;
    timeout = 0;
    while ((PMU_CS & PMU_CS_HDSRF) == 0U)
    {
        if (++timeout > 0x100000) break;
    }
    
    /* 10. 切换系统时钟到 PLL */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;
    timeout = 0;
    while ((RCU_CFG0 & RCU_CFG0_SCSS) != RCU_SCSS_PLL)
    {
        if (++timeout > 0x100000) return;
    }
    
    /* 11. 更新 SystemCoreClock 变量 */
    SystemCoreClockUpdate();
    
    /* 12. 调试：打印 RCU 寄存器值 */
    rt_kprintf("[DEBUG] RCU_CFG0 = 0x%08X\n", RCU_CFG0);
    rt_kprintf("[DEBUG] RCU_CTL = 0x%08X\n", RCU_CTL);
    rt_kprintf("[DEBUG] SystemCoreClock = %d Hz\n", SystemCoreClock);
}

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
//     rcu_periph_clock_enable(RCU_GPIOB);
//     /* enable USART clock */
//     rcu_periph_clock_enable(RCU_USART0);
//     rcu_periph_clock_enable(RCU_AF);

//       /* 2. 配置重映射: 将 USART0 映射到 PB6/PB7 */
//     gpio_pin_remap_config(GPIO_USART0_REMAP, ENABLE);
    
//    /* 3. 配置 PB6 为 TX (复用推挽输出) */
//     gpio_init(GPIOB, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_6);

//     /* 4. 配置 PB7 为 RX (浮空输入) */
//     gpio_init(GPIOB, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_7);

    
//     // /* USART configure */
//     usart_deinit(USART0);
//     usart_baudrate_set(USART0, 115200U);
//     usart_word_length_set(USART0, USART_WL_8BIT);
//     usart_stop_bit_set(USART0, USART_STB_1BIT);
//     usart_parity_config(USART0, USART_PM_NONE);
//     usart_hardware_flow_rts_config(USART0, USART_RTS_DISABLE);
//     usart_hardware_flow_cts_config(USART0, USART_CTS_DISABLE);
//     usart_receive_config(USART0, USART_RECEIVE_ENABLE);
//     usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
//     usart_enable(USART0);
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
    /* 0. 首先配置系统时钟到108MHz (必须在其他初始化之前) */
    system_clock_108m_config();
    
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
