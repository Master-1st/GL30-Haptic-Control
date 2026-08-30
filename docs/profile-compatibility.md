# Force-Feedback Knob Configuration Compatibility

> Status: the V6 format audit is complete; the SmartKnob importer is not implemented. This document only claims evidenced configuration compatibility.

## Scope

A compatibility target must meet all of these conditions:

1. the source device provides motorized force feedback;
2. upstream exposes a public, stable, serializable haptic configuration format;
3. the format contains detents, endstops, magnetic positions, strengths, or other mappable haptic parameters;
4. its source revision and license can be pinned and audited.

Non-haptic application I/O protocols, automation interfaces, ordinary rotary-encoder settings, and UI-only configuration are outside this plan. Projects with only hard-coded behavior or C/C++ constants and no stable external haptic format do not enter the compatibility matrix.

## Current conclusions

- **GL30 AMOLED V6 `profileVersion: 1` is native-format compatible.** The V6 and current Schemas differ only in `$id` and title. Both V6 examples are byte-identical to the current examples, and both old Profile objects pass the current `parseProfile()` validator.
- **SmartKnob `SmartKnobConfig` is mappable, but its adapter is not implemented.** Protobuf transport, normalized strength units, runtime state, and hardware calibration require PC-side conversion, clamping, and warnings.

## Compatibility levels

| Level | Meaning |
| --- | --- |
| `NATIVE_FORMAT` | Legacy haptic fields enter the current validator unchanged; this does not imply device deployment |
| `ADAPTER_PLANNED` | A field mapping exists, but import, unit conversion, and verification are unfinished |
| `REJECTED_HARDWARE_DATA` | Motor-, sensor-, or board-specific data must never migrate |

## Current matrix

| Source | Pinned reference | Current level | Usable now | Missing work |
| --- | --- | --- | --- | --- |
| GL30 AMOLED V6 Profile v1 | Local V6 final design package | `NATIVE_FORMAT` | Individual Profile objects pass the current Schema and semantic validator; old examples pass 2/2 | File-import UI, batch conversion report, and ESP32/STM32 deployment pipeline |
| [SmartKnob `SmartKnobConfig`](https://github.com/scottbez1/smartknob/blob/4eb988399c3fda6ffd3006772856093dfe9adb86/proto/smartknob.proto) | `4eb9883`, Apache-2.0 | `ADAPTER_PLANNED` | Field-level mapping design is complete | Protobuf parser, strength-calibration policy, fixtures, conversion tests, and import UI |

## Adapter boundary

```text
Legacy force-feedback JSON / Protobuf configuration
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

The STM32 real-time core accepts one frozen binary command model. It does not parse legacy JSON/Protobuf or accumulate historical protocol stacks. PC-side adapters own compatibility, and every converted result passes the same validator and safety clamps.

## V6 compatibility available today

The current [Profile Schema](../pc-companion/profiles/src/profile.schema.json) retains the V6 Profile v1 structure, including application matching, haptic primitives, display settings, input mapping, data sources, and user safety caps.

Limits remain explicit:

- passing validation is not the same as having an import button;
- native format compatibility is not unconditional acceptance; inconsistent or out-of-range legacy input remains rejected;
- importing texture or asymmetry fields does not create physical behavior before the STM32 runtime implements them;
- the current device binary protocol is an internal transport compiled from the Canonical Profile, not a promise to support legacy device protocols.

## SmartKnob mapping design

SmartKnob uses a Protobuf `SmartKnobConfig`. The planned adapter converts only portable haptic semantics:

| SmartKnob field | Canonical Profile target | Rule |
| --- | --- | --- |
| `position_width_radians` | `haptic.detent.widthDeg` | Deterministic radians-to-degrees conversion |
| `detent_strength_unit` | `haptic.detent.strengthmNm` | **Not an identity conversion**; requires an explicit strength mapping or calibration policy |
| `endstop_strength_unit` | `haptic.endstops.strengthmNm` | Requires mapping and GL30 safety clamping |
| `min_position` / `max_position` | `haptic.endstops.minPosition/maxPosition` | Preserve logical positions after direction and origin checks |
| `snap_point` | `haptic.detent.snapRatio` | Convert with range validation |
| `detent_positions[]` | `haptic.detent.magneticPositions[]` | Convert within the current Profile count limit |
| `snap_point_bias` | `haptic.detent.asymmetry` | Similar but non-identical semantics; warn and preview |
| `text` | Profile name/description | Text only; not real-time control |
| `position` / `sub_position_unit` | Runtime state or target | Do not import as a static Profile |
| `position_nonce` | Runtime command nonce | Not part of a persistent Profile |

SmartKnob `MotorCalibration`, `StrainCalibration`, electrical zero, direction, pole pairs, and legacy PID/voltage/current values are `REJECTED_HARDWARE_DATA` and must not be applied to GL30.

## Acceptance gates for every adapter

1. The source is a force-feedback device with a public, stable haptic configuration format.
2. Pin the upstream repository, commit, format, and license.
3. Add a lawful minimal fixture without copying unrelated assets.
4. Define every unit, coordinate direction, default, and information loss.
5. Reject electrical zero, PID, current limits, and other hardware-specific values.
6. Produce deterministic Canonical Profiles.
7. Pass the current Schema, semantic checks, and safety clamps.
8. Report unmapped fields and conversion warnings to the user.
9. Golden-test successful, degraded, and rejected conversions.
10. Claim physical usability only after real-device deployment and haptic checks.

## Reserved force-feedback configuration targets

These rows are intentionally blank. Only a format within this document's scope may fill in its pinned version, license, mapping, fixture, and evidence; blank rows are not support commitments.

| Slot | Force-feedback source/format | Pinned version | License | Level | Mapping document | Fixture | Evidence |
| ---: | --- | --- | --- | --- | --- | --- | --- |
| 1 |  |  |  |  |  |  |  |
| 2 |  |  |  |  |  |  |  |
| 3 |  |  |  |  |  |  |  |
| 4 |  |  |  |  |  |  |  |

<!-- Keep this marker: FUTURE_FORCE_FEEDBACK_CONFIG_ADAPTER_SLOT -->
