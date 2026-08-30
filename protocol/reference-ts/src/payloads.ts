export const MOTOR_STATE_FAST_PAYLOAD_LEN = 68;
export const MOTOR_STATE_SLOW_PAYLOAD_LEN = 64;
export const HAPTIC_COMMAND_PAYLOAD_LEN = 64;

export interface MotorStateFast {
  angleRad: number;
  velocityRadPerSec: number;
  accelerationRadPerSec2: number;
  iqRefA: number;
  iqMeasA: number;
  idMeasA: number;
  torqueCmdNm: number;
  torqueEstNm: number;
  busVoltageV: number;
  busCurrentA: number;
  logicalPosition: number;
  subPosition: number;
  motorState: number;
  faultBits: number;
  warningBits: number;
  isrCycles: number;
  encoderStatus: number;
  droppedCmds: number;
  reserved: number;
}

export interface HapticCommand {
  profileId: number;
  commandNonce: number;
  targetPositionRad: number;
  targetVelocityRadS: number;
  detentWidthRad: number;
  detentStrengthNm: number;
  endstopMinRad: number;
  endstopMaxRad: number;
  endstopStrengthNm: number;
  dampingNmPerRadS: number;
  inertiaKgM2: number;
  frictionNm: number;
  userTorqueLimitNm: number;
  activeSpeedLimitRadS: number;
  modeFlags: number;
  textureId: number;
}

export interface MotorStateSlow {
  busVoltageV: number;
  busCurrentA: number;
  busPowerW: number;
  energyJ: number;
  chargeC: number;
  inaDieTemperatureC: number;
  motorTemperatureC: number;
  ambientLux: number;
  uptimeMs: number;
  inaDiag: number;
  sensorStatus: number;
  inaI2cErrors: number;
  vemlI2cErrors: number;
  telemetryDrops: number;
  encoderCrcErrors: number;
  focDeadlineMisses: number;
}

export function encodeMotorStateFast(payload: MotorStateFast): Uint8Array {
  const out = new Uint8Array(MOTOR_STATE_FAST_PAYLOAD_LEN);
  const dv = new DataView(out.buffer);
  let o = 0;
  const writeF32 = (v: number) => {
    dv.setFloat32(o, v, true);
    o += 4;
  };
  const writeI32 = (v: number) => {
    dv.setInt32(o, v | 0, true);
    o += 4;
  };
  const writeU32 = (v: number) => {
    dv.setUint32(o, v >>> 0, true);
    o += 4;
  };
  const writeU16 = (v: number) => {
    dv.setUint16(o, v & 0xffff, true);
    o += 2;
  };

  writeF32(payload.angleRad);
  writeF32(payload.velocityRadPerSec);
  writeF32(payload.accelerationRadPerSec2);
  writeF32(payload.iqRefA);
  writeF32(payload.iqMeasA);
  writeF32(payload.idMeasA);
  writeF32(payload.torqueCmdNm);
  writeF32(payload.torqueEstNm);
  writeF32(payload.busVoltageV);
  writeF32(payload.busCurrentA);
  writeI32(payload.logicalPosition);
  writeF32(payload.subPosition);
  writeU32(payload.motorState);
  writeU32(payload.faultBits);
  writeU32(payload.warningBits);
  writeU16(payload.isrCycles);
  writeU16(payload.encoderStatus);
  writeU16(payload.droppedCmds);
  writeU16(payload.reserved);
  return out;
}

export function decodeMotorStateFast(payload: Uint8Array): MotorStateFast {
  if (payload.length !== MOTOR_STATE_FAST_PAYLOAD_LEN) {
    throw new Error(`motor_state_fast invalid length: ${payload.length}`);
  }
  const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  let o = 0;
  const readF32 = () => {
    const v = dv.getFloat32(o, true);
    o += 4;
    return v;
  };
  const readI32 = () => {
    const v = dv.getInt32(o, true);
    o += 4;
    return v;
  };
  const readU32 = () => {
    const v = dv.getUint32(o, true);
    o += 4;
    return v;
  };
  const readU16 = () => {
    const v = dv.getUint16(o, true);
    o += 2;
    return v;
  };

  return {
    angleRad: readF32(),
    velocityRadPerSec: readF32(),
    accelerationRadPerSec2: readF32(),
    iqRefA: readF32(),
    iqMeasA: readF32(),
    idMeasA: readF32(),
    torqueCmdNm: readF32(),
    torqueEstNm: readF32(),
    busVoltageV: readF32(),
    busCurrentA: readF32(),
    logicalPosition: readI32(),
    subPosition: readF32(),
    motorState: readU32(),
    faultBits: readU32(),
    warningBits: readU32(),
    isrCycles: readU16(),
    encoderStatus: readU16(),
    droppedCmds: readU16(),
    reserved: readU16()
  };
}

export function encodeMotorStateSlow(payload: MotorStateSlow): Uint8Array {
  const out = new Uint8Array(MOTOR_STATE_SLOW_PAYLOAD_LEN);
  const dv = new DataView(out.buffer);
  let o = 0;
  const writeF32 = (v: number) => {
    dv.setFloat32(o, v, true);
    o += 4;
  };
  const writeU32 = (v: number) => {
    dv.setUint32(o, v >>> 0, true);
    o += 4;
  };

  writeF32(payload.busVoltageV);
  writeF32(payload.busCurrentA);
  writeF32(payload.busPowerW);
  writeF32(payload.energyJ);
  writeF32(payload.chargeC);
  writeF32(payload.inaDieTemperatureC);
  writeF32(payload.motorTemperatureC);
  writeF32(payload.ambientLux);
  writeU32(payload.uptimeMs);
  writeU32(payload.inaDiag);
  writeU32(payload.sensorStatus);
  writeU32(payload.inaI2cErrors);
  writeU32(payload.vemlI2cErrors);
  writeU32(payload.telemetryDrops);
  writeU32(payload.encoderCrcErrors);
  writeU32(payload.focDeadlineMisses);
  return out;
}

export function decodeMotorStateSlow(payload: Uint8Array): MotorStateSlow {
  if (payload.length !== MOTOR_STATE_SLOW_PAYLOAD_LEN) {
    throw new Error(`motor_state_slow invalid length: ${payload.length}`);
  }
  const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  let o = 0;
  const readF32 = () => {
    const v = dv.getFloat32(o, true);
    o += 4;
    return v;
  };
  const readU32 = () => {
    const v = dv.getUint32(o, true);
    o += 4;
    return v;
  };

  return {
    busVoltageV: readF32(),
    busCurrentA: readF32(),
    busPowerW: readF32(),
    energyJ: readF32(),
    chargeC: readF32(),
    inaDieTemperatureC: readF32(),
    motorTemperatureC: readF32(),
    ambientLux: readF32(),
    uptimeMs: readU32(),
    inaDiag: readU32(),
    sensorStatus: readU32(),
    inaI2cErrors: readU32(),
    vemlI2cErrors: readU32(),
    telemetryDrops: readU32(),
    encoderCrcErrors: readU32(),
    focDeadlineMisses: readU32()
  };
}

export function encodeHapticCommand(payload: HapticCommand): Uint8Array {
  const out = new Uint8Array(HAPTIC_COMMAND_PAYLOAD_LEN);
  const dv = new DataView(out.buffer);
  let o = 0;
  const writeF32 = (v: number) => {
    dv.setFloat32(o, v, true);
    o += 4;
  };
  const writeU32 = (v: number) => {
    dv.setUint32(o, v >>> 0, true);
    o += 4;
  };

  writeU32(payload.profileId);
  writeU32(payload.commandNonce);
  writeF32(payload.targetPositionRad);
  writeF32(payload.targetVelocityRadS);
  writeF32(payload.detentWidthRad);
  writeF32(payload.detentStrengthNm);
  writeF32(payload.endstopMinRad);
  writeF32(payload.endstopMaxRad);
  writeF32(payload.endstopStrengthNm);
  writeF32(payload.dampingNmPerRadS);
  writeF32(payload.inertiaKgM2);
  writeF32(payload.frictionNm);
  writeF32(payload.userTorqueLimitNm);
  writeF32(payload.activeSpeedLimitRadS);
  writeU32(payload.modeFlags);
  writeU32(payload.textureId);
  return out;
}

export function decodeHapticCommand(payload: Uint8Array): HapticCommand {
  if (payload.length !== HAPTIC_COMMAND_PAYLOAD_LEN) {
    throw new Error(`haptic_command invalid length: ${payload.length}`);
  }
  const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  let o = 0;
  const readF32 = () => {
    const v = dv.getFloat32(o, true);
    o += 4;
    return v;
  };
  const readU32 = () => {
    const v = dv.getUint32(o, true);
    o += 4;
    return v;
  };

  return {
    profileId: readU32(),
    commandNonce: readU32(),
    targetPositionRad: readF32(),
    targetVelocityRadS: readF32(),
    detentWidthRad: readF32(),
    detentStrengthNm: readF32(),
    endstopMinRad: readF32(),
    endstopMaxRad: readF32(),
    endstopStrengthNm: readF32(),
    dampingNmPerRadS: readF32(),
    inertiaKgM2: readF32(),
    frictionNm: readF32(),
    userTorqueLimitNm: readF32(),
    activeSpeedLimitRadS: readF32(),
    modeFlags: readU32(),
    textureId: readU32()
  };
}
