# Bootloader A/B分区升级方案修改指南

## 一、内存布局

### Flash分区 (512KB)
```
0x08000000 ┌─────────────────────┐
           │ Bootloader (48KB)   │
0x0800C000 ├─────────────────────┤
           │ Boot Config (256B)  │ ← 魔数: "BOOT" (0x424F4F54)
0x0800C100 ├─────────────────────┤
           │ Reserved            │
0x08010000 ├─────────────────────┤
           │ APP0 Magic Header   │ ← 魔数: "MAGI" (0x4D414749)
           │ (256B)              │
0x08010100 ├─────────────────────┤
           │ APP0 固件代码        │
           │ ...                 │
0x08050000 ├─────────────────────┤
           │ APP1 Magic Header   │ ← 魔数: "MAGI" (0x4D414749)
           │ (256B)              │
0x08050100 ├─────────────────────┤
           │ APP1 固件代码        │
           │ ...                 │
0x08080000 └─────────────────────┘
```

### 关键地址定义
```c
#define BOOT_CONFIG_ADDRESS         0x0800C000
#define BOOT_CONFIG_SIZE            256

#define APP0_ADDRESS                0x08010000
#define APP0_SIZE                   (256 * 1024)
#define APP0_MAGIC_HEADER_ADDRESS   0x08010000  // APP0分区起始

#define APP1_ADDRESS                0x08050000
#define APP1_SIZE                   (192 * 1024)
#define APP1_MAGIC_HEADER_ADDRESS   0x08050000  // APP1分区起始
```

**说明**: 
- 两个分区完全对称，Magic Header都在分区起始位置
- 魔数均为 `0x4D414749` ("MAGI")
- STM32F407VET6小端格式，内存存储为 `49 41 4D 47`

---

## 二、新增文件

### 文件1: `app/boot_config.h`

```c
#ifndef __BOOT_CONFIG_H__
#define __BOOT_CONFIG_H__

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PARTITION_APP0 = 0,
    PARTITION_APP1 = 1,
} partition_t;

typedef enum {
    UPGRADE_STATE_IDLE = 0,
    UPGRADE_STATE_DOWNLOADING = 1,
    UPGRADE_STATE_DOWNLOADED = 2,
    UPGRADE_STATE_VERIFIED = 3,
    UPGRADE_STATE_COMMITTED = 4,
} upgrade_state_t;

typedef struct {
    uint32_t magic;              // 0x424F4F54 ("BOOT")
    uint32_t crc32;

    uint8_t  active_partition;   // 0=APP0, 1=APP1
    uint8_t  app0_valid;
    uint8_t  app1_valid;
    uint8_t  upgrade_state;

    uint32_t app0_crc32;
    uint32_t app0_size;
    uint32_t app1_crc32;
    uint32_t app1_size;

    uint32_t download_crc32;
    uint32_t download_size;
    uint32_t download_address;

    uint32_t boot_count;
    uint32_t last_error;

    uint32_t reserved[7];
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

#endif
```

### 文件2: `app/boot_config.c`

```c
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "stm32f4xx.h"
#include "stm32_flash.h"
#include "crc32.h"
#include "boot_config.h"

#define LOG_TAG    "bconf"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

#define BOOT_CONFIG_MAGIC       0x424F4F54  // "BOOT"
#define BOOT_CONFIG_SIZE        256
#define BOOT_CONFIG_ADDRESS     0x0800C000
#define BOOT_CONFIG_CRC_OFFSET  8

static uint32_t boot_config_calc_crc32(const boot_config_t *config)
{
    const uint8_t *data = (const uint8_t *)config + BOOT_CONFIG_CRC_OFFSET;
    uint32_t length = BOOT_CONFIG_SIZE - BOOT_CONFIG_CRC_OFFSET;
    return crc32(data, length);
}

bool boot_config_validate(const boot_config_t *config)
{
    if (config == NULL) return false;
    if (config->magic != BOOT_CONFIG_MAGIC) return false;

    uint32_t calculated_crc = boot_config_calc_crc32(config);
    if (config->crc32 != calculated_crc) return false;

    if (config->active_partition != PARTITION_APP0 && 
        config->active_partition != PARTITION_APP1) return false;

    if (config->upgrade_state > UPGRADE_STATE_COMMITTED) return false;

    return true;
}

bool boot_config_load(boot_config_t *config)
{
    if (config == NULL) return false;

    memcpy(config, (void *)BOOT_CONFIG_ADDRESS, BOOT_CONFIG_SIZE);

    if (!boot_config_validate(config)) {
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
    if (config == NULL) return false;

    boot_config_t config_copy;
    memcpy(&config_copy, config, BOOT_CONFIG_SIZE);
    config_copy.crc32 = boot_config_calc_crc32(&config_copy);

    stm32_flash_unlock();
    stm32_flash_erase(BOOT_CONFIG_ADDRESS, BOOT_CONFIG_SIZE);
    stm32_flash_program(BOOT_CONFIG_ADDRESS, (const uint8_t *)&config_copy, BOOT_CONFIG_SIZE);
    stm32_flash_lock();

    boot_config_t verify_config;
    memcpy(&verify_config, (void *)BOOT_CONFIG_ADDRESS, BOOT_CONFIG_SIZE);
    if (memcmp(&config_copy, &verify_config, BOOT_CONFIG_SIZE) != 0) {
        log_e("config save verification failed");
        return false;
    }

    log_i("config saved successfully");
    return true;
}

bool boot_config_init_default(boot_config_t *config)
{
    if (config == NULL) return false;

    memset(config, 0, BOOT_CONFIG_SIZE);
    config->magic = BOOT_CONFIG_MAGIC;
    config->active_partition = PARTITION_APP0;
    config->app0_valid = 1;
    config->app1_valid = 0;
    config->upgrade_state = UPGRADE_STATE_IDLE;
    config->boot_count = 0;
    config->last_error = 0;
    config->crc32 = boot_config_calc_crc32(config);

    log_i("default config initialized");
    return true;
}

partition_t boot_config_get_active_partition(const boot_config_t *config)
{
    if (config == NULL) return PARTITION_APP0;
    return (partition_t)config->active_partition;
}

bool boot_config_set_active_partition(boot_config_t *config, partition_t partition)
{
    if (config == NULL) return false;
    if (partition != PARTITION_APP0 && partition != PARTITION_APP1) return false;
    config->active_partition = (uint8_t)partition;
    return true;
}

bool boot_config_is_partition_valid(const boot_config_t *config, partition_t partition)
{
    if (config == NULL) return false;
    if (partition == PARTITION_APP0) return config->app0_valid != 0;
    if (partition == PARTITION_APP1) return config->app1_valid != 0;
    return false;
}

bool boot_config_set_partition_valid(boot_config_t *config, partition_t partition, bool valid)
{
    if (config == NULL) return false;
    if (partition == PARTITION_APP0) config->app0_valid = valid ? 1 : 0;
    else if (partition == PARTITION_APP1) config->app1_valid = valid ? 1 : 0;
    else return false;
    return true;
}

upgrade_state_t boot_config_get_upgrade_state(const boot_config_t *config)
{
    if (config == NULL) return UPGRADE_STATE_IDLE;
    return (upgrade_state_t)config->upgrade_state;
}

bool boot_config_set_upgrade_state(boot_config_t *config, upgrade_state_t state)
{
    if (config == NULL) return false;
    if (state > UPGRADE_STATE_COMMITTED) return false;
    config->upgrade_state = (uint8_t)state;
    return true;
}
```

### 文件3: `app/partition.h`

```c
#ifndef __PARTITION_H__
#define __PARTITION_H__

#include <stdbool.h>
#include <stdint.h>
#include "boot_config.h"

#define PARTITION_APP0_ADDRESS      0x08010000
#define PARTITION_APP0_SIZE         (256 * 1024)
#define PARTITION_APP1_ADDRESS      0x08050000
#define PARTITION_APP1_SIZE         (192 * 1024)

uint32_t partition_get_address(partition_t partition);
uint32_t partition_get_size(partition_t partition);

bool partition_erase(partition_t partition);
bool partition_erase_range(uint32_t address, uint32_t size);

partition_t partition_get_download_target(const boot_config_t *config);
partition_t partition_get_opposite(partition_t partition);

#endif
```

### 文件4: `app/partition.c`

```c
#include <stdbool.h>
#include <stdint.h>
#include "stm32f4xx.h"
#include "stm32_flash.h"
#include "partition.h"

#define LOG_TAG    "part"
#define LOG_LVL    ELOG_LVL_INFO
#include "elog.h"

uint32_t partition_get_address(partition_t partition)
{
    switch (partition) {
        case PARTITION_APP0: return PARTITION_APP0_ADDRESS;
        case PARTITION_APP1: return PARTITION_APP1_ADDRESS;
        default: log_e("invalid partition: %d", partition); return 0;
    }
}

uint32_t partition_get_size(partition_t partition)
{
    switch (partition) {
        case PARTITION_APP0: return PARTITION_APP0_SIZE;
        case PARTITION_APP1: return PARTITION_APP1_SIZE;
        default: log_e("invalid partition: %d", partition); return 0;
    }
}

bool partition_erase(partition_t partition)
{
    uint32_t address = partition_get_address(partition);
    uint32_t size = partition_get_size(partition);
    if (address == 0) return false;

    log_i("erasing partition %d at 0x%08X, size %u", partition, address, size);
    stm32_flash_unlock();
    stm32_flash_erase(address, size);
    stm32_flash_lock();
    return true;
}

bool partition_erase_range(uint32_t address, uint32_t size)
{
    if (address < STM32_FLASH_BASE || 
        address + size > STM32_FLASH_BASE + STM32_FLASH_SIZE) {
        log_e("address 0x%08X out of range", address);
        return false;
    }

    if (address < 0x0800C100) {
        log_e("address 0x%08X is protected", address);
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
    if (config == NULL) return PARTITION_APP1;
    partition_t active = boot_config_get_active_partition(config);
    return partition_get_opposite(active);
}

partition_t partition_get_opposite(partition_t partition)
{
    return (partition == PARTITION_APP0) ? PARTITION_APP1 : PARTITION_APP0;
}
```

---

## 三、修改现有文件

### 修改1: `app/magic_header.h`

**新增包含 (第4行后)**:
```c
#include "partition.h"
```

**新增声明 (第19行后)**:
```c
bool magic_header_validate_partition(partition_t partition);
magic_header_type_t magic_header_get_type_partition(partition_t partition);
uint32_t magic_header_get_offset_partition(partition_t partition);
uint32_t magic_header_get_address_partition(partition_t partition);
uint32_t magic_header_get_length_partition(partition_t partition);
uint32_t magic_header_get_crc32_partition(partition_t partition);
```

### 修改2: `app/magic_header.c`

**修改宏定义 (第7-8行)**:
```c
#define MAGIC_HEADER_MAGIC 0x4D414749 // "MAGI"

/* Magic Header地址 - 完全对称布局 */
#define MAGIC_HEADER_APP0_ADDR  0x08010000  // APP0分区起始
#define MAGIC_HEADER_APP1_ADDR  0x08050000  // APP1分区起始
```

**修改validate函数 (第33-70行)**:
```c
bool magic_header_validate_partition(partition_t partition)
{
    uint32_t addr;
    if (partition == PARTITION_APP0) {
        addr = MAGIC_HEADER_APP0_ADDR;
    } else if (partition == PARTITION_APP1) {
        addr = MAGIC_HEADER_APP1_ADDR;
    } else {
        return false;
    }

    magic_header_t *header = (magic_header_t *)addr;
    if (header->magic != MAGIC_HEADER_MAGIC) return false;

    uint32_t ccrc = crc32((uint8_t *)header, offset_of(magic_header_t, this_crc32));
    if (ccrc != header->this_crc32) return false;

    return true;
}

bool magic_header_validate(void)
{
    return magic_header_validate_partition(PARTITION_APP0);
}
```

**修改getter函数 (第72-100行)**:
```c
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
magic_header_type_t magic_header_get_type(void) { return magic_header_get_type_partition(PARTITION_APP0); }
uint32_t magic_header_get_offset(void) { return magic_header_get_offset_partition(PARTITION_APP0); }
uint32_t magic_header_get_address(void) { return magic_header_get_address_partition(PARTITION_APP0); }
uint32_t magic_header_get_length(void) { return magic_header_get_length_partition(PARTITION_APP0); }
uint32_t magic_header_get_crc32(void) { return magic_header_get_crc32_partition(PARTITION_APP0); }
```

### 修改3: `app/bootloader.c`

**新增包含 (第19行后)**:
```c
#include "boot_config.h"
#include "partition.h"
```

**新增全局变量 (第90行后)**:
```c
static boot_config_t boot_config;
```

**新增opcode (第59-67行)**:
```c
typedef enum
{
    PACKET_OPCODE_INQUERY = 0x01,
    PACKET_OPCODE_GET_STATUS = 0x02,      // 新增
    PACKET_OPCODE_ERASE = 0x81,
    PACKET_OPCODE_PROGRAM = 0x82,
    PACKET_OPCODE_VERIFY  = 0x83,
    PACKET_OPCODE_START_UPGRADE = 0x84,   // 新增
    PACKET_OPCODE_COMMIT = 0x85,          // 新增
    PACKET_OPCODE_ROLLBACK = 0x86,        // 新增
    PACKET_OPCODE_RESET = 0x21,
    PACKET_OPCODE_BOOT = 0x22,
} packet_opcode_t;
```

**修改application_validate (第96-115行)**:
```c
static bool application_validate_partition(partition_t partition)
{
    if (!magic_header_validate_partition(partition)) {
        log_e("magic header invalid for partition %d", partition);
        return false;
    }

    uint32_t address = partition_get_address(partition);
    uint32_t size = magic_header_get_length_partition(partition);
    uint32_t crc = magic_header_get_crc32_partition(partition);
    uint32_t ccrc = crc32((uint8_t *)address, size);

    if (crc != ccrc) {
        log_e("crc error for partition %d: expected %08X, got %08X", partition, crc, ccrc);
        return false;
    }

    return true;
}

static bool application_validate(void)
{
    return application_validate_partition(PARTITION_APP0);
}
```

**修改boot_application (第117-139行)**:
```c
static void boot_application_partition(partition_t partition)
{
    uint32_t app_address = partition_get_address(partition);
    if (app_address == 0) return;

    if (!application_validate_partition(partition)) {
        log_e("validate failed for partition %d", partition);
        return;
    }

    log_w("booting from partition %d at 0x%08X...", partition, app_address);
    tim_delay_ms(2);

    led_off(led1);
    TIM_DeInit(TIM6);
    USART_DeInit(USART1);
    USART_DeInit(USART3);
    NVIC_DisableIRQ(TIM6_DAC_IRQn);
    NVIC_DisableIRQ(USART1_IRQn);
    NVIC_DisableIRQ(USART3_IRQn);

    SCB->VTOR = app_address;
    extern void JumpApp(uint32_t base);
    JumpApp(app_address);
}

static void boot_application(void)
{
    boot_application_partition(boot_config_get_active_partition(&boot_config));
}
```

**新增命令处理函数 (第331行后)**:
```c
static void bl_opcode_start_upgrade_handler(void)
{
    log_i("start upgrade handler");

    if (boot_config_get_upgrade_state(&boot_config) != UPGRADE_STATE_IDLE &&
        boot_config_get_upgrade_state(&boot_config) != UPGRADE_STATE_COMMITTED) {
        log_w("upgrade already in progress");
    }

    partition_t target = partition_get_download_target(&boot_config);
    log_i("download target: partition %d at 0x%08X", target, partition_get_address(target));

    boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_DOWNLOADING);
    boot_config.download_address = partition_get_address(target);
    boot_config.download_size = 0;
    boot_config.download_crc32 = 0;

    if (!boot_config_save(&boot_config)) {
        log_e("save boot config failed");
        bl_response(PACKET_OPCODE_START_UPGRADE, PACKET_ERRCODE_UNKNOWN, NULL, 0);
        return;
    }

    bl_response(PACKET_OPCODE_START_UPGRADE, PACKET_ERRCODE_OK, NULL, 0);
}

static void bl_opcode_commit_handler(void)
{
    log_i("commit handler");

    if (boot_config_get_upgrade_state(&boot_config) != UPGRADE_STATE_VERIFIED) {
        log_e("upgrade not verified");
        bl_response(PACKET_OPCODE_COMMIT, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    partition_t current_active = boot_config_get_active_partition(&boot_config);
    partition_t new_active = partition_get_opposite(current_active);

    boot_config_set_active_partition(&boot_config, new_active);
    boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_COMMITTED);

    if (!boot_config_save(&boot_config)) {
        log_e("save boot config failed");
        bl_response(PACKET_OPCODE_COMMIT, PACKET_ERRCODE_UNKNOWN, NULL, 0);
        return;
    }

    log_i("upgrade committed, new active: %d", new_active);
    bl_response(PACKET_OPCODE_COMMIT, PACKET_ERRCODE_OK, NULL, 0);
}

static void bl_opcode_get_status_handler(void)
{
    log_i("get status handler");

    uint8_t status_data[16];
    uint8_t *p = status_data;

    put_u8_inc(&p, boot_config.active_partition);
    put_u8_inc(&p, boot_config.app0_valid);
    put_u8_inc(&p, boot_config.app1_valid);
    put_u8_inc(&p, boot_config.upgrade_state);
    put_u32_inc(&p, boot_config.boot_count);

    bl_response(PACKET_OPCODE_GET_STATUS, PACKET_ERRCODE_OK, status_data, p - status_data);
}

static void bl_opcode_rollback_handler(void)
{
    log_i("rollback handler");

    partition_t current_active = boot_config_get_active_partition(&boot_config);
    partition_t rollback_target = partition_get_opposite(current_active);

    if (!boot_config_is_partition_valid(&boot_config, rollback_target)) {
        log_e("rollback target %d is invalid", rollback_target);
        bl_response(PACKET_OPCODE_ROLLBACK, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    boot_config_set_active_partition(&boot_config, rollback_target);
    boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_IDLE);

    if (!boot_config_save(&boot_config)) {
        log_e("save boot config failed");
        bl_response(PACKET_OPCODE_ROLLBACK, PACKET_ERRCODE_UNKNOWN, NULL, 0);
        return;
    }

    log_i("rollback to partition %d", rollback_target);
    bl_response(PACKET_OPCODE_ROLLBACK, PACKET_ERRCODE_OK, NULL, 0);
}
```

**修改bl_packet_handler (第333-360行)**:
在switch中添加:
```c
        case PACKET_OPCODE_START_UPGRADE:
            bl_opcode_start_upgrade_handler();
            break;
        case PACKET_OPCODE_COMMIT:
            bl_opcode_commit_handler();
            break;
        case PACKET_OPCODE_GET_STATUS:
            bl_opcode_get_status_handler();
            break;
        case PACKET_OPCODE_ROLLBACK:
            bl_opcode_rollback_handler();
            break;
```

**修改bl_opcode_erase_handler (第194行后)**:
```c
    if (boot_config_get_upgrade_state(&boot_config) != UPGRADE_STATE_DOWNLOADING) {
        log_w("erase rejected: not in download state");
        bl_response(PACKET_OPCODE_ERASE, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }
```

**修改bl_opcode_program_handler (第232行后)**:
```c
    if (boot_config_get_upgrade_state(&boot_config) != UPGRADE_STATE_DOWNLOADING) {
        log_w("program rejected: not in download state");
        bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }

    uint32_t target_addr = boot_config.download_address;
    uint32_t target_size = partition_get_size(partition_get_download_target(&boot_config));
    if (address < target_addr || address + size > target_addr + target_size) {
        log_e("address out of download range");
        bl_response(PACKET_OPCODE_PROGRAM, PACKET_ERRCODE_PARAM, NULL, 0);
        return;
    }
```

**修改bl_opcode_verify_handler (第278-313行)**:
验证成功后:
```c
    partition_t target = partition_get_download_target(&boot_config);
    boot_config_set_partition_valid(&boot_config, target, true);
    boot_config.download_crc32 = crc;
    boot_config.download_size = size;
    boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_VERIFIED);
    boot_config_save(&boot_config);
```

验证失败后:
```c
    partition_t target = partition_get_download_target(&boot_config);
    boot_config_set_partition_valid(&boot_config, target, false);
    boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_DOWNLOADING);
    boot_config_save(&boot_config);
```

**修改bootloader_main (第544行后)**:
```c
    if (!boot_config_load(&boot_config)) {
        log_e("load boot config failed");
    }

    upgrade_state_t upgrade_state = boot_config_get_upgrade_state(&boot_config);
    if (upgrade_state != UPGRADE_STATE_IDLE && 
        upgrade_state != UPGRADE_STATE_COMMITTED) {
        log_w("upgrade was interrupted, resetting to IDLE");
        boot_config_set_upgrade_state(&boot_config, UPGRADE_STATE_IDLE);
        boot_config_save(&boot_config);
    }

    boot_config.boot_count++;
    boot_config_save(&boot_config);
```

**修改跳转逻辑 (第566行)**:
```c
    if (!trapboot) {
        partition_t active = boot_config_get_active_partition(&boot_config);
        
        if (boot_config_is_partition_valid(&boot_config, active)) {
            log_i("booting from partition %d", active);
            boot_application_partition(active);
        } else {
            log_w("partition %d invalid, trying opposite", active);
            partition_t opposite = partition_get_opposite(active);
            if (boot_config_is_partition_valid(&boot_config, opposite)) {
                log_i("booting from partition %d", opposite);
                boot_application_partition(opposite);
            } else {
                log_e("both partitions invalid, entering upgrade mode");
            }
        }
    }
```

---

## 四、Keil工程配置

添加文件到工程:
- `app/boot_config.c`
- `app/partition.c`

---

## 五、测试要点

- [ ] Boot Config读写正常
- [ ] 双分区切换正常
- [ ] 升级中断后重启正常
- [ ] 两个分区都无效时进入升级模式
- [ ] CRC校验失败时正确处理

---

---

## 六、Review修复记录

| 文件 | 行号 | 问题 | 修复方案 |
|------|------|------|----------|
| magic_header.c | 37 | `magic_header_validate()` 函数缺失 | 新增函数，调用 `magic_header_validate_partition(PARTITION_APP0)` |
| bootloader.c | 161 | void函数使用return调用 | 改为直接调用 |
| bootloader.c | 708-723 | `magic_header_trap_boot()` 硬编码APP0 | 改为读取active_partition动态检查 |

---

## 七、相关文档

- [上位机与Clock主程序修改指南](upper_and_clock_modification_guide.md)

---

**文档版本**: v2.1
**更新日期**: 2026-06-02
