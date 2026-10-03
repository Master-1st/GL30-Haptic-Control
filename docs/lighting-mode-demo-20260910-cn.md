# GL30 灯效调整模式

日期：2026-09-10。[打开预览](http://127.0.0.1:8768/firmware-esp32/ui/preview/?v=lighting-20260910) · [操作说明](../firmware-esp32/ui/preview/README_CN.md) · [466×466 特写](../output/lighting-mode-20260910/lighting-face-466.png) · [完整页面](../output/lighting-mode-20260910/lighting-desktop.png)。

菜单新增第九项“灯效调整”，九项按 40° 间隔连续循环，侧栏同步改为 3×3 排列。灯效为可操作功能，提供随功能、常亮、呼吸、流动四种效果，六种颜色及 0–100% 亮度。旋转先选择效果/颜色/亮度，单击进入调整、再次单击确认；双击先退出编辑，再双击返回菜单。也可直接点击效果、色块或拖动亮度滑块。调节即时生效，在本次页面会话内保留，刷新恢复默认。

常亮/呼吸/流动应用于灯效页、桌面及辅助页面。呼吸平滑改变彩色段亮度，蓝色头保持稳定；流动为连续的四分之一圈色带，蓝色始终位于前端。减少动态效果时冻结呼吸与流动。菜单仍仅有蓝色位置指针；计时器和音量仍按数值显示各圈颜色，所有页面灯环共用亮度设置。亮度 0% 只关闭灯环，屏幕继续工作。

灯效操作不修改后台倒计时、秒表或音量。到时仍切入计时完成页并关闭灯效编辑状态；熄屏、故障和断连沿用原规则。当前为浏览器实现，未下发真实 LED、串口或电机命令，未改变固件、CAD 和电气保护。

| 验证 | 结果与证据 |
| --- | --- |
| 模型和手势 | 67/67；[日志](../output/lighting-mode-20260910/unit-tests.txt) |
| 灯效编辑、动态、颜色亮度、页面优先级及故障/唤醒 | HTTP/file 各32项；[HTTP](../output/lighting-mode-20260910/lighting-http.json) · [file/320px](../output/lighting-mode-20260910/verify-lighting-file-320.json) |
| 九项循环、占位边界及秒表 | HTTP/file 各62项；[HTTP](../output/lighting-mode-20260910/verify-orbit-http.json) · [file](../output/lighting-mode-20260910/verify-orbit-file-320.json) |
| 原计时/音量/灯带及完整加速周期 | HTTP/file 各53项；[HTTP](../output/lighting-mode-20260910/verify-interaction-http.json) · [file](../output/lighting-mode-20260910/verify-interaction-file-320.json) |
| 导航、后台完成和旧手势取消 | HTTP 35项；[结果](../output/lighting-mode-20260910/verify-navigation-http.json) |
| Chromium 触摸事件 | 34项，含九项菜单正反整圈及灯效旋转/双击退出；[结果](../output/lighting-mode-20260910/touch.json) |

主线程查看了实际浏览器的灯效特写与完整页面，文字位于圆屏内，内外灯环颜色对应。466×466 特写为浏览器渲染，不是面板照片。截图工具增加“已进入灯效页”的目标条件，防止把同址首页误作功能截图。

旧回归曾在固定等待结束时提前断言菜单缓动完成、短计时完成；后续读取均已到达预期状态。相应用例改为有超时限制地等待实际完成；临近计时完成的手势用例保留旧输入不得在新页面执行的断言，并验证待测手势确实在截止前触发。上述结果均为修订后复跑数据。

本会话原生 Spark 不可用；HTML/CSS 由现有 Luna Worker 提供初版，主线程完成模型、交互、测试、集成与视觉验收。运行文件校验值和结果汇总见 [验收摘要](../output/lighting-mode-20260910/audit-summary.json)。
