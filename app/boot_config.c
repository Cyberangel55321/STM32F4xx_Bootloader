#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "stm32f4xx.h"
#include "stm32_flash.h"
#include "crc32.h"
#include "boot_config.h"

#define LOG_TAG    "boot"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

#define BOOT_CONFIG_MAGIC       0x424F4F54  // "BOOT"
#define BOOT_CONFIG_SIZE        256
#define BOOT_CONFIG_ADDRESS     0x0800C000

/* CRC32计算范围: 从active_partition字段开始到最后 */
#define BOOT_CONFIG_CRC_OFFSET  8  // 跳过magic和crc32字段

static uint32_t boot_config_calc_crc32(const boot_config_t *config)
{
    /* 计算从active_partition字段到结构体末尾的CRC32 */
    const uint8_t *data = (const uint8_t *)config + BOOT_CONFIG_CRC_OFFSET;
    uint32_t length = BOOT_CONFIG_SIZE - BOOT_CONFIG_CRC_OFFSET;
    return crc32(data, length);
}

bool boot_config_validate(const boot_config_t *config)
{
    if (config == NULL) 
    {
        log_e("config is NULL");
        return false;
    }

    /* 验证魔数 */
    if (config->magic != BOOT_CONFIG_MAGIC)
    {
        log_w("invalid magic: 0x%08X", config->magic);
        return false;
    }

    /* 验证CRC32 */
    uint32_t calculated_crc = boot_config_calc_crc32(config);
    if (config->crc32 != calculated_crc)
    {
        log_w("crc mismatch: expected 0x%08X, got 0x%08X", config->crc32, calculated_crc);
        return false;
    }

    /* 验证active_partition */
    if (config->active_partition != PARTITION_APP0 && 
        config->active_partition != PARTITION_APP1)
    {
        log_w("invalid active_partition: %d", config->active_partition);
        return false;
    }

    /* 验证upgrade_state */
    if (config->upgrade_state > UPGRADE_STATE_COMMITTED)
    {
        log_w("invalid upgrade_state: %d", config->upgrade_state);
        return false;
    }

    return true;
}

bool boot_config_load(boot_config_t *config)
{
    if (config == NULL) 
    {
        log_e("config is NULL");
        return false;
    }

    /* 从Flash读取 */
    memcpy(config, (void *)BOOT_CONFIG_ADDRESS, BOOT_CONFIG_SIZE);

    /* 验证配置有效性 */
    if (!boot_config_validate(config)) 
    {
        log_w("config invalid, initializing default");
        return boot_config_init_default(config);
    }

    log_i("config loaded: active=%s, state=%d", 
          config->active_partition == PARTITION_APP0 ? "APP0" : "APP1",
          config->upgrade_state);
    
    return true;
}

bool boot_config_save(const boot_config_t *config)
{
    if (config == NULL) 
    {
        log_e("config is NULL");
        return false;
    }

    /* 创建可修改的副本 */
    boot_config_t config_copy;
    memcpy(&config_copy, config, BOOT_CONFIG_SIZE);

    /* 更新CRC32 */
    config_copy.crc32 = boot_config_calc_crc32(&config_copy);

    /* 擦除Config扇区 */
    stm32_flash_unlock();
    stm32_flash_erase(BOOT_CONFIG_ADDRESS, BOOT_CONFIG_SIZE);

    /* 写入Flash */
    stm32_flash_program(BOOT_CONFIG_ADDRESS, (const uint8_t *)&config_copy, BOOT_CONFIG_SIZE);
    stm32_flash_lock();

    /* 验证写入 */
    boot_config_t verify_config;
    memcpy(&verify_config, (void *)BOOT_CONFIG_ADDRESS, BOOT_CONFIG_SIZE);
    
    if (memcmp(&config_copy, &verify_config, BOOT_CONFIG_SIZE) != 0)
    {
        log_e("config save verification failed");
        return false;
    }

    log_i("config saved successfully");
    return true;
}

bool boot_config_init_default(boot_config_t *config)
{
    if (config == NULL) 
    {
        log_e("config is NULL");
        return false;
    }

    memset(config, 0, BOOT_CONFIG_SIZE);
    
    config->magic = BOOT_CONFIG_MAGIC;
    config->active_partition = PARTITION_APP0;
    config->app0_valid = 1;  // 假设APP0初始有效
    config->app1_valid = 0;
    config->upgrade_state = UPGRADE_STATE_IDLE;
    config->boot_count = 0;
    config->last_error = 0;

    /* 计算CRC32 */
    config->crc32 = boot_config_calc_crc32(config);

    log_i("default config initialized");
    return true;
}

partition_t boot_config_get_active_partition(const boot_config_t *config)
{
    if (config == NULL) 
    {
        return PARTITION_APP0;  // 默认APP0
    }

    return (partition_t)config->active_partition;
}

bool boot_config_set_active_partition(boot_config_t *config, partition_t partition)
{
    if (config == NULL) 
    {
        log_e("config is NULL");
        return false;
    }

    if (partition != PARTITION_APP0 && partition != PARTITION_APP1)
    {
        log_e("invalid partition: %d", partition);
        return false;
    }

    config->active_partition = (uint8_t)partition;
    log_i("active partition set to: %d", config->active_partition);

    return true;
}

bool boot_config_is_partition_valid(const boot_config_t *config, partition_t partition)
{
    if (config == NULL) 
    {
        return false;
    }

    if (partition == PARTITION_APP0) 
    {
        return config->app0_valid != 0;
    } 
    else if (partition == PARTITION_APP1) 
    {
        return config->app1_valid != 0;
    }

    return false;
}

bool boot_config_set_partition_valid(boot_config_t *config, partition_t partition, bool valid)
{
    if (config == NULL) 
    {
        log_e("config is NULL");
        return false;
    }

    if (partition == PARTITION_APP0)
    {
        config->app0_valid = valid ? 1 : 0;
    }
    else if (partition == PARTITION_APP1)
    {
        config->app1_valid = valid ? 1 : 0;
    }
    else
    {
        log_e("invalid partition: %d", partition);
        return false;
    }

    return true;
}

upgrade_state_t boot_config_get_upgrade_state(const boot_config_t *config)
{
    if (config == NULL)
    {
        return UPGRADE_STATE_IDLE;
    }

    return (upgrade_state_t)config->upgrade_state;
}

bool boot_config_set_upgrade_state(boot_config_t *config, upgrade_state_t state)
{
    if (config == NULL)
    {
        log_e("config is NULL");
        return false;
    }

    if (state > UPGRADE_STATE_COMMITTED)
    {
        log_e("invalid upgrade state: %d", state);
        return false;
    }

    config->upgrade_state = (uint8_t)state;
    return true;
}


