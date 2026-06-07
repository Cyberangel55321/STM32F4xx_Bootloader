#ifndef __BOOT_CONFIG_H__
#define __BOOT_CONFIG_H__

#include <stdbool.h>
#include <stdint.h>

/* 分区定义 */
typedef enum
{
    PARTITION_APP0 = 0,
    PARTITION_APP1 = 1,
} partition_t;

/* 升级状态定义 */
typedef enum
{
    UPGRADE_STATE_IDLE = 0,          // 空闲状态
    UPGRADE_STATE_DOWNLOADING = 1,   // 正在下载
    UPGRADE_STATE_DOWNLOADED = 2,    // 下载完成
    UPGRADE_STATE_VERIFIED = 3,      // 验证完成
    UPGRADE_STATE_COMMITTED = 4,     // 已提交
} upgrade_state_t;

/* Boot Config结构体 - 256字节对齐 */
typedef struct {
    uint32_t magic;                  // 魔数: 0x424F4F54 ("BOOT")
    uint32_t crc32;                  // 整个结构体的CRC32 (除magic和crc32字段外)

    uint8_t  active_partition;       // 当前活动分区: 0=APP0, 1=APP1
    uint8_t  app0_valid;             // APP0有效标志: 0=invalid, 1=valid
    uint8_t  app1_valid;             // APP1有效标志: 0=invalid, 1=valid
    uint8_t  upgrade_state;          // 升级状态: upgrade_state_t

    uint32_t app0_crc32;             // APP0固件CRC32
    uint32_t app0_size;              // APP0固件大小
    uint32_t app1_crc32;             // APP1固件CRC32
    uint32_t app1_size;              // APP1固件大小

    uint32_t download_crc32;         // 正在下载的固件CRC32
    uint32_t download_size;          // 正在下载的固件大小
    uint32_t download_address;       // 正在下载的目标地址

    uint32_t boot_count;             // 启动计数器 (可用于调试)
    uint32_t last_error;             // 最后一次错误码

    uint32_t reserved[52];            // 保留字段，确保总大小为256字节
} boot_config_t;

bool boot_config_load(boot_config_t *config);
bool boot_config_save(const boot_config_t *config);
bool boot_config_validate(const boot_config_t *config);
bool boot_config_init_default(boot_config_t *config);

partition_t boot_config_get_active_partition(const boot_config_t *config);
bool boot_config_set_active_partition(boot_config_t *config, partition_t partition);
bool boot_config_is_partition_valid(const boot_config_t *config, partition_t partition);
bool boot_config_set_partition_valid(boot_config_t *config, partition_t partition, bool valid);

upgrade_state_t boot_config_get_upgrade_state(const boot_config_t *config);
bool boot_config_set_upgrade_state(boot_config_t *config, upgrade_state_t state);

#endif /* __BOOT_CONFIG_H__ */
