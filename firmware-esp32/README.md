# GL30 ESP32-S3 屏幕固件

2026-10-03 公开上传入口：[当前源码与冻结离线交付](../docs/offline-delivery-20261003-cn.md)，[A 板画板指南](../docs/pcb-drawing-guide-20261003-cn.md)。本机 outputs 历史归档不全部随 Git 分发。

2026-10-02 冻结历史：[设置保存、STM32控制权交接与验证](../outputs/esp32-settings-storage-20261002/README_CN.md)。采用ESP-IDF NVS保存13项配置，启动恢复不恢复运行/授权状态；运行写入须有精确RELEASE回显和显示DMA独占。无对端不写Flash，握手完成作废旧ARM，恢复后须新人工请求。三配置各8/8、ESP-IDF构建通过，未烧录。线协议已统一72/44/24字节；千问第一轮架构意见与当前代码意见分别记录，最新实际评审范围见交付。本段评审仅描述该次冻结结果，不代表本次重新调用模型。

2026-10-02 零回显阶段历史：[只读确认、重复停止修订与验证](../outputs/esp32-zero-confirm-review-20261002/README_CN.md)。两路回显须在完整UART接受之后且年龄小于20ms；新STOP换nonce，旧回显不能恢复确认。该阶段三配置各6/6，未烧录；当时未接NVS，已由上方续进实现。软件回显仍不是实体关桥证据。

2026-10-02 输入链路阶段：[停止请求、64位ARM令牌与回归](../outputs/esp32-input-stop-review-20261002/README_CN.md)。`MOTOR OFF`在解析时先提交停止，输入队列满时也立即记录撤授权；停止前开始的半行或排队ARM会失效。溢出清理取消手势并清队列，须在清理后重新发ARM；普通OFF后新的人工ARM仍可被接受。三种主机配置各6/6、专项294项检查与ESP-IDF构建通过，未烧录；最后复核至UART提交间隙的在途帧不能召回，不声称物理停机延迟。该阶段Qwen按当时要求关闭，当前授权规则以上方新包为准。

2026-10-02 状态显示续进：[电机状态、灯效入口、同源画面与验证](../outputs/esp32-motor-feedback-20261002/README_CN.md)。底部显示 OFFLINE/CHECKING/ALIGN/READY/ACTIVE/FAULT/UNKNOWN 及中文提示；有效 FAST 接收年龄达到 20 ms 时投影为失联，读取快照再次检查。失联时的诊断零值表示没有当前证据，不能据此清故障或授权。状态只更新显示，不暂停计时、不取消手势。八项菜单的“设置 → 灯效”入口已按普通输入走通。主机检查和 ESP-IDF 构建通过，本版未烧录。

2026-10-02 电机链路续修：UI与 READY/ACTIVE FAST 健康时，未 armed 也发送固定 nonce 的零限值心跳；STM 同步将有效零限值定义为撤使能/关桥，正常空闲不会因停止发包而锁存通信故障。MOTOR OFF、反馈失配或健康失效后，通信恢复不能自动授权出力；正常页面切换保留已有人工授权。产品仍未对齐时不发送该心跳。见[源码、回归、构建与审查记录](../outputs/idle-fault-and-link-review-20261002/README_CN.md)。本轮未烧录，未测FPS或实体电机手感。

2026-09-27 界面修订：[代码审查、前后画面与验证](../outputs/esp32-ui-review-20260927/README_CN.md)。主目录已合入归并资料中的桌面/月历调整，并统一灰度字体、菜单图标和圆屏设置布局，修复多圈反转蓝灯位置与中文标题缺字。主机和浏览器检查、ESP-IDF 构建通过；本轮未烧录，旧版本 FPS 不代表这版性能。

[9/17 发布准备工作区](../outputs/workspace-consolidated-20260927/firmware/GL30-Haptic-Control-release-20260917/docs/firmware-status-20260917.md)保留独立 Git 关联和未提交内容；仅合入上述 UI 相关文件，没有用整个工作区覆盖当前主目录。版本关系见 [整理入口](../outputs/workspace-consolidated-20260927/README_CN.md)。

当前实际固件入口为根目录 ESP-IDF 工程：`main/app_main.c` → `components/gl30_ui` → `KK_UI` → RGB565 版 `KK_OLED` → CO5300/CST820 BSP。目标为 Waveshare ESP32-S3-Touch-AMOLED-1.32，466×466。

[当前进度与 9/17 最新验证记录](../docs/project-progress-20260927-cn.md) · [9/16 60 FPS 显示链优化与代码审计（历史阶段）](../docs/esp32-60fps-optimization-20260916-cn.md) · [9/15 按帧跳过比较与代码审计（历史基线）](../docs/esp32-dirty-audit-20260915-cn.md) · [9/15 菜单与性能优化（历史阶段）](../docs/esp32-menu-optimization-20260915-cn.md) · [9/15 自动校时](../docs/esp32-auto-clock-and-kk-animations-20260915-cn.md) · [9/15 USB 实机调试](../docs/esp32-usb-bringup-20260915-cn.md) · [9/14 接入、构建与边界](../docs/esp32-kk-integration-20260914-cn.md) · [固件同源预览说明](ui/kk-preview/README_CN.md)。预览显示同一份 C 代码的实际画布。此前 `ui/src` 的 LVGL 组件和 `ui/preview` 的 SVG 演示是历史设计参考，不在新固件构建链路内，也不是运行时备用实现。

2026-09-15 已连接 Type-C、备份原始 8 MB Flash，并通过 COM11 烧录与读取 USB 状态。CO5300、CST820 和 PSRAM 初始化已在实机日志中确认；实屏目视、色序、触摸方向和实际触摸手势仍待确认。闹钟、手感与天气联网保留待接入状态。当前已编译接入默认未使能的 STM32 菜单链路，20 项 USB 功能与连续菜单测试中 `motor_tx=0`；没有烧录 STM32 或驱动外部灯环。

9/15 已验证当前 Windows 用户的 Type-C 自动校时程序与复位恢复；Wi-Fi/SNTP 尚未接入。KK_UI 保持 Custom 页面与统一刷新调度，软件菜单复用 Q12 缓动，电机 q/f 反馈直接决定视觉相位。9/16 完成 wire-native RGB565、80 MHz QSPI、64 行传输、三帧缓存及后台清屏等优化，见 [该阶段审计](../docs/esp32-60fps-optimization-20260916-cn.md)。9/17 进一步加入彩色图标、双语和设置页，当前目标为 **60 Hz / 16,667 μs**；最新连续菜单测试 **59.980 FPS / 60 秒**，`frame_errors=0`、`input_drops=0`、`motor_tx=0`。这是屏幕链路记录，尚未完成人工视觉、实际手势、真实 STM32 满流量与电机手感验收；详见 [当前进度中的原始记录与边界](../docs/project-progress-20260927-cn.md)。

已冻结接口：

- USART 5 Mbaud、8N1、全双工 DMA；不实现旧草案的 6 Mbaud。
- Waveshare J1-11 `ESP TX -> STM PB11 RX`。
- Waveshare J1-12 `ESP RX <- STM PB10 TX`。
- GP1 接经开漏缓冲后的 `SYS_FAULT_N`；GP0 首版 DNP；GP2 禁止外部驱动。
- ESP UART0 启动日志必须关闭或移动，不能污染 V1 二进制链路。
- 3.3 V 电源不通过扩展口互相回灌，只共地和信号。

后续硬件闭环：验收实屏与触摸后，联调已有 UART0 接入及新增 HAPTIC_STATE 生效反馈。屏幕固件默认不授权出力；只有显式调试使能后才可能建立有力矩的菜单会话，健康 READY/ACTIVE 链路可先发送零限值心跳。编译和主机仿真不代表实体电机已验收。计时器回旋目标和零点止挡意图已经在界面模型中计算；电流环与触觉闭环仍由 STM32 承担，不能用触摸输入或电机端角度观察自动冒充手动施力。
