# STM32 V7 上电、编码器接入与调参手册

当前产品固件使用 `factory_encoder_pending`。它不访问编码器引脚/外设，始终无效；启动时保持持久编码器 warning 并拒绝 arm，不把“厂家资料尚未闭合”伪装成运行故障。只有系统已经进入 `ACTIVE` 后编码器失效，才在 4 kHz 监控路径锁存 `GL30_FAULT_ENCODER` 并执行安全关断。4 kHz 的 `250 us` 只是标称调度周期，不是未经实板测量的最坏关断时间。因此该版本只能用于编译、静态检查和安全链验证，不能让电机带转。

## 1. 当前构建

```powershell
Set-Location firmware-stm32\cubemx
.\generate.ps1

cd .\GL30_AMOLED_V7\MDK-ARM
$args = @('-r', '.\GL30_AMOLED_V7.uvprojx', '-t', 'GL30_AMOLED_V7', '-j0', '-o', '.\build-ac6.log')
Start-Process G:\software\KEILV5\UV4\UV4.exe -ArgumentList $args -WindowStyle Hidden -Wait
```

`generate.ps1` 只允许一个 CubeMX Java 进程，使用无界面 `-q` 生成 MDK-ARM；不要使用 `-h`，也不要并行打开 CubeMX。当前 clean rebuild 已用 ARMCLANG 6.21 验证为 `0 Error(s), 0 Warning(s)`。

主机测试使用临时目录，不在项目里保留构建历史：

```powershell
cmake -S firmware-stm32/tests -B build/stm32-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stm32-host
ctest --test-dir build/stm32-host --output-on-failure
```

验收：唯一产品目标是 CubeMX 生成的 `GL30_AMOLED_V7`；活动生产路径不含 `mt6835`、旧 HAL BSP 或 `ENCODER_BENCH`；pending 样本永远不能通过 control-ready 门。编译和主机测试仍只属于 `BUILD_ONLY/SIM_ONLY`。

## 2. 厂家回复前可做

- 阅读/测试 FOC、协议、Profile、安全监督、电流换算和故障路径。
- 画 STM32/DRV8316/三电流/电源/硬件保护公共原理图。
- 用逻辑分析或主机测试证明 pending 实现不配置 SPI1/TIM3/编码器 GPIO。
- 对所有命令入口做失效安全测试：故障锁存、MOE 关闭、DRV 输出关闭。

禁止：接编码器电源、猜线序、烧录猜测后端、跨过编码器门强制 PWM、把编译通过写成硬件通过。

## 3. 厂家回复后的代码动作

1. 保存厂家原文、datasheet、线束图和 STEP 修订。
2. 从 `factory_encoder_pending.c` 切换为一个厂家确认的实现；删除 pending 文件，不保留多后端或兼容层。
3. 仅按确认接口初始化对应 GPIO/DMA/定时器；更新率、超时和滤波从厂家指标及实测得到。
4. 把实际角度、错误位、方向、零位与电角偏置写入测试表，不直接写死未经测量的“常用值”。
5. 重新跑主机测试、ARM clean build、静态搜索和代码审查。

## 4. 实板顺序

### G2：不上电

- 核对元件方向、连接器、焊接、短路、地和电源域。
- 测六路 PWM 下拉、`DRVOFF` 上拉、`HARD_FAULT_N` 上拉和 BOOT0 下拉。
- 编码器断开；电机断开；母线断开。

### G3：只上 3.3 V

- 限流电源从低限流开始，记录静态电流。
- 检查 NRST、24 MHz、SWD、IWDG、UART 和故障输出。
- 用 pending 固件验证：TIM1 MOE 始终关闭、六路 PWM 为低、DRV 不 arm。
- 逐路注入 SOA/B/C 上下阈值、DRV nFAULT 和 16 V 比较器，确认 BKIN 异步关断。

### G4：工厂编码器，仍不接电机

- 先用万用表/示波器确认厂家电源和逻辑电平，再连接信号。
- 慢速手转，保存原始帧、角度、时间戳、错误位和参考角度。
- 验证 0/360° 连续性、方向、静止噪声、刷新率、延迟、掉线和断电恢复。
- 错误/超时/线断开必须马上撤销 control-ready；不能靠上层 UI 判断安全。

### G5：低压低流电机

- 先确认 7 极对、三相电阻/电感和相序；只在机械自由、可急停条件下进行。
- 从受限母线和 `0.2 A` 级电流限值开始；确认方向后再做电角度对齐。
- 先开环测相序，再闭电流环；禁止同时改相序、编码器方向、PI 和力矩常数。
- 每次只改一个参数，保留固件哈希、波形、参数和结果。

## 5. FOC 调参顺序

1. ADC 零偏：三相在无电流时的均值、峰峰值和温漂。
2. 电流比例：用已知直流电流核对 SOx 与 INA228/电流表。
3. 相序与编码器方向：正电角必须对应期望机械正方向。
4. 电角零位：多次重复对齐，统计均值和离散，不用一次值冻结。
5. 电流 PI：从低带宽开始，记录阶跃上升、超调、稳态误差和 deadline。
6. 力矩/触觉：先纯阻尼，再摩擦、惯量、卡点、端止挡；每项独立验证。
7. 回灌与热：从低速轻拨开始，确认 14.4/16 V 链，再增加工况和持续时间。

## 6. 每次实验记录

- 日期、操作者、板号、电机/编码器批次、机械版本。
- Git commit、固件哈希、编译器和构建命令。
- 母线、电流限值、转速/手动动作、环境温度和持续时间。
- 原始波形/日志，不只留截图结论。
- 通过标准、实际结果、失败模式和下一步唯一变量。

只有 G0–G7 对应证据闭合后，才能把项目从 `BUILD_ONLY/CAD_CHECKED` 提升到 `BENCH_TESTED/INTEGRATED_TESTED`。
