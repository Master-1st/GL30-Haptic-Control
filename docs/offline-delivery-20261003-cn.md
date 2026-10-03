# GL30 当前代码与离线交付（2026-10-03）

本次同步当前 STM32、ESP32、线协议和主机端源码、项目本地 KK Skills、A 板无测试点 AD 原理图快照及画板指南。代码仍是 **pre-alpha 离线候选**，没有把历史台架验证扩展成自制产品板通过。

[GitHub 交付下载](https://github.com/Master-1st/GL30-Haptic-Control/releases/tag/offline-code-20261003) · [A 板画板指南](pcb-drawing-guide-20261003-cn.md) · [原生 AD 快照](../hardware/pcb/stm32-foc-a-altium-r6/README_CN.md)

机械 STEP/STL/DXF 与 40 页修正版装配 PDF 也作为发布附件提供，见[机械交付说明](mechanical-delivery-20261003-cn.md)。

## 当前行为

- 产品 STM32G474CET6 的 AS5048A SPI1 只读端口、驱动配置读回、无转矩上电准备、三通道采零、失联/故障关断及控制权交接已接入。电角零位有效宏仍为 0。
- ESP32-S3 使用 KK_OLED/KK_UI 派生的 466×466 RGB565 界面。C 画布和主机预览同源，菜单显示可跟随 STM32 反馈；新增 13 项 NVS 配置保存，不保存 ARM、故障、运行状态或系统时间。
- 写 Flash 前须精确收到 STM32 RELEASE 回显，并取得显示 DMA 的独占持有；无对端时不写。控制权恢复后的首次零握手使旧 ARM 失效，出力仍需新显式请求以及 STM32 自身保护条件。
- 唯一当前协议载荷为 HAPTIC_COMMAND 72 字节、HAPTIC_STATE 44 字节、CONTROL_LEASE 24 字节；两端和 TypeScript 金样已同步。
- 天气联网、实际闹钟、真实电机驱动计时及整机手感尚未完成验收。旧 60 FPS 实测仅对应当时固件，不是本次未烧录代码的实屏结果。

## 已留存验证

| 检查 | 结果 | 范围 |
| --- | --- | --- |
| STM32 主机回归 | Debug/ASan+UBSan/Release+LTO，各 15/15 | 生产源与确定性假硬件，不代表实体保护波形 |
| ESP32 当前源回归 | 同三配置，各 8/8 | 模型、渲染、DMA、输入停止、UART 所有权、设置保存 |
| TypeScript | 四个 typecheck、25/25 单测、SIM_ONLY E2E | 无串口模拟链路 |
| ESP-IDF 5.5.1 | 构建通过 | 未烧录 |
| Keil 产品/NUCLEO 两目标 | 均 0 错误/0 警告 | 新候选未烧录，不取代 H25 原台架资格 |
| 独立代码审查 | 主线程、DeepSeek、Qwen；失败与修正也保留 | 最终裁决和各轮覆盖关系在交付 ZIP 中 |

以上是上传前已经冻结的验证。上传副本已重新通过 GCC 三配置 STM32 15 项、ESP32 当前源 8 项、历史 UI 3 项，以及 TypeScript 25 项；本次日志和两处历史测试构建/输入修正见[上传副本验证](evidence/publication-20261003/README_CN.md)。GitHub CI 在提交后的 Actions 页面另行记录。原始冻结记录见[最终验证 JSON](evidence/offline-20261002/verification-final.json)、[代码裁决](evidence/offline-20261002/code-adjudication-cn.md)和[评审参与记录](evidence/offline-20261002/review-final.json)。完整日志、各轮证据、来源清单和分目标固件在发布附件中。

## 离线包完整性

公开附件 `GL30_offline_delivery_20261003.zip` 为带许可证的交付封装，6,224,204 字节。SHA-256：

```text
46706f809cc8c3c5057c4bbf9bae69915649080ff172954a5b834b3e177543a0
```

其中 `frozen/GL30_离线代码交付_20261002.zip` 是未改动的原始冻结包，6,163,945 字节。原包 SHA-256：

```text
46f9f417004682083d7d299c69fe2a91eeeed1e4b8ad6ba9530bf0d879a5148c
```

原包有 586 个文件条目，包括源快照、验证日志与候选固件；公开封装另外附上 70 份项目、ESP-IDF 和相关组件许可证、使用说明及哈希清单。它是相关代码交付，需要本仓库和对应 SDK 重建。Git 使用换行规范化，包内 source-after.json 是原本机字节哈希，不应直接拿 CRLF 哈希判断 Linux Git 工作树损坏。公开版文档/CI/AD 项目列表另有上传整理，固件生产代码保持冻结版本的语义。

历史 H25 资料仍在 `docs/evidence/haptic25-20260908`。其冻结源码身份检查继续约束 H25 实物脚本，不表示当前代码获得 H25 实测资格。代码、模型和日志分别保留证据边界。

## 在干净源码目录检查

```sh
pnpm install --frozen-lockfile
pnpm verify
cmake -S firmware-stm32/tests -B build/stm32-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stm32-host
ctest --test-dir build/stm32-host --output-on-failure
cmake -S firmware-esp32/ui/kk-preview -B build/esp-current -DCMAKE_BUILD_TYPE=Debug
cmake --build build/esp-current
ctest --test-dir build/esp-current --output-on-failure
```

Windows 用已安装的 MSVC 开发环境与 CMake；Linux 用 GCC/CMake。固件构建分别看 STM32 和 ESP32 目录说明。上述主机检查不会打开串口或驱动电机。

历史工程文档中的 `outputs/...` 链接指向本机工程归档；不把全部中间产物、原厂模型、个人库或采购截图塞进 Git。最新公开下载和画板入口以本页为准。本次没有新增 Gerber、PCB DRC 或自制板上电结果。
