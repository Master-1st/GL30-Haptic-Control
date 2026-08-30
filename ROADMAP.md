# Roadmap to a reproducible v1.0

This roadmap starts from the first public snapshot in August 2026 and targets a reproducible v1.0 around August 2027. It is evidence-gated: a calendar date never overrides an unsafe or unverified design.

User-facing priorities and explicit exclusions are defined in [feature and community-demand research](docs/feature-research.md). Physical feel remains P0; integrations do not bypass motor, timing, or safety gates.

## Definition of v1.0

v1.0 means another developer can build, commission, and safely evaluate the reference design from published sources. It requires:

- released schematics, PCB source, manufacturing outputs, BOM, and assembly notes;
- released mechanical source, drawings, tolerance notes, and load-path rationale;
- reproducible STM32 and ESP32 firmware builds;
- measured encoder, current-sense, FOC, fault, thermal, and regeneration behavior;
- documented calibration and safe commissioning procedures;
- stable Haptic Profile schema and at least four composable core effects;
- one maintained desktop/host reference implementation;
- maintained reference flows for global volume/media, a local timer, one Home Assistant light entity, and one weather data source;
- at least two independent external builds or equivalent third-party reproduction evidence;
- open issues documenting remaining limitations.

## Phase 0 — Public engineering baseline (August–September 2026)

- [x] Publish a clean repository without private history or generated build trees.
- [x] Publish current CubeMX/Keil project, host tests, protocol, concept CAD, and bench-board guide.
- [x] Add CI, contribution process, security policy, and explicit evidence boundary.
- [ ] Close every public absolute-path and vendor-redistribution issue.
- [ ] Convert the current technical backlog into labelled, independently checkable issues.

Exit gate: a new contributor can understand the architecture, run software-only checks, and see exactly what is not yet proven.

## Phase 1 — Interfaces and safe bench hardware (months 1–3)

- [ ] Obtain written factory-encoder electrical, framing, rate, latency, checksum, direction, zero, harness, and connector data.
- [ ] Freeze the motor/encoder connector only after that written evidence.
- [ ] Review and manufacture the isolated DRV8316R validation board.
- [ ] Validate DRVOFF, nFAULT-to-TIM1_BKIN, MOE shutdown, current-sense scaling, and fault latching with qualified loads.
- [ ] Publish oscilloscope captures, test configuration, raw measurements, and failure notes.

Exit gate: the gate-driver and safety chain work on real bench hardware without energizing an unqualified motor loop.

## Phase 2 — Motor core and calibration (months 3–6)

- [ ] Confirm phase order, electrical zero, encoder direction, and update timing.
- [ ] Commission current sensing and limited-voltage open-loop checks.
- [ ] Close current control, then safe torque control, using staged current/voltage/temperature limits.
- [ ] Measure loop timing, current noise, torque linearity, cogging, encoder nonlinearity, regeneration, and thermal behavior.
- [ ] Add encoder correction and cogging compensation only when measurements justify them.

Exit gate: repeatable low-energy torque control with traceable calibration and fault evidence.

## Phase 3 — Haptic runtime and developer API (months 5–9)

- [ ] Freeze Haptic Profile schema v1 after implementation feedback.
- [ ] Validate detent, spring, damper, and endstop primitives.
- [ ] Add texture, asymmetric detent, and composite effects after the core primitives are measurable.
- [ ] Publish C++, TypeScript, and Python profile examples as each implementation becomes maintained.
- [ ] Add Profile editing, preview, clamp reports, sharing, and safe SmartKnob/X-Knob haptic-configuration conversion.
- [ ] Demonstrate profile changes without application-specific FOC code.

Exit gate: applications can create repeatable, bounded haptic behavior through a stable profile interface.

## Phase 4 — Product integration and external reproduction (months 8–12)

- [ ] Integrate ESP32-S3 UI, HID, transport, and configuration while preserving motor-core deadlines.
- [ ] Deliver global volume/media and the local timer as the first end-to-end user applications.
- [ ] Deliver Home Assistant `light` over MQTT with discovery/availability before adding `climate`, `cover`, or `scene`.
- [ ] Deliver an Open-Meteo weather flow with user-set coordinates, visible source, update age, cache, and offline state; add Home Assistant weather only as a separate adapter.
- [ ] Add creator/host adapters only behind maintained interfaces: video timeline, MIDI, and Windows per-app volume first.
- [ ] Freeze product PCB only after bench evidence closes its risk items.
- [ ] Validate mechanical support, tolerance stack, user loads, acoustic behavior, and thermal paths.
- [ ] Publish assembly, bring-up, calibration, troubleshooting, and recovery guides.
- [ ] Support external builds and resolve reproducibility gaps.
- [ ] Release v1.0 only after the definition above is met.

## Planned release sequence

| Release | Purpose | Minimum evidence |
| --- | --- | --- |
| v0.1 | Public engineering baseline | CI green; safety and status boundaries published |
| v0.2 | Bench driver and fault path | Real measured switching/current/fault evidence |
| v0.3 | Motor core | Calibrated, bounded torque-control evidence |
| v0.5 | Haptic runtime | Measured core effects and profile schema candidate |
| v0.7 | Integrated developer preview | Motor/application separation demonstrated on hardware |
| v1.0 | Reproducible reference design | Manufacturing files, guides, measurements, and external reproduction |

Progress will be reported through issues, pull requests, discussions, changelog entries, and signed GitHub releases. Features may move; exit evidence does not.
