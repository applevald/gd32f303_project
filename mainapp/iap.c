/*
 * @file iap.c
 * @brief IAP (In-Application Programming) 模块实现
 */

#include "iap.h"
#include "protocol.h"
#include <string.h>
#include "gd32f30x.h"
#include "gd32f30x_fmc.h"

/* 调试开关 */
#define IAP_DEBUG_VERBOSE  0

/* Flash操作超时时间 */
#define FLASH_TIMEOUT_MS   5000

/* IAP全局上下文 */
static iap_context_t g_iap_ctx;

/* 配置区域地址 */
#define CONFIG_BASE        ((volatile uint32_t *)CONFIG_ADDR)

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
 * @brief 等待Flash操作完成
 * @return 0: 成功, -1: 超时
 */
static int flash_wait_ready(void)
{
    uint32_t timeout = FLASH_TIMEOUT_MS;
    
    while ((fmc_flag_get(FMC_FLAG_BANK0_BUSY) != RESET) && timeout > 0)
    {
        rt_thread_mdelay(1);
        timeout--;
    }
    
    return (timeout > 0) ? 0 : -1;
}

/**
 * @brief 擦除指定页
 * @param addr: 起始地址
 * @return 0: 成功, 其他: 失败
 */
static int flash_erase_page(uint32_t addr)
{
    fmc_state_enum status;
    
    flash_unlock();
    
    /* 清除标志 */
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    
    /* 执行页擦除 */
    status = fmc_page_erase(addr);
    
    flash_lock();
    
    return (status == FMC_READY) ? 0 : -1;
}

/**
 * @brief 写入一个字（4字节）
 * @param addr: 目标地址
 * @param data: 数据
 * @return 0: 成功, 其他: 失败
 */
static int flash_write_word(uint32_t addr, uint32_t data)
{
    fmc_state_enum status;
    
    flash_unlock();
    
    /* 清除标志 */
    fmc_flag_clear(FMC_FLAG_BANK0_END);
    fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
    fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
    
    /* 执行编程 */
    status = fmc_word_program(addr, data);
    
    flash_lock();
    
    return (status == FMC_READY) ? 0 : -1;
}

/**
 * @brief 计算CRC16 (Modbus)
 * @param data: 数据指针
 * @param len: 数据长度
 * @return CRC16值
 */
uint16_t iap_calculate_crc16(uint8_t *data, uint32_t len)
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
 * @brief 设置升级标志
 * @note  会保留配置区中的固件大小和CRC信息
 */
void iap_set_upgrade_flag(uint32_t flag)
{
    /* 从IAP上下文获取固件信息（这些是新固件的正确值）*/
    uint32_t firmware_size = g_iap_ctx.firmware_size;
    uint16_t firmware_crc = (uint16_t)g_iap_ctx.firmware_crc;
    
    rt_kprintf("[IAP] Setting upgrade flag with firmware info: size=%d, crc=0x%04X\n", 
               firmware_size, firmware_crc);
    
    /* 擦除配置页 */
    flash_erase_page(CONFIG_ADDR);
    
    /* 写入升级标志 */
    flash_write_word(CONFIG_ADDR + CONFIG_OFFSET_UPGRADE_FLAG, flag);
    
    /* 写入固件大小和CRC（使用新固件的信息）*/
    flash_write_word(CONFIG_ADDR + CONFIG_OFFSET_FIRMWARE_SIZE, firmware_size);
    flash_write_word(CONFIG_ADDR + CONFIG_OFFSET_FIRMWARE_CRC, firmware_crc);
    
#if IAP_DEBUG_VERBOSE
    rt_kprintf("[IAP] Set upgrade flag: 0x%08X\n", flag);
#endif
}

/**
 * @brief 获取升级标志
 */
uint32_t iap_get_upgrade_flag(void)
{
    return *((volatile uint32_t*)(CONFIG_ADDR + CONFIG_OFFSET_UPGRADE_FLAG));
}

/**
 * @brief 设置固件信息到配置区
 */
static void iap_set_firmware_info(uint32_t size, uint16_t crc)
{
    /* 写入固件大小 */
    flash_write_word(CONFIG_ADDR + CONFIG_OFFSET_FIRMWARE_SIZE, size);
    
    /* 写入固件CRC */
    flash_write_word(CONFIG_ADDR + CONFIG_OFFSET_FIRMWARE_CRC, crc);
}

/**
 * @brief 初始化IAP模块
 */
int iap_init(void)
{
    rt_memset(&g_iap_ctx, 0, sizeof(g_iap_ctx));
    g_iap_ctx.state = IAP_STATE_IDLE;
    
    rt_kprintf("[IAP] Module initialized\n");
    rt_kprintf("[IAP] APP addr: 0x%08X, Backup addr: 0x%08X\n", APP_ADDR, BACKUP_ADDR);
    
    return RT_EOK;
}

/**
 * @brief 擦除备用区域
 */
int iap_erase_backup_region(void)
{
    uint32_t addr;
    uint32_t pages;
    int ret = 0;
    
    /* 计算需要擦除的页数 */
    /* GD32F303 页大小为2KB */
    pages = (g_iap_ctx.firmware_size + 2047) / 2048;
    if (pages == 0) pages = 1;
    
#if IAP_DEBUG_VERBOSE
    rt_kprintf("[IAP] Erasing %d pages starting from 0x%08X\n", pages, BACKUP_ADDR);
#endif
    
    for (uint32_t i = 0; i < pages; i++)
    {
        addr = BACKUP_ADDR + i * 2048;
        
        if (flash_erase_page(addr) != 0)
        {
            rt_kprintf("[IAP] Erase failed at 0x%08X\n", addr);
            return -1;
        }
        
        /* 每擦除10页打印一次进度 */
        if ((i % 10) == 0)
        {
            rt_kprintf("[IAP] Erase progress: %d/%d pages\n", i, pages);
        }
    }
    
    rt_kprintf("[IAP] Erase complete: %d pages\n", pages);
    return 0;
}

/**
 * @brief 写入固件数据到备用区域
 */
int iap_write_firmware(uint32_t offset, uint8_t *data, uint32_t len)
{
    uint32_t addr;
    uint32_t *word_ptr;
    uint32_t word_data;
    
    /* 检查偏移是否超出范围 */
    if (offset + len > BACKUP_SIZE)
    {
        rt_kprintf("[IAP] Write offset out of range: %d + %d > %d\n", offset, len, BACKUP_SIZE);
        return -1;
    }
    
    addr = BACKUP_ADDR + offset;
    
    /* 按字（4字节）写入 */
    for (uint32_t i = 0; i < len; i += 4)
    {
        /* 组装4字节数据 */
        word_ptr = (uint32_t*)(data + i);
        word_data = *word_ptr;
        
        /* 如果不是4字节对齐的最后一部分，需要补0 */
        if (i + 4 > len)
        {
            uint8_t temp[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            uint32_t remain = len - i;
            rt_memcpy(temp, data + i, remain);
            word_data = *((uint32_t*)temp);
        }
        
        if (flash_write_word(addr + i, word_data) != 0)
        {
            rt_kprintf("[IAP] Write failed at 0x%08X\n", addr + i);
            return -1;
        }
    }
    
    return 0;
}

/**
 * @brief 验证固件CRC
 */
int iap_verify_firmware(uint32_t size, uint16_t expected_crc)
{
    uint16_t calculated_crc;
    
    calculated_crc = iap_calculate_crc16((uint8_t*)BACKUP_ADDR, size);
    
#if IAP_DEBUG_VERBOSE
    rt_kprintf("[IAP] CRC check: calc=0x%04X, expected=0x%04X\n", calculated_crc, expected_crc);
#endif
    
    return (calculated_crc == expected_crc) ? 0 : -1;
}

/**
 * @brief 预处理IAP请求命令 (0xAA) - 仅验证参数和初始化上下文，不执行擦除
 */
iap_result_t iap_prepare_request(uint8_t *data, uint16_t len)
{
    uint32_t firmware_size;
    uint16_t packet_size;
    uint16_t total_packets;
    uint16_t firmware_crc;
    
    /* 检查数据长度 */
    if (len < 10)
    {
        rt_kprintf("[IAP] Invalid request length: %d\n", len);
        return IAP_RESULT_INVALID;
    }
    
    /* 解析参数（大端字节序） */
    firmware_size = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    packet_size = ((uint16_t)data[4] << 8) | data[5];
    total_packets = ((uint16_t)data[6] << 8) | data[7];
    firmware_crc = ((uint16_t)data[8] << 8) | data[9];
    
    /* 检查固件大小是否合法 */
    if (firmware_size == 0 || firmware_size > BACKUP_SIZE)
    {
        rt_kprintf("[IAP] Invalid firmware size: %d (max %d)\n", firmware_size, BACKUP_SIZE);
        return IAP_RESULT_INVALID;
    }
    
    rt_kprintf("[IAP] Upgrade request: size=%d, packets=%d, crc=0x%04X\n", 
               firmware_size, total_packets, firmware_crc);
    
    /* 初始化IAP上下文 */
    rt_memset(&g_iap_ctx, 0, sizeof(g_iap_ctx));
    g_iap_ctx.state = IAP_STATE_RECEIVING;
    g_iap_ctx.firmware_size = firmware_size;
    g_iap_ctx.firmware_crc = firmware_crc;
    g_iap_ctx.total_packets = total_packets;
    g_iap_ctx.received_packets = 0;
    g_iap_ctx.current_offset = 0;
    g_iap_ctx.last_packet_time = rt_tick_get();
    
    return IAP_RESULT_ACCEPT;
}

/**
 * @brief 开始擦除备用区域（在iap_prepare_request成功后调用）
 */
int iap_start_erase(void)
{
    /* 擦除备用区域 */
    rt_kprintf("[IAP] Erasing backup region...\n");
    if (iap_erase_backup_region() != 0)
    {
        g_iap_ctx.state = IAP_STATE_ERROR;
        return -1;
    }
    
    rt_kprintf("[IAP] Ready to receive firmware\n");
    
    return 0;
}

/**
 * @brief 处理IAP请求命令 (0xAA) - 完整处理（验证+擦除）
 * @note 此函数会阻塞执行擦除，建议使用 iap_prepare_request + iap_start_erase 分离
 */
iap_result_t iap_handle_request(uint8_t *data, uint16_t len)
{
    iap_result_t result = iap_prepare_request(data, len);
    
    if (result != IAP_RESULT_ACCEPT)
    {
        return result;
    }
    
    /* 执行擦除 */
    if (iap_start_erase() != 0)
    {
        return IAP_RESULT_ERASE_FAIL;
    }
    
    return IAP_RESULT_ACCEPT;
}

/**
 * @brief 处理IAP数据包命令 (0xAB)
 */
iap_result_t iap_handle_packet(uint8_t *data, uint16_t len, uint16_t *next_seq)
{
    uint16_t packet_seq;
    uint8_t *packet_data;
    uint16_t packet_len;
    
    /* 检查状态 */
    if (g_iap_ctx.state != IAP_STATE_RECEIVING)
    {
        rt_kprintf("[IAP] Not in receiving state\n");
        return IAP_RESULT_INVALID;
    }
    
    /* 检查数据长度 */
    if (len < 258)
    {
        rt_kprintf("[IAP] Packet too short: %d\n", len);
        return IAP_RESULT_INVALID;
    }
    
    /* 解析包序列号（大端字节序） */
    packet_seq = ((uint16_t)data[0] << 8) | data[1];
    packet_data = &data[2];
    packet_len = IAP_PACKET_SIZE;  /* 固定256字节 */
    
    /* 检查序列号 */
    if (packet_seq != g_iap_ctx.received_packets)
    {
        rt_kprintf("[IAP] Sequence mismatch: expected %d, got %d\n", 
                   g_iap_ctx.received_packets, packet_seq);
        /* 返回期望的包序号 */
        *next_seq = g_iap_ctx.received_packets;
        return IAP_RESULT_ACCEPT;
    }
    
    /* 写入数据 */
    if (iap_write_firmware(g_iap_ctx.current_offset, packet_data, packet_len) != 0)
    {
        g_iap_ctx.state = IAP_STATE_ERROR;
        return IAP_RESULT_WRITE_FAIL;
    }
    
    /* 更新状态 */
    g_iap_ctx.received_packets++;
    g_iap_ctx.current_offset += packet_len;
    g_iap_ctx.last_packet_time = rt_tick_get();
    
    /* 打印进度 */
    if ((g_iap_ctx.received_packets % 10) == 0 || 
        g_iap_ctx.received_packets == g_iap_ctx.total_packets)
    {
        rt_kprintf("[IAP] Progress: %d/%d packets (%d%%)\n", 
                   g_iap_ctx.received_packets, g_iap_ctx.total_packets,
                   g_iap_ctx.received_packets * 100 / g_iap_ctx.total_packets);
    }
    
    /* 检查是否接收完成 */
    if (g_iap_ctx.received_packets >= g_iap_ctx.total_packets)
    {
        rt_kprintf("[IAP] All packets received, verifying...\n");
        
        /* 验证CRC */
        if (iap_verify_firmware(g_iap_ctx.firmware_size, (uint16_t)g_iap_ctx.firmware_crc) != 0)
        {
            rt_kprintf("[IAP] CRC verification failed!\n");
            g_iap_ctx.state = IAP_STATE_ERROR;
            return IAP_RESULT_CRC_FAIL;
        }
        
        /* 保存固件信息并设置升级标志 */
        iap_set_firmware_info(g_iap_ctx.firmware_size, (uint16_t)g_iap_ctx.firmware_crc);
        
        rt_kprintf("[IAP] Firmware verified, upgrade flag set\n");
        g_iap_ctx.state = IAP_STATE_READY_UPGRADE;
        
        /* 返回升级完成 */
        return IAP_RESULT_COMPLETE;
    }
    
    /* 返回下一包序列号 */
    *next_seq = g_iap_ctx.received_packets;
    
    return IAP_RESULT_ACCEPT;
}

/**
 * @brief 获取IAP上下文
 */
iap_context_t* iap_get_context(void)
{
    return &g_iap_ctx;
}

/**
 * @brief 触发升级（重启进入bootloader进行升级）
 */
void iap_trigger_upgrade(void)
{
    rt_kprintf("[IAP] Triggering upgrade, system will restart...\n");
    
    /* 设置升级标志 */
    iap_set_upgrade_flag(UPGRADE_FLAG_REQUEST);
    
    /* 延时确保串口输出完成 */
    rt_thread_mdelay(100);
    
    /* 触发软件复位 */
    NVIC_SystemReset();
}

/**
 * @brief 测试升级命令（用于Shell测试）
 */
void iap_test_upgrade(void)
{
    rt_kprintf("[IAP] Test upgrade triggered\n");
    rt_kprintf("[IAP] This will set upgrade flag and restart system\n");
    rt_kprintf("[IAP] Bootloader will copy firmware from backup to APP region\n");
    
    iap_trigger_upgrade();
}

/* 自动初始化 */
INIT_APP_EXPORT(iap_init);

/* Shell命令：触发升级测试 */
static void cmd_iap_upgrade(int argc, char **argv)
{
    rt_kprintf("=== IAP Upgrade Test ===\n");
    rt_kprintf("This command will:\n");
    rt_kprintf("1. Set upgrade flag in config region\n");
    rt_kprintf("2. Restart system\n");
    rt_kprintf("3. Bootloader will copy backup firmware to APP region\n");
    rt_kprintf("\n");
    rt_kprintf("Current upgrade flag: 0x%08X\n", iap_get_upgrade_flag());
    rt_kprintf("\n");
    rt_kprintf("Type 'iap_upgrade confirm' to proceed\n");
    
    if (argc >= 2 && rt_strcmp(argv[1], "confirm") == 0)
    {
        iap_test_upgrade();
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_iap_upgrade, iap_upgrade, Trigger IAP upgrade test);

/* Shell命令：查看IAP状态 */
static void cmd_iap_status(int argc, char **argv)
{
    rt_kprintf("=== IAP Status ===\n");
    rt_kprintf("State: %d\n", g_iap_ctx.state);
    rt_kprintf("Firmware size: %d bytes\n", g_iap_ctx.firmware_size);
    rt_kprintf("Total packets: %d\n", g_iap_ctx.total_packets);
    rt_kprintf("Received packets: %d\n", g_iap_ctx.received_packets);
    rt_kprintf("Current offset: 0x%08X\n", g_iap_ctx.current_offset);
    rt_kprintf("Expected CRC: 0x%04X\n", (uint16_t)g_iap_ctx.firmware_crc);
    rt_kprintf("\n");
    rt_kprintf("Upgrade flag: 0x%08X\n", iap_get_upgrade_flag());
    rt_kprintf("\n");
    rt_kprintf("Flash layout:\n");
    rt_kprintf("  Bootloader: 0x%08X - 0x%08X (%dKB)\n", BOOTLOADER_ADDR, BOOTLOADER_ADDR + BOOTLOADER_SIZE - 1, BOOTLOADER_SIZE/1024);
    rt_kprintf("  Config:     0x%08X - 0x%08X (%dKB)\n", CONFIG_ADDR, CONFIG_ADDR + CONFIG_SIZE - 1, CONFIG_SIZE/1024);
    rt_kprintf("  APP:        0x%08X - 0x%08X (%dKB)\n", APP_ADDR, APP_ADDR + APP_SIZE - 1, APP_SIZE/1024);
    rt_kprintf("  Backup:     0x%08X - 0x%08X (%dKB)\n", BACKUP_ADDR, BACKUP_ADDR + BACKUP_SIZE - 1, BACKUP_SIZE/1024);
}
MSH_CMD_EXPORT_ALIAS(cmd_iap_status, iap_status, Show IAP status);

/* Shell命令：复制当前APP区到备用区 */
static void cmd_iap_backup(int argc, char **argv)
{
    rt_kprintf("=== IAP Backup: Copy APP -> Backup ===\n");
    rt_kprintf("APP    region: 0x%08X - 0x%08X (%dKB)\n",
               APP_ADDR, APP_ADDR + APP_SIZE - 1, APP_SIZE / 1024);
    rt_kprintf("Backup region: 0x%08X - 0x%08X (%dKB)\n",
               BACKUP_ADDR, BACKUP_ADDR + BACKUP_SIZE - 1, BACKUP_SIZE / 1024);
    rt_kprintf("\n");

    if (argc < 2 || rt_strcmp(argv[1], "confirm") != 0)
    {
        rt_kprintf("WARNING: This will ERASE the backup region and copy current APP into it!\n");
        rt_kprintf("Type 'iap_backup confirm' to proceed\n");
        return;
    }

    /* 1. 擦除备用区（按页逐页擦除，GD32F303 页大小2KB） */
    rt_kprintf("[IAP] Step 1: Erasing backup region (%d pages)...\n", BACKUP_SIZE / 2048);
    uint32_t pages = BACKUP_SIZE / 2048;
    for (uint32_t i = 0; i < pages; i++)
    {
        uint32_t erase_addr = BACKUP_ADDR + i * 2048;
        if (flash_erase_page(erase_addr) != 0)
        {
            rt_kprintf("[IAP] Erase FAILED at 0x%08X, page %d\n", erase_addr, i);
            return;
        }
        if ((i % 10) == 0)
        {
            rt_kprintf("[IAP] Erase progress: %d/%d pages\n", i, pages);
        }
    }
    rt_kprintf("[IAP] Erase done.\n");

    /* 2. 按字（4字节）复制APP区到备用区 */
    rt_kprintf("[IAP] Step 2: Copying APP to backup...\n");
    uint32_t total_words = APP_SIZE / 4;
    for (uint32_t i = 0; i < total_words; i++)
    {
        uint32_t src_addr  = APP_ADDR   + i * 4;
        uint32_t dst_addr  = BACKUP_ADDR + i * 4;
        uint32_t word_data = *((volatile uint32_t *)src_addr);

        if (flash_write_word(dst_addr, word_data) != 0)
        {
            rt_kprintf("[IAP] Write FAILED at 0x%08X (word %d)\n", dst_addr, i);
            return;
        }

        /* 每2KB打印一次进度 */
        if ((i % 512) == 0)
        {
            rt_kprintf("[IAP] Copy progress: %dKB / %dKB\n", i / 256, APP_SIZE / 1024);
        }
    }
    rt_kprintf("[IAP] Copy done.\n");

    /* 3. CRC校验：比较APP区与备用区 */
    rt_kprintf("[IAP] Step 3: Verifying...\n");
    uint16_t crc_app    = iap_calculate_crc16((uint8_t *)APP_ADDR,    APP_SIZE);
    uint16_t crc_backup = iap_calculate_crc16((uint8_t *)BACKUP_ADDR, APP_SIZE);

    if (crc_app != crc_backup)
    {
        rt_kprintf("[IAP] CRC MISMATCH! APP=0x%04X, Backup=0x%04X\n", crc_app, crc_backup);
        rt_kprintf("[IAP] Backup FAILED!\n");
        return;
    }

    /* 4. 将固件信息写入配置区，供后续 iap_upgrade 使用 */
    iap_set_firmware_info(APP_SIZE, crc_backup);

    rt_kprintf("[IAP] CRC OK: 0x%04X\n", crc_backup);
    rt_kprintf("[IAP] Backup SUCCESS! %dKB copied to backup region.\n", APP_SIZE / 1024);
    rt_kprintf("[IAP] You can now use 'iap_upgrade confirm' to restore this backup.\n");
}
MSH_CMD_EXPORT_ALIAS(cmd_iap_backup, iap_backup, Copy current APP to backup region);
