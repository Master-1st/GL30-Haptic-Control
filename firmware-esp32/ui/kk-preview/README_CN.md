# KK 固件同源预览

这不是第二份 JavaScript 界面模型。ESP32 和此预览共同编译 `../../components/gl30_ui`、`kk_ui`、`kk_oled`；浏览器将本机 C 渲染器的 466×466 RGB565 帧解码后显示。

## 启动

本机 Windows 已有 MSVC、CMake、Python：

```powershell
Set-Location firmware-esp32\ui\kk-preview
.\build-host.cmd
python .\server.py --port 8770
```

打开 http://127.0.0.1:8770 。服务只监听本机。修改 C 代码时先结束这个预览服务再重新编译，以释放 Windows DLL；不用修改浏览器中的第二份业务逻辑。

`build-host.cmd` 中 MSVC 路径是本机实际安装位置。其它机器可在自己的 C 编译器终端运行 `cmake -S . -B build`、`cmake --build build`、`ctest --test-dir build`。

## 操作

- 单击屏幕或按下旋钮：确认；双击：后退。单击等候 300 ms，以免双击同时执行确认。
- 长按达到 650 ms：不分配动作，松开也不生成单击。消抖 20 ms。
- 滚轮、左右箭头、`＋/−`：旋转。圆屏边缘拖动按角度调节，中心横拖也可操作。
- 空格按下/释放模拟真实按键；菜单旋转一格切换一项，双向无限循环。
- 计时器一格 1 分钟、一圈 60 分钟，上限 6 小时；运行/暂停时都可转动调时。计时结束切到完成页，反向保持零，正向重新设定。
- 秒表可在后台运行；侧键“重置当前计时”可清零当前秒表或计时器。
- 灯效中：转动选效果/颜色/亮度 → 单击编辑 → 转动调值 → 单击保存。双击先退出编辑，再返回菜单。
- 屏幕开关只关闭显示，计时继续。第一次短按或双击只唤醒。

演示工具中的“快进 60 秒”只用于检查时间推进；普通运行按实际经过时间计时。

## 真实边界

- 中心屏幕是 C 画布；外壳、外围光带是浏览器绘制的产品示意。原机械模型未改动。
- C 模型计算 24 颗 RGB 的目标颜色和旋钮角度，菜单只有一颗蓝色 LED。图中的连续光带代表扩散罩观感，不代表实体 LED 光学验收。
- 模型的零点 `endstop` 是止挡意图；本预览不连接电机。屏幕固件已带默认未使能的菜单通信链路，详见 [9/15 优化与边界](../../../docs/esp32-menu-optimization-20260915-cn.md)；当前测试不向电机输出电流，也不播放或驱动实体声音。
- 天气为示例；表盘和日历按北京时间显示。预览从电脑取得真实 Unix 时间；板端未校时时显示“待校时”。
- 闹钟、手感页当前为明确的待接入页面；设置页已实现屏幕亮度。灯效设置本次运行内保存，未写入 NVS。

## 验证入口

`tests/model_tests.c` 检查计时、菜单、灯效、按键和边界；`tests/render_tests.c` 检查 RGB565 裁剪/旋转、跨度/清屏等价性、异步模拟冻结、错误恢复及 KK_UI 实际页面入口；`tests/motor_menu_tests.c` 通过真实 C 编码和 STM 触觉算法验证 q/f、会话、跨零和失效处理。软件菜单复用 KK_UI 的 200 ms Q12 缓动；实际电机位置不叠加追赶动画。

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
