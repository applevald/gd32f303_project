/*
 * @file main.c
 * @brief Bootloader主程序 - 支持IAP固件升级
 * 
 * Flash布局 (GD32F303xC, 256KB):
 * +----------------+----------------+------------------+
 * | 区域           | 地址范围       | 大小             |
 * +----------------+----------------+------------------+
 * | Bootloader     | 0x08000000 -   | 48KB             |
 * |                | 0x0800BFFF     |                  |
 * +----------------+----------------+------------------+
 * | 配置区域       | 0x0800C000 -   | 2KB              |
 * | (升级标志等)   | 0x0800C7FF     |                  |
 * +----------------+----------------+------------------+
 * | APP区域        | 0x0800C800 -   | 约103KB          |
 * |                | 0x0801FFFF     |                  |
 * +----------------+----------------+------------------+
 * | 备用区域       | 0x08020000 -   | 128KB            |
 * | (新固件存放)   | 0x0803FFFF     |                  |
 * +----------------+----------------+------------------+
 */

#include <rtthread.h>
#include "board.h"
#include "includes.h"
#include "gd32f30x.h"
#include "gd32f30x_fmc.h"
#include <rtdbg.h>

/* Flash地址定义 */
#define BOOTLOADER_ADDR        0x08000000
#define BOOTLOADER_SIZE        (48 * 1024)

#define CONFIG_ADDR            0x0800C000
#define CONFIG_SIZE            (2 * 1024)

#define APP_ADDR               0x0800C800
#define APP_SIZE               (103 * 1024)

#define BACKUP_ADDR            0x08020000
#define BACKUP_SIZE            (128 * 1024)

/* 配置区域偏移 */
#define CONFIG_OFFSET_UPGRADE_FLAG    0x000
#define CONFIG_OFFSET_FIRMWARE_SIZE   0x004
#define CONFIG_OFFSET_FIRMWARE_CRC    0x008

/* 升级标志值 */
#define UPGRADE_FLAG_NONE      0x00000000
#define UPGRADE_FLAG_REQUEST   0x5A5A5A5A
#define UPGRADE_FLAG_DONE      0xA5A5A5A5

/* Function declarations */
int rtthread_startup(void);

/**
 * @brief 读取配置区域的升级标志
 */
static uint32_t read_upgrade_flag(void)
{
    return *((volatile uint32_t*)(CONFIG_ADDR + CONFIG_OFFSET_UPGRADE_FLAG));
}

/**
 * @brief 读取配置区域的固件大小
 */
static uint32_t read_firmware_size(void)
{
    return *((volatile uint32_t*)(CONFIG_ADDR + CONFIG_OFFSET_FIRMWARE_SIZE));
}

/**
 * @brief 读取配置区域的固件CRC
 */
static uint16_t read_firmware_crc(void)
{
    return (uint16_t)(*((volatile uint32_t*)(CONFIG_ADDR + CONFIG_OFFSET_FIRMWARE_CRC)) & 0xFFFF);
}

/**
 * @brief 计算CRC16 (Modbus)
 */
static uint16_t calculate_crc16(uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

/**
 * @brief 解锁Flash
 */
static void flash_unlock(void)
{
    fmc_unlock();
}

/**
 * @brief 锁定Flash
 */
static void flash_lock(void)
{
    fmc_lock();
}

/**
 * @brief 擦除指定页
 */
static int flash_erase_page(uint32_t addr)
{
    fmc_state_enum status;
    
    flash_unlock();
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    status = fmc_page_erase(addr);
    flash_lock();
    
    return (status == FMC_READY) ? 0 : -1;
}

/**
 * @brief 写入一个字
 */
static int flash_write_word(uint32_t addr, uint32_t data)
{
    fmc_state_enum status;
    
    flash_unlock();
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    status = fmc_word_program(addr, data);
    flash_lock();
    
    return (status == FMC_READY) ? 0 : -1;
}

/**
 * @brief 清除升级标志
 */
static void clear_upgrade_flag(void)
{
    flash_erase_page(CONFIG_ADDR);
    flash_write_word(CONFIG_ADDR + CONFIG_OFFSET_UPGRADE_FLAG, UPGRADE_FLAG_DONE);
}

/**
 * @brief 检查APP是否有效
 */
static int is_app_valid(void)
{
    uint32_t app_stack_ptr;
    uint32_t app_reset_handler;
    
    /* 读取APP的栈顶指针和复位向量 */
    app_stack_ptr = *((volatile uint32_t*)APP_ADDR);
    app_reset_handler = *((volatile uint32_t*)(APP_ADDR + 4));
    
    /* 检查栈顶指针是否在SRAM范围内 */
    if ((app_stack_ptr & 0xFFFE0000) != 0x20000000)
    {
        LOG_E("Invalid APP stack pointer: 0x%08X", app_stack_ptr);
        return 0;
    }
    
    /* 检查复位向量是否在Flash范围内 */
    if (app_reset_handler < APP_ADDR || app_reset_handler > (APP_ADDR + APP_SIZE))
    {
        LOG_E("Invalid APP reset handler: 0x%08X", app_reset_handler);
        return 0;
    }
    
    return 1;
}

/**
 * @brief 检查备用区固件是否有效
 */
static int is_backup_valid(void)
{
    uint32_t firmware_size;
    uint16_t stored_crc;
    uint16_t calculated_crc;
    
    firmware_size = read_firmware_size();
    stored_crc = read_firmware_crc();
    
    if (firmware_size == 0 || firmware_size > BACKUP_SIZE)
    {
        LOG_E("Invalid firmware size: %d", firmware_size);
        return 0;
    }
    
    /* 计算CRC */
    calculated_crc = calculate_crc16((uint8_t*)BACKUP_ADDR, firmware_size);
    
    if (calculated_crc != stored_crc)
    {
        LOG_E("CRC mismatch: calc=0x%04X, stored=0x%04X", calculated_crc, stored_crc);
        return 0;
    }
    
    return 1;
}

/**
 * @brief 执行固件升级：将备用区复制到APP区
 */
static int do_firmware_upgrade(void)
{
    uint32_t firmware_size;
    uint32_t pages;
    uint32_t addr;
    uint32_t *src_ptr;
    uint32_t *dst_ptr;
    uint32_t words;
    
    LOG_I("Starting firmware upgrade...");
    
    firmware_size = read_firmware_size();
    
    /* 计算需要擦除的页数 */
    pages = (firmware_size + 2047) / 2048;
    if (pages == 0) pages = 1;
    
    LOG_I("Erasing APP region (%d pages)...", pages);
    
    /* 擦除APP区域 */
    for (uint32_t i = 0; i < pages; i++)
    {
        addr = APP_ADDR + i * 2048;
        
        if (flash_erase_page(addr) != 0)
        {
            LOG_E("Erase failed at 0x%08X", addr);
            return -1;
        }
        
        if ((i % 10) == 0)
        {
            LOG_I("Erase progress: %d/%d", i, pages);
        }
    }
    
    LOG_I("Copying firmware from backup to APP...");
    
    /* 复制固件数据 */
    src_ptr = (uint32_t*)BACKUP_ADDR;
    dst_ptr = (uint32_t*)APP_ADDR;
    words = (firmware_size + 3) / 4;  /* 向上取整到字 */
    
    for (uint32_t i = 0; i < words; i++)
    {
        if (flash_write_word((uint32_t)(dst_ptr + i), src_ptr[i]) != 0)
        {
            LOG_E("Write failed at 0x%08X", (uint32_t)(dst_ptr + i));
            return -1;
        }
        
        /* 每1KB打印一次进度 */
        if ((i % 256) == 0)
        {
            LOG_I("Copy progress: %d/%d words", i, words);
        }
    }
    
    LOG_I("Firmware upgrade completed!");
    
    return 0;
}

/**
 * @brief  跳转到应用程序
 * @param  app_addr: 应用程序起始地址
 */
static void jump_to_app(uint32_t app_addr)
{
    uint32_t jump_addr;
    void (*app_reset_handler)(void);
    
    if (!is_app_valid())
    {
        LOG_E("APP validation failed, cannot jump");
        return;
    }
    
    LOG_I("Jumping to application at 0x%08X...", app_addr);
    
    jump_addr = *((volatile uint32_t*)(app_addr + 4));
    app_reset_handler = (void (*)(void))jump_addr;
    
    rt_thread_mdelay(10);
    
    __disable_irq();
    
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    
    for (int i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFF;
        NVIC->ICPR[i] = 0xFFFFFFFF;
    }
    
    SCB->VTOR = app_addr;
    __set_MSP(*((volatile uint32_t*)app_addr));
    
    __DSB();
    __ISB();
    
    app_reset_handler();
    
    while(1);
}

int main(void)
{
    uint32_t upgrade_flag;
    
    rt_thread_mdelay(500);
    
    LOG_I("========================================");
    LOG_I("Bootloader v1.0 started");
    LOG_I("========================================");
    LOG_I("Flash layout:");
    LOG_I("  Bootloader: 0x%08X - 0x%08X", BOOTLOADER_ADDR, BOOTLOADER_ADDR + BOOTLOADER_SIZE - 1);
    LOG_I("  Config:     0x%08X - 0x%08X", CONFIG_ADDR, CONFIG_ADDR + CONFIG_SIZE - 1);
    LOG_I("  APP:        0x%08X - 0x%08X", APP_ADDR, APP_ADDR + APP_SIZE - 1);
    LOG_I("  Backup:     0x%08X - 0x%08X", BACKUP_ADDR, BACKUP_ADDR + BACKUP_SIZE - 1);
    
    /* 检查升级标志 */
    upgrade_flag = read_upgrade_flag();
    LOG_I("Upgrade flag: 0x%08X", upgrade_flag);
    
    if (upgrade_flag == UPGRADE_FLAG_REQUEST)
    {
        LOG_I("Upgrade request detected!");
        
        /* 验证备用区固件 */
        if (is_backup_valid())
        {
            LOG_I("Backup firmware is valid, starting upgrade...");
            
            /* 执行升级 */
            if (do_firmware_upgrade() == 0)
            {
                LOG_I("Upgrade successful!");
                clear_upgrade_flag();
            }
            else
            {
                LOG_E("Upgrade failed!");
                /* 升级失败，停留在bootloader */
                while (1)
                {
                    rt_thread_mdelay(1000);
                    LOG_W("Upgrade failed, staying in bootloader...");
                }
            }
        }
        else
        {
            LOG_E("Backup firmware is invalid!");
            clear_upgrade_flag();
        }
    }
    
    /* 检查APP是否有效 */
    if (is_app_valid())
    {
        LOG_I("APP is valid, jumping to application...");
        rt_thread_mdelay(100);
        jump_to_app(APP_ADDR);
    }
    else
    {
        LOG_E("APP is invalid, staying in bootloader...");
    }
    
    /* 停留在bootloader */
    LOG_W("Running in bootloader mode...");
    
    while (1)
    {
        rt_thread_mdelay(1000);
    }
}