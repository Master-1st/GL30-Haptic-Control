# GL30 STM32 固件：产品工程与 NUCLEO 联调工程

2026-10-02 当前离线续进：[控制权交接与完整验证](../outputs/esp32-settings-storage-20261002/README_CN.md)。普通命令匹配64位租约，ACQUIRE后先接受精确全零命令，RELEASE期间关闭输出并暂停周期遥测而保留保护/故障；第二临界区重新检查10ms命令年龄。三种主机配置各15/15，CET6产品和NUCLEO台架构建均0错误0警告。产品电角零位宏仍为0，本轮未烧录、不放行出力。

当前两个硬件目标均由 STM32CubeMX 生成，使用 Keil MDK-ARM 与 ARMCLANG 6.21；外设和高频路径采用 STM32 LL。它们是不同硬件端口，不是历史版本。旧 HAL/CMake ARM 工程不作为活动路径。

**手上是 NUCLEO-G474RE + TI DRV8316REVM 时，从 [FOC 联调工程](bench/NUCLEO_G474RE_FOC/README_CN.md) 开始。** 板上为 HAPTIC25：20 kHz 电流环、AS5048A SPI、受限 ALIGN/双向 IQ/八模式已通过实测。2026-09-08 追加定时边界、重复运行、运行中 STOP 和失联关断，详见 [扩展验收](../docs/bench-validation-20260908-haptic25-extended-cn.md)。不要把下面 CET6 产品板工程烧到 NUCLEO，也不要混用引脚表。

## CET6 产品板当前实现

- 160 MHz；TIM1 40 kHz 中心对齐互补 6-PWM，默认死区 500 ns。
- TIM1 CH4/TRGO2 同步 ADC1/2/3 注入采样；40 kHz FOC/SVPWM。
- TIM6 4 kHz 角度观测，TIM7 2 kHz 触觉与安全调度。
- DRV8316：SPI3 Mode 1、5 MHz；USART3：5 Mbaud DMA；I2C1：400 kHz。
- DRVOFF 上电关断、TIM1 MOE 默认关闭、PB12/TIM1_BKIN 低有效、故障锁存。
- 9/27 已购器件接口统一：PB9 为 MOTOR_PWR_EN，PA12 为 nSLEEP，默认均低；PC13 低速推挽驱动 U7，未就绪/故障为低。母线 ADC 倍率 11，10k/B3950 NTC 在 200 Hz 任务换算。
- 产品编码器实现为 `drivers/factory_encoder_as5048a.c`，复用共享 AS5048A 编解码：SPI1 Mode 1、16 位、2.5 MHz，TIM6 4 kHz 执行 ANGLE→DIAG→NOP。角度和诊断回复均检查奇偶校验与 EF；异常不刷新最后成功的时间戳。整轮 50 µs 为超时判据，实板时序尚未验收。只读接入不解除供电、驱动配置及电角零位门控。
- 10/02 驱动复查：SPI3 增加 FIFO 上限、共用超时和 CS 间隔；配置与 arm 可被故障取消。普通停转保留配置，实际休眠/断电前明确使其失效；arm 不再自动清障。致命异常入口增加紧急关断。见[本轮修订、三方审查与验证](../outputs/driver-startup-review-20261002/README_CN.md)。

## CET6 产品板 CubeMX/Keil 工程

10/02 电流采零续修：移除驱动休眠时的开机平均，改为配置/电源/休眠资格检查、10 ms 稳定等待、512 组三相样本与均值/波动验收；掉电或故障清除零偏，50 ms 期限也由前台检查。过压保护在采零之前执行，零偏的三通道快照在临界区内取得。见[采零阶段证据](../outputs/current-zero-review-20261002/README_CN.md)。

10/02 启动流程续修：逻辑、温度、编码器和新鲜 ADC 合格后，固件自动请求母线；母线连续稳定 10 ms 后唤醒驱动，等待 10 ms、配置读回、完成采零，再在 MOE=0/六路 PWM 输入低的条件下确认驱动状态。仅首次 NPOR 可受控确认一次，其他故障不自动清除；失败后关断且不重试。此准备过程不会开启 PWM，出力仍要求电角零位合格及 ESP 的显式启用。当前电角零位仍无效，未烧录、未验收实物时序。见[本轮实现及验证记录](../outputs/power-startup-review-20261002/README_CN.md)。

收到的命令记录本地接收时刻；排队达到 10 ms 的命令不刷新通信心跳，首次 READY 之前接收的命令不能触发 arm。开机 50 ms 仍无新鲜 ADC 会锁存故障；故障态只有在输出与电源请求均读回关断、2 kHz 安全调度仍有进度时才继续喂狗。IWDG/WWDG 复位会在下一次准备前锁存启动故障，避免自动重试供电。主循环或安全调度失去进度仍由看门狗复位。

10/02 空闲与停止续修：准备完成且 DRVOFF 实际为低时，2 kHz 监督和前台监测会锁存共享 PB12 低；verify 返回到 COMPLETE 发布前也复查。普通关桥的 DRVOFF 高不会被该检查误判；重新启用事务中，仅当实际 MOE 和驱动输出均关闭时暂由驱动最终读回把关，避免把正常释放稳定期误判。一旦任一输出指示开启即继续监视，事务返回后标记清除。TIM7 不新增 SPI/等待。有效零力矩上限命令刷新租约并立即撤 arm、清零 FOC、关桥，不能使能 PWM；正限值保留原门控。ESP 健康空闲时发送零心跳，失效后不自动恢复出力。见[本轮证据与审查](../outputs/idle-fault-and-link-review-20261002/README_CN.md)及[协议约定](../protocol/schema/protocol-v1.md)。未烧录，电角零位宏仍为0。

- 产品 IOC：`cubemx/GL30_AMOLED_V7/GL30_AMOLED_V7.ioc`
- 生成脚本：`cubemx/generate.ps1`
- Keil 工程：`cubemx/GL30_AMOLED_V7/MDK-ARM/GL30_AMOLED_V7.uvprojx`
- 应用入口：`app/gl30_app.c`
- 默认硬件参数与引脚：`config/board_config.h`

需要重新生成时，在仓库根目录运行：

```powershell
Set-Location firmware-stm32\cubemx
.\generate.ps1 -CubeMxPath 'C:\path\to\STM32CubeMX.exe' -FirmwarePackagePath 'C:\path\to\STM32Cube_FW_G4_V1.6.3'
```

也可以设置 `STM32CUBEMX_EXE` 和 `STM32CUBE_FW_G4` 环境变量后直接运行脚本。脚本内部使用 CubeMX `-q`，并在启动前后检查 CubeMX 进程，已有实例时立即失败；不要使用 `-h`，也不要手工并行启动多个 CubeMX。生成后脚本以文本方式幂等加入 ARMCLANG 6.21 和 `Application/GL30`，不会用 XML 重写器破坏 `.uvprojx`；本机固件包路径只在本次生成中使用，结束后恢复仓库中的 IOC。

Keil 中打开上述 `.uvprojx` 后执行 Rebuild；命令行验收示例：

```powershell
Set-Location firmware-stm32\cubemx\GL30_AMOLED_V7\MDK-ARM
$keil = (Get-Command UV4.exe -ErrorAction Stop).Source
$args = @('-r', '.\GL30_AMOLED_V7.uvprojx', '-t', 'GL30_AMOLED_V7', '-j0', '-o', '.\build-ac6.log')
Start-Process $keil -ArgumentList $args -WindowStyle Hidden -Wait
```

2026-09-27 本地产品工程 rebuild 结果：ARMCLANG 6.21，`0 Error(s), 0 Warning(s)`；程序占用 `Code=38952, RO=672, RW=16, ZI=61040`。日志、接口静态核对和测试见 [本轮验证包](../outputs/product-interface-20260927/README_CN.md)。这是构建证据，不是实物运行证据；本次修订没有发布到 GitHub。

9/27 已用 CubeMX 6.18.1 重新生成成功：修正 `ADC12CLockSelection/ADC345CLockSelection` 键名大小写，移除 ADC 空值及未启用的 RCC RNG/USB 派生字段。ADC 实际时钟保持 PLLP 40 MHz；任何新增生成错误仍需检查，不能把日志中的错误一概忽略。USART3 DMA 在运行时配置，所以两个 DMA 中断向量由 `app/gl30_app.c` 持有，避免 CubeMX 删除。

## 主机测试

2026-09-28 复查已修复产品 BKIN 两级极性、DRV8316 状态判定、TIM2 回绕竞态、UART DMA 接收边界和编码器非有限角度准入；[历史复查记录](../outputs/stm32-review-20260928/README_CN.md)。BKIN 源不反相（LL 源极性 HIGH/BKINP=0），最终刹车 LOW/BKP=0。当时三种主机配置各 10/10、静态检查 24/24。2026-10-02 增加真实产品 SPI1 端口及独立故障注入测试，见[本次接入与验证记录](../outputs/project-advance-20261002/README_CN.md)。

```powershell
cmake -S firmware-stm32/tests -B build/stm32-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stm32-host --config Debug
ctest --test-dir build/stm32-host -C Debug --output-on-failure
```

当前注册 14 个测试程序：通用 host、FOC 数值安全、bench safety、硬件诊断、应用诊断、触觉行为、CORDIC，以及产品 TIM2、DRV8316、UART RX、AS5048A SPI1、采零状态机、实际 ADC 中断采零和供电准备路径。产品编码器测试编译真实驱动的 MCU 分支，仅替换 LL 外设和时钟；一般 `GL30_BUILD_ONLY` 构建仍保持 INITIALIZING/无效，不能伪造硬件就绪。主机测试不验证 MCU 中断时序或电气波形。

10/02 DRV8316 专项扩展至 111 项检查：旧源码先复现 FIFO、迟到读数和 DRVOFF 取消问题，修订后 Windows Debug、Linux ASan/UBSan、Release＋LTO 各 11/11；产品与 NUCLEO ARM 重建均 0 错误、0 警告。日志保存在上方驱动修订包。普通停转后的成功重新 arm、故障时拒绝 arm、原 PRIMASK 保持均有模拟验证；不把假外设时间当成 MCU 测量。

## 产品板实物验证前的硬边界

- PA4 是上电高电平 CS，PA5/PA6/PA7 为 AF5 SPI1；PC14/PC15 保留。MISO 串联 1 kΩ 按原理图选用 2.5 MHz；边沿与编码器小板本地去耦仍须实物核验。
- PB9/nSLEEP 只在复位及准备条件未满足时保持低；条件满足会自动准备母线和驱动，但不会开启 PWM。不要再按“全程不会拉高电源请求”的旧版本假设接板；当前没有烧录此版本，准备流程也需无电机限流验收。
- 不允许绕过 `control_ready()`、编码器有效门或安全监督器强行开 MOE。
- 可完成代码阅读、协议/状态机测试、公共原理图、保护链评审和 CAD 包络检查。
- 不能宣称已验证编码器协议、相序、电角零位、电流比例、闭环稳定性、温升、回灌或手感。

以上限制针对产品板硬件端口；不应误读为 AS5048A 型号/协议仍未知。现在可以用 NUCLEO + TI 评估板进入受限实物首测，但需要接板后的检查和独立验收。
