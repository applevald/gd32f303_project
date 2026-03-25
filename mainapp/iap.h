/*
 * @file iap.h
 * @brief IAP (In-Application Programming) 模块 - 固件升级功能
 * 
 * Flash布局 (GD32F303xC, 256KB, 页大小2KB):
 * +----------------+----------------+------------------+------------------+
 * | 区域           | 地址范围       | 大小             | 页范围           |
 * +----------------+----------------+------------------+------------------+
 * | Bootloader     | 0x08000000 -   | 48KB (24页)      | 页0  ~ 页23      |
 * |                | 0x0800BFFF     |                  |                  |
 * +----------------+----------------+------------------+------------------+
 * | 配置区域       | 0x0800C000 -   | 2KB  (1页)       | 页24             |
 * | (升级标志等)   | 0x0800C7FF     |                  |                  |
 * +----------------+----------------+------------------+------------------+
 * | APP区域        | 0x0800C800 -   | 102KB (51页)     | 页25 ~ 页75      |
 * |                | 0x08025FFF     |                  |                  |
 * +----------------+----------------+------------------+------------------+
 * | 备用区域       | 0x08026000 -   | 104KB (52页)     | 页76 ~ 页127     |
 * | (新固件存放)   | 0x0803FFFF     |                  |                  |
 * +----------------+----------------+------------------+------------------+
 * 总计: 48+2+102+104 = 256KB，所有边界均2KB页对齐
 */

#ifndef __IAP_H__
#define __IAP_H__

#include <rtthread.h>
#include <stdint.h>

/* Flash地址定义 */
#define FLASH_BASE_ADDR        0x08000000

#define BOOTLOADER_ADDR        0x08000000          /* Bootloader起始地址 */
#define BOOTLOADER_SIZE        (48 * 1024)         /* 48KB */

#define CONFIG_ADDR            0x0800C000          /* 配置区域起始地址 */
#define CONFIG_SIZE            (2 * 1024)          /* 2KB */

#define APP_ADDR               0x0800C800          /* APP起始地址 (第25页起始) */
#define APP_SIZE               (102 * 1024)        /* 102KB (51页, 2KB页对齐) */
#define APP_END_ADDR           0x08025FFF          /* APP结束地址 (第75页末尾) */

#define BACKUP_ADDR            0x08026000          /* 备用区域起始地址 (第76页起始) */
#define BACKUP_SIZE            (104 * 1024)        /* 104KB (52页, 2KB页对齐) */

/* 配置区域偏移 */
#define CONFIG_OFFSET_UPGRADE_FLAG    0x000        /* 升级标志偏移 */
#define CONFIG_OFFSET_FIRMWARE_SIZE   0x004        /* 固件大小偏移 */
#define CONFIG_OFFSET_FIRMWARE_CRC    0x008        /* 固件CRC偏移 */
#define CONFIG_OFFSET_APP_VALID       0x00C        /* APP有效标志偏移 */

/* 升级标志值 */
#define UPGRADE_FLAG_NONE      0x00000000         /* 无升级请求 */
#define UPGRADE_FLAG_REQUEST   0x5A5A5A5A         /* 升级请求 */
#define UPGRADE_FLAG_DONE      0xA5A5A5A5         /* 升级完成 */
#define UPGRADE_FLAG_ERROR     0xFFFFFFFF         /* 升级错误 */

/* APP有效标志值 */
#define APP_VALID_FLAG         0x55AA55AA         /* APP有效 */
#define APP_INVALID_FLAG       0x00000000         /* APP无效 */

/* IAP数据包大小 */
#define IAP_PACKET_SIZE        256                /* 每包256字节 */
#define IAP_MAX_PACKETS        1024               /* 最大包数 (256KB / 256 = 1024) */

/* IAP状态 */
typedef enum {
    IAP_STATE_IDLE = 0,          /* 空闲状态 */
    IAP_STATE_RECEIVING,         /* 接收固件中 */
    IAP_STATE_VERIFYING,         /* 校验中 */
    IAP_STATE_READY_UPGRADE,     /* 准备升级 */
    IAP_STATE_ERROR              /* 错误状态 */
} iap_state_t;

/* IAP结果码 */
typedef enum {
    IAP_RESULT_ACCEPT = 0,       /* 接收升级请求 */
    IAP_RESULT_COMPLETE = 1,     /* 升级完成 */
    IAP_RESULT_ERASE_FAIL = 2,   /* 擦除失败 */
    IAP_RESULT_CRC_FAIL = 3,     /* 校验失败 */
    IAP_RESULT_WRITE_FAIL = 4,   /* 写入失败 */
    IAP_RESULT_TIMEOUT = 5,      /* 超时 */
    IAP_RESULT_INVALID = 6       /* 无效请求 */
} iap_result_t;

/* IAP上下文结构 */
typedef struct {
    iap_state_t state;                    /* 当前状态 */
    uint32_t firmware_size;               /* 固件总大小 */
    uint32_t firmware_crc;                /* 固件CRC16校验值 */
    uint32_t total_packets;               /* 总包数 */
    uint32_t received_packets;            /* 已接收包数 */
    uint32_t current_offset;              /* 当前写入偏移 */
    uint32_t last_packet_time;            /* 最后收包时间 */
    uint8_t packet_buf[IAP_PACKET_SIZE];  /* 数据包缓冲区 */
} iap_context_t;

/**
 * @brief 初始化IAP模块
 * @return RT_EOK: 成功, 其他: 失败
 */
int iap_init(void);

/**
 * @brief 处理IAP请求命令 (0xAA)
 * @param data: 命令数据指针
 * @param len: 数据长度
 * @return IAP结果码
 */
iap_result_t iap_handle_request(uint8_t *data, uint16_t len);

/**
 * @brief 处理IAP数据包命令 (0xAB)
 * @param data: 命令数据指针
 * @param len: 数据长度
 * @param[out] next_seq: 下一包序列号
 * @return IAP结果码
 */
iap_result_t iap_handle_packet(uint8_t *data, uint16_t len, uint16_t *next_seq);

/**
 * @brief 获取IAP上下文
 * @return IAP上下文指针
 */
iap_context_t* iap_get_context(void);

/**
 * @brief 设置升级标志
 * @param flag: 升级标志值
 */
void iap_set_upgrade_flag(uint32_t flag);

/**
 * @brief 获取升级标志
 * @return 升级标志值
 */
uint32_t iap_get_upgrade_flag(void);

/**
 * @brief 触发升级（重启进入bootloader进行升级）
 */
void iap_trigger_upgrade(void);

/**
 * @brief 计算CRC16
 * @param data: 数据指针
 * @param len: 数据长度
 * @return CRC16值
 */
uint16_t iap_calculate_crc16(uint8_t *data, uint32_t len);

/**
 * @brief 擦除备用区域
 * @return 0: 成功, 其他: 失败
 */
int iap_erase_backup_region(void);

/**
 * @brief 写入固件数据到备用区域
 * @param offset: 偏移地址
 * @param data: 数据指针
 * @param len: 数据长度
 * @return 0: 成功, 其他: 失败
 */
int iap_write_firmware(uint32_t offset, uint8_t *data, uint32_t len);

/**
 * @brief 验证固件CRC
 * @param size: 固件大小
 * @param expected_crc: 期望的CRC值
 * @return 0: 校验通过, 其他: 校验失败
 */
int iap_verify_firmware(uint32_t size, uint16_t expected_crc);

/**
 * @brief 触发一次测试升级（用于测试）
 */
void iap_test_upgrade(void);

#endif /* __IAP_H__ */
