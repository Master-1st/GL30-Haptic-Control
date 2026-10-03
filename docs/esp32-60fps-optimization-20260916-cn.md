# GL30 ESP32 AMOLED 60 FPS 显示链优化与代码审计

日期：2026-09-16。项目：`G:\Agent\GL30-Haptic-Control`。

目标硬件：Waveshare ESP32-S3-Touch-AMOLED-1.32，ESP32-S3-PICO-1，466×466 RGB565，CO5300 QSPI。工具链：ESP-IDF 5.5.1。实机端口：COM11。本文只讨论 ESP32 显示/UI 链；没有烧录 STM32、没有使能电机，所有本轮性能测试 `motor_tx=0`。

## 1. 最终结论

连续菜单旋转工作负载已经从上一阶段约 **35.706 FPS** 提升到最终 **61.587 FPS**。最终固件采用：

- CO5300 QSPI：**80 MHz**；
- 每次传输 strip：**64 行，8 strips/完整帧**；
- KK_UI nominal frame interval：**15 ms**；
- CO5300 wire-native RGB565 framebuffer；
- 已知变化帧跳过整帧 `memcmp`；
- **3 个物理 framebuffer**：stable / transfer / draw-spare 所有权解耦；
- 旧 stable framebuffer 在 core 1、IDLE priority 的后台任务中 retire/clear；
- retire clear 完成后立即唤醒 UI owner；
- 菜单静态 chrome 跨 framebuffer 保留，只重画 carousel 动态带；
- 菜单 1 px 装饰外圈使用中点圆性能路径；
- `menu_visual` 在进入 `sinf/cosf` 前按 9 个应用的天然周期归一化，避免长时间旋转后 libm 大参数 range reduction。

最终 3×60 秒正式长测共完成 **11,090 帧 / 180.071496 s = 61.586649 FPS**。三组分别为：

| Run | 完成帧 | 平均 FPS | 帧间隔 P50 | P95 | P99 | Max |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 3,700 | 61.649 | 15.841 ms | 20.723 ms | 24.249 ms | 25.408 ms |
| 2 | 3,686 | 61.415 | 15.877 ms | 22.253 ms | 24.611 ms | 25.745 ms |
| 3 | 3,704 | 61.695 | 15.858 ms | 20.928 ms | 24.521 ms | 25.575 ms |
| 合并 | 11,090 | **61.587** | **15.860 ms** | **21.380 ms** | **24.479 ms** | **25.745 ms** |

18 个独立的 10 秒窗口为：

```text
61.7, 61.6, 61.7, 61.5, 61.7, 61.7,
61.2, 61.5, 61.3, 61.3, 61.6, 61.6,
61.7, 61.8, 61.6, 61.7, 61.7, 61.6 FPS
```

因此在该连续菜单软件 ROTATE 负载下，**每个 10 秒窗口都稳定超过 60 FPS**；最差 61.2，最好 61.8。三组全部 `frame_errors=0`、`input_queue_drops=0`、`motor_tx=0`，trace 中 `raster_id_gaps=0`、整帧比较调用增量为 0。

但这不是硬实时“每一帧都严格 16.667 ms”。11,087 个组内完成间隔中：

- 19.04% > 16.667 ms；
- 8.56% > 18 ms；
- 5.54% > 20 ms；
- 0.388% > 25 ms。

所以准确结论是：**持续菜单显示吞吐达到并重复验证了 60+ FPS；单帧完成间隔仍存在约 20–25 ms 的长尾。**

最终证据：`outputs/esp32-60fps-20260916/final-wrapped-3x60s/`，合并结果见 `aggregate-final.json`。

## 2. 为什么 40 MHz 全帧不可能达到 60 FPS

466×466×RGB565：

```text
466 × 466 × 16 bit = 3,474,496 bit/frame
```

CO5300 QSPI 40 MHz、4 data lines，仅算理想像素 payload：

```text
3,474,496 / (4 × 40,000,000) ≈ 21.716 ms/frame
```

即使没有任何命令、CS、driver、DMA 和调度开销，理论上也只有约 **46 FPS**。因此不能靠把 `KK_UI_FRAME_INTERVAL_MS` 改成 16 来“得到 60 FPS”。

ESP32-S3 的 GPSPI 默认 APB source 为 80 MHz，ESP-IDF 5.5.1 明确提供 80 MHz SPI master 档位。本轮把 CO5300 IO 的 `pclk_hz` 提升到 **80 MHz** 并实机验证传输错误计数为 0。

重要边界：Waveshare 对这块 1.32" 板公开资料没有给出“80 MHz guaranteed”承诺。因此本文证明的是**这块当前样机在软件/传输计数上的稳定运行**，不等于供应商电气保证。80 MHz 下的实屏色彩、边缘错误、偶发闪烁/撕裂仍需要人工长时间目视确认。

## 3. 总线侧：80 MHz + 64-row strip

阶段 A/B：

| 配置 | send wall P95 | 备注 |
|---|---:|---|
| 40 MHz / 16-row（上一阶段） | ≈26.565 ms | 无法 60 FPS |
| 80 MHz / 16-row | ≈16.688 ms | 已接近但仍卡 16.67 ms |
| 80 MHz / 32-row | ≈14.981 ms | 15 strips/frame |
| 80 MHz / 64-row | ≈14.78–15.10 ms | **最终，8 strips/frame** |

最终 3×60 秒合并 send wall：

- P50：**14.540 ms**；
- P95：**14.934 ms**；
- P99：**15.102 ms**；
- Max：**15.466 ms**。

与上一阶段 P95 26.565 ms 相比，P95 降低约 **43.8%**。

64-row 继续保留双 internal DMA strip，AHB-GDMA 从 PSRAM staging 到内部 SRAM，并与 SPI DMA 流水重叠。没有再次尝试直接把 PSRAM pointer 交给 `esp_lcd_panel_draw_bitmap()`；ESP-IDF 5.5.1 SPI master 对该路径仍会因为 DMA-capable pointer 检查而生成内部临时 copy。

## 4. Renderer：从约 21 ms 到约 15 ms

上一阶段菜单 raster P95 约 **21.229 ms**；最终 3×60 秒合并：

- P50：**14.893 ms**；
- P95：**15.784 ms**；
- P99：**16.001 ms**；
- Max：**16.460 ms**。

P95 降低约 **25.65%**。

### 4.1 数学等价优化

保留的优化包括：

1. RGB565 alpha 的 `/255` 改为已穷举验证的 exact integer identity；
2. 0° rotation 的 alpha blend 使用直接物理地址路径，避免每像素重复通用旋转映射；
3. 水平/垂直 rounded line 使用轴对齐快路径，避免每候选像素投影运算；
4. 菜单 `menu_visual` 在进入 `sinf/cosf` 前按 9-item 周期归一化。

`menu_visual` 周期归一化的原因由长测直接暴露：未归一化的前三组 60 秒，在 benchmark 前 30 秒持续 `+1`、后 30 秒持续 `-1` 时，20–40 秒窗口稳定下降到约 59.2–59.6 FPS；同一时段 raster 中位数也从 14.7–14.9 ms 上升到 15.2–15.3 ms，而 send 基本不变。

renderer 旧表达式：

```c
float a = (i - s->menu_visual) * 2*pi / 9;
```

当 `menu_visual` 累积到数百时，每帧 9 个 item 的 `sinf/cosf` 都需要更昂贵的大参数 range reduction。由于 carousel 几何严格 9-item 周期，先减去整数个 9 等价于角度减整数个 `2*pi`。

加入归一化后，60 秒六个窗口从此前典型：

```text
62.x, 61.x, 59.x, 59.x, 61.x, 61.x
```

变为：

```text
61.3–61.6 FPS
```

随后正式 3×60 秒的全部 18 个窗口均 ≥61.2 FPS。

Host A/B 对比 4,002 个提交画面：平均仅 **21.5 pixels/frame** 因浮点周期约简舍入产生差异；中位 6 pixels，最坏 547 pixels，占全屏 **0.252%**。其几何周期语义不变。

### 4.2 1 px menu ring 性能路径——明确的画质取舍

此前 9 个菜单 item 的 1 px AA full ring 单项约占 **5 ms/frame**，几乎和实心 disc 一样贵。为了达到 60 FPS，最终默认：

```c
#define GL30_FAST_MENU_RING 1
```

改用 KK_OLED 的中点圆 `OLED_DrawCircle()`，仍保留 9 个可见外圈，但不再做该 1 px 装饰层的亚像素 alpha AA。

这个修改**不是像素等价优化**，必须明确写出来。A/B 对比 296 个菜单提交画面：

- 平均变化约 **3,093 pixels/frame**；
- 中位约 3,099；
- 最大 3,212；
- 平均约占整个 466×466 framebuffer 的 **1.424%**；
- 差异集中在 9 个 1 px 外圈，item 位置、实心 disc、图标、文字和交互状态未删除。

如果将 `GL30_FAST_MENU_RING=0` 恢复原 AA ring，在其余架构相同条件下，实机吞吐约 **52 FPS**，达不到当前 60 FPS 目标。

## 5. 双缓冲为什么仍然只有约 45 FPS

即使 raster 和 send 分别接近 15 ms，旧双 framebuffer 架构仍存在约 4 ms 的隐藏关键路径：

```text
OLED_UpdateDMA()
  -> 冻结当前 transfer frame
  -> swap draw/stable
  -> 同步 oled_clear_buffer(next_draw)
  -> return UI owner
```

这个 clear 在 `submit_us` 之后执行，早期 trace 中不容易从当前帧 `clear_us` 直接看出来。通过累计指标核对，旧方案平均：

- 每帧清约 **57k pixels**；
- 同步 buffer clear 约 **4.2 ms/frame**。

所以所谓 `raster≈15 ms` 实际 UI owner 准备下一帧的总工作仍接近 19–20 ms。

## 6. 三 framebuffer + 后台 retire clear

最终 KK_OLED 使用三个物理 framebuffer：

```text
stable    最近完整恢复源
transfer  当前冻结给 display worker 的帧
draw      UI 当前绘制帧
spare     由上述角色轮转得到的预清理备用所有权
```

实际物理存储为 3 × 434,368 bytes（包含每帧 64-byte cache-line padding），相对两 framebuffer 增加约 **434 KB PSRAM**。最终 STATUS 仍有约 **6.55 MB PSRAM free**。

关键规则：

1. driver 未接受异步传输时，stable/draw/spare 全部精确回滚；
2. driver 已接受冻结 transfer 后，该 transfer 就作为最新完整 recovery source；
3. 旧 stable 不再在 UI owner 中同步清，而是送给 core 1、IDLE priority 的 retire clear task；
4. spare 未清好时，submit 返回 Busy，不允许覆盖其内存；
5. retire clear 完成后立即 `xTaskNotifyGive()` UI owner，避免 READY frame 白等下一次 timer/100 Hz input wake；
6. `OLED_Init()` 在 retire 尚未完成时拒绝重初始化，避免清空 DMA/后台任务仍可能拥有的 buffer。

### 为什么最后没有用 block/tile coverage

本轮实测了 8/16/32 pixel block coverage，以及固定左右双区间 coverage。它们确实减少 PSRAM 写像素数，但更多小 `memset` 和每像素 bookkeeping 反而拖慢整体：

| coverage | 清理写量趋势 | 实机结果 |
|---|---|---|
| 原单区间/row | ~57k px/frame | 旧同步 clear 最快基线 |
| 8 px block | ~34k px/frame | ≈41.5 FPS，反而更慢 |
| 16 px block | ~39k px/frame | ≈41.6 FPS |
| 32 px block | ~46k px/frame | ≈41.6 FPS |
| 左右双区间 | ~35k px/frame | ≈49.9 FPS（三缓冲条件），仍低于单区间 |

因此最终保留简单单 `[lo,hi)` coverage，把主要收益放在**隐藏 clear 延迟**而不是碎片化清除。

## 7. 菜单静态 chrome 缓存与 N-buffer 语义

菜单顶部 `APPLICATIONS`、底部提示、选中名称等静态内容不再每个动画帧重复清/画。

最终策略：

- mode 0：完整菜单 chrome；
- mode 1：carousel + selected label；
- mode 2：carousel only；
- 动态带默认 `y=100..340`；
- label 刷新可扩大到 `y<390`；
- 进入菜单或 index 变化时，refresh count 使用 `OLED_GetFrameBufferCount()`，不再写死 2。

三 framebuffer 引入后还发现一个重要耦合：`OLED_SetClearRows()` 不能同时代表“下一 draw 的 pre-clear policy”和“当前帧未来退休时的 clear policy”。否则 full-prime 好的 chrome 会在该 buffer 成为 old stable 后被 full-clear 擦掉。

最终新增：

```c
OLED_SetRetireClearRows(...)
```

每个物理 framebuffer 在绘制完成时记录自己的 retire clear policy。菜单 frame 即使本次是 full-prime，退休时也只清 carousel dynamic band；以后若 label/index 需要变化，再由 pre-draw policy 扩大清除范围。

最终 cache-on vs full-redraw 主机验证：**242 个实际提交画面逐字节一致**，mode 0/1/2 = **5 / 45 / 192**。

## 8. 调度与 watchdog 修复

60 FPS 初版出现过 IDLE0 task watchdog。根因不是 DMA crash，而是高负载下 main/UI owner 可持续 runnable，IDLE0 五秒得不到调度。

最终：

- substantial UI pass 后允许 bounded catch-up；
- READY frame 不允许被再次 raster 覆盖；
- display completion 会唤醒 UI owner；
- retire-clear completion 也会唤醒 UI owner；
- main 约每 100 ms 主动 `vTaskDelay(1)` 给 IDLE0，而不是每帧强加 1 ms；
- missed presentation slot 不创建无限 backlog，系统仍最多保留一个 READY drawing。

最终全部正式长测的 benchmark 会主动搜索 raw serial 日志中的 `watchdog`；3×60 秒通过，没有 watchdog 事件。

## 9. 最终验证矩阵

| 验证 | 结果 |
|---|---|
| Windows native CTest | **4/4 PASS** |
| WSL ASan + UBSan + `-Werror` | **4/4 PASS** |
| cache-on / full-redraw 最终等价 | **242 submitted frames byte-for-byte identical** |
| menu ring 性能路径视觉 A/B | 已量化，平均 1.424% framebuffer 像素变化，仅 1px rings |
| menu phase wrap A/B | 4,002 frames，平均 21.5 pixel/frame，max 0.252% framebuffer |
| ESP-IDF final build | PASS，无检出的 warning/error |
| COM11 3×60 秒菜单长测 | **61.649 / 61.415 / 61.695 FPS** |
| 18×10 秒窗口 | **全部 61.2–61.8 FPS** |
| USB 软件输入回归 | **20/20 PASS** |
| final clean state | HOME，timer=0，motor unarmed |
| board application readback | 与 frozen/build binary SHA-256 **完全一致** |

物理触摸没有在本轮人工操作；USB smoke 只证明软件输入路径。真实 STM32 UART 满流量和运行中的电机也未接入本轮 60 FPS 验收。

## 10. 最终镜像

冻结镜像：

```text
outputs/esp32-60fps-20260916/gl30_60fps_final.bin
```

大小：**837,856 bytes**。

SHA-256：

```text
42ACD0A34D52A654128DA48ECED2B02DAE5F5AFF32D9627CDCFEB33B19EB39B1
```

以下三者已实际逐字节 hash 核对一致：

1. `gl30_60fps_final.bin`；
2. `firmware-esp32/build-idf/gl30_haptic_control.bin`；
3. 从 COM11 Flash `0x10000` 按最终 binary 长度回读的 `board-readback-final.bin`。

## 11. 相比 35.706 FPS 基线的量化改善

基线来自 `outputs/esp32-dirty-audit-20260915/aggregate.json`。

- sustained FPS：35.706 → **61.587**，提升约 **72.48%**；
- send P95：26.565 → **14.934 ms**，降低约 **43.78%**；
- menu raster P95：21.229 → **15.784 ms**，降低约 **25.65%**；
- full-frame `memcmp`：最终正式长测调用增量仍为 **0**。

## 12. 不能外推的边界

本轮可以证明：

> 当前这块 ESP32-S3 + CO5300 样机，在 80 MHz QSPI、当前菜单 renderer、软件 ROTATE 连续负载下，3×60 秒平均 61.4–61.7 FPS，所有 10 秒窗口均 >61 FPS，且没有显示错误、输入丢弃或 watchdog。

本轮不能证明：

- 所有 app 页面都能达到 60 FPS；
- 80 MHz QSPI 是供应商对所有 Waveshare 1.32" 板批次保证的频率；
- 实屏在 80 MHz 长时间运行不存在肉眼可见的边缘色错/闪烁/撕裂；
- 真实 CST820 手指触摸与 60 FPS 同时负载已经人工验收；
- STM32 5 Mbaud 满流量、FOC/HAPTIC 帧和实体电机运行时仍保持完全相同的 60 FPS 分布；
- 单帧完成是硬实时 16.667 ms deadline。

下一阶段若继续追求“P95 也压到 16–17 ms”，问题已经不再是平均带宽，而是 presentation pacing、spare retire 偶发等待和任务调度长尾；应单独定义硬实时/低抖动验收，而不是继续只追平均 FPS。

## 13. 关键证据目录

```text
outputs/esp32-60fps-20260916/
├─ final-wrapped-3x60s/          # 最终 3×60 秒正式结果
│  ├─ run-1.json
│  ├─ run-2.json
│  ├─ run-3.json
│  ├─ summary.json
│  └─ aggregate-final.json
├─ bench-phase-wrap-60s/         # 相位归一化单组60秒确认
├─ final-cache-equivalence.json  # 最终 cache/full-redraw 等价
├─ fast-ring-visual-diff.json    # 1px ring 性能路径视觉差异
├─ menu-phase-wrap-visual-diff.json
├─ host-final.log
├─ sanitizer-final.log
├─ usb-smoke-final.json
├─ final-clean-state.json
├─ idf-phase-wrap.log
├─ gl30_60fps_final.bin
├─ board-readback-final.bin
└─ readback-final.log
```

失败实验目录（block coverage、优先级、10 ms throughput 等）保留作为工程判断证据，不代表最终固件配置。
