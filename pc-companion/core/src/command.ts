import type { HapticCommand } from "@gl30/protocol";
import type { HapticCommandApplied } from "./types.js";

export const ABSOLUTE_LIMITS = {
  targetPositionRad: undefined,
  targetVelocityRadS: undefined,
  detentWidthRad: Math.PI,
  detentStrengthNm: 0.030,
  endstopMinRad: undefined,
  endstopMaxRad: undefined,
  endstopStrengthNm: 0.045,
  dampingNmPerRadS: 0.004,
  inertiaKgM2: 0.0005,
  frictionNm: 0.01,
  userTorqueLimitNm: 0.060,
  activeSpeedLimitRadS: 2 * Math.PI,
  selfDriveTorqueNm: 0.015,
  modeFlags: 0xffffffff,
  textureId: 0xffffffff
} as const;

type FieldResult = {
  value: number;
  valid: boolean;
  clamped: boolean;
};

function clampWithWarn(v: number, field: string, warnings: string[], min?: number, max?: number): FieldResult {
  if (Number.isNaN(v) || !Number.isFinite(v)) {
    warnings.push(`字段 ${field} 非法数值`);
    return { value: 0, valid: false, clamped: false };
  }
  if (min !== undefined && v < min) {
    warnings.push(`字段 ${field} 低于编译上限，硬限幅为 ${min}`);
    return { value: min, valid: true, clamped: true };
  }
  if (max !== undefined && v > max) {
    warnings.push(`字段 ${field} 超过编译上限，硬限幅为 ${max}`);
    return { value: max, valid: true, clamped: true };
  }
  return { value: v, valid: true, clamped: false };
}

export function clampHapticCommand(input: HapticCommand): HapticCommandApplied {
  const warnings: string[] = [];
  const results = {
    profileId: clampWithWarn(input.profileId >>> 0, "profileId", warnings, 0, ABSOLUTE_LIMITS.modeFlags),
    targetPositionRad: clampWithWarn(input.targetPositionRad, "targetPositionRad", warnings),
    targetVelocityRadS: clampWithWarn(input.targetVelocityRadS, "targetVelocityRadS", warnings),
    detentWidthRad: clampWithWarn(input.detentWidthRad, "detentWidthRad", warnings, 0.0, ABSOLUTE_LIMITS.detentWidthRad),
    detentStrengthNm: clampWithWarn(input.detentStrengthNm, "detentStrengthNm", warnings, 0.0, ABSOLUTE_LIMITS.detentStrengthNm),
    endstopMinRad: clampWithWarn(input.endstopMinRad, "endstopMinRad", warnings, ABSOLUTE_LIMITS.endstopMinRad, ABSOLUTE_LIMITS.endstopMaxRad),
    endstopMaxRad: clampWithWarn(input.endstopMaxRad, "endstopMaxRad", warnings, ABSOLUTE_LIMITS.endstopMinRad, ABSOLUTE_LIMITS.endstopMaxRad),
    endstopStrengthNm: clampWithWarn(input.endstopStrengthNm, "endstopStrengthNm", warnings, 0.0, ABSOLUTE_LIMITS.endstopStrengthNm),
    dampingNmPerRadS: clampWithWarn(input.dampingNmPerRadS, "dampingNmPerRadS", warnings, 0.0, ABSOLUTE_LIMITS.dampingNmPerRadS),
    inertiaKgM2: clampWithWarn(input.inertiaKgM2, "inertiaKgM2", warnings, 0.0, ABSOLUTE_LIMITS.inertiaKgM2),
    frictionNm: clampWithWarn(input.frictionNm, "frictionNm", warnings, 0.0, ABSOLUTE_LIMITS.frictionNm),
    userTorqueLimitNm: clampWithWarn(input.userTorqueLimitNm, "userTorqueLimitNm", warnings, 0.0, ABSOLUTE_LIMITS.userTorqueLimitNm),
    activeSpeedLimitRadS: clampWithWarn(input.activeSpeedLimitRadS, "activeSpeedLimitRadS", warnings, 0.0, ABSOLUTE_LIMITS.activeSpeedLimitRadS),
    modeFlags: clampWithWarn(input.modeFlags >>> 0, "modeFlags", warnings, 0, ABSOLUTE_LIMITS.modeFlags),
    textureId: clampWithWarn(input.textureId >>> 0, "textureId", warnings, 0, ABSOLUTE_LIMITS.textureId)
  };
  const valid = Object.values(results).every((item) => item.valid);
  const out: HapticCommand = {
    profileId: results.profileId.value,
    commandNonce: input.commandNonce >>> 0,
    targetPositionRad: results.targetPositionRad.value,
    targetVelocityRadS: results.targetVelocityRadS.value,
    detentWidthRad: results.detentWidthRad.value,
    detentStrengthNm: results.detentStrengthNm.value,
    endstopMinRad: results.endstopMinRad.value,
    endstopMaxRad: results.endstopMaxRad.value,
    endstopStrengthNm: results.endstopStrengthNm.value,
    dampingNmPerRadS: results.dampingNmPerRadS.value,
    inertiaKgM2: results.inertiaKgM2.value,
    frictionNm: results.frictionNm.value,
    userTorqueLimitNm: results.userTorqueLimitNm.value,
    activeSpeedLimitRadS: results.activeSpeedLimitRadS.value,
    modeFlags: results.modeFlags.value,
    textureId: results.textureId.value,
    leaseGeneration: input.leaseGeneration
  };
  return { original: input, clamped: out, warnings, valid };
}
