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
    
    // rt_kprintf("[CLOCK] Step 1: Enable IRC8M\n");
    /* 1. 使能 IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;
    while ((RCU_CTL & RCU_CTL_IRC8MSTB) == 0U)
    {
        if (++timeout > 0x100000) {
            rt_kprintf("[CLOCK] ERROR: IRC8M timeout!\n");
            return;
        }
    }
    // rt_kprintf("[CLOCK] IRC8M enabled\n");
    
    // rt_kprintf("[CLOCK] Step 2: Switch to IRC8M\n");
    /* 2. 切换到 IRC8M 作为临时系统时钟 */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_IRC8M;
    timeout = 0;
    while ((RCU_CFG0 & RCU_CFG0_SCSS) != RCU_SCSS_IRC8M)
    {
        if (++timeout > 0x100000) {
            rt_kprintf("[CLOCK] ERROR: Switch to IRC8M timeout!\n");
            return;
        }
    }
    // rt_kprintf("[CLOCK] Switched to IRC8M\n");
    
    // rt_kprintf("[CLOCK] Step 3: Disable PLL\n");
    /* 3. 关闭 PLL */
    RCU_CTL &= ~RCU_CTL_PLLEN;
    timeout = 0;
    while ((RCU_CTL & RCU_CTL_PLLSTB) != 0U)
    {
        if (++timeout > 0x100000) break;
    }
    // rt_kprintf("[CLOCK] PLL disabled\n");
    // rt_kprintf("[CLOCK] PLL disabled\n");
    
    // rt_kprintf("[CLOCK] Step 4: Configure PMU\n");
    /* 4. 使能 PMU 时钟，配置 LDO 高压模式 */
    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL |= PMU_CTL_LDOVS;
    // rt_kprintf("[CLOCK] PMU configured\n");
    
    // rt_kprintf("[CLOCK] Step 5: Configure bus dividers\n");
    /* 5. 配置 AHB/APB 分频 */
    RCU_CFG0 &= ~(RCU_CFG0_AHBPSC | RCU_CFG0_APB1PSC | RCU_CFG0_APB2PSC);
    RCU_CFG0 |= RCU_AHB_CKSYS_DIV1;   /* AHB = SYSCLK */
    RCU_CFG0 |= RCU_APB2_CKAHB_DIV1;  /* APB2 = AHB */
    RCU_CFG0 |= RCU_APB1_CKAHB_DIV2;  /* APB1 = AHB/2 */
    // rt_kprintf("[CLOCK] Bus dividers configured\n");
    
    // rt_kprintf("[CLOCK] Step 6: Configure PLL\n");
    /* 6. 配置 PLL: IRC8M/2 * 27 = 4MHz * 27 = 108MHz */
    /* 清除 PLL 配置位 */
    RCU_CFG0 &= ~(RCU_CFG0_PLLMF | RCU_CFG0_PLLMF_4 | RCU_CFG0_PLLMF_5 | RCU_CFG0_PLLSEL);
    /* 设置 PLL 倍频为 27，PLL 源默认为 IRC8M/2(PLLSEL=0) */
    RCU_CFG0 |= RCU_PLL_MUL27;
    // rt_kprintf("[CLOCK] PLL configured\n");
    
    // rt_kprintf("[CLOCK] Step 7: Enable PLL\n");
    /* 7. 使能 PLL 并等待锁定 */
    RCU_CTL |= RCU_CTL_PLLEN;
    timeout = 0;
    while ((RCU_CTL & RCU_CTL_PLLSTB) == 0U)
    {
        if (++timeout > 0x100000) {
            rt_kprintf("[CLOCK] ERROR: PLL lock timeout!\n");
            return;
        }
    }
    // rt_kprintf("[CLOCK] PLL locked\n");
    
    // rt_kprintf("[CLOCK] Step 8: Enable high drive mode\n");
    /* 8. 使能高驱动模式 (108MHz 需要) */
    PMU_CTL |= PMU_CTL_HDEN;
    timeout = 0;
    while ((PMU_CS & PMU_CS_HDRF) == 0U)
    {
        if (++timeout > 0x100000) {
            rt_kprintf("[CLOCK] WARN: HDEN timeout, continuing...\n");
            break;
        }
    }
    // rt_kprintf("[CLOCK] High drive mode enabled\n");
    
    // rt_kprintf("[CLOCK] Step 9: Switch to high drive mode\n");
    /* 9. 切换到高驱动模式 */
    PMU_CTL |= PMU_CTL_HDS;
    timeout = 0;
    while ((PMU_CS & PMU_CS_HDSRF) == 0U)
    {
        if (++timeout > 0x100000) {
            rt_kprintf("[CLOCK] WARN: HDS timeout, continuing...\n");
            break;
        }
    }
    // rt_kprintf("[CLOCK] High drive switch completed\n");
    
    // rt_kprintf("[CLOCK] Step 10: Switch system clock to PLL\n");
    /* 10. 切换系统时钟到 PLL */
    RCU_CFG0 &= ~RCU_CFG0_SCS;
    RCU_CFG0 |= RCU_CKSYSSRC_PLL;
    timeout = 0;
    while ((RCU_CFG0 & RCU_CFG0_SCSS) != RCU_SCSS_PLL)
    {
        if (++timeout > 0x100000) {
            rt_kprintf("[CLOCK] ERROR: Switch to PLL timeout!\n");
            return;
        }
    }
    // rt_kprintf("[CLOCK] Switched to PLL\n");
    
    /* 11. 更新 SystemCoreClock 变量 */
    SystemCoreClockUpdate();
    
    // rt_kprintf("[CLOCK] Clock configuration completed successfully\n");
    // rt_kprintf("[CLOCK] SystemCoreClock = %d Hz\n", SystemCoreClock);
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

/* BSS段测试变量（未初始化，应该被自动清零） */
static uint32_t bss_test_variable;

/* .data段测试变量（有初始值，应该从Flash复制到RAM） */
static uint32_t data_test_variable = 0xAABBCCDD;

/**
 * This function will initial GD32 board.
 */
void rt_hw_board_init(void)
{
    /* ===== 关键修复：立即禁用所有串口接收中断 ===== */
    /* Boot跳转后，串口硬件中断仍处于使能状态，但RT-Thread设备对象尚未创建 */
    /* 必须在最开始就禁用中断，等设备完全初始化后再由RT-Thread重新启用 */
    #ifdef BSP_USING_UART0
    usart_interrupt_disable(USART0, USART_INT_RBNE);
    NVIC_DisableIRQ(USART0_IRQn);
    #endif
    
    #ifdef BSP_USING_UART1
    usart_interrupt_disable(USART1, USART_INT_RBNE);
    NVIC_DisableIRQ(USART1_IRQn);
    #endif
    
    #ifdef BSP_USING_UART2
    usart_interrupt_disable(USART2, USART_INT_RBNE);
    NVIC_DisableIRQ(USART2_IRQn);
    #endif
    
    #ifdef BSP_USING_UART3
    usart_interrupt_disable(UART3, USART_INT_RBNE);
    NVIC_DisableIRQ(UART3_IRQn);
    #endif
    
    #ifdef BSP_USING_UART4
    usart_interrupt_disable(UART4, USART_INT_RBNE);
    NVIC_DisableIRQ(UART4_IRQn);
    #endif
    
    /* 检查当前栈指针位置 */
    uint32_t current_msp;
    __asm volatile ("MRS %0, MSP" : "=r" (current_msp));
    // rt_kprintf("[DEBUG] Current MSP = 0x%08X\n", current_msp);
    
    // /* DEBUG: 检查 BSS 是否被正确清零 */
    // rt_kprintf("[DEBUG] BSS Test Variable = 0x%08X (should be 0x00000000)\n", bss_test_variable);
    
    // /* DEBUG: 检查 .data 段是否被正确初始化 */
    // rt_kprintf("[DEBUG] DATA Test Variable = 0x%08X (should be 0xAABBCCDD)\n", data_test_variable);
    
    if (bss_test_variable != 0) {
        rt_kprintf("[ERROR] BSS NOT CLEARED!\n");
    }
    if (data_test_variable != 0xAABBCCDD) {
        rt_kprintf("[ERROR] .data segment NOT properly initialized!\n");
        rt_kprintf("[ERROR] This means __iar_data_init3() failed or ROM address is wrong!\n");
    }
    
    if (bss_test_variable == 0 && data_test_variable == 0xAABBCCDD) {
        rt_kprintf("[OK] Both BSS and .data segments are correct\n");
    }
    
    /* 检查系统时钟是否已经配置好（由 Bootloader 配置） */
    SystemCoreClockUpdate();
    if (SystemCoreClock == 108000000) {
        rt_kprintf("[CLOCK] System clock already configured to 108MHz by Bootloader\n");
        rt_kprintf("[CLOCK] Skipping clock reconfiguration\n");
    } else {
        /* 0. 如果时钟未配置，则配置系统时钟到108MHz */
        rt_kprintf("[CLOCK] System clock = %d Hz, reconfiguring to 108MHz\n", SystemCoreClock);
        rt_kprintf("[DEBUG] About to call system_clock_108m_config()\n");
        system_clock_108m_config();
        rt_kprintf("[DEBUG] system_clock_108m_config() completed\n");
    }
    
    /* NVIC Configuration */
#define NVIC_VTOR_MASK              0x3FFFFF80
#ifdef  VECT_TAB_RAM
    /* Set the Vector Table base location at 0x20000000 */
    SCB->VTOR  = (0x20000000 & NVIC_VTOR_MASK);
#else  /* VECT_TAB_FLASH  */
    /* Set the Vector Table base location at 0x0800C800 (APP start address after bootloader) */
    SCB->VTOR  = (0x0800C800 & NVIC_VTOR_MASK);
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
    
    // rt_kprintf("[DEBUG] CSTACK$$Base = 0x%08X\n", (uint32_t)&CSTACK$$Base);
    // rt_kprintf("[DEBUG] CSTACK$$Limit = 0x%08X\n", (uint32_t)&CSTACK$$Limit);
    // rt_kprintf("[DEBUG] GD32_SRAM_END = 0x%08X\n", GD32_SRAM_END);
    
    /* CRITICAL FIX: 堆应该在所有静态数据之后，而不是在栈空间内！ */
    /* IAR 的内存布局：.data/.bss -> HEAP (from ICF) -> CSTACK */
    /* 但是 RT-Thread 期望：.data/.bss -> CSTACK -> HEAP */
    /* 所以我们需要使用 CSTACK$$Base 作为堆的起始地址 */
    /* 注意：这假设链接脚本已经正确放置了 CSTACK 和 HEAP 块 */
    
    /* 检查链接脚本配置：如果 HEAP 块已经在 RAM_region 中定义，则使用它 */
    /* 否则，在栈之前的空闲RAM中创建堆 */
    extern uint32_t __ICFEDIT_region_RAM_start__;
    uint32_t ram_start = (uint32_t)&__ICFEDIT_region_RAM_start__;
    uint32_t data_end = (uint32_t)&CSTACK$$Base;  /* 栈的底部 */
    
    // rt_kprintf("[DEBUG] RAM start = 0x%08X\n", ram_start);
    // rt_kprintf("[DEBUG] Data/BSS end (CSTACK$$Base) = 0x%08X\n", data_end);
    // rt_kprintf("[DEBUG] Heap range: 0x%08X ~ 0x%08X\n", data_end, (uint32_t)GD32_SRAM_END);
    // rt_kprintf("[DEBUG] Heap size = %d bytes\n", (uint32_t)GD32_SRAM_END - data_end);
    
    // rt_kprintf("[DEBUG] Calling rt_system_heap_init(0x%08X, 0x%08X)\n", 
    //            data_end, (uint32_t)GD32_SRAM_END);
    rt_system_heap_init((void *)data_end, (void *)GD32_SRAM_END);
    // rt_kprintf("[DEBUG] rt_system_heap_init() completed\n");
#endif

#ifdef RT_USING_SERIAL
    /* init uart device */
    /* 由于 Bootloader 已经初始化了串口硬件，但没有创建 RT-Thread 设备对象 */
    /* 所以这里需要强制重新初始化，确保 rx_fifo 等结构被正确创建 */
    rt_kprintf("[UART] Initializing UART device...\n");
    rt_hw_uart_init();
    rt_kprintf("[UART] UART device initialized\n");
#endif

#ifdef RT_USING_CONSOLE
    /* set console device */
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
#endif

    /* show version - call removed to avoid double banner (rtthread_startup calls it too) */
    /* rt_show_version(); */
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
