# Bootloader A/B分区修改快速参考

## 文件清单

### 新增文件 (4个)

| 文件 | 路径 | 说明 |
|------|------|------|
| boot_config.h | app/boot_config.h | Boot Config结构体和函数声明 |
| boot_config.c | app/boot_config.c | Boot Config读写实现 |
| partition.h | app/partition.h | 分区管理函数声明 |
| partition.c | app/partition.c | 分区管理实现 |

### 修改文件 (3个)

| 文件 | 修改内容 |
|------|----------|
| app/bootloader.c | 主要逻辑修改，新增命令处理 |
| app/magic_header.c | 支持双分区验证 |
| app/magic_header.h | 新增函数声明 |

---

## 关键修改位置速查

### bootloader.c

```
第19行后    → 新增 #include "boot_config.h" 和 "partition.h"
第28行后    → 新增分区地址宏定义
第90行后    → 新增全局变量 boot_config_t boot_config
第96-115行  → 修改 application_validate 支持分区
第117-139行 → 修改 boot_application 支持分区
第194-230行 → 修改 bl_opcode_erase_handler 添加状态检查
第232-276行 → 修改 bl_opcode_program_handler 添加状态检查
第278-313行 → 修改 bl_opcode_verify_handler 更新Boot Config
第331行后   → 新增4个命令处理函数
第333-360行 → 修改 bl_packet_handler 添加新case
第399-404行 → 修改opcode验证添加新命令
第544-591行 → 修改 bootloader_main 添加Config加载和恢复逻辑
```

### magic_header.c

```
第5行后     → 新增 #include "partition.h"
第7-8行     → 修改宏定义，新增APP1地址
第33-75行   → 新增 magic_header_validate_partition 函数
```

---

## 新增函数列表

### boot_config.c

```c
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
```

### partition.c

```c
uint32_t partition_get_address(partition_t partition);
uint32_t partition_get_size(partition_t partition);
bool partition_validate(partition_t partition);
bool partition_validate_crc32(partition_t partition, uint32_t expected_crc, uint32_t expected_size);
bool partition_erase(partition_t partition);
bool partition_erase_range(uint32_t address, uint32_t size);
partition_t partition_get_download_target(const boot_config_t *config);
partition_t partition_get_opposite(partition_t partition);
```

### bootloader.c (新增)

```c
static void bl_opcode_start_upgrade_handler(void);
static void bl_opcode_commit_handler(void);
static void bl_opcode_get_status_handler(void);
static void bl_opcode_rollback_handler(void);
```

---

## 新增协议命令

| Opcode | 名称 | 说明 |
|--------|------|------|
| 0x02 | GET_STATUS | 获取升级状态 |
| 0x84 | START_UPGRADE | 开始升级 |
| 0x85 | COMMIT | 提交升级 |
| 0x86 | ROLLBACK | 回滚 |

---

## 内存布局

```
0x08000000 ┌─────────────────────┐
           │ Bootloader (48KB)   │
0x0800C000 ├─────────────────────┤
           │ Boot Config (256B)  │
0x0800C100 ├─────────────────────┤
           │ Reserved            │
0x08010000 ├─────────────────────┤
           │ APP0 (256KB)        │
0x08050000 ├─────────────────────┤
           │ APP1 (192KB)        │
0x08080000 └─────────────────────┘
```

---

## 升级状态机

```
IDLE → DOWNLOADING → DOWNLOADED → VERIFIED → COMMITTED
  ↑         ↓            ↓           ↓
  └─────────┴────────────┴───────────┘
              (失败时回滚到IDLE)
```

---

## 修改步骤

1. 创建4个新文件
2. 按照指南修改bootloader.c
3. 按照指南修改magic_header.c/h
4. 在Keil中添加新文件到工程
5. 编译测试
6. 测试基本功能
7. 测试中断恢复

---

详细说明请参考: [bootloader_ab_partition_guide.md](bootloader_ab_partition_guide.md)
