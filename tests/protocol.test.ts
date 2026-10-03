import { test } from "node:test";
import assert from "node:assert/strict";
import {
  crc32c,
  encodeFrame,
  decodeFrame,
  encodeMotorStateFast,
  decodeMotorStateFast,
  encodeMotorStateSlow,
  decodeMotorStateSlow,
  MOTOR_STATE_SLOW_PAYLOAD_LEN,
  encodeHapticCommand,
  decodeHapticCommand,
  encodeHapticState,
  decodeHapticState,
  encodeControlLease,
  decodeControlLease,
  CONTROL_LEASE_ACQUIRE,
  CONTROL_LEASE_PAYLOAD_LEN,
  HAPTIC_STATE_PAYLOAD_LEN,
  FRAME_SYNC,
  FRAME_VERSION,
  MOTOR_STATE_FAST_PAYLOAD_LEN,
  HAPTIC_COMMAND_PAYLOAD_LEN
} from "@gl30/protocol";

const textEncoder = new TextEncoder();

test("applied haptic state carries a 64-bit lease generation", () => {
  const state = { profileId: 0x4d454e55, commandNonce: 0xffffffff, modeFlags: 1,
    logicalPosition: -13, subPosition: -0.375, detentWidthRad: 0.5,
    motorState: 7, faultBits: 0, status: 3, leaseGeneration: 0x1_0000_0001n };
  const bytes = encodeHapticState(state);
  assert.equal(bytes.length, HAPTIC_STATE_PAYLOAD_LEN);
  assert.equal(Buffer.from(bytes).toString("hex"),
    "554e454dffffffff01000000f3ffffff0000c0be0000003f0700000000000000030000000100000001000000");
  assert.deepEqual(decodeHapticState(bytes), state);
  const frame = encodeFrame(FRAME_VERSION, 0x06, bytes, { sequence: 9, timestampUs: 123456n });
  assert.equal(frame.length, 68);
  assert.deepEqual(decodeHapticState(decodeFrame(frame).payload), state);
  assert.throws(() => decodeHapticState(bytes.subarray(0, 36)), /invalid length/);
  assert.throws(() => encodeFrame(FRAME_VERSION, 0x06, bytes.subarray(0, 36),
    { timestampUs: 1n }), /payload length mismatch/);
});

function makeMotorState(override?: Partial<{ logicalPosition: number }>) {
  return {
    angleRad: 1.25,
    velocityRadPerSec: -0.5,
    accelerationRadPerSec2: 0.01,
    iqRefA: 0.2,
    iqMeasA: 0.21,
    idMeasA: 0.22,
    torqueCmdNm: 0.3,
    torqueEstNm: 0.31,
    busVoltageV: 7.2,
    busCurrentA: 0.12,
    logicalPosition: override?.logicalPosition ?? 321,
    subPosition: 0.33,
    motorState: 5,
    faultBits: 0x1,
    warningBits: 0x2,
    isrCycles: 99,
    encoderStatus: 11,
    droppedCmds: 0,
    reserved: 0
  };
}

function makeHapticCommand() {
  return {
    profileId: 7,
    commandNonce: 12,
    targetPositionRad: 0.04,
    targetVelocityRadS: 0.03,
    detentWidthRad: 0.5,
    detentStrengthNm: 0.012,
    endstopMinRad: -0.4,
    endstopMaxRad: 0.6,
    endstopStrengthNm: 0.005,
    dampingNmPerRadS: 0.0003,
    inertiaKgM2: 0.0002,
    frictionNm: 0.0002,
    userTorqueLimitNm: 0.04,
    activeSpeedLimitRadS: 1.5,
    modeFlags: 0x80000000,
    textureId: 4,
    leaseGeneration: 0x1_0000_0002n
  };
}

function makeMotorStateSlow(override?: Partial<{
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
}>) {
  return {
    busVoltageV: 10.5,
    busCurrentA: 0.8,
    busPowerW: 12.3,
    energyJ: 9.9,
    chargeC: 3.4,
    inaDieTemperatureC: 42.5,
    motorTemperatureC: 40.2,
    ambientLux: 120.5,
    uptimeMs: 987654,
    inaDiag: 1,
    sensorStatus: 2,
    inaI2cErrors: 0,
    vemlI2cErrors: 0,
    telemetryDrops: 0,
    encoderCrcErrors: 0,
    focDeadlineMisses: 0,
    ...override
  };
}

test("CRC32C known vector", () => {
  const input = textEncoder.encode("123456789");
  assert.equal(crc32c(input), 0xe3069283);
});

test("protocol v1 frame layout and offsets are fixed and deterministic", () => {
  const payload = encodeMotorStateFast(makeMotorState());
  const timestampUs = (1n << 32n) + 123456789n;
  const frame = encodeFrame(FRAME_VERSION, 0x01, payload, {
    sequence: 0x11223344,
    timestampUs,
    flags: 0x55aa
  });

  assert.equal(frame.length, 24 + MOTOR_STATE_FAST_PAYLOAD_LEN);
  assert.equal(frame.length, 92);

  assert.equal(frame[0], 0x5a);
  assert.equal(frame[1], 0xa5);
  assert.equal(frame[2], FRAME_VERSION);
  assert.equal(frame[3], 0x01);
  const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength);
  assert.equal(view.getUint16(4, true), MOTOR_STATE_FAST_PAYLOAD_LEN);
  assert.equal(view.getUint16(6, true), 0x55aa);
  assert.equal(view.getUint32(8, true), 0x11223344);
  assert.equal(view.getBigUint64(12, true), timestampUs);

  for (let i = 0; i < payload.length; i++) {
    assert.equal(frame[20 + i], payload[i]);
  }

  const crcOffset = frame.length - 4;
  const computedCrc = crc32c(frame.slice(2, crcOffset));
  assert.equal(view.getUint32(crcOffset, true), computedCrc);

  const decoded = decodeFrame(frame);
  assert.equal(decoded.version, FRAME_VERSION);
  assert.equal(decoded.type, 0x01);
  assert.equal(decoded.payloadLen, MOTOR_STATE_FAST_PAYLOAD_LEN);
  assert.equal(decoded.flags, 0x55aa);
  assert.equal(decoded.sequence, 0x11223344);
  assert.equal(decoded.timestampUs, timestampUs);
});

test("payload sizes and logicalPosition int32", () => {
  assert.equal(MOTOR_STATE_FAST_PAYLOAD_LEN, 68);
  assert.equal(HAPTIC_COMMAND_PAYLOAD_LEN, 72);
  assert.equal(MOTOR_STATE_SLOW_PAYLOAD_LEN, 64);

  const hapticPayload = encodeHapticCommand(makeHapticCommand());
  assert.equal(hapticPayload.length, 72);
  const decodedCommand = decodeHapticCommand(hapticPayload);
  assert.equal(decodedCommand.leaseGeneration, 0x1_0000_0002n);
  assert.deepEqual(encodeHapticCommand(decodedCommand), hapticPayload);
  const hapticFrame = encodeFrame(FRAME_VERSION, 0x10, hapticPayload, {
    timestampUs: 0x1_0000_0000n + 7n
  });
  assert.equal(hapticFrame.length, 24 + HAPTIC_COMMAND_PAYLOAD_LEN);
  assert.equal(hapticFrame.length, 96);
  assert.throws(() => decodeHapticCommand(hapticPayload.subarray(0, 64)), /invalid length/);

  const decoded = decodeMotorStateFast(encodeMotorStateFast(makeMotorState({ logicalPosition: -2147483648 })));
  assert.equal(decoded.logicalPosition, -2147483648);

  const slowPayload = encodeMotorStateSlow(makeMotorStateSlow());
  assert.equal(slowPayload.length, 64);
  const decodedSlow = decodeMotorStateSlow(slowPayload);
  const regenSlowPayload = encodeMotorStateSlow(decodedSlow);
  assert.deepStrictEqual(regenSlowPayload, slowPayload);
});

test("control lease carries 64-bit generations in a strict 24-byte payload", () => {
  const request = {
    action: CONTROL_LEASE_ACQUIRE,
    zeroNonce: 0x12345678,
    currentGeneration: 0x1_0000_0001n,
    nextGeneration: 0x2_0000_0002n
  };
  const payload = encodeControlLease(request);
  assert.equal(payload.length, CONTROL_LEASE_PAYLOAD_LEN);
  assert.equal(Buffer.from(payload).toString("hex"),
    "010000007856341201000000010000000200000002000000");
  assert.deepEqual(decodeControlLease(payload), request);
  assert.throws(() => decodeControlLease(payload.subarray(0, 16)), /invalid length/);
  assert.throws(() => encodeFrame(FRAME_VERSION, 0x15, payload.subarray(0, 16),
    { timestampUs: 5n }), /payload length mismatch/);
});

test("control lease rejects malformed action-specific shapes and generations", () => {
  const base = {
    action: CONTROL_LEASE_ACQUIRE,
    zeroNonce: 1,
    currentGeneration: 0n,
    nextGeneration: 2n
  };
  assert.throws(() => encodeControlLease({ ...base, action: 9 as 0 | 1 | 2 }),
    /action is unknown/);
  assert.throws(() => encodeControlLease({ ...base, zeroNonce: 0 }), /ACQUIRE fields/);
  assert.throws(() => encodeControlLease({ ...base, currentGeneration: 2n }), /ACQUIRE fields/);
  assert.throws(() => encodeControlLease({ ...base, nextGeneration: 0n }), /ACQUIRE fields/);
  assert.throws(() => encodeControlLease({
    action: 0, zeroNonce: 1, currentGeneration: 1n, nextGeneration: 2n
  }), /RELEASE fields/);
  assert.throws(() => encodeControlLease({
    action: 2, zeroNonce: 1, currentGeneration: 0n, nextGeneration: 0n
  }), /QUERY fields/);
  assert.throws(() => encodeControlLease({ ...base, currentGeneration: -1n }), /unsigned 64-bit/);
});

test("haptic state rejects unknown status bits and simultaneous released/waiting", () => {
  const state = { profileId: 0, commandNonce: 1, modeFlags: 0, logicalPosition: 0,
    subPosition: 0, detentWidthRad: 0, motorState: 6, faultBits: 0,
    status: 0, leaseGeneration: 0x1_0000_0001n };
  assert.throws(() => encodeHapticState({ ...state, status: 0x10 }), /status/);
  assert.throws(() => encodeHapticState({ ...state, status: 0x0c }), /status/);
  for (const invalidStatus of [0x10, 0x0c]) {
    const malformed = encodeHapticState(state);
    new DataView(malformed.buffer).setUint32(32, invalidStatus, true);
    assert.throws(() => decodeHapticState(malformed), /status/);
  }
});

test("bad fixed payload length is rejected by codec", () => {
  const motorPayload = encodeMotorStateFast(makeMotorState());
  assert.throws(
    () =>
      encodeFrame(FRAME_VERSION, 0x01, motorPayload.slice(0, MOTOR_STATE_FAST_PAYLOAD_LEN - 1), {
        timestampUs: 0n + 1n
      }),
    /payload length mismatch/
  );

  const slowPayload = encodeMotorStateSlow(makeMotorStateSlow());
  assert.throws(
    () =>
      encodeFrame(FRAME_VERSION, 0x02, slowPayload.slice(0, MOTOR_STATE_SLOW_PAYLOAD_LEN - 1), {
        timestampUs: 0n + 3n
      }),
    /payload length mismatch/
  );

  const slowFrame = encodeFrame(FRAME_VERSION, 0x02, slowPayload, {
    timestampUs: 0x200000000n
  });
  const shortFrame = new Uint8Array(slowFrame.slice(0, 24 + MOTOR_STATE_SLOW_PAYLOAD_LEN - 1));
  const shortView = new DataView(shortFrame.buffer, shortFrame.byteOffset, shortFrame.byteLength);
  shortView.setUint16(4, MOTOR_STATE_SLOW_PAYLOAD_LEN - 1, true);
  const shortBody = shortFrame.slice(2, shortFrame.length - 4);
  const shortCrc = crc32c(shortBody);
  shortView.setUint32(shortFrame.length - 4, shortCrc, true);
  assert.throws(() => decodeFrame(shortFrame), (error) => {
    assert.equal((error as Error & { code?: string }).code, "bad_payload_len");
    return true;
  });

  const frame = encodeFrame(FRAME_VERSION, 0x10, encodeHapticCommand(makeHapticCommand()), { timestampUs: 5n });
  const broken = new Uint8Array(frame.slice(0, 24 + (HAPTIC_COMMAND_PAYLOAD_LEN - 1)));
  const brokenView = new DataView(broken.buffer, broken.byteOffset, broken.byteLength);
  brokenView.setUint16(4, HAPTIC_COMMAND_PAYLOAD_LEN - 1, true);

  assert.throws(() => decodeFrame(broken), (error) => {
    assert.equal((error as Error & { code?: string }).code, "bad_payload_len");
    return true;
  });
});

test("same frame bytes decode deterministically", () => {
  const frame = encodeFrame(FRAME_VERSION, 0x10, encodeHapticCommand(makeHapticCommand()), {
    timestampUs: 321n
  });
  const first = decodeFrame(new Uint8Array(frame));
  const second = decodeFrame(new Uint8Array(frame));
  assert.deepStrictEqual(first, second);
});
