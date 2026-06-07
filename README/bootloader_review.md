# Bootloader A/B分区方案 Review报告 (第二轮)

## 上轮问题修复确认

| # | 问题 | 状态 | 修复位置 |
|---|------|------|----------|
| 1 | CRC校验地址错误 | **已修复** | bootloader.c:113 使用 magic_header_get_address_partition() |
| 2 | VTOR地址错误 | **已修复** | bootloader.c:130 使用 magic_header_get_address_partition() |
| 3 | 结构体大小不匹配 | **已修复** | boot_config.h:46 reserved[52] -> sizeof=256 |
| 4 | partition_validate地址错误 | **已修复** | partition.c:44 使用 magic_header_get_address_partition() |
| 5 | magic_header_trap_boot硬编码APP0 | **已修复** | bootloader.c:720 使用 boot_config_get_active_partition() |
| 6 | erase未保护BootConfig | **已修复** | bootloader.c:252-257 增加Boot Config区域保护 |
| 7 | boot_count每次启动写Flash | **已修复** | bootloader.c 已删除boot_count相关代码 |

## 本轮检查结果

**无新发现的严重或中等问题。**

### 次要观察 (不影响功能)

1. partition_validate_crc32函数使用partition_get_address而非magic_header_get_address_partition (partition.c:76)，当前未被调用，不影响功能

2. partition_erase_range函数未保护Boot Config区域 (partition.c:129)，当前未被调用，erase handler已有独立保护

3. BOOT_DELAY从3000改为300 (bootloader.c:25)，按键等待从3秒缩短到300毫秒，请确认是否符合预期

## 逻辑流程验证

### 启动流程
```
bootloader_main
  -> boot_config_load (加载配置，失败则初始化默认值)
  -> 检查upgrade_state，中断的升级重置为IDLE
  -> magic_header_trap_boot (检查active分区的Magic Header和固件)
  -> key_trap_check (300ms按键检测)
  -> rx_trap_boot (3000ms串口数据检测)
  -> 根据active分区启动固件
     -> active有效: boot_application_partition(active)
     -> active无效: 尝试opposite分区
     -> 都无效: 进入升级模式
```

### 升级流程
```
START_UPGRADE: 设置state=DOWNLOADING, 记录download_address
  -> ERASE: 需要state=DOWNLOADING, 保护Bootloader和Boot Config区域
  -> PROGRAM: 需要state=DOWNLOADING, 验证地址在目标分区内
  -> VERIFY: 验证CRC32, 成功则设置state=VERIFIED, 失败则回退到DOWNLOADING
  -> COMMIT: 需要state=VERIFIED, 切换active分区, 设置state=COMMITTED
```

### 中断恢复
```
重启后检查upgrade_state:
  - IDLE/COMMITTED: 正常启动
  - DOWNLOADING/VERIFIED: 重置为IDLE, 使用原active分区启动
```

## 结论

Bootloader侧代码逻辑正确，可以进行后续的上位机和Clock项目适配工作。

---

**Review日期**: 2026-06-03
