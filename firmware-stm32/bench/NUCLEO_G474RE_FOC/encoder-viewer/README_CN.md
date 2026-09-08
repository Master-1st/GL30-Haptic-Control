# 手转编码器实时窗口

用途：NUCLEO-G474RE 联调固件的 **STATUS 只读曲线查看器**。不属于产品 Companion 协议实现，不需要重新烧录，也不会发送驱动唤醒、校准、力矩或停止命令。

请保持 **TI 动力电源关闭、B1 松开**。本窗口只能看到固件报告，不能替代物理电源检查；它不是电机急停开关。

## 打开

本机已构建程序：`G:/Agent/GL30-Haptic-Control/output/bench/encoder-viewer/GL30-Encoder-Viewer.exe`。双击打开，默认 COM4；已有窗口时不要再打开串口终端或运行其他联调脚本。

从源码构建并打开（Windows 自带 .NET Framework，不安装软件）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'G:/Agent/GL30-Haptic-Control/firmware-stm32/bench/NUCLEO_G474RE_FOC/encoder-viewer/Open_Encoder_Viewer.ps1'
```

只构建和运行纯单元测试，不占用串口、不打开窗口：

```powershell
.\Open_Encoder_Viewer.ps1 -Check
```

## 怎么看

- **单圈角度**：`原始码 × 360 / 16384`，跨越 0°/360°会回绕，这是绝对角度的正常行为。
- **相对连续转角**：相邻有效样本的最短环形差累计；用于缓慢手转看一圈约 360°，不是固件电角零位或绝对精度标定。
- **相对清零**：只重新选择电脑显示的起点，不写编码器寄存器或修改电机校准。
- **暂停显示**：冻结曲线，串口采集和记录继续；要停止采集，请点“断开”或关闭窗口。
- **健康/诊断/错误计数**：同时显示角度是否新鲜、SPI 诊断、固件输出状态；不能只凭曲线在动就认为所有检查通过。

电脑端以约 **10 Hz** 请求状态，不是将 MCU 内部 4 kHz 编码器流完整传到电脑。请缓慢手转；两次显示采样之间超过半圈、失联或坏数据都会使连续圈数不可靠。断线、计数回退、数据无效等情况会断开连续曲线，不能用旧读数伪装新采样。

## 保存的数据

输出目录：`G:/Agent/GL30-Haptic-Control/output/bench/encoder-viewer`。

- `encoder-status-*.csv`：UTC 时间、电脑相对时间、原始码、角度、连续转角、有效标记及状态字段。
- `encoder-serial-*.log`：原始收发记录；发送内容只能是 STATUS。
- `latest-state.json`：当前进程和连接、样本统计，供调试工具检查；无需另开串口抢占 COM4。

曲线仅保留最近 30 秒，并限制点数，日志流式写入；长时间打开时内存不会随全部历史样本不断增长。窗口关闭时释放串口并刷新文件。复盘异常时请保留 CSV 和原始串口日志。
