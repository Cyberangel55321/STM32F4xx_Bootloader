#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx.h"
#include "stm32_flash.h"
#include "crc32.h"
#include "magic_header.h"
#include "partition.h"

#define LOG_TAG "part"
#define LOG_LVL ELOG_LVL_INFO
#include "elog.h"

uint32_t partition_get_address(partition_t partition)
{
    switch (partition)
    {
        case PARTITION_APP0:
            return PARTITION_APP0_ADDRESS;
        case PARTITION_APP1:
            return PARTITION_APP1_ADDRESS;
        default:
            log_e("invalid partition: %d", partition);
            return 0;
    }
}

uint32_t partition_get_size(partition_t partition)
{
    switch (partition)
    {
        case PARTITION_APP0:
            return PARTITION_APP0_SIZE;
        case PARTITION_APP1:
            return PARTITION_APP1_SIZE;
        default:
            log_e("invalid partition: %d", partition);
            return 0;
    }
}

bool partition_validate(partition_t partition)
{
    // 从Magic Header获取固件实际地址
    uint32_t address = magic_header_get_address_partition(partition);
    if (address == 0 || address == 0xFFFFFFFF)
    {
        log_w("partition %d: invalid firmware address", partition);
        return false;
    }

    /* 检查Magic Header是否有效 */
    /* 注意: 这里需要临时修改magic_header的地址来验证不同分区 */
    /* 或者直接读取分区起始地址的前几个字节验证 */

    /* 简单验证: 检查栈顶地址是否合理 (在RAM范围内) */
    uint32_t stack_top = *(volatile uint32_t *)address;
    if (stack_top < 0x20000000 || stack_top > 0x20020000)
    {
        log_w("partition %d: invalid stack top 0x%08X", partition, stack_top);
        return false;
    }

    /* 检查复位向量是否合理 (在Flash范围内) */
    uint32_t reset_vector = *(volatile uint32_t *)(address + 4);
    if (reset_vector < 0x08000000 || reset_vector >= 0x08080000)
    {
        log_w("partition %d: invalid reset vector 0x%08X", partition, reset_vector);
        return false;
    }

    return true;
}

bool partition_validate_crc32(partition_t partition, uint32_t expected_crc, uint32_t expected_size)
{
    uint32_t address = partition_get_address(partition);
    uint32_t max_size = partition_get_size(partition);

    if (address == 0)
    {
        return false;
    }

    if (expected_size > max_size)
    {
        log_e("size %u exceeds partition max %u", expected_size, max_size);
        return false;
    }

    uint32_t calculated_crc = crc32((uint8_t *)address, expected_size);
    if (calculated_crc != expected_crc)
    {
        log_e("crc mismatch: expected 0x%08X, got 0x%08X", expected_crc, calculated_crc);
        return false;
    }

    return true;
}

bool partition_erase(partition_t partition)
{
    uint32_t address = partition_get_address(partition);
    uint32_t size = partition_get_size(partition);

    if (address == 0)
    {
        return false;
    }

    log_i("erasing partition %d at 0x%08X, size %u", partition, address, size);

    stm32_flash_unlock();
    stm32_flash_erase(address, size);
    stm32_flash_lock();

    return true;
}

bool partition_erase_range(uint32_t address, uint32_t size)
{
    /* 验证地址范围在Flash内 */
    if (address < STM32_FLASH_BASE || address + size > STM32_FLASH_BASE + STM32_FLASH_SIZE)
    {
        log_e("address 0x%08X out of range", address);
        return false;
    }

    /* 保护Bootloader区域 */
    if (address < 0x0800C000)
    {
        log_e("address 0x%08X is in bootloader area", address);
        return false;
    }

    log_i("erasing range at 0x%08X, size %u", address, size);

    stm32_flash_unlock();
    stm32_flash_erase(address, size);
    stm32_flash_lock();

    return true;
}

partition_t partition_get_download_target(const boot_config_t *config)
{
    if (config == NULL)
    {
        return PARTITION_APP1; // 默认下载到APP1
    }

    /* 下载目标是当前非活动分区 */
    partition_t active = boot_config_get_active_partition(config);
    return partition_get_opposite(active);
}

partition_t partition_get_opposite(partition_t partition)
{
    if (partition == PARTITION_APP0)
    {
        return PARTITION_APP1;
    }
    else
    {
        return PARTITION_APP0;
    }
}
