# GL30 STM32 固件：产品工程与 NUCLEO 联调工程

当前两个硬件目标均由 STM32CubeMX 生成，使用 Keil MDK-ARM 与 ARMCLANG 6.21；外设和高频路径采用 STM32 LL。它们是不同硬件端口，不是历史版本。旧 HAL/CMake ARM 工程不作为活动路径。

**手上是 NUCLEO-G474RE + TI DRV8316REVM 时，从 [FOC 联调工程](bench/NUCLEO_G474RE_FOC/README_CN.md) 开始。** 板上为 HAPTIC25：20 kHz 电流环、AS5048A SPI、受限 ALIGN/双向 IQ/八模式已通过实测。2026-09-08 追加定时边界、重复运行、运行中 STOP 和失联关断，详见 [扩展验收](../docs/bench-validation-20260908-haptic25-extended-cn.md)。不要把下面 CET6 产品板工程烧到 NUCLEO，也不要混用引脚表。

## CET6 产品板当前实现

- 160 MHz；TIM1 40 kHz 中心对齐互补 6-PWM，默认死区 500 ns。
- TIM1 CH4/TRGO2 同步 ADC1/2/3 注入采样；40 kHz FOC/SVPWM。
- TIM6 4 kHz 角度观测，TIM7 2 kHz 触觉与安全调度。
- DRV8316：SPI3 Mode 1、5 MHz；USART3：5 Mbaud DMA；I2C1：400 kHz。
- DRVOFF 上电关断、TIM1 MOE 默认关闭、PB12/TIM1_BKIN 低有效、故障锁存。
- 产品板公共编码器接口为 `drivers/factory_encoder.h`；本目标仍使用 `factory_encoder_pending.c`，不允许 arm。已确认的 AS5048A 编解码和真实 SPI 端口先在 NUCLEO 联调工程实现，尚未把产品板端口假装验证通过。

## CET6 产品板 CubeMX/Keil 工程

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

本次本地产品工程 clean rebuild 结果：ARMCLANG 6.21，`0 Error(s), 0 Warning(s)`；程序占用 `Code=30540, RO=608, RW=16, ZI=61024`。这是构建证据，不是实物运行证据；本次修订没有发布到 GitHub。

CubeMX 6.18.1 会在重新载入该 IOC 时重复输出几条已知、精确匹配的迁移诊断（未使用的 RIF 虚拟引脚、ADC `CommonPathInternal` 空值和未启用的 RCC RNG/USB 频率字段），随后仍返回 `project generate: OK`。脚本只过滤这些固定文本；任何新增 `OptionalMessage_ERROR`、`KO`、LL 拒绝或生成失败仍会停止。USART3 DMA 在运行时配置，所以两个 DMA 中断向量由 `app/gl30_app.c` 持有，避免 CubeMX 删除。

## 主机测试

```powershell
cmake -S firmware-stm32/tests -B build/stm32-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stm32-host --config Debug
ctest --test-dir build/stm32-host -C Debug --output-on-failure
```

HAPTIC25 当前为 `7/7` 测试程序：通用 host、FOC 数值安全、bench safety、硬件诊断、应用诊断、触觉行为和 CORDIC 端口。独立 Debug、ASan+UBSan、Release+LTO 均需通过；主机测试不验证 MCU 中断时序或电气波形，实机证据单列在扩展验收报告中。

## 产品板端口移植前的硬边界

- PA4/PA5/PA6/PA7/PC14 只保留为高阻候选资源，不命名协议、不接供电。
- 不允许绕过 `control_ready()`、编码器有效门或安全监督器强行开 MOE。
- 可完成代码阅读、协议/状态机测试、公共原理图、保护链评审和 CAD 包络检查。
- 不能宣称已验证编码器协议、相序、电角零位、电流比例、闭环稳定性、温升、回灌或手感。

以上限制针对产品板硬件端口；不应误读为 AS5048A 型号/协议仍未知。现在可以用 NUCLEO + TI 评估板进入受限实物首测，但需要接板后的检查和独立验收。
