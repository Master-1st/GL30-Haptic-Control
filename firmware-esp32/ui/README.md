# ESP32 界面与历史参考

当前运行和预览使用 `components/gl30_ui` 与 [kk-preview](kk-preview/README_CN.md) 的同源 C 画布；构建入口是 `ui/kk-preview/CMakeLists.txt`。下方 LVGL 模型和 SVG 目录是历史设计参考，不是当前固件或运行时备用路径。最新离线结果见[公开交付说明](../../docs/offline-delivery-20261003-cn.md)。

# UI (ESP32-LVGL)

该目录仅负责 **UI 表现层（模型 + 视图）**，不包含 UART、Motorcontrol、GPIO 或 OTA/Flash 路径。

硬件锚定：`Waveshare ESP32-S3-Touch-AMOLED-1.32`，显示分辨率 `466x466`。
官方基线显示链路记录见 `../../docs/waveshare-baseline.md`（LVGL v9，30 行双缓冲提示）。

## 1) Native C model API（无 LVGL 依赖）

- Header: `include/gl30_ui_anim_model.h`
- 输入类型：`gl30_ui_anim_input_t`
  - `volume`（0~100，默认 `42`）
  - `timer_minutes`（0~120，默认 `15`）
  - `timer_running`, `mode`, `wake_signal`, `off`, `connected`, `fault`
- 输出类型：`gl30_ui_anim_output_t`
  - 平滑后的 `value_ratio`、`dial_angle_deg`
  - `wake_level`
  - 文本：`value_text`, `mode_text`, `status_text`, `accessibility_text`
- API:
  - `gl30_ui_anim_model_init(gl30_ui_anim_model_t*)`
  - `gl30_ui_anim_model_tick(model, input, elapsed_ms, output)`
- 行为约束：
  - `NaN`/`inf` 输入被夹紧并降级到默认值
  - `off`：直接清空动态显示并强制 `wake_level=0`
  - `lost/fault` 只通过 `output->lost_connection` / `output->fault` 反映给视图
  - `gl30_ui_anim_model_tick` 可接受 `input == NULL`，此时视图进入断连显示（`connected=false`）

## 2) LVGL 视图（C）

- Header: `include/gl30_ui_anim_view.h`
- 生命周期：
  - `gl30_ui_view_create(parent, 466)`：创建 466x466 屏幕组件；`parent == NULL` 时挂到 active screen
  - `gl30_ui_view_render(view, output)`：按模型输出更新数值、角度、颜色、亮度
  - `gl30_ui_view_destroy(view)`
- 显示特性：
  - 黑底+蓝色弧线（`2D9DFF`），故障/断联会改变提示色
  - `off` 时变为黑屏
  - 本模块没有每帧显式堆分配，标签使用静态字符缓冲；不保证 LVGL 内部绘制过程零分配

## 3) 独立编译（离线/主机层）

```bash
cmake -S firmware-esp32/ui -B firmware-esp32/ui/build
cmake --build firmware-esp32/ui/build --config Debug
ctest --test-dir firmware-esp32/ui/build -C Debug --output-on-failure
.\firmware-esp32\ui\build\Debug\gl30_ui_host_model_demo.exe
```

不需要 ESP、LVGL 或串口，用于验证纯 C 动画模型；非 MSVC 平台链接数学库。

## 4) 启用 LVGL host preview

如果工程有可用 LVGL 源码：
```bash
cmake -S firmware-esp32/ui -B firmware-esp32/ui/build-host -DGL30_UI_BUILD_HOST_PREVIEW=ON -DGL30_UI_LVGL_SOURCE_DIR=<LVGL_SOURCE_DIR>
cmake --build firmware-esp32/ui/build-host --config Debug --target gl30_ui_host_demo
.\firmware-esp32\ui\build-host\Debug\gl30_ui_host_demo.exe
```

如无本地源码，也可用：
```bash
cmake -S firmware-esp32/ui -B firmware-esp32/ui/build-host -DGL30_UI_BUILD_HOST_PREVIEW=ON -DGL30_UI_FETCH_LVGL=ON
```

生成的 `gl30_ui_host_demo` 依赖 `gl30_ui_anim_view` 与同层模型层（仍为纯宿主 C 目标）。
拉取时固定到 LVGL commit `aa7446344c6ec7631112ef031983ef24077e24d5`（9.2.0），本轮实际主机构建使用同一提交。本地源码覆盖选项由调用者负责版本一致性。

程序在当前目录生成 `gl30_ui_host_awake.ppm` 和 `gl30_ui_host_off.ppm`，并验证刷新确实发生、亮屏含蓝色像素、熄屏全部像素为黑；失败退出非零。它是无窗口的真实 LVGL 渲染检查，不是 ESP 固件。主机 RGB888 全帧缓冲仅用于截图，不作为 ESP 内存/像素格式配置。MSVC 静态链接时移除上游 CMake 错加的 DLL 数据属性。

## 当前屏幕实现（2026-09-14）

新固件使用 [KK_OLED + KK_UI 的 C 同源预览](kk-preview/README_CN.md)，实际业务代码在 `../components/gl30_ui`。九项圆环菜单、表盘、彩色圈、可回调计时器、秒表与灯效已进入这条 C 实现。下方 SVG 和上方 LVGL 章节记录各自历史实现，不代表新固件状态。验证层级见 [本次接入报告](../../docs/esp32-kk-integration-20260914-cn.md)。

## 5) 浏览器 SVG 交互演示（2026-09-10，历史设计参考）

- 入口：`preview/index.html`，双击即可打开；操作与复现说明见 `preview/README_CN.md`。
- 原生 JavaScript 状态模型与 SVG 视图：黑色旋钮、固定圆屏、外环旋转/释放按压、四侧键、后键；图标/结构为 SVG，新增项目内置的 1254×1254 日照金山生成背景，运行无需远端依赖。
- 计时从零设定，一圈 60 分钟，演示上限 360 分钟，显示 HH:MM:SS。按最新用户反馈采用烤箱式自动回转，运行/暂停中可手动调时；连续灯带以蓝色标示前端，多圈分别着色；零点有模拟止挡。保留熄屏继续、首次按压仅唤醒与标注 450× 的 12 秒演示。
- 保留模拟音量/静音，加入失联/故障检查、键盘和减少动态效果支持。
- 照片式手表桌面（日照金山背景、本地时间/日期、示例天气）→黑底九项循环圆环菜单。前方图标放大、后方缩小，菜单灯光只有一个蓝色指针。计时器/音量保留；新增独立秒表、本地日历和天气示例，闹钟/手感/设置为占位页。单击确认、双击返回、长按未设置；后台倒计时和秒表继续，到时切回计时完成画面。
- `node --test firmware-esp32/ui/preview/model.test.cjs firmware-esp32/ui/preview/gesture.test.cjs` 运行纯模型与手势验收；浏览器导航、应用回归、触摸入口分别为 `verify-navigation.js`、`verify-interaction.js` 和 `verify-touch.cjs`。
- **2026-09-10 的历史边界**：当时没有把新 SVG 产品交互移植进 C，也没有生成或烧录 ESP-IDF 固件。上面的 C API、0–120 分钟和蓝色 LVGL 视图说明只适用于既有 LVGL 组件；当前 KK 固件见本文开头。
- 新增可操作灯效调整：四种效果、六种颜色、0–100% 亮度，旋转选项目/调值、单击进入/确认、双击逐级返回。设置即时生效；菜单保留单蓝指针，计时/音量保留数值光带，亮度统一生效。本次页面会话内保留，刷新恢复默认。验收见 `../../docs/lighting-mode-demo-20260910-cn.md`，脚本为 `preview/verify-lighting.js`。自动回转与止挡仍为网页模拟。
