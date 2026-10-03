# KK 固件同源预览

2026-10-03 当前入口：[设置持久化、控制权交接与离线交付](../../../docs/offline-delivery-20261003-cn.md)。当前 CMake 有 8 项测试；NVS 配置保存与维护独占已实现，恢复不恢复 ARM/运行状态。下方 10/02 的 6 项测试及“尚未实现 NVS”描述是前序阶段历史，不代表本次上传版本。

ESP32 和此预览共同编译 `../../components/gl30_ui`、`kk_ui`、`kk_oled`；浏览器将本机 C 渲染器的 466×466 RGB565 帧解码后显示。

2026-10-02 只读回显续进：[零命令确认与重复停止回归](../../../outputs/esp32-zero-confirm-review-20261002/README_CN.md)。实际owner验证本地全零配置、非零nonce、完整UART接受及新鲜FAST/HAPTIC回显；消费新停止代次时换nonce，旧回显不能重新确认。公共snapshot在读取时复核年龄、pending ARM和停止代次，包含两锁间注入回归。三种配置各6/6及ESP-IDF构建通过，未烧录。NVS和维护独占未实现；本轮C画布/菜单/灯效算法不变，浏览器不连接电机。

2026-10-02 新增真实输入/owner源回归：[停止与旧ARM检查包](../../../outputs/esp32-input-stop-review-20261002/README_CN.md)。主机CMake现在共6项测试，额外执行实际`app_main.c`与`gl30_motor.c`的确定性交错，验证溢出清理后须重新ARM、半行旧授权和64位代次、UART异常与提交间隙后续零命令；专项294项断言检查。三种配置各6/6及ESP-IDF构建通过，未烧录；fake SDK不代表跨核调度或真实停机时限。浏览器仍不连接电机，界面画布未改动。

2026-10-02 当前菜单为八项、45°档距，灯效从“桌面 → 菜单 → 设置 → 灯效”进入；普通旋转和单击已通过实际 C 页面核对。此前“菜单/设置没有入口”的表述有误。底部新增只读电机状态和双语文字；读取快照时按 20 ms 检查 FAST 时效，失联不等于故障清除，显示不修改计时、手势或授权。见[画面与本轮验证](../../../outputs/esp32-motor-feedback-20261002/README_CN.md)。健康 READY/ACTIVE 链路即使未 armed 也发送零限值心跳，STM 同步撤使能/关桥；失效后恢复通信不自动授权，见[前一阶段链路记录](../../../outputs/idle-fault-and-link-review-20261002/README_CN.md)。

## 启动

本机 Windows 已有 MSVC、CMake、Python：

```powershell
& 'G:\Agent\GL30-Haptic-Control\firmware-esp32\ui\kk-preview\build-host.cmd'
& 'C:\Users\foke\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' 'G:\Agent\GL30-Haptic-Control\firmware-esp32\ui\kk-preview\server.py' --port 8770
```

打开 http://127.0.0.1:8770 。服务只监听本机。修改 C 代码时先结束这个预览服务再重新编译，以释放 Windows DLL；不用修改浏览器中的第二份业务逻辑。

`build-host.cmd` 中 MSVC 路径是本机实际安装位置。其它机器可在自己的 C 编译器终端运行 `cmake -S . -B build`、`cmake --build build`、`ctest --test-dir build`。

## 操作

- 单击屏幕或按下旋钮：确认；双击：后退。单击等候 300 ms，以免双击同时执行确认。
- 长按达到 650 ms：进入当前功能设置；松开不再生成单击。消抖 20 ms。
- 滚轮、左右箭头、`＋/−`：旋转。圆屏边缘拖动按角度调节，中心横拖也可操作。
- 空格按下/释放模拟真实按键；菜单旋转一格切换一项，双向无限循环。
- 计时器一格 1 分钟、一圈 60 分钟，上限 6 小时；运行/暂停时都可转动调时。计时结束切到完成页，反向保持零，正向重新设定。
- 秒表可在后台运行；侧键“重置当前计时”可清零当前秒表或计时器。
- 灯效入口：桌面单击 → 转到设置并单击 → 转到灯效并单击。转动选效果/颜色/亮度 → 单击编辑 → 转动调值 → 单击保存；双击先退出编辑，再退到设置。
- 屏幕开关只关闭显示，计时继续。第一次短按或双击只唤醒。

演示工具中的“快进 60 秒”只用于检查时间推进；普通运行按实际经过时间计时。

## 真实边界

- 中心屏幕是 C 画布；外壳、外围光带是浏览器绘制的产品示意。原机械模型未改动。
- C 模型计算 24 颗 RGB 的目标颜色和旋钮角度，菜单只有一颗蓝色 LED。图中的连续光带代表扩散罩观感，不代表实体 LED 光学验收。
- 模型的零点 `endstop` 是止挡意图；本预览不连接电机。屏幕固件已带默认未使能的菜单通信链路，详见 [9/15 优化与边界](../../../docs/esp32-menu-optimization-20260915-cn.md)；当前测试不向电机输出电流，也不播放或驱动实体声音。
- 天气为示例，画面中明确标示；表盘和月历按北京时间显示。预览从电脑取得真实 Unix 时间；板端未校时时显示“待校时”。
- 闹钟、手感页当前为明确的待接入页面；设置页已实现屏幕亮度。灯效设置本次运行内保存，未写入 NVS。
- 普通预览不连接 STM32，因此底部默认显示“电机失联”；本轮报告里的其它电机状态画面来自明确注入的模拟数据，不是实板反馈照片。20 ms 只约束读取时的证据年龄，不是物理屏幕的显示延迟。

## 验证入口

`tests/model_tests.c` 检查计时、菜单、灯效、按键和边界；`tests/render_tests.c` 检查 RGB565 裁剪/旋转、跨度/清屏等价性、异步模拟冻结、错误恢复及 KK_UI 实际页面入口；`tests/motor_menu_tests.c` 通过真实 C 编码和 STM 触觉算法验证 q/f、会话、跨零和失效处理。软件菜单复用 KK_UI 的 200 ms Q12 缓动；实际电机位置不叠加追赶动画。

`tests/input_stop_tests.c`包括真实解析任务及UI有界pass；`tests/motor_stop_tests.c`包括真实owner与线协议帧。测试用`stop_fake`替代SDK，并用注入/有界退出检查停止竞态；不发送串口数据。普通浏览器演示不提供MOTOR ARM操作。详见上方修订包中的日志、源码快照和时序边界。

浏览器脚本为 `verify-browser.js`。长脚本应在浏览器后台启动并读取 `window.gl30Checks`，不要让 CLI 的同步 eval 等待整个测试，避免超时重试重复执行同一个交互序列：

```powershell
agent-browser --session gl30-kk open http://127.0.0.1:8770
$body = Get-Content -LiteralPath '.\verify-browser.js' -Raw
$js = 'window.gl30Checks={status:"running"};' + $body.Trim().TrimEnd(';') + '.then(r=>window.gl30Checks=r).catch(e=>window.gl30Checks={status:"failed",error:String(e)}); "started"'
$encoded = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($js))
agent-browser --session gl30-kk eval -b $encoded
# 完成其它工作后读取结果
agent-browser --session gl30-kk eval 'window.gl30Checks'
```

该脚本用 DOM 键盘/侧键，并用 HTTP 做批量旋转和时间快进。它验证同一 C 模型，不能替代实屏的色序、触摸方向、帧率或真实手感检查。
