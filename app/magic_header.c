#include <stdbool.h>
#include <stdint.h>
#include "crc32.h"
#include "utils.h"
#include "magic_header.h"
#include "partition.h"

#define MAGIC_HEADER_MAGIC 0x4D414749 // "MAGI"

/* Magic Header地址 - 完全对称布局 */
#define MAGIC_HEADER_APP0_ADDR  0x08010000  // APP0分区起始
#define MAGIC_HEADER_APP1_ADDR  0x08050000  // APP1分区起始


typedef struct
{
    uint32_t magic;         // 魔数，用于标识这是一个有效的魔术头
    uint32_t bitmask;       // 位掩码，用于标识哪些字段有效
    uint32_t reserved1[6];  // 保留字段，供将来扩展使用

    uint32_t data_type;     // 类型，根据type类型选择固件下载位置
    uint32_t data_offset;   // 固件文件相对于magic header的偏移
    uint32_t data_address;  // 固件写入的实际地址
    uint32_t data_length;   // 固件长度
    uint32_t data_crc32;    // 固件的CRC32校验值
    uint32_t reserved2[11]; // 保留字段，供将来扩展使用

    char version[128];      // 固件版本字符串

    uint32_t reserved3[6];  // 保留字段，供将来扩展使用
    uint32_t this_address;  // 该结构体在存储介质中的实际地址
    uint32_t this_crc32;    // 该结构体本身的CRC32校验值
} magic_header_t;

// const uint32_t a = sizeof(magic_header_t);

bool magic_header_validate(void)
{
    return magic_header_validate_partition(PARTITION_APP0);
}

bool magic_header_validate_partition(partition_t partition)
{
    uint32_t addr;
    if (partition == PARTITION_APP0)
    {
        addr = MAGIC_HEADER_APP0_ADDR;
    }
    else if (partition == PARTITION_APP1)
    {
        addr = MAGIC_HEADER_APP1_ADDR;
    }
    else
    {
        return false;
    }

    magic_header_t *header = (magic_header_t *)addr;

    if (header->magic != MAGIC_HEADER_MAGIC)
        return false;

    uint32_t ccrc = crc32((uint8_t *)header, offset_of(magic_header_t, this_crc32));
    if (ccrc != header->this_crc32)
        return false;

    return true;
}

magic_header_type_t magic_header_get_type_partition(partition_t partition)
{
    uint32_t addr = (partition == PARTITION_APP0) ? MAGIC_HEADER_APP0_ADDR : MAGIC_HEADER_APP1_ADDR;
    magic_header_t *header = (magic_header_t *)addr;
    return (magic_header_type_t)header->data_type;
}

uint32_t magic_header_get_offset_partition(partition_t partition)
{
    uint32_t addr = (partition == PARTITION_APP0) ? MAGIC_HEADER_APP0_ADDR : MAGIC_HEADER_APP1_ADDR;
    magic_header_t *header = (magic_header_t *)addr;
    return header->data_offset;
}

uint32_t magic_header_get_address_partition(partition_t partition)
{
    uint32_t addr = (partition == PARTITION_APP0) ? MAGIC_HEADER_APP0_ADDR : MAGIC_HEADER_APP1_ADDR;
    magic_header_t *header = (magic_header_t *)addr;
    return header->data_address;
}

uint32_t magic_header_get_length_partition(partition_t partition)
{
    uint32_t addr = (partition == PARTITION_APP0) ? MAGIC_HEADER_APP0_ADDR : MAGIC_HEADER_APP1_ADDR;
    magic_header_t *header = (magic_header_t *)addr;
    return header->data_length;
}

uint32_t magic_header_get_crc32_partition(partition_t partition)
{
    uint32_t addr = (partition == PARTITION_APP0) ? MAGIC_HEADER_APP0_ADDR : MAGIC_HEADER_APP1_ADDR;
    magic_header_t *header = (magic_header_t *)addr;
    return header->data_crc32;
}

/* 原函数兼容，默认使用APP0 */
magic_header_type_t magic_header_get_type(void) 
{ 
    return magic_header_get_type_partition(PARTITION_APP0);
}

uint32_t magic_header_get_offset(void)
{ 
    return magic_header_get_offset_partition(PARTITION_APP0);
}

uint32_t magic_header_get_address(void)
{
    return magic_header_get_address_partition(PARTITION_APP0);
}

uint32_t magic_header_get_length(void)
{
    return magic_header_get_length_partition(PARTITION_APP0);
}

uint32_t magic_header_get_crc32(void)
{
    return magic_header_get_crc32_partition(PARTITION_APP0);
}
