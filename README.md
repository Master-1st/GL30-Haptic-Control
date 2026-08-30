# GL30 Haptic Control

[简体中文](README_CN.md) · [Roadmap](ROADMAP.md) · [Contributing](CONTRIBUTING.md) · [Evidence boundary](docs/evidence-boundary.md)

[![CI](https://github.com/Master-1st/GL30-Haptic-Control/actions/workflows/ci.yml/badge.svg)](https://github.com/Master-1st/GL30-Haptic-Control/actions/workflows/ci.yml)
[![Status: pre-alpha](https://img.shields.io/badge/status-pre--alpha-orange)](ROADMAP.md)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE)

**An open-source, real-time haptic control platform for software-defined physical interfaces.**

The project turns “what a control feels like” from fixed mechanics or hard-coded motor behavior into an application-configurable **Haptic Profile**. The same hardware can behave like a detented knob, a spring-return controller, a soft-limited adjuster, a damped flywheel, or a physical interface that changes with software state.

The CubeMars GL30 force-feedback knob is the first reference device, not the platform boundary. The longer-term target includes CNC, media, CAD, robotics, simulation equipment, and custom control surfaces.

![GL30 Haptic Control concept render](hardware/cad/out/CONCEPT_FIT_DEFAULTS/V7_CONCEPT_FIT_DEFAULTS_isometric.png)

> [!IMPORTANT]
> **Current reality:** the no-hardware stack builds, tests, and simulates. The STM32 FOC, haptic primitives, safety path, telemetry, and fault trace exist at code level. The GL30 factory-encoder interface still needs written vendor confirmation, so the real motor is intentionally prevented from producing torque. This repository does not yet claim physical closed-loop or measured haptic performance.

## What it is intended to do

An application selects a Profile, and the device changes its physical feel, input behavior, and display content without requiring a new motor-control implementation for every application.

| Use case | Intended interaction |
| --- | --- |
| CNC / machine jog wheel | Coarse/fine detents, speed-dependent damping, travel limits, and tactile hazard cues |
| Video editing / DAW | Frame or beat detents, timeline inertia, marker attraction, and fast scrubbing |
| CAD / 3D tools | Parameter stepping, zoom damping, mode changes, and tangible numerical boundaries |
| Robotics / teleoperation | Joint limits, recentering, resistance changes, state warnings, and constrained guidance |
| Games / driving simulation | Encoder controls, trim, damping, springs, and state-dependent control feel |
| Instruments / smart devices | Volume, menus, precise settings, on-device status, and custom shortcuts |

These are platform targets, not present-day product claims. The status sections below distinguish usable software, code awaiting hardware, planned work, and unresolved decisions.

## Why this approach matters

| Advantage | Practical value | Current basis |
| --- | --- | --- |
| **Dedicated real-time motor core** | AMOLED rendering, BLE/Wi-Fi traffic, and application stalls stay outside the motor-control critical path | STM32G474 independently schedules 40 kHz FOC and 2 kHz haptics; ESP32-S3 owns UI, connectivity, and configuration |
| **Profile-defined physical feel** | Applications compose detents, springs, endstops, damping, friction, and inertia without changing FOC | JSON Schema, example Profiles, the binary command, and six base haptic primitives exist; full runtime mapping remains open |
| **Fail-off safety model** | Lost communication, invalid sensing, overcurrent, or missed control progress cannot keep replaying stale torque | DRVOFF, TIM1 BKIN/MOE gating, startup gates, command timeouts, watchdogs, torque/current/speed limits, and latched faults are implemented and host-tested |
| **Measurable and debuggable** | Tuning is based on current, torque, timing, loss, temperature, and pre-fault data—not only subjective feel | 2 kHz fast telemetry, slower power/thermal telemetry, a 4096 × 6 frozen fault trace, and deterministic protocol vectors exist |
| **Designed for reproduction** | Other developers can inspect, repeat, challenge, and improve the engineering rather than only watch a demo | CubeMX/Keil project, protocol, simulator, tests, BOM, PCB drawing guide, parametric CAD, and evidence policy are public |

These are design and engineering advantages, not completed performance claims. Torque quality, noise, thermal behavior, lifetime, and usable bandwidth require physical measurements.

## Reusing earlier configurations and ecosystems

**Reuse is a goal, but native compatibility, converted compatibility, and behavioral references are different claims.** The project should not force the community to recreate every preset, and it must not import motor-specific calibration, PID, or current values into GL30.

| Source | Current conclusion | Practical meaning |
| --- | --- | --- |
| GL30 AMOLED V6 `profileVersion: 1` | ✅ **Native format compatibility** | The V6 and current Schemas differ only in identifiers; both V6 examples pass unchanged, 2/2 in the present audit; inconsistent or out-of-range input is still rejected by stricter semantic/safety rules |
| V6 real-time frame / `HAPTIC_COMMAND` | 🧩 **Implemented-subset specification compatibility** | Header, CRC32C, packet IDs, `0x01` fast state, and the 64-byte `0x10` command retain their layouts; V6 registered `0x02` without freezing its payload; physical-device interoperability is unproven |
| SmartKnob `SmartKnobConfig` | 🛠 **Converter planned** | Detent width, endstops, snap point, and magnetic positions are mappable; Protobuf, normalized strengths, and runtime state are not direct copies |
| X-Knob `XKnobConfig` | 🛠 **Planned to reuse SmartKnob mapping** | Core haptic fields are similar, but existing modes are C++ constants rather than portable Profile files |
| SuperDial | 📖 **Behavioral reference, not file compatibility** | BLE Dial/HID, concentric mechanics, and recentering can inform the design; upstream has no stable external haptic configuration format |
| Surface Dial, Home Assistant, MQTT | 🛠 **Planned application adapters** | Reuses mature ecosystems, but these are application I/O and data-source integrations—not STM32 motor protocols |

Compatibility belongs in PC Companion: `legacy source → adapter → Canonical Profile → validation/clamping/report → device command`. The STM32 real-time core keeps one deterministic binary interface and does not parse JSON/Protobuf or accumulate legacy protocol stacks.

See the [Profile compatibility and migration plan](docs/profile-compatibility.md) for field mappings, rejected hardware data, pinned upstream commits, and intentionally blank future-adapter slots. **Only V6 Profile objects have native format compatibility today; SmartKnob/X-Knob importers do not yet exist.**

## How the system works

```mermaid
flowchart LR
    Host[PC / application] -->|Profiles, data, and actions| App[ESP32-S3<br/>AMOLED / HID / connectivity]
    App -->|1 kHz commands| Core[STM32G474<br/>real-time haptics and FOC]
    Core --> Driver[DRV8316R]
    Driver --> Motor[CubeMars GL30]
    Encoder[GL30 factory encoder<br/>PENDING_VENDOR] --> Core
    Core -->|Telemetry, trace, and faults| App
    App -->|State and input events| Host
    Core -->|Independent fault line| App
```

| Real-time task | Firmware schedule | Purpose |
| --- | ---: | --- |
| Current loop / FOC | 40 kHz | Current sampling, transforms, PI control, and SVPWM |
| Encoder / observer | 4 kHz | Angle, velocity, and acceleration estimation |
| Haptic runtime | 2 kHz | Convert Profile/command state into target torque |
| Fast telemetry | 2 kHz | Angle, current, torque, state, and timing |
| Application command | 1 kHz | Receive haptic parameters and control requests |
| Safety monitor | 200 Hz | Power, thermal, communication, and latched-fault supervision |

These are code scheduling values, not measured mechanical closed-loop bandwidth.

## Capability status

Status vocabulary:

- ✅ **Available now:** runnable or inspectable without physical hardware;
- 🧩 **Implemented in code:** present in STM32/PC code and automated tests, but requires physical validation and tuning;
- 🛠 **Planned:** explicitly on the roadmap but not usable today;
- ⏳ **Pending:** blocked on vendor data, sample measurements, or design evidence.

### ✅ Available now without hardware

| Capability | What works now | Evidence limit |
| --- | --- | --- |
| STM32 project | Regenerate the MDK-ARM project with STM32CubeMX and build the active LL path | `BUILD_ONLY`; it does not prove motor motion |
| Protocol and PC core | Encode/decode V1 binary frames, CRC32C, stream resynchronization, command clamping, time sync, and trace downsampling | TypeScript tests and deterministic golden vectors |
| Haptic Profile | Validate JSON Profiles and inspect game-dashboard and video-timeline examples | Schema works; the physical-device pipeline does not yet |
| Device simulator | Exercise commands, telemetry, communication timeouts, safety states, and the no-hardware end-to-end data path | `SIM_ONLY`; it does not model real motor mechanics or feel |
| Firmware algorithm regression | Host-test protocol, driver logic, FOC mathematics, haptics, safety supervision, and fault trace | Software regression only; no peripheral or power-stage evidence |
| Digital hardware package | Inspect parametric concept CAD, bench-board BOM, PartsBridge/Altium import data, PCB drawing guide, and bring-up procedure | `CAD_CHECKED` / design material; no manufactured assembly evidence |

### 🧩 Implemented in code, awaiting hardware validation

| Capability | Current implementation | What physical work must prove |
| --- | --- | --- |
| Three-phase FOC | Clarke/Park, d/q PI control, anti-windup, SVPWM, sampling-window enforcement, and torque estimation | Phase order, current polarity/scaling, dead time, bus ripple, electrical zero, and stability |
| Base haptic primitives | Detent, position/spring, velocity/damper, soft endstop, friction, and inertia, composable at the low level | Real torque, noise, vibration, feel, parameter range, and instability boundaries |
| Motor safety path | Hardware BKIN, DRVOFF, MOE gate, startup checks, communication timeouts, IWDG, overcurrent/thermal/bus faults, and latching | Fault injection, shutdown delay, nuisance trips, recovery, and full-envelope safety |
| Driver and sensing | DRV8316R SPI/CSA framework, synchronized three-phase ADC, current-zero calibration, VBUS, and motor-temperature input | SPI waveforms, CSA gain/matrix, ADC noise, comparator threshold, phase-node overshoot, and temperature |
| Telemetry and fault trace | Fast/slow telemetry, error/drop/deadline counters, and a 40 kHz six-channel RAM trace frozen on fault | Sustained throughput, timestamps, pre-fault data integrity, and PC visualization |
| Auxiliary sensing | INA228 power/energy and VEML7700 ambient-light drivers, retries, and telemetry interfaces | Addresses, calibration, noise, layout effects, and actual product usefulness |

### 🛠 Planned user-facing capabilities

1. **Close the real motor loop:** implement the confirmed encoder backend, then calibrate phase order, current sensing, electrical zero, and low-torque operation.
2. **Complete the Profile pipeline:** `JSON Profile → PC/ESP32 → binary command → STM32 haptic runtime`, so changing applications changes physical feel.
3. **Expand haptic effects:** texture, asymmetric detents, marker attraction, composed effects, robust recentering, and a safety-limited active-position mode.
4. **Build the ESP32-S3 application:** 5 Mbaud motor-core transport, log record/replay, AMOLED UI, Profile selection, and engineering status pages.
5. **Expose application interfaces:** USB/BLE HID first, then MIDI, WebSocket, MQTT, or local application plugins where justified by real use cases.
6. **Deliver desktop tooling:** connection management, live plots, fault traces, parameter tuning, Profile editing/deployment, and calibration workflows.
7. **Release a reproducible device:** final PCB, mechanics, harnesses, assembly, calibration, thermal/regeneration design, lifetime evidence, and external reproductions.

### ⏳ Decisions and evidence still pending

| Pending item | Why it cannot be frozen yet | What it affects |
| --- | --- | --- |
| GL30 factory encoder part, supply, logic levels, protocol, pinout, connector, update rate, and latency | Public information is insufficient; written vendor data and real waveforms are required | Encoder driver, MCU pins, connector, and first-board schematic |
| Whether the factory unit is specifically AS5048A and which output it exposes | A verbal part name is not a factory SKU, datasheet, and harness definition | Only one real SPI/PWM/ABI path should be frozen; guessed parallel backends will not be retained |
| GL30 phase parameters, torque constant, current envelope, thermal resistance, and regenerated energy | Current numbers are first-pass design inputs; unit and operating-condition variation matters | Current-loop tuning, torque caps, cooling, bus protection, and power supply |
| Final PCB size, copper, cooling, and cost | Bench-board power, thermal, and EMI results must come first | Layout, layer count, BOM, and enclosure volume |
| Mechanical tolerances, support/load path, cable routing, and mounting | Vendor tolerances plus physical assembly and load tests are required | Final CAD, rotating clearance, lifetime, and assembly yield |
| Usable haptic parameters and performance metrics | “Clear,” “quiet,” and “comfortable” need sample measurements and user evaluation | Default Profiles, tuning limits, performance documentation, and release claims |
| EMC, reliability, and safety level | These can only be tested on hardware close to the final design | Whether the platform can become a specific product or pursue certification |

If the confirmed factory interface matches current assumptions, first-board work should mostly be calibration and validation. If its electrical interface or protocol differs, the encoder driver and schematic must change first; that cannot honestly be described as “tuning only.”

## What a Haptic Profile looks like

A Profile describes the physical behavior an application wants. This fragment means “one sinusoidal detent every 15 degrees, with a small amount of damping, friction, and inertia”:

```json
{
  "haptic": {
    "mode": "detent",
    "detent": {
      "widthDeg": 15,
      "strengthmNm": 12,
      "waveform": "sine"
    },
    "damping": 0.00035,
    "friction": 0.001,
    "inertia": 0.00003
  }
}
```

The Schema and examples validate today, and the STM32 contains matching base primitives. Application matching, complete Profile translation, ESP32 delivery, and measured physical behavior remain unfinished.

## Repository map

```text
firmware-stm32/   STM32G474 motor core, CubeMX/Keil project, and host tests
firmware-esp32/   ESP32-S3 application-layer boundaries (application incomplete)
protocol/         Frame format, payload specification, and TypeScript codec
pc-companion/     Profile API, device simulator, and desktop-side core packages
hardware/         Bench-board BOM/guides and parameterized concept CAD
docs/             Architecture, pinout, bring-up, test gates, and evidence limits
scripts/          Deterministic vector generation and no-hardware verification
tests/            TypeScript protocol, Profile, and system tests
```

The public repository keeps the latest snapshot only. Historical design folders, generated build trees, local logs, and redistributable copies of vendor attachments are intentionally excluded.

## Run the no-hardware checks

Requirements: Node.js 24+, pnpm 11.19+, CMake 3.22+, and a C11 compiler.

```bash
pnpm install --frozen-lockfile
pnpm verify

cmake -S firmware-stm32/tests -B build/stm32-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stm32-host --config Debug
ctest --test-dir build/stm32-host -C Debug --output-on-failure
```

The only active STM32 project is:

```text
firmware-stm32/cubemx/GL30_AMOLED_V7/MDK-ARM/GL30_AMOLED_V7.uvprojx
```

It is generated by STM32CubeMX and uses LL in the active peripheral path. Regenerate with `firmware-stm32/cubemx/generate.ps1`; see [the motor-core guide](firmware-stm32/README.md).

> [!CAUTION]
> Until the factory encoder interface is confirmed, the `PENDING_VENDOR` backend intentionally refuses motor arming and keeps DRVOFF/TIM1 MOE disabled. Do not bypass `control_ready()` or any hardware safety gate.

## Roadmap and participation

The project is aiming toward a reproducible v1.0 over roughly 12 months, with August 2027 as a target rather than a promise. Vendor closure, electrical validation, motor evidence, thermal/lifetime work, and external reproductions must close before the project calls itself complete. See [ROADMAP.md](ROADMAP.md) for the evidence gates.

This is both an engineering project and a public learning process. Contributions are welcome in safety and schematic review, haptic algorithms and Profiles, STM32/ESP32 code, tests and tools, mechanical tolerances, documentation, and reproduction. Start with [CONTRIBUTING.md](CONTRIBUTING.md), or open a [Discussion](https://github.com/Master-1st/GL30-Haptic-Control/discussions).

## License and third-party material

Original project material is licensed under [Apache License 2.0](LICENSE), unless a file says otherwise. STM32Cube-generated dependencies and other third-party components retain their own notices; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Vendor STEP/PDF files are not redistributed here—download them from the sources listed in `hardware/cad/vendor/SOURCE_MANIFEST.md`.

This independent community project is not affiliated with or endorsed by CubeMars, STMicroelectronics, Texas Instruments, Espressif, Waveshare, Arm, or Keil. Product names and trademarks belong to their respective owners.
