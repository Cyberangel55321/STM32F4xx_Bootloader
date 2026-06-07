#ifndef __PARTITION_H__
#define __PARTITION_H__

#include <stdbool.h>
#include <stdint.h>
#include "boot_config.h"

/* 分区地址和大小定义 */
#define PARTITION_APP0_ADDRESS      0x08010000
#define PARTITION_APP0_SIZE         (256 * 1024)  // APP0分区大小为256KB

#define PARTITION_APP1_ADDRESS      0x08050000
#define PARTITION_APP1_SIZE         (192 * 1024)  // APP1分区大小为192KB

/* 分区操作函数 */
uint32_t partition_get_address(partition_t partition);
uint32_t partition_get_size(partition_t partition);

bool partition_validate(partition_t partition);
bool partition_validate_crc32(partition_t partition, uint32_t expected_crc, uint32_t expected_size);

bool partition_erase(partition_t partition);
bool partition_erase_range(uint32_t address, uint32_t size);

partition_t partition_get_download_target(const boot_config_t *config);
partition_t partition_get_opposite(partition_t partition);

#endif /* __PARTITION_H__ */

