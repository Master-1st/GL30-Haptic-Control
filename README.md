# GL30 Haptic Control

**2026-10-03 update:** [Current offline delivery](docs/offline-delivery-20261003-cn.md), [Altium PCB guide](docs/pcb-drawing-guide-20261003-cn.md), and [A-board native schematic snapshot](hardware/pcb/stm32-foc-a-altium-r6/README_CN.md). Current ESP32/STM32 host suites have 8/15 cases respectively; frozen firmware builds passed. This candidate is unflashed and the custom PCB is not laid out or routed.

English · [简体中文](README_CN.md) · [Documentation](docs/README.md) · [Roadmap](ROADMAP.md) · [Contribute](CONTRIBUTING.md)

[![CI](https://github.com/Master-1st/GL30-Haptic-Control/actions/workflows/ci.yml/badge.svg)](https://github.com/Master-1st/GL30-Haptic-Control/actions/workflows/ci.yml)
[![pre-alpha](https://img.shields.io/badge/status-pre--alpha-orange)](ROADMAP.md)
[![Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE)

**Give rotary controls a programmable feel.**

GL30 Haptic Control is an open-source haptic knob project in development for desktop controls, creative tools, and everyday interactions. Built around a CubeMars GL30 brushless motor, an encoder and closed-loop control produce virtual detents, damping, spring return, and soft limits.

The aim is to let one knob change its feel with the application: distinct steps for volume, smooth movement through a timeline, or resistance at a parameter's boundary. A round display provides context, while physical feedback helps you feel the operation.

![GL30 Haptic Control concept](hardware/cad/out/CONCEPT_FIT_DEFAULTS/V7_CONCEPT_FIT_DEFAULTS_isometric.png)

*Existing concept render, not a finished-device photo. It does not include the new press mechanism or bottom light ring.*

## Different actions, different feel

- **Detents:** configurable positions and strength for selection and stepped adjustments.
- **Damping:** resistance for continuous control, from careful adjustment to faster movement.
- **Spring return:** a centered interaction for exploring playback speed or directional input.
- **Soft limits:** resistance at parameter boundaries, making a range tangible.

These behaviors are organized around **Haptic Profiles**. The goal is to separate the feel an application requests from low-level motor control, so developers can adjust configurations and compose interactions.

## What could you use it for?

These are priority application directions; their software and whole-device integrations are still in development.

| Application | Intended interaction |
| --- | --- |
| **Volume and media** | Stepped volume, on-knob status, and press-to-control playback |
| **Focus timer** | One revolution sets one hour; orange progress rings represent additional turns, with press-to-start/pause |
| **Video and creative tools** | Timeline navigation, frame stepping, and parameter adjustment with task-specific feel |
| **Smart home** | Control lights, temperature, or scenes through platforms such as Home Assistant |

GL30 is the first reference device. The longer-term direction is to bring these haptic configurations and application interfaces to other physical controls. See the [feature research](docs/feature-research.md).

## Screen, light, and motion

The interaction direction combines a black rotary ring, a black screen surround, and RGB light emerging from the knob-to-base gap. At least 24 LEDs form a fixed ring: the illuminated position follows the measured knob angle instead of a painted marker. Pressing the ring provides confirmation and timer controls.

Expressive screen motion uses anticipation, elastic exaggeration, and rebound to respond to the user's actions. When off, the display should blend into the black surface.

The new press mechanism and light ring are not yet implemented in CAD or firmware. Board and assembly details belong in the separate [design plan](docs/knob-press-rgb-architecture-cn.md).

## A platform to build on

- **Separate control and UI roles:** STM32G474 handles the motor and haptics; ESP32-S3 handles the display, applications, and connectivity.
- **Inspectable configurations:** Profile formats, examples, and validation tools support experimentation with application-specific feel.
- **Start without hardware:** protocol checks, a device simulator, host tests, and a separately buildable UI model run on a computer.
- **Open project materials:** firmware, application-layer sources, protocol definitions, prototype CAD, and engineering notes have dedicated entry points.

## Current progress

The project is a **pre-alpha engineering prototype**.

- **Bench-tested:** bounded GL30/AS5048A closed-loop operation, basic haptic modes, and stop/communication-loss shutdown.
- **Available offline:** the current protocol, Profile validation, device simulator, same-source KK C UI, NVS preferences, control-owner handoff, and product-port protection regressions.
- **Engineering materials:** the seven-page A-board Altium schematic without test points, BOM and placement guide, plus R21 nominal mechanical models and the corrected assembly manual.
- **Next:** finish four A-board pad footprints and placement, complete B/C circuits, and validate the custom board, display, mechanism and assembled haptic feel on hardware.

This is not yet a ready-to-replicate kit. Bench results do not establish finished-device feel, thermal performance, battery life, or durability. See the [bench report](docs/bench-validation-20260908-haptic25-extended-cn.md) and [roadmap](ROADMAP.md).

## Start exploring

- [UI model and host preview](firmware-esp32/ui/README.md)
- [Haptic configuration compatibility](docs/profile-compatibility.md)
- [Prototype CAD and printing notes](hardware/cad/README_CN.md)

To run software checks from the repository root, use Node.js 24+ and pnpm 11.19+:

```bash
pnpm install --frozen-lockfile
pnpm verify
```

These commands check the protocol, Profiles, and simulator without hardware. They do not launch a GUI or drive a motor.

Go deeper: [STM32 motor core](firmware-stm32/README.md) · [PC Companion](pc-companion/README.md) · [Protocol](protocol/schema/protocol-v1.md) · [Documentation](docs/README.md).

## Help shape the feel

Contributions to haptics, application adapters, screen animation, mechanics, tests, and documentation are welcome. Use cases and reproduction reports are valuable too. Start a [Discussion](https://github.com/Master-1st/GL30-Haptic-Control/discussions) or read the [contributing guide](CONTRIBUTING.md).

[SmartKnob](https://github.com/scottbez1/smartknob) and [X-Knob](https://github.com/SmallPond/X-Knob) are important interaction references; sources are documented in the [feature research](docs/feature-research.md). GL30 hardware parameters and validation remain independent.

## License

Original material is licensed under [Apache-2.0](LICENSE). Third-party components retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Obtain vendor material from its [original sources](hardware/cad/vendor/SOURCE_MANIFEST.md).

This project is independent of the referenced vendors and projects and does not imply their endorsement.
