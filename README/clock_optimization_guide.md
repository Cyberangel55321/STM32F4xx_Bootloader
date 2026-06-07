# Clock项目固件优化指南

## 目标

将固件从约353KB压缩到255KB以下，以支持A/B分区方案。

需要减少: 至少 **97KB**

---

## 一、可删除的未使用字体 (节省约24-31KB编译后)

以下3个字体文件从未被任何页面代码引用，可安全删除：

| 文件 | 路径 | 源码大小 | 编译后估算 |
|------|------|---------|-----------|
| font48.c | `app/font/font48.c` | 81.9KB | ~15KB |
| font32.c | `app/font/font32.c` | 37.4KB | ~7KB |
| font16.c | `app/font/font16.c` | 10.7KB | ~2KB |

**操作步骤**:

1. 从Keil工程中移除这3个.c文件（右键Remove File）
2. 从 `app/font/font.h` 中删除对应的extern声明：
   - 删除: `extern const font_t font48;`
   - 删除: `extern const font_t font32;`
   - 删除: `extern const font_t font16;`
3. 保留对应的 .c 文件在磁盘上（以防以后需要），只是不编译

---

## 二、大图优化 (节省约60-150KB编译后)

三张大图占用大量Flash，需要优化：

| 文件 | 符号名 | 源码大小 | 编译后估算 | 使用位置 | 建议 |
|------|--------|---------|-----------|---------|------|
| img_meihua.c | img_touxiang | 320.9KB | ~80KB | welcome_page.c 欢迎页 | **缩小或删除** |
| img_wifi.c | img_wifi | 295.7KB | ~75KB | wifi_page.c WiFi连接页 | **缩小** |
| img_error.c | img_error | 256.5KB | ~65KB | error_page.c 错误页 | **缩小或删除** |

### 方案A: 删除欢迎页和错误页大图 (节省约145KB)

welcome_page.c 和 error_page.c 只在启动和出错时显示一次，可以用纯文字替代。

**welcome_page.c 修改**:

原代码:
```c
void welcome_page_display(void)
{
    const uint16_t color_bg = mkcolor(0, 0, 0);
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, color_bg);
    ui_draw_image(30, 10, &img_touxiang);  // 320KB图片
    ui_write_string(40, 205, "...", ...);
    ui_write_string(60, 285, "loading...", ...);
}
```

修改为（去掉图片，纯文字）:
```c
void welcome_page_display(void)
{
    const uint16_t color_bg = mkcolor(0, 0, 0);
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, color_bg);
    ui_write_string(40, 100, "Weather Clock", mkcolor(255, 255, 255), color_bg, &font32_maple_bold);
    ui_write_string(60, 150, "v1.1", mkcolor(128, 128, 128), color_bg, &font20_maple_bold);
    ui_write_string(60, 285, "loading...", mkcolor(255, 255, 255), color_bg, &font24_maple_bold);
}
```

**error_page.c 修改**:

原代码:
```c
void error_page_display(const char *msg)
{
    const uint16_t color_bg = mkcolor(0, 0, 0);
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, color_bg);
    ui_draw_image(40, 37, &img_error);  // 256KB图片
    ui_write_string(startx, 245, msg, ...);
}
```

修改为（去掉图片，纯文字）:
```c
void error_page_display(const char *msg)
{
    const uint16_t color_bg = mkcolor(0, 0, 0);
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, color_bg);
    ui_write_string(70, 100, "ERROR", mkcolor(255, 0, 0), color_bg, &font32_maple_bold);
    // 居中显示错误信息
    uint16_t startx = 0;
    int len = strlen(msg) * font20_maple_bold.size / 2;
    if (len < UI_WIDTH)
        startx = (UI_WIDTH - len + 1) / 2;
    ui_write_string(startx, 160, msg, mkcolor(255, 255, 0), color_bg, &font20_maple_bold);
}
```

然后删除对应的图片文件（从Keil工程移除）：
- `app/image/img_meihua.c` (320.9KB)
- `app/image/img_error.c` (256.5KB)

从 `app/image/image.h` 中删除对应的extern声明。

### 方案B: 缩小WiFi连接页图片 (节省约50KB)

wifi_page.c 的 `img_wifi` 图片可以缩小分辨率。需要使用图片处理工具将图片缩小到合适尺寸后重新生成C数组。

---

## 三、优化效果预估

| 优化项 | 编译后节省 |
|--------|-----------|
| 删除3个未使用字体 | ~24KB |
| 删除img_touxiang (欢迎页图片) | ~80KB |
| 删除img_error (错误页图片) | ~65KB |
| **合计** | **~169KB** |

优化后固件大小: 353KB - 169KB ≈ **184KB** < 255KB ✓

---

## 四、需要修改的文件清单

### 从Keil工程移除文件（不删除磁盘文件）

1. `app/font/font48.c`
2. `app/font/font32.c`
3. `app/font/font16.c`
4. `app/image/img_meihua.c`
5. `app/image/img_error.c`

### 修改文件

#### 1. `app/font/font.h`

**路径**: `F:\project\Smart_weather_clock_based_on_FreeRTOS\my_project\STM32F4_Clock\app\font\font.h`

删除以下3行extern声明：
```c
extern const font_t font16;
extern const font_t font32;
extern const font_t font48;
```

#### 2. `app/image/image.h`

**路径**: `F:\project\Smart_weather_clock_based_on_FreeRTOS\my_project\STM32F4_Clock\app\image\image.h`

删除以下2行extern声明：
```c
extern const image_t img_touxiang;  // 来自img_meihua.c
extern const image_t img_error;
```

#### 3. `app/page/welcome_page.c`

**路径**: `F:\project\Smart_weather_clock_based_on_FreeRTOS\my_project\STM32F4_Clock\app\page\welcome_page.c`

删除 `ui_draw_image(30, 10, &img_touxiang);` 行，用纯文字替代。

#### 4. `app/page/error_page.c`

**路径**: `F:\project\Smart_weather_clock_based_on_FreeRTOS\my_project\STM32F4_Clock\app\page\error_page.c`

删除 `ui_draw_image(40, 37, &img_error);` 行，用纯文字替代。

---

## 五、验证步骤

1. 修改后重新编译，确认无错误
2. 查看编译输出的Code size，确认 < 255KB
3. 运行 gen_magic_header.py 生成xbin
4. 使用上位机升级测试

---

**文档版本**: v1.0
**创建日期**: 2026-06-03
