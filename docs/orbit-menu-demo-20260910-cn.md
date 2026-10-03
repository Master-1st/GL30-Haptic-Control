# GL30 黑底循环圆环菜单

后续新增了第九项“灯效调整”，当前实现与验证见 [灯效调整验收](lighting-mode-demo-20260910-cn.md)。本页保留八项菜单版本的设计与验收记录。

日期：2026-09-10。当前浏览器版本按用户要求扩展菜单；日照金山照片首页保留。

- [打开预览](http://127.0.0.1:8768/firmware-esp32/ui/preview/?v=orbit-20260910) · [操作说明](../firmware-esp32/ui/preview/README_CN.md)
- [菜单 466×466 特写](../output/orbit-menu-20260910/menu-face-466.png) · [秒表特写](../output/orbit-menu-20260910/stopwatch-face-466.png) · [桌面浏览器](../output/orbit-menu-20260910/menu-desktop.png) · [320px 页面](../output/orbit-menu-20260910/menu-320.png)

## 当前行为

黑底椭圆轨道承载八项应用，前方图标放大至后方约 2.19 倍，并按深度排序遮挡。旋转可连续跨首尾，正反多圈均不设菜单止挡。菜单灯光仅保留蓝色位置指针，图标、外环与指针在滚轮动画中同步移动；直接点击侧栏入口立即把对应图标置于前方。鼠标拖动直接跟随，减少动态效果时不缓动。

| 入口 | 当前实现 |
| --- | --- |
| 计时器 | 已保留：设时、启停、自动回转、运行中调时、到零模拟止挡、HH:MM:SS、多色连续光带 |
| 音量 | 已保留：模拟音量与静音、范围止挡、屏内外位置对应；不改变系统声音 |
| 秒表 | 新增：HH:MM:SS.xx、单击启停/继续、按钮清零、后台及熄屏继续；与倒计时独立 |
| 闹钟 | 占位页，尚无时间设置、重复计划或到点提醒 |
| 天气 | 明确标注的 24°C 晴天示例，未接位置或实时接口 |
| 手感 | 占位页，尚未接电机参数调节 |
| 设置 | 占位页，尚未接显示/声音/连接配置 |
| 日历 | 当前电脑本地日期，只读 |

单击确认、双击返回、长按未设置。导航不清零后台计时和秒表；熄屏后的首次单击/双击仅唤醒。故障暂停两个时钟，解除后手动继续。倒计时完成仍优先进入完成页并取消旧手势，保留熄屏状态；秒表时间不会因此被改写。辅助页旋转、占位页单击不操作隐藏的计时器或音量。

菜单每项 45°，`menuPosition` 保留连续槽数，只对应用序号取正模。直接选择走最近路径，正好相对的入口固定走负向半圈。秒表使用完整单调经过时间，不受计时器 450× 演示倍率影响。

## 验证与证据

| 验证 | 结果 |
| --- | --- |
| 纯模型与手势 | 60/60；[日志](../output/orbit-menu-20260910/unit-results.txt) |
| 圆环/八入口/占位边界/秒表 | HTTP、file 各 58 项；[HTTP](../output/orbit-menu-20260910/verify-orbit-http.json) · [file/320px](../output/orbit-menu-20260910/verify-orbit-file-320.json) |
| 原导航、后台计时及输入取消 | HTTP、file 各 35 项；[HTTP](../output/orbit-menu-20260910/verify-navigation-http.json) · [file](../output/orbit-menu-20260910/verify-navigation-file-320.json) |
| 原计时器、音量、灯带和完整加速周期 | HTTP、file 各 53 项；[HTTP](../output/orbit-menu-20260910/verify-interaction-http.json) · [file](../output/orbit-menu-20260910/verify-interaction-file-320.json) |
| 照片首页与黑底菜单切换 | HTTP、file 各 20 项；[HTTP](../output/orbit-menu-20260910/verify-wallpaper-http.json) · [file](../output/orbit-menu-20260910/verify-wallpaper-file-320.json) |
| Chromium 真实触摸事件 | 30/30，含菜单正反整圈及原单击/双击/长按/运行中调时；[结果](../output/orbit-menu-20260910/touch.json) |
| 浏览器冻结 2.2 秒后恢复 | 倒计时从 00:25:00 到 00:24:58；[结果](../output/orbit-menu-20260910/background.json) |
| 视觉 | 主线程查看实际浏览器 466×466 特写和 320px 完整页面，菜单/秒表文字无圆边裁切，页面无横向溢出 |

首次 HTTP 回归有一次旧按键取消用例失败。该用例原设时 120 ms，确认等待后仅余约 60 ms 来建立按住状态；加入轨迹记录后复跑通过，未稳定复现业务缺陷。现改为 1.2 秒、明确断言运行态后建立按住状态、等待实际完成，再检查旧按键不能重置或改值，原取消断言保留；修订后 HTTP/file 均通过。[复跑轨迹](../output/orbit-menu-20260910/race-debug-trace.json)保留供核对。

原生 Spark 在本会话已确认不可用，HTML/CSS 由现有 Luna Worker 完成初版；主线程完成模型、交互、测试、颜色整理和最终视觉验收。DeepSeek Pro 对正模、最近路径、视觉指针同步和秒表隔离作独立分析，主线程采用适用意见并复测。[审阅原文](../output/orbit-menu-20260910/deepseek-review.txt) · [采纳记录](../output/orbit-menu-20260910/review-adjudication.md)。

本次交付为浏览器实现；466×466 截图不是实屏照片。闹钟提醒、实时天气、硬件手感和设置仍未接入，没有修改固件、电气保护或 CAD，没有连接电机或串口。光学、真实力矩和面板性能仍需实机验证。完整运行文件校验值与结果见 [验收摘要](../output/orbit-menu-20260910/audit-summary.json)。
