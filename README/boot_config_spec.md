# Boot Config 结构体规范

## 概述

Boot Config是A/B分区升级方案的核心数据结构，存储在Flash的固定位置(0x0800C000)，用于记录：
- 当前活动分区
- 各分区有效性
- 升级状态
- 固件校验信息

## 存储位置

```
Flash地址: 0x0800C000
大小: 256B
扇区: Sector 3 (16KB, 0x0800C000 - 0x0800FFFF)
```

## 结构体定义

```c
typedef struct {
    uint32_t magic;              // 偏移: 0x00, 长度: 4字节
    uint32_t crc32;              // 偏移: 0x04, 长度: 4字节
    
    uint8_t  active_partition;   // 偏移: 0x08, 长度: 1字节
    uint8_t  app0_valid;         // 偏移: 0x09, 长度: 1字节
    uint8_t  app1_valid;         // 偏移: 0x0A, 长度: 1字节
    uint8_t  upgrade_state;      // 偏移: 0x0B, 长度: 1字节
    
    uint32_t app0_crc32;         // 偏移: 0x0C, 长度: 4字节
    uint32_t app0_size;          // 偏移: 0x10, 长度: 4字节
    uint32_t app1_crc32;         // 偏移: 0x14, 长度: 4字节
    uint32_t app1_size;          // 偏移: 0x18, 长度: 4字节
    
    uint32_t download_crc32;     // 偏移: 0x1C, 长度: 4字节
    uint32_t download_size;      // 偏移: 0x20, 长度: 4字节
    uint32_t download_address;   // 偏移: 0x24, 长度: 4字节
    
    uint32_t boot_count;         // 偏移: 0x28, 长度: 4字节
    uint32_t last_error;         // 偏移: 0x2C, 长度: 4字节
    
    uint32_t reserved[7];        // 偏移: 0x30, 长度: 28字节
} boot_config_t;                 // 总大小: 256字节
```

## 字段说明

### magic (魔数)
- **值**: 0x424F4F54 (ASCII: "BOOT")
- **用途**: 标识Boot Config结构体有效性
- **验证**: 必须等于0x424F4F54

### crc32 (校验值)
- **计算范围**: 从active_partition字段到reserved字段末尾
- **用途**: 验证Boot Config数据完整性
- **计算公式**: `crc32(config + 8, 248)`

### active_partition (活动分区)
- **类型**: partition_t枚举
- **值**:
  - 0 (PARTITION_APP0): 从APP0启动
  - 1 (PARTITION_APP1): 从APP1启动
- **默认值**: 0 (APP0)

### app0_valid (APP0有效标志)
- **类型**: 布尔值
- **值**:
  - 0: APP0无效
  - 1: APP0有效
- **默认值**: 1

### app1_valid (APP1有效标志)
- **类型**: 布尔值
- **值**:
  - 0: APP1无效
  - 1: APP1有效
- **默认值**: 0

### upgrade_state (升级状态)
- **类型**: upgrade_state_t枚举
- **值**:
  - 0 (UPGRADE_STATE_IDLE): 空闲
  - 1 (UPGRADE_STATE_DOWNLOADING): 下载中
  - 2 (UPGRADE_STATE_DOWNLOADED): 下载完成
  - 3 (UPGRADE_STATE_VERIFIED): 验证完成
  - 4 (UPGRADE_STATE_COMMITTED): 已提交
- **默认值**: 0

### app0_crc32 (APP0固件CRC32)
- **用途**: APP0固件的CRC32校验值
- **更新时机**: 验证APP0成功时
- **默认值**: 0

### app0_size (APP0固件大小)
- **用途**: APP0固件的字节大小
- **更新时机**: 验证APP0成功时
- **默认值**: 0

### app1_crc32 (APP1固件CRC32)
- **用途**: APP1固件的CRC32校验值
- **更新时机**: 验证APP1成功时
- **默认值**: 0

### app1_size (APP1固件大小)
- **用途**: APP1固件的字节大小
- **更新时机**: 验证APP1成功时
- **默认值**: 0

### download_crc32 (下载固件CRC32)
- **用途**: 正在下载的固件CRC32
- **更新时机**: 上位机发送START_UPGRADE命令时
- **默认值**: 0

### download_size (下载固件大小)
- **用途**: 正在下载的固件大小
- **更新时机**: 上位机发送START_UPGRADE命令时
- **默认值**: 0

### download_address (下载目标地址)
- **用途**: 正在下载的目标分区地址
- **更新时机**: 上位机发送START_UPGRADE命令时
- **默认值**: 0

### boot_count (启动计数)
- **用途**: 记录设备启动次数
- **更新时机**: 每次启动时+1
- **用途**: 调试和统计

### last_error (最后错误码)
- **用途**: 记录最后一次错误
- **更新时机**: 发生错误时
- **用途**: 调试和故障诊断

### reserved (保留字段)
- **用途**: 未来扩展
- **大小**: 28字节
- **默认值**: 0

## CRC32计算

```c
uint32_t boot_config_calc_crc32(const boot_config_t *config)
{
    /* 计算从active_partition字段到结构体末尾的CRC32 */
    const uint8_t *data = (const uint8_t *)config + 8;
    uint32_t length = 256 - 8;
    return crc32(data, length);
}
```

## 读写流程

### 读取流程
```c
bool boot_config_load(boot_config_t *config)
{
    // 1. 从Flash读取256字节
    memcpy(config, (void *)0x0800C000, 256);
    
    // 2. 验证魔数
    if (config->magic != 0x424F4F54) {
        // 初始化默认值
        boot_config_init_default(config);
        return true;
    }
    
    // 3. 验证CRC32
    uint32_t calculated = boot_config_calc_crc32(config);
    if (config->crc32 != calculated) {
        // 数据损坏，初始化默认值
        boot_config_init_default(config);
        return true;
    }
    
    return true;
}
```

### 保存流程
```c
bool boot_config_save(const boot_config_t *config)
{
    // 1. 创建副本
    boot_config_t copy;
    memcpy(&copy, config, 256);
    
    // 2. 计算CRC32
    copy.crc32 = boot_config_calc_crc32(&copy);
    
    // 3. 擦除扇区
    stm32_flash_unlock();
    stm32_flash_erase(0x0800C000, 256);
    
    // 4. 写入Flash
    stm32_flash_program(0x0800C000, (uint8_t *)&copy, 256);
    stm32_flash_lock();
    
    // 5. 验证写入
    boot_config_t verify;
    memcpy(&verify, (void *)0x0800C000, 256);
    if (memcmp(&copy, &verify, 256) != 0) {
        return false;
    }
    
    return true;
}
```

## 状态转换规则

### 正常升级流程
```
IDLE → DOWNLOADING → VERIFIED → COMMITTED → IDLE
```

### 中断恢复规则
```
如果 upgrade_state == DOWNLOADING:
    → 恢复到 IDLE
    → 保持原活动分区不变

如果 upgrade_state == VERIFIED:
    → 恢复到 IDLE
    → 保持原活动分区不变

如果 upgrade_state == COMMITTED:
    → 保持 COMMITTED
    → 切换到新活动分区
```

## 数据完整性保护

### 1. CRC32校验
- 每次读取都验证CRC32
- 如果CRC32不匹配，使用默认值

### 2. 魔数验证
- 必须等于0x424F4F54
- 如果魔数错误，使用默认值

### 3. 范围验证
- active_partition: 0或1
- upgrade_state: 0-4
- 其他字段: 合理范围

## 使用示例

### 初始化
```c
boot_config_t config;
boot_config_load(&config);
```

### 切换分区
```c
partition_t current = boot_config_get_active_partition(&config);
partition_t target = (current == PARTITION_APP0) ? PARTITION_APP1 : PARTITION_APP0;
boot_config_set_active_partition(&config, target);
boot_config_save(&config);
```

### 开始升级
```c
boot_config_set_upgrade_state(&config, UPGRADE_STATE_DOWNLOADING);
boot_config.download_address = partition_get_address(target);
boot_config_save(&config);
```

### 提交升级
```c
boot_config_set_partition_valid(&config, target, true);
boot_config_set_upgrade_state(&config, UPGRADE_STATE_COMMITTED);
boot_config_set_active_partition(&config, target);
boot_config_save(&config);
```

## 注意事项

1. **Flash擦写次数**: STM32F407的Flash擦写次数约10万次，避免频繁保存

2. **原子性**: 保存操作不是原子的，如果在擦除或写入过程中断电，数据会丢失

3. **备份策略**: 可以在Reserved区域保存Boot Config的备份

4. **初始化时机**: 首次运行或检测到损坏时，自动初始化为默认值

5. **日志记录**: 所有读写操作都应该记录日志，便于调试

---

**文档版本**: v1.0
**创建日期**: 2026-06-02
