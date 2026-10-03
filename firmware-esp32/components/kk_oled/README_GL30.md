# GL30 KK_OLED RGB565 组件

本目录是 GL30 ESP32-S3 466×466 彩色 AMOLED 的 KK_OLED 图形核心副本。上游 KK_OLED 的图元、u8g2 字体解析、UTF-8 文字、XBM 读取和旋转采样算法保留在本组件中；上游运行时原本是 1-bit 页式单色 OLED，本副本已将显存核心改为原生 RGB565 全帧布局，因此不宣称上游库原生支持彩色屏。

## 帧布局和提交

- 物理尺寸固定为 `466×466`，像素按行主序连续存放。
- 每个像素为 `uint16_t` RGB565；每份帧缓冲区为 `466×466×2 = 434312` 字节。
- 运行时只保留两份静态帧缓冲区。定义 `ESP_PLATFORM` 时，帧缓冲区带 `EXT_RAM_BSS_ATTR`，由 ESP-IDF 的外部 BSS/PSRAM 配置承载；主机编译使用普通静态存储。
- 更新前按整帧比较新旧缓冲区。没有差异时不调用 driver；阻塞提交成功后交换缓冲区，失败则保持待提交帧并强制下一次全帧发送。
- 异步更新会在调用 BSP driver 前设置核心 Busy 状态并交换绘制索引，传输指针在整个异步过程保持冻结。driver 必须在真实完成或错误时调用 `OLED_InternalTransferFinished` 一次；未接入真实 IT/DMA 时，`OLED_DriverWriteIT` 和 `OLED_DriverWriteDMA` 应返回 `OLED_UNSUPPORTED`，不能用阻塞发送伪装异步。

平台 driver 不在本组件内。BSP driver 通过 `OLED_InternalGetTransferBuffer()` 取得 `const uint16_t *` 的完整 466×466 帧，并实现 `OLED_DriverInit`、`OLED_DriverWriteBlocking`、`OLED_DriverWriteIT`、`OLED_DriverWriteDMA`、`OLED_DriverIsBusy`、对比度/省电控制及异步事件转发。

## 颜色接口

```c
void OLED_SetColor(uint16_t foreground, uint16_t background);
void OLED_BlitRGB565(int16_t x, int16_t y, uint16_t width, uint16_t height,
                     const uint16_t *pixels);
void OLED_BlendPixelRGB565(int16_t x, int16_t y, uint16_t color,
                           uint8_t alpha);
```

`OLED_Clear()` 使用当前背景色，`OLED_Fill()` 使用当前前景色。普通图元的 `OLED_DRAW_SET` 写前景色，`OLED_DRAW_CLEAR` 写背景色，`OLED_DRAW_XOR` 对当前 RGB565 像素逐位取反。`OLED_BG_SOLID` 延续上游文字/XBM 的背景反操作语义；`OLED_BG_TRANSPARENT` 保留透明背景。RGB565 位图源是行主序 `uint16_t` 数组，按主机字节序读取。位图和像素混合都会经过当前裁剪窗口及画布旋转映射。

组件不内置或改写上层视觉资源；上层可用 `OLED_BlitRGB565` 继续呈现日照金山照片和彩色圈。

## 许可和范围

图形源和许可证保留上游 MIT 条款，详见同目录 `LICENSE.txt`。本组件只包含图形核心，不包含板级显示控制器初始化、总线传输、PSRAM 启用配置或 KK_UI 业务界面；这些由 `firmware-esp32/bsp` 和上层 UI 集成负责。
