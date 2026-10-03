# Motor Control

电机实物已到货。厂家参数、曲线一致性、星形线—线到相值换算、当前固件限流和首测门槛统一记录在 [`GL30_KV290_ARRIVAL_BASELINE_CN.md`](GL30_KV290_ARRIVAL_BASELINE_CN.md)。官方 STEP 与本次用户上传文件哈希一致，不重复保留历史副本。

当前画图输入为 `../../docs/stm32-pinout-and-schematic-cn.md`。ECAD 源文件建立后只保留当前版，不导入历史修订目录。

DRV8316R 低成本验证子板按 `../drv8316r_bench/SCHEMATIC_PCB_DRAWING_GUIDE_CN.md` 逐页绘制，并按 `../drv8316r_bench/BUILD_AND_TEST_CN.md` 验证。验证子板不包含工厂编码器，也不包含正式无线产品的 3S/BQ25798/BQ77915、电机高侧隔离和再生制动闭环，不能替代最终主功率板证据。

正式产品唯一首板路径为 `BQ25798 SYS → 外部双向高侧隔离 → INA228 → MOTOR_BUS → DRV8316R → GL30`，并在 MOTOR_BUS 上布置不依赖主 MCU 的制动电阻/MOSFET和过压 BKIN。SYS 路径必须与独立电池轨做实物 A/B，最终只保留通过的一种。
