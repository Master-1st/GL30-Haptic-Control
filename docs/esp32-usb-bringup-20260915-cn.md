# ESP32-S3 USB 实机调试记录（2026-09-15）

用户已连接 Type-C，并授权先烧入固件，晚上再检查实物。本次证据针对这块屏幕板；不继承 STM32、电机、外部灯带或音频的整机验收。

## 已连接的设备与备份

- 本次使用 `COM11`，Espressif USB Serial/JTAG，VID/PID `303A:1001`。没有操作 ST-LINK 的 COM4。
- esptool 识别为 ESP32-S3-PICO-1，revision v0.2；8 MB Flash、8 MB PSRAM，40 MHz 晶振。
- 烧录前读取原始 Flash 全部 8,388,608 字节，保存为 [original-flash-8MB.bin](../outputs/esp32-debug-20260915/original-flash-8MB.bin)。SHA-256：`7DBEB9160BC72C924FCA1DDE727FF877A970C977028D39FEABE361998AB8D6E2`。后续重烧没有覆盖该备份。
- 初次烧录 bootloader、分区表、应用，偏移分别为 `0x0`、`0x8000`、`0x10000`；后续调试仅更新应用。每次均检查 esptool 的数据校验结果。

启动日志确认 PSRAM 自检通过、CPU 240 MHz、CO5300 初始化成功、CST820 初始化成功（ID 183）。这证明驱动初始化与传输链路运行，不等于已经目视确认颜色、裁切、触摸方向或撕裂。

## 实机发现并修正的问题

初版 16 行一段、共 30 段的整帧传输耗时约 294–302 ms。分段计时显示其中 265–274 ms 花在等待传输完成，实际复制约 24 ms。

在当前固定的 ESP-IDF 5.5.1 中，`components/esp_lcd/spi/esp_lcd_panel_io_spi.c` 的 `lcd_spi_post_trans_color_cb` 没有处理用户完成回调返回的唤醒标记。应用只返回 `true`，被唤醒任务仍可能等到下一次 10 ms 系统 tick。修正为回调在信号量释放后显式 `portYIELD_FROM_ISR()`；未修改 SDK 或全局 tick。

只改变上述回调，实机整帧从约 302 ms 降到 50.2 ms，等待总和从约 274 ms 降到 21.9 ms。再启用 `CONFIG_COMPILER_OPTIMIZATION_PERF`，传输约 35.2 ms。保留断言、超时和显示故障锁存；未提高 QSPI 时钟，也未改变任何引脚。

随后将照片的固定遮罩预混入唯一一份 RGB565 资产，非旋转位图按已裁剪的整行复制。主机对比 45 个完整画面，优化前后逐字节一致。静止页面按内容变化请求刷新，表盘秒数变化才重画；计时器、秒表、菜单与动态灯效保留动画请求。

实测仅优化绘制仍会漏掉 100 ms 的短按：最初 USB 测试停在“单击进入菜单”断言。最终把触摸与 USB 接收放入 core 1 的独立 100 Hz 输入任务，通过 64 条定长事件队列携带采样时间。core 0 的 UI 任务独占模型、手势和绘制；批量处理事件时不逐条重画，也不先把模型时钟推进到尚未消费事件的未来。队列丢失事件时取消整段手势，避免伪造单击。

## 最终验收记录

| 验证 | 结果 / 原始证据 |
| --- | --- |
| ESP-IDF 5.5.1 构建 | 通过；[build-input-task.log](../outputs/esp32-debug-20260915/build-input-task.log)，应用 790,416 字节 |
| 应用烧录校验 | `Hash of data verified`；[flash-input-task.log](../outputs/esp32-debug-20260915/flash-input-task.log) |
| 主机 C 回归 | 模型/手势 62 项、RGB565/共享 UI 98 项，共 160 项通过；包含 60 ms 触摸样本回放、双击、旋转后确认 |
| ASan / UBSan | 两组测试通过；[host-sanitized-final.log](../outputs/esp32-debug-20260915/host-sanitized-final.log) |
| 画面一致性 | 45 个整屏 RGB565 画面逐字节一致；[compare-frames.json](../outputs/esp32-debug-20260915/compare-frames.json) |
| 浏览器同源回归 | 41 项通过；[browser-checks-final.json](../outputs/esp32-debug-20260915/browser-checks-final.json) |
| 实机 USB 软件输入 | 20 项通过：单击、双击、循环菜单、33% 音量与零点限幅、计时器运行中调时、秒表、灯效编辑与非法命令拒绝；[完整状态](../outputs/esp32-debug-20260915/usb-ui-smoke-after-input-fix.json) |
| 采样与错误 | 本次操作序列最大采样间隔 10,012 µs；队列丢事件、显示传输错误、触摸读取错误均 0 |
| 内存 | 操作序列 UI 栈最小剩余 840 字节，输入栈 2,536 字节；未见 panic / 看门狗重启 |
| 最终运行状态 | 测试后重新启动，USB 校时收到确认；约 25 秒采样保持表盘、计时器归零、秒表停止、无上述错误；[final-running.log](../outputs/esp32-debug-20260915/final-running.log) |

**性能边界：**整帧传输约 35 ms，不代表整幅界面达到 30 fps。此次复杂页面的一次完整处理/绘制约 0.16–0.26 秒，动画流畅度仍需后续渲染优化；100 Hz 只指输入采样，不能作为显示帧率。此次通过的是软件事件与总线运行验证，实体手指操作尚待用户检查。

本次主机增量构建曾因预览占用 DLL 而链接失败；已修正 `build-host.cmd` 对负返回码的识别，停止对应预览后重新链接，并确认新测试二进制实际执行。Spark 原生子代理在当前账户不可用；新增测试、输入任务及最终集成验收由主线程完成。照片预混资源经独立处理后，由主线程逐帧比对验收。

最终二进制及 ELF 放在 [firmware](../outputs/esp32-debug-20260915/firmware/)；[firmware-manifest.json](../outputs/esp32-debug-20260915/firmware-manifest.json) 包含文件和源码校验值。应用 SHA-256：`836eee9af7cbcc95aac987eb4e8e889dabaf14edfd6bb52538e1c989e9aa60fb`。原始备份独立保留，不与新固件混用。

## USB 调试方式

控制台使用板子的原生 USB，不占 STM32 二进制 UART。命令以换行结束：

| 命令 | 用途 |
| --- | --- |
| `STATUS` | 查询画面、手势/功能状态、内存、帧耗时、触摸读取/错误计数 |
| `TIME <Unix秒>` | 按电脑真实 Unix 时间校时；显示层固定为北京时间 UTC+8 |
| `BUTTON 1` / `BUTTON 0` | 软件模拟按下/释放，经过真实 UI 手势识别 |
| `ROTATE <整数>` | 软件输入 -360 至 360 格的旋转事件 |

后两项只验证软件交互，不证明手指触摸、实体按钮或电机已工作。

可运行下面的只读状态采样和校时工具；先收到 `STATUS` 才发送校时，并检查确认消息，避免刚开机时命令丢失：

```powershell
& 'G:\Agent\.tools\esp-idf-tools\python_env\idf5.5_py3.14_env\Scripts\python.exe' `
  'G:\Agent\GL30-Haptic-Control\firmware-esp32\tools\usb_status.py' `
  --port COM11 --sync-time --samples 3 --interval 2
```

串口号可能随电脑或 USB 接口变化。工具会核对 Espressif VID/PID。一次只开一个串口程序；程序结束会释放串口。UI 自动验收脚本为 [usb_ui_smoke.py](../firmware-esp32/tools/usb_ui_smoke.py)，应在重启到初始表盘后运行，会修改当前 UI 测试状态。

## 断点调试与尚未验证项目

USB 烧录、日志和状态查询已打通。OpenOCD 断点调试尚未打通：实际报告 `LIBUSB_ERROR_NOT_FOUND`。只读检查表明 MI_02 使用 Windows 通用 WinUSB，未注册设备接口 GUID；Espressif 专用驱动包含该注册项。参考 [Espressif 官方说明](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/api-guides/jtag-debugging/configure-builtin-jtag.html)，需要管理员安装相应驱动。本次未修改系统驱动、注册表或执行需要现场 UAC 的安装。

用户当前不在板旁，所以没有声称完成实屏目视和实际触摸验收。晚上检查：表盘照片/中文/时间、单击进菜单、沿边缘滑动换项、双击后退，以及计时器运行中调整。长按仍按此前约定保留。

本记录初次烧录时，断电重启会显示“待校时”。同日后续已增加并实机验证 Type-C 自动校时，详见 [自动校时与 KK_UI 动画使用情况](esp32-auto-clock-and-kk-animations-20260915-cn.md)；本文件上面的二进制哈希和测试数据保留为上一阶段记录。Wi-Fi/NTP 和 RTC 自动校时尚未接入，天气仍为明确标识的示例。STM32/FOC 链路、实体旋钮反馈、外部灯环和真实音频未在本次固件中驱动。
