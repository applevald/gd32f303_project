
#include <rtthread.h>
#include "board.h"
#include "includes.h"

#include <rtdbg.h>

//BOOT留48K，中间2k留给用于存储参数
//APP和备用区用104K
//app启动地址为0x800C800

#define APP_ADDRESS     0x0800C800  // APP起始地址

/* Function declarations */
int rtthread_startup(void);

/**
 * @brief  跳转到应用程序
 * @param  app_addr: 应用程序起始地址
 * @retval None
 */
static void jump_to_app(uint32_t app_addr)
{
    uint32_t jump_addr;
    void (*app_reset_handler)(void);
    
    // 检查栈顶地址是否合法(应该在SRAM范围内)
    // GD32F303 SRAM范围: 0x20000000 - 0x20010000 (64KB)
    if (((*(volatile uint32_t*)app_addr) & 0xFFFE0000) != 0x20000000)
    {
        LOG_E("Invalid app address or stack pointer: 0x%08X", *(volatile uint32_t*)app_addr);
        return;
    }
    
    LOG_I("Jumping to application at 0x%08X...", app_addr);
    
    // 获取APP的复位向量地址
    jump_addr = *(volatile uint32_t*)(app_addr + 4);
    app_reset_handler = (void (*)(void))jump_addr;
    
    // 关闭RT-Thread调度器
    rt_thread_mdelay(10);
    
    // 关闭全局中断
    __disable_irq();
    
    // 关闭SysTick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    
    // 关闭所有中断,清除中断挂起标志
    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    
    // 重置所有外设时钟(可选,根据需要)
    // RCU_APB1RST = 0xFFFFFFFF;
    // RCU_APB2RST = 0xFFFFFFFF;
    // RCU_APB1RST = 0;
    // RCU_APB2RST = 0;
    
    // 重置NVIC优先级分组为默认值(Group 4: 4位抢占,0位响应)
    // SCB->AIRCR = 0x05FA0000 | 0x300;  // PRIGROUP = 3
    
    // 设置向量表偏移
    SCB->VTOR = app_addr;
    
    // 设置主栈指针MSP
    __set_MSP(*(volatile uint32_t*)app_addr);
    
    // 内存屏障,确保之前的操作完成
    __DSB();
    __ISB();
    
    // 跳转到APP的复位向量
    app_reset_handler();
    
    // 理论上不会执行到这里
    while(1);
}

int main(void)
{
    // 延时一段时间,可以在这里检查是否需要进入升级模式
    rt_thread_mdelay(1000);
    
    LOG_I("Bootloader started...");
    
    // 这里可以添加判断逻辑:
    // 1. 检查是否需要固件升级
    // 2. 检查APP是否有效
    // 3. 如果需要升级,则停留在bootloader
    // 4. 如果不需要升级,则跳转到APP
    
    // 示例: 简单延时后跳转
    rt_thread_mdelay(2000);
    
    LOG_I("No upgrade required, jumping to application...");
    
    // 方式1: 直接跳转(已实现,但可能有兼容性问题)
    jump_to_app(APP_ADDRESS);
    
    /* 方式2: 使用软件复位(更可靠,需要在复位后判断是否进入bootloader)
    // 设置标志位表示要跳转到APP
    // *((volatile uint32_t*)0x20000000) = 0x12345678; // 在SRAM起始位置写标志
    // NVIC_SystemReset(); // 软件复位
    */
    
    // 如果跳转失败,则停留在bootloader
    LOG_E("Failed to jump to application, staying in bootloader...");
    
    while (1)
    {
        rt_thread_mdelay(1000);
        LOG_W("Bootloader running...");
    }
}
