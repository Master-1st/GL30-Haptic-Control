# Roadmap to a reproducible v1.0

The project targets a reproducible v1.0 around August 2027, about one year after the first public snapshot. This is an estimate, not a completion guarantee; dates do not replace the required evidence.

User-facing priorities and explicit exclusions are defined in [feature and community-demand research](docs/feature-research.md). Physical feel remains P0; integrations do not bypass motor, timing, or safety gates.

## Definition of v1.0

The September 9 [pressable-ring / 24-RGB architecture plan](docs/knob-press-rgb-architecture-cn.md) adds the following uncompleted product work:

- [ ] Verify the floating press mechanism, independent load path, hard stops, and display/cable clearance.
- [ ] Verify localized light output and leakage control in the knob-to-base gap with at least 24 LEDs.
- [ ] Lay out and validate three custom PCBs: control, power, and RGB/press; count display, encoder, and BMS assemblies separately.
- [ ] Implement measured-angle lighting and press/release timer controls; validate one turn = 60 minutes without counting commanded motor motion as user input.

These are design requirements, not existing firmware/CAD or completed bench results.

`v1.0` means another developer can build, commission, and safely evaluate the reference design from published sources. It requires:

- published schematics, PCB source, manufacturing outputs, BOM, and assembly notes;
- published mechanical source, drawings, tolerance notes, and load-path rationale;
- reproducible STM32 and ESP32 firmware builds;
- measured encoder, current-sense, FOC, fault, thermal, and regeneration behavior;
- documented calibration and safe commissioning procedures;
- stable Haptic Profile schema and at least four composable core effects;
- one maintained desktop/host reference implementation;
- maintained reference flows for global volume/media, local timer, one Home Assistant light entity, and one weather data source;
- at least two independent external builds or equivalent third-party reproduction evidence;
- open issues documenting remaining limitations.

## 已取得的台架证据（2026-09-08 UTC）

[HAPTIC25 续测报告](docs/bench-validation-20260907-haptic25-cn.md)记录 NUCLEO + TI EVM + AS5048A：三次 ALIGN、±10/25/50/100 mA 各 20 ms、八类 HAPTIC 各 250 ms、58 项关断命令、60 s 无 PWM 及原始采样审计通过。本轮修复控制时序，没有提高电源限流或放宽保护。

这关闭了此前“台架完全没有 IQ / 触觉出力通过”的缺口，**不勾选下列完整产品里程碑**：力矩/手感、长时运行、热/回灌、nFAULT 整链、CET6 端口、ESP32/AMOLED/无线仍缺实测。短动作与软件故障注入不能代替测量验收。

[2026-09-08 扩展验收](docs/bench-validation-20260908-haptic25-extended-cn.md)补充 37 组实机冻结采样、ACTIVE STOP/失联关断、重复运行和 2000 次通信回归。只关闭相应台架测试项，不勾选下列产品里程碑。

## 近期 4 周路径（当前事实约束下）

以下按每周约 6–10 小时、评估板和线材已齐全估计；若装配/信号不通过，后续周次顺延。

1. **第1周**：安装三件式电机夹具；在 NUCLEO + TI EVM 功率输出关闭时确认 AS5048A 通信、电流零点和故障门。H25 已取得接 EVM 的受限记录；装配变更后仍需新的基线。
2. **第2周**：完成低电流编码器 + 电流采样 + 首次闭环可重复验证，输出 `Iq ≤ 0.10 A` 且每次动作 ≤ 2 s。
3. **第3周**：闭环实测通过后，准备四类基础手感（detent/spring/damper/endstop）的运行入口与受限测试。现有 IQ 上限 ±0.10 A / 2 s，HAPTIC 上限 10 s；H25 追加实测 IQ 2 s（仅 1 mA）、自由模式 10 s、八模式各 1 s；这不是满电流/满负载最大时长或完整产品手感验收。
4. **第4周**：上线最小有线终端链路（屏幕/音量/定时器），再回填无线路线前的验证清单。

## Phase 0 — Public engineering baseline（保持）

- [x] Publish a clean repository without private history or generated build trees.
- [x] Publish current CubeMX/Keil project, host tests, protocol, concept CAD, and bench-board guide.
- [x] Add CI, contribution process, security policy, and explicit evidence boundary.
- [ ] Close every public absolute-path and vendor-redistribution issue.
- [ ] Convert the current technical backlog into labelled, independently checkable issues.

Exit gate: a new contributor can understand the architecture, run software-only checks, and see exactly what is not yet proven.

## Phase 1 — Interfaces and safe bench hardware

- [ ] Obtain written factory-encoder electrical, framing, rate, latency, checksum, direction, zero, harness, and connector data.
- [ ] Freeze motor/encoder connector and logic assumptions only after that written evidence.
- [ ] Validate `DRV8316` fault path and safety chain on NUCLEO + TI EVM（DRVOFF, nFAULT→TIM1_BKIN, MOE 关闭, 电流采样链路）.
- [ ] Publish oscilloscope captures, test configuration, raw measurements, and failure notes.

Exit gate: the bench-first TI chain is safe in hardware and no powered motor closed-loop is claimed until both software and hardware gates are green.

## Phase 2 — Motor core and calibration（优先硬件闭环）

- [ ] Confirm phase order, electrical zero, encoder direction, and update timing.
- [ ] Complete low-energy current calibration and limited-voltage open-loop checks.
- [ ] Close current control then safe torque control on staged current/voltage/temperature limits.
- [ ] Measure loop timing, current noise, torque linearity, cogging, encoder nonlinearity, regeneration, and thermal behavior.
- [ ] Add encoder correction and cogging compensation only when measurements justify them.

Exit gate: repeatable low-energy torque control with traceable calibration and fault evidence.

## Phase 3 — Haptic runtime and developer API

- [ ] Freeze Haptic Profile schema v1 after implementation feedback.
- [ ] Validate detent, spring, damper, and endstop primitives.
- [ ] Add texture, asymmetric detent, and composite effects only when the core primitives are measurable.
- [ ] Publish C++, TypeScript, and Python profile examples as each implementation becomes maintained.
- [ ] Add Profile editing, preview, clamp reports, sharing, and safe SmartKnob/X-Knob conversion.
- [ ] Demonstrate profile changes without application-specific FOC code.

Exit gate: applications can create repeatable, bounded haptic behavior through a stable profile interface.

## Phase 4 — Product integration and external reproduction（长期）

- [ ] Integrate ESP32-S3 UI, HID, transport, and configuration while preserving motor-core deadlines.
- [ ] Deliver global volume/media and the local timer as the first end-to-end user applications.
- [ ] Deliver Home Assistant `light` over MQTT with discovery/availability before adding `climate`, `cover`, or `scene`。
- [ ] Deliver an Open-Meteo weather flow with user-set coordinates, visible source, update age, cache, and offline state; add Home Assistant weather only as a separate adapter.
- [ ] Add creator/host adapters only behind maintained interfaces: video timeline, MIDI, and Windows per-app volume first.
- [ ] Publish assembly, bring-up, calibration, troubleshooting, and recovery guides.
- [ ] Freeze the custom product PCB only after its interface, power and thermal gates pass; keep wireless full haptics as the final product goal.
- [ ] Validate mechanical support, tolerance stack, user loads, acoustic behavior, and thermal paths.
- [ ] Support external builds and resolve reproducibility gaps.
- [ ] Release v1.0 only after the definition above is met.

## 能力里程碑（不再以版本号放行）

| 能力门 | 最低证据 |
| --- | --- |
| EVM 首轮安全底线 | 已完成 12V、B1/KEEPALIVE、`Iq ±0.10 A`（≤2s）与 `align/zero` 的硬件+日志闭环 |
| 电机核心闭环 | FOC 闭环、故障链路、热行为、再生与采样一致性都有原始测量数据 |
| 触觉核心行为 | 四类核心手感有可复现实验、噪声/偏差边界和回归条件 |
| 有线端到端 | 屏幕/音量/定时器的基本交互可稳定运行并可回退 |
| 无线闭环与外延 | 在满足机械与功率门后，完成无线主链路整机联调 |

Progress is reported through issues, pull requests, discussions, changelog entries, and signed GitHub releases. Features may move; exit evidence does not. 里程碑达成是闭环条件，不以已有 `v0.2 / v0.3 / ...` 标签替代实测。
