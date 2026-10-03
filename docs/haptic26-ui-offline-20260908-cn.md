# H26 行为层与 ESP 动画：离线开发记录

本轮没有串口、SWD、烧录或电机控制操作。板上历史 H25 的有限台架结论不适用于本轮新源代码。ESP 部分是独立动画模型与 LVGL 组件，不是完整、可烧录的 ESP-IDF 产品固件。

## 借鉴与实现

参考 [SmartKnob 固定版本 motor_task.cpp](https://github.com/scottbez1/smartknob/blob/4eb988399c3fda6ffd3006772856093dfe9adb86/firmware/src/motor_task.cpp) 的逻辑档位、迟滞和中心死区思路；未复制电压/PID 参数。X-Knob 的应用组织作为模式层参考，本轮不移植其 FOC。授权说明见 `THIRD_PARTY_NOTICES.md`。

实现仍使用 Nm/A，保留原有 FOC、保护、电流限值和台架相序。新行为在既有 2 kHz 调用中运行，不加入 ADC 中断；新增双精度运算的实际耗时仍需上板测量。

- 档位中心为 `origin+n×width`，首次最近格点定位，半格恰好相等选较大索引；正角度对应索引增加。
- 超过当前中心的 `±0.55×width` 才跨档；多格跨越使用有界算术，不按跨格数量循环。
- 死区 `min(0.1×width, 1°)`，外侧线性回复，半格处达到指定档位强度；取代原正弦规则，实际触感尚未验收。
- DETENT 宽度须大于 `1e-5 rad`；与 ENDSTOP 同开时只允许区间内格点。空格点/越界配置拒绝；运行中非法角度/索引归零。
- 有效参数变化后下拍重新定位，旧到新输出过渡 40 tick，首拍旧输出、第 41 拍完全新输出。仅 nonce 改变不重置。
- 新 Nm/速度限幅立即生效，优先于过渡。禁止输出/观察器无效立即归零并清旧过渡；恢复从零渐入。
- 命令仍 64 字节、快速遥测仍 68 字节。DETENT 下 `logicalPosition` 为档位，`subPosition=(angle-center)/width`；非 DETENT 保留原圈数/弧度语义，客户端必须按模式区分单位。

## 已验证

- STM32 完整 CTest：Debug、ASan+UBSan、Release+LTO 各 7/7 通过；行为测试 154 项断言通过。
- Keil ARMCLANG 6.21 第二次构建零错误、零警告；第一次双精度隐式提升警告未予通过，显式转换后重建，未关闭警告。
- 产品 CET6 工程同步编译零错误、零警告（Code 36728 / RO 608 / RW 16 / ZI 61040），只验证共享代码能编译，不代表该板已运行。
- `pnpm verify` 通过：TypeScript 检查、21 项测试及 SIM_ONLY 端到端流程；未接硬件。
- 新镜像 `output/implementation-20260908/haptic26-r2/HAPTIC26_OFFLINE_UNQUALIFIED.bin`，60152 字节，未烧录。
- 启动身份 `20260908_HAPTIC26_OFFLINE_UNQUALIFIED`。
- SHA-256 `AB8405AD34703B602E4BF79956F198F099AEAD4062DB5A990AC36F411AD6BBF6`。
- ESP 动画模型独立 C 测试 13 组，MSVC Debug、WSL ASan+UBSan、Release+LTO 通过，覆盖零时间、非法数值、熄屏、断连/故障、平滑和唤醒。
- LVGL 9.2.0（提交 `aa7446344c6ec7631112ef031983ef24077e24d5`）主机真实渲染成功，生成 466×466 亮屏和熄屏图；熄屏 RGB 像素全部为 0。上游 MSVC 构建有枚举运算 C5287 警告，不宣称该构建零警告。
- 网页实际浏览器检查通过：音量/计时器切换、增减、0/100 边界、计时器 120、方向键、故障优先于断连、熄屏全黑像素、唤醒、减少动态效果；320px 宽无横向溢出，未报告页面脚本错误。
- 截图证据目录 `output/implementation-20260908/`：`ui-desktop-final.png`、`ui-mobile-fault.png`、`lvgl-awake.png`、`lvgl-off.png`。网页和 LVGL 字体/装饰不是逐像素相同。

H25 历史 source-identity.json、实机记录及 live runner 校验不变。CI 验证当前代码，不再要求新源码匹配冻结 H25；旧 H25 runner 对修改后源码应拒绝运行，不可绕过此检查。

## 使用入口与限制

预览：`firmware-esp32/ui/preview/index.html`。原生模块及构建方法见 `firmware-esp32/ui/README.md`。预览输入是模拟快照，不会向电机发送命令。

未完成：ESP-IDF 屏幕/触摸 BSP、UART 实际数据接入、设备帧率/内存测量、面板真正休眠指令，以及 H26 台架验证。纯黑像素不等于面板断电，也不能证明盖板关机时光学上完全隐屏。
