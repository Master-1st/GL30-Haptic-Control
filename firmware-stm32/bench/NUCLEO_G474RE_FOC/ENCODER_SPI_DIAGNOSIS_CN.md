# AS5048A SPI 当前联调记录

更新日期：2026-09-05。当前阶段：**编码器接入、TI 动力关闭；通信及无输出回归通过，尚未进行电机出力测试。** 本文件记录当前状态，早先“CS/MISO 全高”的采集不是当前结果。

## 1. 已解决什么

用户修正编码器黑色地线后，串口取得有效角度和诊断字，不再只是“有编码器中断”。本次固件增加 `enc_ok / enc_err`，分别统计完整读取通过和失败的组数。

随后修复了接入实物后暴露的两处软件问题：

1. **自检与真实角度观测叠加超时。** 旧固件重跑一次 20,000 次合成自检，`deadline` 从 3999 增至 7999。现在优先处理新的真实角度，合成自检使用其余 ADC 中断时隙；保留 4 kHz 真实观测，仍执行完整 20,000 次合成迭代。没有提高 3600 cycles / 22.5 μs 的预算。
2. **状态报文的时间戳竞争。** 读取主控时间后，编码器中断可能更新样本时间，导致无符号年龄计算下溢。实测抓到 `health=0 age_us=1 enc_err=0` 的矛盾报告。现在仅在短临界区采集健康位及相关编码器、自检状态，解锁后才格式化和发送；不在关中断期间执行 `snprintf`。

最终镜像已构建、烧录并完整读回核对。连续 3 次合成自检及 120.021 秒连续采样通过：480,483 组编码器有效读取，读取错误、UART 错误、ADC 同步异常及期限超限均为 0。具体结果见 [TEST_RESULT_CN.md](TEST_RESULT_CN.md)。

本次未执行 PREPARE / ALIGN / IQ，未改变电机初值、限流、B1 或主机许可保护，未写入编码器 OTP。通信通过不代表真实电流环、全圈线性或整机手感通过。

## 2. 逻辑分析仪设置

| 设置项 | 当前固件对应值 |
| --- | --- |
| CLK | D1 / Channel 1 |
| MISO | D3 / Channel 3 |
| MOSI | D2 / Channel 2 |
| CS# | D0 / Channel 0 |
| CS# polarity | active-low |
| Clock polarity / phase | **0 / 1，即 Mode 1** |
| Bit order / Word size | **msb-first / 16** |
| 显示 | hex |

16 位是帧长度，14 位是角度数据宽度；STATUS 的 `mode=0` 表示控制状态 OFF，不是 SPI Mode 0。

现有采集配置可用 50 MHz、20 ms、1.6 V 门限。每个读取组依次发送 `FFFF → 7FFD → 0000`；每个 16 位字有独立的低有效 CS 窗口，字间拉高。**不要将 CS 长期接地。** AS5048A 为流水线返回，不能把发送读命令的同一帧直接当作该寄存器的响应。

数字波形能检查协议、位值和相对时序，不能据此宣称过冲、振铃或模拟电平余量合格。旧异常波形的解码参数改正确，也不会使物理上全高的 MISO 自动变成有效返回。

## 3. 当前线色和探头对应

| 分析仪 | 编码器线色 | MCU / Arduino | Morpho |
| --- | --- | --- | --- |
| D0 | 白 CSN | PB6 / D10 | CN10-17 |
| D1 | 蓝 CLK | PB3 / D3 | CN10-31 |
| D2 | 黄 MOSI | PB5 / D4 | CN10-29 |
| D3 | 绿 MISO | PB4 / D5 | CN10-27 |
| GND | 黑 GND | NUCLEO GND | 例如 CN7-19 |
| 不接分析仪供电输出 | 红 +5 V | NUCLEO +5 V | CN7-18 |

不要套用 NUCLEO 丝印 D13/SCK、D12/MISO、D11/MOSI；那不是当前固件的接口。保持原厂 5 V 编码器供电方式。调整线束或探头前先断相关电源，分析仪、编码器和 MCU 必须有可靠的信号参考地。

## 4. 现在如何判断

TI 动力关闭时，自检完成后的目标状态是：

```text
mode=0 fault=0 health=34 moe=0 off=1 button=0
zero=0 calibrated=0 self_left=0 self_fail=0 deadline=0
enc_ok 持续增加；enc_err、adc_bad、uart_err 不增加
```

- `health=34` 只表示编码器位 2 和时序位 32；未供电的 TI、未校零的 ADC 不要求达到整体 63。
- 自检运行时 `self_left>0` 暂时撤销时序位是正常门控；完成后必须恢复，不能只看一句 `OK`。
- 本次有效诊断字曾为 `0x01FB / 0x01FE`：当时芯片的诊断检查通过。AGC 数值接近其量程上端，不等于已经测得磁场强度，更不等于全圈线性/装配余量验收通过。
- `zero=0` 时约 −2.2 A 是未校零的 ADC 换算值，不是实际电机电流；TI 未上电时软件 `vm_mv` 的非零读数也不能代替万用表确认母线。

可自动复跑的脚本只发 STATUS 和无输出 SELFTEST：

```powershell
Set-Location 'G:/Agent/GL30-Haptic-Control/firmware-stm32/bench/NUCLEO_G474RE_FOC'
.\test_encoder_off.ps1 -Port COM4 -ConfirmDriverPowerOff -DurationSeconds 120 -SelfTestRuns 3
```

运行前仍须确认 TI 动力关闭；该参数是使用者确认，不是脚本能独立测量物理电源的证明。脚本遇到模式、MOE、DRVOFF、错误计数或时序不符会记录 FAIL 并退出，不会自动提高阈值。不要同时打开另一个串口工具，也不要在当前接线状态运行裸板刹车/看门狗故障注入。

## 5. 下一步真正需要人做什么

下一道最小检查是**保持 TI 动力关闭，配合串口采集，缓慢手转转子一圈**，检查角度是否变化、能否跨越 0/16383、诊断及读取错误是否仍正常。必须先开始采集再转动；不需要测速设备或再测电感。此检查不是绝对角度线性标定。

通过后才进入有人在场的 TI 上电静态检查：限流电源、接线及固定情况复核，PREPARE 输出关闭的驱动 SPI/ADC 零点检查；再分别安排方向/电角零位校准和受限力矩脉冲。不得因无动力通信通过，直接自动执行这些有机械运动的步骤。

## 6. 定位证据保留位置

- 早期 CS/MISO 全高的原始副本：`G:/Agent/.cache/gl30-spi-20260905/atk-raw-1788597960`。这是修正地线之前的失败证据。
- 早期采集的参数修正版：`output/bench/as5048a-spi/AS5048A_SPI_Mode1_16bit.atkdl`，与 `capture-analysis.json`、`validation.json` 同目录；只改解码参数，未改采样位值。本机 ATK 原生重开未在本轮验证。
- 修复前状态竞争实测：`G:/Agent/.cache/gl30-timing-20260905/status-race-before.log` 和同名 `.json`。
- 当前重复自检及连续采样：`output/bench/encoder-off/encoder-off-transcript.log`、`encoder-off-result.json`。

协议依据：[AS5048A 官方手册](https://www.infineon.com/assets/row/public/documents/24/49/infineon-as5048a-as5048b-datasheet-en.pdf)；项目实现与实际串口日志为本次固件状态的依据。
