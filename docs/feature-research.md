# GL30 Haptic Control feature and community-demand research

Research date: 2026-08-30

This document answers three questions: what people actually want from a force-feedback knob, how the GL30 platform can address those needs, and what it cannot honestly promise. It is a product-scope input, not a completion report, and upstream demos are not evidence of GL30 hardware behavior.

## Findings

1. **Physical feel must lead the product.** Adjustable detents, smooth free rotation, soft limits, recentering, damping, momentum, and landmark attraction determine whether the device is useful.
2. **Volume, timers, smart home, and weather are the clearest first applications.** They demonstrate software state changing physical feel without requiring a control-theory explanation.
3. **Creator workflows expose the unique value of force feedback.** Video timelines, shuttle speed, MIDI/DAW, color controls, and CAD parameters all combine precision with meaningful limits, centers, and landmarks.
4. **Saveable, shareable, and portable feel is a platform feature.** Community requests repeatedly ask for file-based configuration and more flexible detents; that is the purpose of Haptic Profile.
5. **Connectivity claims need explicit boundaries.** Home Assistant/MQTT, standard HID, and network weather have viable paths. “Directly connects to every smart-home device,” “HID controls every application,” and “fully offline weather” do not.
6. **GL30's intended differentiation is a dedicated real-time motor core, fail-off safety, physical units, telemetry, and reproducible tuning.** Those choices can turn a demo into measurable and reusable engineering, but physical feel is not yet proven.

## Scope and evidence

The review uses public source code, READMEs, releases, issues, and official platform documentation. Issues are qualitative public demand samples, not a statistical user survey. An upstream implementation does not mean the same feature is implemented here.

| Source | Pinned revision or page | Evidence used |
| --- | --- | --- |
| [SmartKnob](https://github.com/scottbez1/smartknob/tree/4eb988399c3fda6ffd3006772856093dfe9adb86) | `4eb9883` | Configurable detents/endstops, press haptics, video timeline demo, MQTT/Home Assistant direction, public issues |
| [X-Knob](https://github.com/SmallPond/X-Knob/tree/05be44fc62b27c4fa941aabd2a7e9b2553f91fb9) | `05be44f` | Seven haptic modes, LVGL, Surface Dial, press vibration, Home Assistant/MQTT, web configuration, OTA, power features |
| [SuperDial](https://github.com/CharlieYu4994/superdial/tree/1973d9436a7220f16eec6f76aac6d7029588c03f) | `1973d94` | BLDC haptics, BLE Dial, display/button interaction, PC peripheral direction; no portable structured haptic configuration |
| [Home Assistant MQTT](https://www.home-assistant.io/integrations/mqtt/) | Current official documentation | Broker, Discovery, entities, and availability |
| [Home Assistant Weather](https://www.home-assistant.io/integrations/weather) | Current official documentation | Weather entities, current state, forecasts, and provider dependency |
| [Open-Meteo Forecast API](https://open-meteo.com/en/docs) | Current official documentation | Coordinate-based JSON forecasts suitable for the first direct demo; license, limits, and commercial terms still apply |
| [ESP32-S3 overview](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/product-overview.html) | Current Espressif documentation | 2.4 GHz Wi-Fi and Bluetooth LE; no integrated 802.15.4 |
| [ESP-Matter guide](https://docs.espressif.com/projects/esp-matter/en/latest/esp32s3/esp-matter-en-master-esp32s3.pdf) | Current Espressif documentation | Matter over Wi-Fi on ESP32-S3; Thread requires 802.15.4 silicon or a border router |
| [USB HID](https://www.usb.org/hid) | Current USB-IF documentation | Standard usages and the limit that defining a usage does not ensure every host supports it |
| [Windows volume controls](https://learn.microsoft.com/en-us/windows/win32/coreaudio/volume-controls) | Microsoft documentation | Endpoint and per-application audio sessions use different host interfaces |

## What prior projects demonstrate

### SmartKnob

SmartKnob combines a BLDC motor and magnetic encoder so software can change detents and endstops. Its public timeline example makes the product value concrete: clip boundaries can be felt, while playback speed springs back to pause and snaps to 1×, 2×, and 4×. The key is not replacing an encoder; it is turning software semantics into touch.

The project also states that it is not a mature plug-and-play product and lists Wi-Fi/MQTT, Home Assistant, and real applications as future work. GL30 therefore must not turn the existence of protocol fields into a claim that an application is finished.

### X-Knob

X-Knob is a force-feedback knob. Its releases describe seven combinations of bounded, detent, and rebound modes plus LVGL, Surface Dial, press vibration, MQTT/Home Assistant, web configuration, OTA, and power management. It demonstrates the viability of “feel + display + smart home” and exposes the separate engineering work in discovery, configuration, UI, memory, power, and invalid-input handling.

### SuperDial

SuperDial uses BLDC/SimpleFOC for restoring force and BLE Dial for PC input, with UI and PC telemetry in its direction. It confirms interest in PC-peripheral use cases. The public implementation is primarily hard-coded behavior rather than a versioned haptic configuration, so it informs scenarios but is not a current configuration-adapter target.

## Recurring public requests

This is a qualitative grouping, not a fabricated popularity ranking.

| Theme | Public evidence | Product implication for GL30 |
| --- | --- | --- |
| Inertia and momentum | SmartKnob [#38](https://github.com/scottbez1/smartknob/issues/38), [#43](https://github.com/scottbez1/smartknob/issues/43) | `inertia` needs a controllable, interruptible state machine, not only a field |
| Flexible and persistent feel | SmartKnob [#52](https://github.com/scottbez1/smartknob/issues/52), [#53](https://github.com/scottbez1/smartknob/issues/53) | Profile editing, preview, import, clamp reports, and sharing belong on the main path |
| Isolated haptic examples | SmartKnob [#92](https://github.com/scottbez1/smartknob/issues/92) | Each primitive needs a small example, physical units, safe limits, and reproducible test |
| MIDI, DJ, and creator tools | SmartKnob [#80](https://github.com/scottbez1/smartknob/issues/80), [#132](https://github.com/scottbez1/smartknob/issues/132), [#161](https://github.com/scottbez1/smartknob/issues/161) | MIDI CC, high-resolution parameters, center attraction, and beat detents belong in P2 |
| Video shuttle/jog | SmartKnob [demo](https://github.com/scottbez1/smartknob#demo-video-editor-timeline-control), [#173](https://github.com/scottbez1/smartknob/issues/173) | Frame stepping, landmarks, timeline inertia, and spring shuttle are strong showcase interactions |
| Home Assistant, MQTT, ESPHome | SmartKnob [#17](https://github.com/scottbez1/smartknob/issues/17), [#137](https://github.com/scottbez1/smartknob/issues/137), [#144](https://github.com/scottbez1/smartknob/issues/144); X-Knob [#3](https://github.com/SmallPond/X-Knob/issues/3), [#4](https://github.com/SmallPond/X-Knob/issues/4) | Support a maintained gateway/entity model instead of private links for every light brand |
| More physical inputs and feedback | SmartKnob [#57](https://github.com/scottbez1/smartknob/issues/57), [#75](https://github.com/scottbez1/smartknob/issues/75), [#174](https://github.com/scottbez1/smartknob/issues/174) | Touch, buttons, display, and lighting may complement rotation but must not disturb real-time control |
| Portable/battery operation | SmartKnob [#100](https://github.com/scottbez1/smartknob/issues/100); X-Knob power management | It is now a main-product target: the current design uses a 3S wireless architecture and an 800 mAh mechanical reference; X-Knob runtime claims do not transfer, so capacity, safety, and runtime need measurement |
| Crunchy or rough feel from scheduling | SmartKnob [#159](https://github.com/scottbez1/smartknob/issues/159) | UI/network work must not disturb motor timing; GL30's split STM32/ESP32 architecture targets this risk but still needs measurement |
| SpaceMouse-like control | SmartKnob [#133](https://github.com/scottbez1/smartknob/issues/133) | CAD zoom/parameter control is viable; one rotary axis is not a six-DOF device |

## GL30 haptic capability matrix

Status vocabulary:

- 🧩 **Low-level code, hardware pending:** executable STM32/protocol code and host tests exist, but there is no physical-feel evidence;
- 🛠 **Feasible and planned, unfinished:** the architecture and interfaces are viable, but users cannot use it today;
- 🔌 **External dependency:** a host, network, service, gateway, or third-party application is required;
- ❌ **Unsupported or not promised:** present hardware/evidence is insufficient or the feature is outside current scope.

| Haptic capability | Current basis | Missing closure | Status |
| --- | --- | --- | --- |
| Free rotation / low damping | Haptic flags, damping, and friction can be zero while the safety path remains active | Motor cogging, bearing drag, encoder/current ripple, zero-speed noise, and any justified compensation | 🧩 |
| Periodic virtual detents | The 2 kHz haptic tick produces a bounded sinusoidal detent torque | GL30 sensing/current calibration, cogging/acoustics measurement, and default-profile evaluation | 🧩 |
| Spring / target position | Position-error torque primitive exists | Touch/release policy, speed, overshoot, pinch, and fail-safe validation | 🧩 |
| Soft endstops | Restoring torque outside minimum/maximum positions exists | Soft-zone curve, peak torque, recovery, and mechanical-stop interaction | 🧩 |
| Damping | Velocity-dependent torque exists | Velocity noise, delay, stable range, and subjective feel | 🧩 |
| Friction | Smoothed low-speed friction exists | Static-friction feel, zero-speed vibration, and cogging compensation | 🧩 |
| Inertia | Acceleration-dependent term exists | Stateful momentum, takeover, stop rules, and list/timeline mapping | 🛠 |
| Magnetic landmarks | Profiles contain `magneticPositions` | Profile-to-command mapping and multi-landmark STM32 runtime | 🛠 |
| Asymmetric detents | Profiles contain `asymmetry` | Waveform, direction semantics, runtime code, and tuning | 🛠 |
| Texture and short tactile cues | Profile/protocol fields and texture IDs exist | Generator, sampling policy, acoustic limits, and physical evaluation | 🛠 |
| Dynamic composition | Mode flags can combine low-level primitives | Full Profile deployment, transition smoothing, and state synchronization | 🛠 |
| Active pointer / autonomous rotation | Position, velocity, and self-drive limits are reserved | Touch detection, regeneration, runaway detection, and physical safety tests | ❌ Not a first-wave user feature |

## Application capability matrix

| Feature | Minimum viable experience | Path | Current conclusion |
| --- | --- | --- | --- |
| Global volume/media | Rotate volume, touch/button mute, play/pause, next/previous | ESP32 USB/BLE HID + volume Profile | 🛠 Feasible; no HID application exists |
| Windows per-app volume | Select an audio session, show its app, synchronize volume | PC Companion + Windows Core Audio + device protocol | 🔌 Feasible; HID alone is insufficient |
| Timer/Pomodoro | Set, start/pause, remaining time, completion haptic | ESP32 local state machine + UI + persistence | 🛠 Feasible offline; no app exists |
| Home Assistant light | Select entity, set power/brightness/color temperature, receive state | MQTT Discovery/state/command topics + Profile mapping | 🔌 Feasible; HA and broker required |
| HA climate/cover/scene | 0.5 °C detents, 0–100% limits, scene list | Domain-specific MQTT entity mapping | 🔌 Feasible; semantics must be defined per domain |
| Weather/air quality | Current values, hourly forecast, threshold landmarks, stale-data state | Use Open-Meteo HTTPS JSON in P1; a HA weather adapter may follow | 🔌 Feasible; location/network/provider required |
| Video timeline | Frames, landmarks, inertial navigation, spring-return shuttle | Host plugin/Companion + dynamic Profiles | 🔌 Feasible; application integration required |
| MIDI/DAW | CC, fine control, center/0 dB attraction, beat detents | USB MIDI or host bridge | 🛠 Feasible; descriptors/mapping do not exist |
| CAD/creative tools | Zoom, parameters, brush, history, and mode changes | HID shortcuts for basics; plugins for semantic feedback | 🔌 Partly generic; full experience needs plugins |
| PC telemetry | FPS, frame time, CPU/GPU, and device telemetry | PresentMon/LHM/RTSS adapters + UI | 🛠 Schema fields exist; collectors do not |
| Profile import/sharing | Import, preview, clamp report, deploy, export | PC Companion canonical Profile | 🛠 V6 data validates; UI/deployment/external converters do not exist |

## Claims deliberately excluded

| Excluded claim | Why | Evidence needed to reconsider |
| --- | --- | --- |
| Premium, smooth, or silent feel already exists | No real closed loop or measurement | Cogging, linearity, timing, acoustic, and user-evaluation evidence |
| Direct connection to all Zigbee/Thread devices | ESP32-S3 has no 802.15.4 radio | Add suitable hardware or require HA/border router explicitly |
| Fully offline, permanently free weather | No local weather sensor array; service terms can change | Self-hosted data or physical sensors plus a new license/cost review |
| Zero-configuration compatibility with arbitrary software | Application semantics and permissions differ; HID covers only generic input | Maintained per-application adapters and version testing |
| Single-axis six-DOF SpaceMouse replacement | Insufficient mechanical/sensing degrees of freedom | A new multi-axis hardware project |
| Reuse of another motor's control parameters | Motor, sensing, power, and thermal systems differ | Import intent only and recalibrate on GL30 |
| Months of battery life | The current 3S/800 mAh mechanical reference serves compact packaging and has no average-power or runtime evidence | Measure modal average/peak power, temperature, cycling, and safety before enlarging either battery or enclosure |
| Safety-critical or unattended active motion | No touch detection or full-device fault evidence | Hazard analysis, redundant limits, physical tests, and potentially certification |

## Implementation priorities

### P0 — make good feel measurable

1. Obtain and verify the GL30 factory-encoder specification; preserve the `PENDING_VENDOR` gate until closed.
2. Close low-energy FOC and measure current, encoder error, cogging, delay, noise, temperature, and regeneration.
3. Validate free rotation, detents, damping, soft limits, and recentering in order, publishing raw results and the user-evaluation method.
4. Complete `Profile → ESP32/PC → HapticCommand → STM32` with transition smoothing and clamp reports.
5. Validate the 3S pack, BQ25798 SYS path, power-off back-drive, and independent regenerative brake, publishing bus-voltage, bidirectional-energy, and temperature data.

### P1 — four applications that communicate the value immediately

1. **Volume:** standard HID global volume, mute, and soft limits as the first end-to-end app.
2. **Timer:** local offline state machine demonstrating speed-dependent coarse/fine adjustment and completion cues.
3. **Home Assistant:** implement `light` first, then `climate`, `cover`, and `scene`, using MQTT Discovery and availability.
4. **Weather:** use Open-Meteo as the fixed first demo provider with user-supplied coordinates, always showing source, age, and offline state. A Home Assistant weather adapter is separate, not an implicit fallback.

### P2 — creator and configuration ecosystem

1. Video timeline/shuttle reference integration;
2. USB MIDI with assignable CC;
3. Windows per-app volume;
4. Haptic Profile editing, preview, sharing, and SmartKnob/X-Knob converters;
5. Community CAD/DAW/color adapters behind a stable plugin interface.

### Not on the main path yet

- voice assistant, direct Zigbee/Thread, and six-DOF input;
- unattended active rotation, safety alerting, or safety-rated machine control;
- private direct protocols for every smart-home brand.

These are not necessarily rejected forever. They are excluded from the main path until core feel, real-time behavior, and safety evidence are closed.
