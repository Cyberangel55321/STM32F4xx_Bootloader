# 三端修改指南

## 一、核心问题：固件不可重定位

ARM Cortex-M固件使用**绝对地址**。固件链接在 `0x08010100`（APP0），写入 `0x08050100`（APP1）后，向量表中的函数地址仍指向 `0x0801xxxx`，导致跳转到错误位置。

**解决方案**: 生成两个xbin文件（APP0版和APP1版），上位机根据目标分区选择对应文件。

---

## 二、Bootloader端修改

### 已修复

| 文件 | 行号 | 修改 |
|------|------|------|
| stm32_flash.c | 47 | 擦除条件: `addr >= address` → `addr + size > address` |

### 待修复

| 文件 | 行号 | 修改 |
|------|------|------|
| bootloader.c | 146 | 添加 `__disable_irq()` 在跳转前 |
| bootloader.c | 378 | 删除 `boot_config_set_upgrade_state(..., VERIFIED)` |
| bootloader.c | 448 | `!= VERIFIED` → `!= DOWNLOADING` |

---

## 三、Clock主程序端修改

### 3.1 链接脚本 (两个版本)

需要两个链接脚本，分别用于APP0和APP1编译。

**文件1**: `mdk/stm32f407_app0.sct`

```
LR_IROM1 0x08010100 0x0003FF00  {
  ER_IROM1 0x08010100 0x0003FF00  {
   *.o (RESET, +First)
   *(InRoot$$Sections)
   .ANY (+RO)
   .ANY (+XO)
  }
  RW_IRAM1 0x20000000 0x00020000  {
   .ANY (+RW +ZI)
  }
}
```

**文件2**: `mdk/stm32f407_app1.sct`

```
LR_IROM1 0x08050100 0x0002FF00  {
  ER_IROM1 0x08050100 0x0002FF00  {
   *.o (RESET, +First)
   *(InRoot$$Sections)
   .ANY (+RO)
   .ANY (+XO)
  }
  RW_IRAM1 0x20000000 0x00020000  {
   .ANY (+RW +ZI)
  }
}
```

**说明**: `0x0002FF00` = 192KB - 256B = APP1固件可用空间

### 3.2 Keil工程配置

创建两个Target：
- **Target_APP0**: 使用 `stm32f407_app0.sct`，输出 `stm32f407_app0.bin`
- **Target_APP1**: 使用 `stm32f407_app1.sct`，输出 `stm32f407_app1.bin`

### 3.3 board.c VTOR

**文件**: `app/board.c` 第19-35行

```c
volatile uint32_t boot_config_magic = *(volatile uint32_t *)0x0800C000;
if (boot_config_magic == 0x424F4F54)
{
    uint8_t active_partition = *(volatile uint8_t *)0x0800C008;
    if (active_partition == 1)
        SCB->VTOR = 0x08050100;
    else
        SCB->VTOR = 0x08010100;
}
else
{
    SCB->VTOR = 0x08010100;
}
```

### 3.4 gen_magic_header.py

**文件**: `scripts/gen_magic_header.py`

支持参数指定目标分区：

```python
import sys

def main():
    if len(sys.argv) < 3:
        print("Usage: gen_magic_header.py <input_bin> <partition:0|1>")
        sys.exit(1)

    binfile = sys.argv[1]
    partition = int(sys.argv[2])

    if partition == 0:
        this_addr = 0x08010000
        data_addr = 0x08010100
        suffix = "_app0"
    elif partition == 1:
        this_addr = 0x08050000
        data_addr = 0x08050100
        suffix = "_app1"
    else:
        print("Error: partition must be 0 or 1")
        sys.exit(1)

    # ... 生成xbin，使用this_addr和data_addr ...
```

**使用方式**:
```bash
python scripts/gen_magic_header.py mdk/Objects/stm32f407_app0.bin 0
python scripts/gen_magic_header.py mdk/Objects/stm32f407_app1.bin 1
```

生成文件：
- `generated/stm32f407_app0_upgrade.xbin`
- `generated/stm32f407_app1_upgrade.xbin`

### 3.5 gen_magic_header.py data_type

**第10行**: `DATA_TYPE_FIRMWARE = 1` → `DATA_TYPE_FIRMWARE = 0`

---

## 四、上位机修改

### 4.1 选择xbin文件

**文件**: `Form1.cs`

上位机需要根据目标分区选择对应的xbin文件。

修改 `buttonSelectFirmware_Click`，支持选择两个xbin文件：

```csharp
private string? firmwareApp0 = null;
private string? firmwareApp1 = null;

private void buttonSelectFirmware_Click(object sender, EventArgs e)
{
    OpenFileDialog ofd = new()
    {
        Filter = "firmware|*.xbin",
        Multiselect = true
    };
    if (ofd.ShowDialog() == DialogResult.OK)
    {
        foreach (string file in ofd.FileNames)
        {
            if (file.Contains("app0"))
                firmwareApp0 = file;
            else if (file.Contains("app1"))
                firmwareApp1 = file;
        }
        textBoxFirmware.Text = string.Join("; ", 
            new[] { firmwareApp0, firmwareApp1 }.Where(f => f != null));
    }
}
```

### 4.2 升级流程修改

在 `buttonUpgrade_Click` 中，根据目标分区选择xbin：

```csharp
// 确定目标分区后
string? targetFirmware;
if (targetBase == APP0_BASE)
    targetFirmware = firmwareApp0;
else
    targetFirmware = firmwareApp1;

if (targetFirmware == null)
{
    textBoxLog.AppendText($"缺少目标分区{xbin文件}\r\n");
    return;
}

byte[] xbin = File.ReadAllBytes(targetFirmware);
// ... 继续原有流程 ...
```

### 4.3 移除地址调整逻辑

由于每个xbin文件已经包含正确的地址，不再需要上位机调整地址：

```csharp
// 删除这段代码
if (header.ThisAddress == APP0_BASE)
{
    headerTargetAddr = targetBase;
    dataTargetAddr = targetBase + (header.DataAddress - APP0_BASE);
}
```

### 4.4 写入顺序

保持先写Data再写Header：
```
Data擦除 → Data写入 → Data校验 → Header写入 → Header校验
```

---

## 五、完整工作流程

### 编译
```
1. Keil Target_APP0 → Rebuild → stm32f407_app0.bin
2. Keil Target_APP1 → Rebuild → stm32f407_app1.bin
```

### 打包
```
python gen_magic_header.py mdk/Objects/stm32f407_app0.bin 0
python gen_magic_header.py mdk/Objects/stm32f407_app1.bin 1
→ generated/stm32f407_app0_upgrade.xbin
→ generated/stm32f407_app1_upgrade.xbin
```

### 升级
```
1. 上位机选择两个xbin文件
2. START_UPGRADE → Bootloader返回目标分区(如APP1)
3. 上位机选择app1对应的xbin
4. 先写Data，再写Header
5. COMMIT → 切换活动分区
6. RESET → 重启
7. Bootloader验证APP1，跳转到0x08050100
8. APP1固件正常运行（地址正确）
```

---

## 六、内存布局

```
0x08000000 +-----------------------+
           | Bootloader (48KB)     |
0x0800C000 +-----------------------+
           | Boot Config (256B)    |
0x0800C100 +-----------------------+
           | Reserved              |
0x08010000 +-----------------------+
           | APP0 Magic Header     |
0x08010100 +-----------------------+
           | APP0 Firmware         | <- 链接地址 0x08010100
           | (256KB - 256B)        |
0x08050000 +-----------------------+
           | APP1 Magic Header     |
0x08050100 +-----------------------+
           | APP1 Firmware         | <- 链接地址 0x08050100
           | (192KB - 256B)        |
0x08080000 +-----------------------+
```

---

**文档版本**: v3.0
**更新日期**: 2026-06-04
**更新内容**: 解决固件不可重定位问题，改为双版本打包方案
