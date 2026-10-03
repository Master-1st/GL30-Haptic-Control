# ESP32 CO5300 原生线序 framebuffer 与 AHB-GDMA 显示流水优化

> **后续审计更正：** 本文保留为前一阶段记录。这里的 `copy_us≈0.5 ms` 只覆盖等待 DMA 的剩余时间，漏计了 `esp_async_memcpy()` 的提交、描述符和缓存同步开销，不能据此认定全部搬运相关成本下降 95%。后续已补齐统计、修复 DMA 故障隔离和 READY 帧覆盖，最终菜单 3×60 秒约 35.706 FPS；完整结果与限制见 [按帧跳过比较与代码审计](esp32-dirty-audit-20260915-cn.md)。

日期：2026-09-15。目标硬件：Waveshare ESP32-S3-Touch-AMOLED-1.32，466×466 RGB565，CO5300 QSPI 40 MHz。

## 结论

本轮已把 KK_OLED 双 framebuffer 改为 **CO5300 线序原生 RGB565 存储**：对外 API、颜色计算、alpha blend、XOR、字体和位图接口仍使用普通 RGB565 数值；仅 framebuffer 中的 16 位字以高低字节交换后的 wire-native 形式保存。因此显示提交阶段不再执行逐像素 RGB565 高低字节重排。

仅改为 wire-native 后仍不能消除主要耗时。实测表明，旧 `copy_us` 约 10 ms 中大头是 434,312 字节从 PSRAM 搬到内部 DMA RAM，而不是字节交换运算本身。最终方案使用 **AHB-GDMA 异步 PSRAM→内部双 strip 搬运**，并与 SPI LCD DMA 发送流水重叠，使显示任务因搬运而实际阻塞的剩余时间降到约 0.5 ms P95。

必须准确表述：**物理数据搬运仍然存在，但不再由 CPU 在显示关键路径上同步完成。** 本轮不是“434 KB 完全零拷贝”，而是“取消逐像素 byte-swap，并将必要 staging copy DMA 化、流水化”。

## 最终架构

```text
KK_UI / GL30 renderer
        |
        v
PSRAM 双 framebuffer
CO5300 wire-native RGB565
        |
        | AHB-GDMA async memcpy
        v
内部 RAM strip A / strip B (16 rows, DMA capable)
        |
        | SPI/QSPI DMA, 40 MHz
        v
CO5300 AMOLED
```

两个内部 strip 交替使用。当前 strip 由 SPI DMA 发往面板时，AHB-GDMA 同时把下一 strip 从冻结的 PSRAM framebuffer 搬到另一块内部 DMA RAM。只有下一 strip 尚未准备好时，display worker 才等待 copy completion。

## 为什么没有直接 PSRAM → SPI DMA

ESP32-S3 SoC 本身声明 `SOC_PSRAM_DMA_CAPABLE=1`、`SOC_AHB_GDMA_SUPPORT_PSRAM=1`，但 ESP-IDF 5.5.1 的通用 SPI master 在 TX 路径仍使用 `esp_ptr_dma_capable()` 判断源缓冲区。该判断只接受 `SOC_DMA_LOW..SOC_DMA_HIGH` 的内部 DMA 区域；PSRAM 源因此进入 SPI driver 的临时 DMA buffer 分配与 `memcpy` 路径。

实机验证直接把 PSRAM framebuffer strip 交给 `esp_lcd_panel_draw_bitmap()`：

- `copy_us = 0`（应用侧）；
- 但 `io_submit_us` 从约 2.9 ms 增至约 11–13 ms；
- send wall P95 增至约 35.8 ms；
- 面板帧率降至约 21.6 FPS。

因此该“零拷贝”分支被撤回，不保留。

## 最终 30 秒实机结果

证据目录：`outputs/fps-wire-native-async-copy-30ms-final-20260915/`。

| 指标 | 旧 30 ms 稳定版 | wire-native + AHB-GDMA 最终版 |
|---|---:|---:|
| 完整不同帧 | 1000 / 30.022 s | 1000 / 30.022 s |
| Panel FPS | 33.309 | **33.309** |
| 10 s 窗口 | 33.2 / 33.4 / 33.3 | **33.2 / 33.4 / 33.3** |
| 帧间隔 P95 | 30.336 ms | **30.407 ms** |
| 帧间隔 P99 | 30.833 ms | **30.781 ms** |
| 最大帧间隔 | 31.059 ms | **30.917 ms** |
| Raster P95 | 20.447 ms | 21.229 ms |
| Send wall P95 | 26.053 ms | 26.565 ms |
| `copy_us` P95 | 10.406 ms | **0.498 ms** |
| `copy_us` P99 | 10.569 ms | **0.550 ms** |
| `compare_us` P95 | 3.240 ms | 3.450 ms |
| frame errors | 0 | **0** |
| input drops | 0 | **0** |
| motor tx | 0 | **0** |

`copy_us` 在旧实现表示 display worker 同步执行 PSRAM→DMA strip staging 的耗时；新实现表示流水中 copy 尚未完成时 display worker 真正需要等待的剩余时间。因此它反映的是**显示关键路径上的 copy 阻塞**，不是 AHB-GDMA 物理搬运的完整持续时间。P95 从 10.406 ms 降到 0.498 ms，关键路径阻塞下降约 95%。

最终仍维持 30 ms UI deadline，因此 FPS 被主动限制在约 33.3。该优化的主要收益是释放 CPU / display worker 关键路径，而不是在本轮直接提高最终 FPS。

## 27 ms 边界测试

在相同 AHB-GDMA 流水基础上将 UI deadline 压到 27 ms 后，单次 send wall 仍约 26.7 ms，但双 framebuffer 的绘制、清屏和提交相位发生冲突：

- 平均约 24.4 FPS；
- 帧间隔 P50 约 50.2 ms；
- P95 约 51.7 ms；
- 最大约 52.3 ms。

因此 27 ms 被判定为失败工况并撤回。最终源码和 COM11 实机均恢复 30 ms。

## 功能与回归验证

- Windows host：3/3 CTest 通过（model/gesture、RGB565/shared UI、motor menu protocol/state）。
- WSL ASan/UBSan 构建与 3/3 CTest 通过。
- ESP-IDF 5.5.1 产品构建通过。
- COM11 烧录成功，esptool hash verified。
- 最终 30 秒逐帧 trace：1000 个 distinct complete frames；`frame_errors=0`、`input_drops=0`。
- USB UI smoke：20/20 通过；未 arm 电机，`motor_tx=0`。
- 本轮未验证实体手指触摸、真实 STM32 UART 满流量或电机运行下最坏时序。

## 剩余瓶颈与下一步

当前不应继续简单压低 `KK_UI_FRAME_INTERVAL_MS`。优先级应为：

1. **取消已知动画帧上的整帧 `memcmp`**：当前约 3.4 ms P95；动画/明确 dirty 场景不需要再扫描 434 KB 判断是否相同。
2. **降低 raster 开销**：菜单当前约 21.2 ms P95；wire-native store 使部分写入多一个 byte-swap，照片 blit 的普通方向也由原始 memcpy 改为逐像素 encode，照片密集页面需单独做实机性能回归。
3. **评估 tile/strip renderer**：若目标必须显著超过 35 FPS，可考虑直接绘制到内部 DMA strip，避免完整 PSRAM framebuffer→strip 搬运；代价是绘制器需要真正支持分块重绘和稳定的图元裁剪。
4. QSPI 提频仍不是当前首选；40 MHz 下先把软件全帧扫描和 raster 热点解决，再讨论 60/80 MHz 的硬件裕量与可靠性。

当前可接受的工程结论：**30 ms / 约 33.3 FPS 是已实机验证的稳定工作点；原约 10 ms 的同步 staging copy 已从 CPU 显示关键路径中移除。**
