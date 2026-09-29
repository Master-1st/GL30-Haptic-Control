# GL30 UI 与显示流水线恢复审计 — 2026-09-29

## 1. 原 main 缺失什么

本次开始时，远端 Git ref 和独立 clone 均确认 `main` 为
[`26da47a61862be94ba880a13a0b63a96d2073714`](https://github.com/Master-1st/GL30-Haptic-Control/commit/26da47a61862be94ba880a13a0b63a96d2073714)，
提交时间为 2026-09-08 05:13:22 UTC，标题为
`feat: publish H25 bounded bench validation and reproducible evidence (#6)`。
该基线的 `firmware-esp32` 只有八份 README，没有后续应用、`gl30_render.c`、
`KK_UI`、`KK_OLED`、桌面背景和显示调度实现。因此，不能将“仓库没有天气
图标”当成“已经从实现的桌面中删除天气图标”。

| 用户要求 | 原 main 核对结果 | 本分支实现位置 |
| --- | --- | --- |
| 桌面删除大天气图标、保留山景 | 桌面及资源未发布 | `components/gl30_ui/src/gl30_render.c` 与 `assets/gl30_summit.c` |
| 去掉主题名称，日期靠近时钟 | 对应布局未发布 | 同一桌面绘制函数 |
| 调整月历与星期栏 | 月历状态与网格未发布 | `gl30_model.c`、`gl30_render.c` |
| 环形菜单、统一大小图标、文字不越界 | 菜单和字体未发布 | `gl30_render.c` 与同源 host renderer |
| 恢复渲染/发送重叠并排查帧率 | 主循环和显示驱动未发布 | `main/app_main.c`、`kk_ui.c`、`kk_oled.c`、`gl30_board.c` |
| 发布后续 UI/性能改动 | 远端无对应开发提交 | 独立分支 `fix/ui-display-pipeline-20260929` |

表中实现路径相对于 `firmware-esp32`。

## 2. 源码恢复范围

本机存在 `ui/esp32-round-ui-20260917` 工作区：提交仍停在上述 main，但工作树
包含未提交的完整 ESP32 应用、字体/山景、驱动、预览、测试与构建入口。
本次从远端 main 建立干净分支，提取这些必要源码；未整体提交混有机械、台架
及其他 STM32 实验的主工作区。

恢复范围为 ESP-IDF 配置、五个 ESP32 组件、BSP、主循环、遥测与实验菜单
协议模块、C 预览、host 测试和 USB 诊断工具。生成的 `sdkconfig`、
`managed_components`、本机构建目录及二进制不进入源码。
本次 `VERSION` 为 `esp32-ui-20260929`，用于区分历史采集。

共享 `firmware-stm32/protocol/v6_protocol.c/.h` 仅增加 `0x06 HAPTIC_STATE`
的 36 字节类型、编解码及编码长度检查，原协议向量保持不变。这两份文件不在
H25 冻结的 37 项源码清单中。本次未为 STM32 应用增加该报文生产者，也未
修改 H25 台架程序、既有证据或 hardware-gates 的验证结论。

## 3. UI 恢复及补齐

桌面保留山景与轻量天气文字，去掉大天气图标和主题名称；星期、短日期集中在
时钟下方。活动计时状态移入圆屏可见区域。日历包含月份标题、七列星期、六行
日期空间、分隔线、当天高亮，以及周日/周一作为首日的设置，处理跨月与闰年。

恢复版仍有大小图标采用两份几何的遗漏。本次统一声音、手感、设置、天气等
图标的来源和轮廓，并按真实外接尺寸校准 14 / 6 / 3 mm 设计目标。
换算基于 466 像素与标称 1.32 英寸圆屏，是布局校准，未进行实体量具测量。

英文仍为默认语言，设置中可切换中文。恢复版标题字体是子集，部分中文标题
缺字；本次使用已有完整字体补齐，并检查温度符号和圆屏裁切。预览直接来自
设备实际 C renderer，没有另外建立 JavaScript 业务界面。

## 4. 历史调度问题与本次修复

历史差异保存在
[cadence-before-to-release.diff](evidence/esp32-ui-20260929/history/cadence-before-to-release.diff)，
双方 SHA-256、原位置及汇总来源见
[manifest.json](evidence/esp32-ui-20260929/history/manifest.json)。

| 历史代码行为 | 已恢复的行为 | 对流水线的影响 |
| --- | --- | --- |
| 只在 `frame_due` 分支调用 render/submit | 每次循环先调用 `gl30_demo_service_display()` / `KK_UI_ServiceDisplay()` | DMA/备用帧完成后，READY 帧立即获得提交机会 |
| render 调用后无条件消费帧槽 | 只有实际 raster 或静态页面才消费槽 | 缓冲受阻时保留到期槽，释放后继续 |
| 一旦越过下一时间点就跳到未来槽 | 只有整周期积压才计 drop 并跳过 | 不足一周期的迟到不浪费整帧 |
| 绘制、发送和旧帧清理互相等待 | 三缓冲分别管理绘制、READY/传输与回收 | 发送中可绘制下一帧，READY 和 DMA 引用保持不可变 |

本次还修复恢复版中的两处性能问题：

- FreeRTOS 100 Hz 下 `pdMS_TO_TICKS(4)` 为零；受阻等待现在至少一个 tick，
  DMA/备用帧通知仍可提前唤醒，避免忙轮询。
- 原调度每帧两次调用完整指标接口以判断是否 raster，连带执行 P95 排序。
  调度改读轻量计数，百分位计算保留在状态诊断路径。

渲染和发送重叠时，不能将两者耗时简单相加作为稳态帧周期。主机缓冲所有权、
背压与 DMA 模拟测试也不能预测当前固件的实机 FPS；实际吞吐仍取决于
PSRAM、QSPI、回收任务、触摸与通信负载。

## 5. 历史实测按原始数值读取

以下为恢复的 **2026-09-16/17 历史汇总**，不是本分支重新烧录的实测。
各阶段还包含 renderer 等其他改动，不能作为严格单变量 A/B。

| 历史记录 | 完成帧吞吐 FPS | 原始汇总 |
| --- | ---: | --- |
| audit baseline | 27.9729 | [audit-baseline-summary.json](evidence/esp32-ui-20260929/history/audit-baseline-summary.json) |
| completion service 阶段 | 53.9011 | [service-display-summary.json](evidence/esp32-ui-20260929/history/service-display-summary.json) |
| 保留受阻帧槽阶段 | 59.7849 | [slot-retry-summary.json](evidence/esp32-ui-20260929/history/slot-retry-summary.json) |
| 9/16 最后三次 60 秒 | 59.9959 / 60.0021 / 59.9821 | [final-3x60s-summary.json](evidence/esp32-ui-20260929/history/final-3x60s-summary.json) |
| 9/17 菜单 60.003718 秒，3599 帧 | 59.9796 | [benchmark-60s-summary.json](evidence/esp32-ui-20260917/benchmark-60s-summary.json) |
| 9/17 桌面/月历最后修改后 10.033865 秒，600 帧 | 59.7975 | [release-smoke-10s-summary.json](evidence/esp32-ui-20260917/release-smoke-10s-summary.json) |

名为 `current-40fps-recheck-20260916` 的目录实际记录 **60.0086 FPS**，见
[current-recheck-summary.json](evidence/esp32-ui-20260929/history/current-recheck-summary.json)。
不能将目录名中的“40fps”当作测量值。

这些 summary 的 `pixel_uniqueness_verified=false`，统计已完成 raster
标识，未独立证明每帧像素唯一。历史 `motor_tx=0`，不覆盖真实 STM32 控制
流量。旧截图保存在 `evidence/esp32-ui-20260917/`，本次预览单独归档。

## 6. 当前 STM32 配套边界

恢复的菜单控制需要 `HAPTIC_STATE` 的 profile、nonce 和模式回执；当前
已发布 STM32 应用不生产该回执。合成回执的 host 测试只能验证解析/会话，
不能证明实物联调。如果直接恢复 ARM，可能先发送控制命令，约 100 ms 后
才因缺少回执退出，因此本分支不将这条开发路径默认开放。

默认固件保留遥测接收、禁用电机发送，`MOTOR ARM` 明确拒绝。
`CONFIG_GL30_EXPERIMENTAL_MOTOR_CONTROL=y` 仅用于另外配套的开发固件。
本次 UI/帧率验证不启用该选项，也不改变 STM32/H25 的硬件放行结果。

## 7. 可复现验证

构建命令见 [ESP32 README](../firmware-esp32/README.md)，自动入口见
[CI](../.github/workflows/ci.yml)。验证覆盖 UI/模型、缓冲与 DMA、协议/会话、
资产/工具、ESP-IDF 编译，以及已有 STM32/TypeScript 回归。
主机测试和编译只证明对应软件行为。本次没有烧录或重新测量面板 FPS。

H25 身份校验保留原命令与原清单：

```sh
python firmware-stm32/bench/NUCLEO_G474RE_FOC/tools/verify_source_identity.py \
  docs/evidence/haptic25-20260908/source-identity.json
```

hardware-gates、H25 证据、37 项冻结源码和黄金协议向量保持相对于
`26da47a61862be94ba880a13a0b63a96d2073714` 原样，CI 继续执行既有身份校验。
