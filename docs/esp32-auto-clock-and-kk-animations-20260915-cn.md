# ESP32 自动校时与 KK_UI 动画使用情况（2026-09-15）

这次已更新并烧入屏幕固件，同时在当前 Windows 用户下安装 Type-C 自动校时程序。时间跟随电脑的真实 Unix 时间，屏幕显示北京时间 UTC+8，无须手动输入日期或时分秒。

## 当前自动校时方式

- 电脑登录后自动在后台启动，已连接的屏幕板自动识别。每 5 秒检查一次，每 5 分钟重新校时；未校时或偏差超过 2 秒时立即纠正。
- 仅匹配这块板子的 USB 序列号 `AC:27:6E:D2:F6:5C`，同时核对 Espressif VID/PID；不写 ST-LINK 的 COM4。COM 号码变化时仍按序列号寻找。
- 先收到 GL30 固件的 `STATUS`，再发送时间；必须收到确认并回读 `clock_valid`、`clock_unix` 才记为成功。旧固件没有这些字段时明确报错。
- 每次检查后释放串口，不触发 DTR/RTS 复位。调试器占用串口时等待下次检查，退出后恢复。
- 校时只修改日历时间基准，不修改计时器、秒表、菜单或电机状态。

程序：[usb_clock.py](../firmware-esp32/tools/usb_clock.py)。安装/停止自启动：[install-usb-clock.ps1](../firmware-esp32/tools/install-usb-clock.ps1)。日常不需要运行这些命令；本次已经安装并启动。

当前用户登录自启动项：

`C:\Users\foke\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup\GL30 USB Clock.lnk`

滚动日志：[service.log](../outputs/esp32-usb-clock/service.log)。只记录连接状态变化、校时和错误，单个日志上限约 1 MB、保留一个旧日志。

**使用条件：**电脑处于开机登录且未休眠状态，使用可传输数据的 USB 线。单独接充电器不能从电脑获取时间；保持供电时已校准的时间会继续走，完全断电后需再次连接此电脑自动校准。另一台电脑需要安装该辅助程序。

联网自动校时可以用 Wi-Fi + SNTP 实现，但当前运行固件尚未接入 Wi-Fi 配网或 SNTP，也没有配置网络凭据。这次落实的是用户明确接受的 Type-C 自动校时方案，不能据此称为已经完成联网校时。

## 实际验证

| 检查 | 本次结果 |
| --- | --- |
| 构建与烧录 | ESP-IDF 5.5.1 构建通过；790,640 字节应用写入 `0x10000`，esptool `Hash of data verified` |
| 初始无时间 → 自动校时 | 烧录后状态为 `clock_valid=false, clock_unix=0`；启动后台程序后收到校时确认并回读成功 |
| 时间故意慢 60 秒 | 后台自动恢复，观测用时 2.81 秒；运行中的计时器继续递减 |
| 时间故意快 60 秒 | 后台自动恢复，观测用时 2.86 秒；运行中的秒表继续累加 |
| 串口被占用 7 秒 | 后台记录占用并重试；没有复位设备，释放后自动恢复 |
| 屏幕板硬件复位 | 自动恢复时间，观测用时 4.36 秒；最后停在表盘，计时器归零、秒表停止 |
| C 回归 | 模型/手势 62 项、共享 UI 109 项断言通过；新增校时前后计时连续性检查 |
| ASan / UBSan | 两组测试通过 |
| 电脑校时程序 | 9 个单元测试通过，覆盖确认丢失、错误回读、时钟偏差、串口号变化、其他设备和异常电脑时间 |
| 自启动和重复实例 | 快捷方式属性回读正确；安装器停止/重启成功；从快捷方式重复启动时已有实例保持运行 |

完整证据目录：[esp32-auto-clock-20260915](../outputs/esp32-auto-clock-20260915/)。重点文件为 [automatic-clock-bench.json](../outputs/esp32-auto-clock-20260915/automatic-clock-bench.json)、[automatic-clock-bench.log](../outputs/esp32-auto-clock-20260915/automatic-clock-bench.log)、[flash.log](../outputs/esp32-auto-clock-20260915/flash.log)。

本次烧录应用 SHA-256：`87c62cd2305e1a09b234956a4b44feeb04a98f14b3d044a942170bba2e8405d1`。二进制与对应源码校验见本次证据目录的 `firmware-manifest.json`。之前的原始 8 MB Flash 备份未改动。

没有实际拔插 USB 或重新登录 Windows，复位与后台恢复经过实机验证，自启动配置及快捷方式执行经过检查。用户仍未现场目视验收屏幕。当前账户的 Spark 原生子代理不可用，测试由主线程编写并运行。

## 哪些动画使用了 KK_UI

当前应用注册了一个 KK_UI Custom 页面，各个产品页面由应用自己的 `state.page` 切换。KK_UI 负责刷新调度、脏状态、裁剪与显示提交；KK_OLED 的 RGB565 适配负责绘制照片、文字、线条和图形。

| 屏幕表现 | 实际实现 |
| --- | --- |
| 圆环菜单连续旋转、前大后小 | 应用的 `menu_visual` 指数插值，加上椭圆位置、深度排序和缩放自绘 |
| 计时器回转、音量多圈进度、蓝色指针 | 应用按时间/数值计算角度并自绘圆弧 |
| 秒表进度 | 应用按经过时间计算并绘制 |
| 呼吸、流动灯效的画面与 LED 预览 | 应用的余弦亮度变化和相位计算；没有在本次接入外部实体灯带 |
| 表盘时间更新 | 应用检测秒变化，交由 KK_UI 调度重画 |

**上述可见动画没有直接使用 KK_UI 内置动画控件。** 当前 `KK_UI_ENABLE_HOME/MENU/INFO` 及编辑器、对话框、Toast 均关闭，只有 Custom 开启。`kk_ui_anim.c` 在库源码列表中，但应用未调用其 `KK_UI_EaseQ12/LerpQ12/RoundQ8`；单 Custom route 也没有触发 KK_UI 的页面滑动导航。

证据源码：[gl30_demo.c](../firmware-esp32/components/gl30_ui/src/gl30_demo.c)、[gl30_model.c](../firmware-esp32/components/gl30_ui/src/gl30_model.c)、[gl30_render.c](../firmware-esp32/components/gl30_ui/src/gl30_render.c)、[kk_ui_config.h](../firmware-esp32/components/kk_ui/include/kk_ui_config.h)。独立只读检查与主线程核查一致。

本次没有为了改变库的使用统计而调整已展示的菜单外观或重做动画。现有复杂界面刷新速度仍受完整画面绘制影响，不能用输入采样 100 Hz 或目标刷新间隔 33 ms 宣称动画已达 30 fps。

## 开发时停止/恢复自动校时

烧录或长时间串口调试前可先停止本程序，避免刚打开串口时竞争：

```powershell
& 'G:\Agent\.tools\esp-idf-tools\python_env\idf5.5_py3.14_env\Scripts\python.exe' `
  'G:\Agent\GL30-Haptic-Control\firmware-esp32\tools\usb_clock.py' stop
```

重新运行 `install-usb-clock.ps1` 可恢复；加 `-Remove` 只移除本程序的自启动项并停止它，不删除其他文件或服务。
