# 证据等级与放行边界

| 标记 | 能证明 | 不能证明 |
| --- | --- | --- |
| `SIM_ONLY` | 协议、Profile、虚拟设备和错误路径的软件行为 | MCU 时序、电气、力矩、手感 |
| `BUILD_ONLY` | 指定工具链能编译、链接；主机测试按断言通过 | 固件在目标板能启动或满足实时性 |
| `CAD_CHECKED` | 官方 STEP 已导入，参数化包络/指定清距断言通过 | 公差、装配、载荷、寿命、热、可制造性 |
| `BENCH_TESTED` | 指定单板/部件在记录工况下实测 | 整机、全温、批量、长期寿命 |
| `INTEGRATED_TESTED` | 整机在规定工况、持续时间和故障注入下通过 | 未覆盖工况和量产一致性 |

## 当前状态

- 官方 GL30/Waveshare STEP 和 V7 默认装配：`CAD_CHECKED`。
- STM32：CubeMX MDK-ARM + LL 工程已用 Keil ARMCLANG 6.21 clean rebuild，`0 Error(s), 0 Warning(s)`；主机测试 `1/1` 通过，仍只记录为 `BUILD_ONLY / SIM_ONLY`。
- TypeScript：按本轮软件回归结果记录为 `SIM_ONLY`，不能替代目标 MCU 或整机测试。
- 编码器电气、PCB、机械载荷、FOC 带转、温升和整机：没有实物证据。

## 当前可追溯入口

- 当前设计输入：`design/reference/current/`。
- 官方 CAD 来源与哈希：`hardware/cad/vendor/SOURCE_MANIFEST.md`。
- CAD 断言：`hardware/cad/out/CONCEPT_FIT_DEFAULTS/V7_CONCEPT_FIT_DEFAULTS_geometry_report.json`。
- 厂家未决项：`docs/cubemars-vendor-confirmation-cn.md`。
- 编码器安全边界：`firmware-stm32/drivers/factory_encoder.h` 与当前唯一实现。
- 公开仓库从当前工程建立干净快照；旧内部工程、迁移记录和生成目录不进入活动文件树。

禁止把模型评审、CAD 渲染、编译大小、单元测试数量或 AI 意见写成编码器性能、承载能力、限流响应、回灌、温升、寿命或手感实测。
