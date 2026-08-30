# Profile Compatibility and Migration Plan

> Status: interface audit complete; source adapters are not implemented. This document only claims evidenced compatibility.

## Summary

- **GL30 AMOLED V6 `profileVersion: 1` Profiles are format-compatible.** The V6 and current Schemas differ only in `$id` and title. Both V6 examples are byte-identical to the current examples and both old Profile objects pass the current `parseProfile()` validator.
- **The implemented V6 real-time frame and `HAPTIC_COMMAND` subset is specification- and layout-compatible.** Current V1 retains the `0xA55A` header, CRC32C, packet registry, `0x01 MOTOR_STATE_FAST`, and the 64-byte `0x10 HAPTIC_COMMAND` layout. V6 registered the type and purpose of `0x02` without freezing its payload. This is not evidence of interoperability between two physical devices.
- **SmartKnob, X-Knob, and SuperDial are not directly importable or wire-compatible today.** Their haptic semantics are useful, but files, transports, units, hardware gains, and safety limits require a PC-side adapter.

## Compatibility levels

| Level | Meaning |
| --- | --- |
| `NATIVE_FORMAT` | Source fields enter the current validator unchanged; this does not imply device deployment |
| `SPEC_SUBSET` | The implemented wire-format subset matches; unimplemented messages remain safely rejected |
| `ADAPTER_PLANNED` | A deterministic mapping is possible but requires conversion, clamping, and loss reporting |
| `SEMANTIC_REFERENCE` | Interaction behavior can be studied or recreated, but there is no portable source configuration |
| `REJECTED_HARDWARE_DATA` | Motor-, sensor-, or board-specific data must never migrate |

## Current matrix

| Source | Pinned reference | Current level | Usable now | Missing work |
| --- | --- | --- | --- | --- |
| GL30 AMOLED V6 Profile v1 | Local V6 final design package | `NATIVE_FORMAT` | Individual Profile objects pass the current Schema and semantic validator unchanged | File-import UI, batch report, and ESP32/STM32 deployment pipeline |
| GL30 AMOLED V6 real-time protocol | Local V6 protocol manual | `SPEC_SUBSET` | Current implementation supports `0x01`, `0x02`, and `0x10`; framing, `0x01`, and `0x10` retain V6 layouts, while the current `0x02` payload is a project extension | Remaining packet types, physical interoperability, and capability negotiation |
| [SmartKnob](https://github.com/scottbez1/smartknob/tree/4eb988399c3fda6ffd3006772856093dfe9adb86) | `4eb9883`, Apache-2.0 | `ADAPTER_PLANNED` | A `SmartKnobConfig` Protobuf-to-Canonical-Profile mapping can be specified | Importer, unit/strength calibration, golden fixtures, and mapping tests |
| [X-Knob](https://github.com/SmallPond/X-Knob/tree/05be44fc62b27c4fa941aabd2a7e9b2553f91fb9) | `05be44f`, MIT | `ADAPTER_PLANNED` | Static `XKnobConfig` modes can be manually or mechanically translated | Haptic modes are C++ constants rather than portable configuration files |
| [SuperDial](https://github.com/CharlieYu4994/superdial/tree/1973d9436a7220f16eec6f76aac6d7029588c03f) | `1973d94`, MIT | `SEMANTIC_REFERENCE` | BLE Dial/HID behavior, concentric mechanics, and basic recentering can inform the design | No portable Haptic Profile exists; behavior must be recreated |
| Surface Dial / HID | Protocol and host target to be pinned | `ADAPTER_PLANNED` | Can become an application I/O target | HID descriptor, host adapter, and platform tests |
| Home Assistant / MQTT | Data model to be pinned | `ADAPTER_PLANNED` | Can map Profile actions and data sources | Topics/entity discovery, permissions, security, and disconnect behavior |

## Adapter boundary

```text
Legacy JSON / Protobuf / C constants / community preset
                         ↓
              PC Companion source adapter
                         ↓
              Canonical Haptic Profile v1
                         ↓
              Schema + semantics + limits
                         ↓
             Conversion report and preview
                         ↓
              Binary Haptic Command / Config
                         ↓
                    ESP32 → STM32
```

The STM32 real-time core accepts one frozen binary command model. It does not parse JSON or Protobuf and will not accumulate historical protocol stacks. Compatibility belongs in source adapters, and every converted result passes the same validator and safety clamps.

## V6 compatibility retained today

The current [Profile Schema](../pc-companion/profiles/src/profile.schema.json) retains the V6 Profile v1 structure: application matching; haptic primitives; display pages and themes; input mapping; actions; data sources; and user safety caps. The current [protocol v1](../protocol/schema/protocol-v1.md) retains the V6 frame header, timestamp, CRC32C, packet registry, `MOTOR_STATE_FAST`, and `HAPTIC_COMMAND` field order. V6 did not define the `MOTOR_STATE_SLOW` payload, so the current slow payload is not claimed as byte-compatible.

Limits remain explicit:

- passing validation is not the same as having an import button;
- format compatibility is not unconditional acceptance; semantically inconsistent or out-of-range input remains rejected by current safety rules;
- matching bytes do not prove physical-device interoperability;
- V6 packet types not implemented today remain safely rejected;
- importing texture or asymmetry fields does not create physical behavior before the STM32 runtime implements them.

## SmartKnob mapping proposal

SmartKnob uses a Protobuf `SmartKnobConfig`, not this project's JSON Profile.

| SmartKnob field | Canonical Profile target | Rule |
| --- | --- | --- |
| `position_width_radians` | `haptic.detent.widthDeg` | Deterministic radians-to-degrees conversion |
| `detent_strength_unit` | `haptic.detent.strengthmNm` | **Not an identity conversion**; requires source calibration or an explicit mapping curve |
| `endstop_strength_unit` | `haptic.endstops.strengthmNm` | Requires calibration and GL30 safety clamping |
| `min_position` / `max_position` | `haptic.endstops.minPosition/maxPosition` | Preserve logical positions after direction and origin checks |
| `snap_point` | `haptic.detent.snapRatio` | Convert with range validation |
| `detent_positions[]` | `haptic.detent.magneticPositions[]` | Direct logical-position mapping, capped by the current Profile limit |
| `snap_point_bias` | `haptic.detent.asymmetry` | Similar but not identical semantics; warn and preview |
| `text` | Profile name/description | Non-real-time text mapping |
| `led_hue` | `display.accent` | Optional mapping after defining color conversion |
| `position` / `sub_position_unit` | Runtime state or target | Do not persist as a static Profile |
| `position_nonce` | Runtime command nonce | Not part of a persistent Profile |

SmartKnob `MotorCalibration`, `StrainCalibration`, electrical zero, direction, pole pairs, and legacy PID/voltage/current values are `REJECTED_HARDWARE_DATA`. They are valid only for the source hardware and must not be applied to GL30.

## X-Knob and SuperDial

X-Knob's `XKnobConfig` shares SmartKnob-like position width, detent/endstop strength, and snap-point semantics. Most conversion rules can therefore be reused. Its modes are compiled into a C++ array in `motor.cpp`; `SystemSave.json` stores system settings, not a full haptic Profile. An initial adapter should consume a deliberately extracted intermediate JSON rather than execute or parse arbitrary C++.

SuperDial keeps its recentering, page behavior, and BLE Dial logic largely in `main.ino`, with no stable external haptic configuration. It is a behavioral reference, not a file-compatibility target.

## Acceptance gates for every adapter

1. Pin the upstream repository, commit, file format, and license.
2. Add a lawful minimal fixture without copying unrelated assets.
3. Define every unit, coordinate direction, default, and information loss.
4. Reject electrical zero, PID, current limits, and other hardware-specific values.
5. Produce deterministic Canonical Profiles.
6. Pass the current Schema, semantic checks, and safety clamps.
7. Report unmapped fields and conversion warnings to the user.
8. Golden-test successful, degraded, and rejected conversions.
9. Upgrade “format conversion” to “physical interoperability” only after a real-device test.

## Reserved future targets

These rows are intentionally blank. A future target must fill in its pinned version, license, mapping, fixture, and evidence before support is claimed.

| Slot | Source/format | Pinned version | License | Level | Mapping document | Fixture | Evidence |
| ---: | --- | --- | --- | --- | --- | --- | --- |
| 1 |  |  |  |  |  |  |  |
| 2 |  |  |  |  |  |  |  |
| 3 |  |  |  |  |  |  |  |
| 4 |  |  |  |  |  |  |  |

<!-- Keep this marker when adding adapters: FUTURE_COMPATIBILITY_ADAPTER_SLOT -->
