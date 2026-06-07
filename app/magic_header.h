#ifndef __MAGIC_HEADER_H__
#define __MAGIC_HEADER_H__

#include <stdbool.h>
#include <stdint.h>
#include "boot_config.h"

typedef enum
{
    MAGIC_HEADER_TYPE_APP = 0,
} magic_header_type_t;

bool magic_header_validate_partition(partition_t partition);
bool magic_header_validate(void);
magic_header_type_t magic_header_get_type_partition(partition_t partition);
uint32_t magic_header_get_offset_partition(partition_t partition);
uint32_t magic_header_get_address_partition(partition_t partition);
uint32_t magic_header_get_length_partition(partition_t partition);
uint32_t magic_header_get_crc32_partition(partition_t partition);

#endif /* __MAGIC_HEADER_H__ */
