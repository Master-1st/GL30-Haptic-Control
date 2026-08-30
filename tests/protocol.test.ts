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
  FRAME_SYNC,
  FRAME_VERSION,
  MOTOR_STATE_FAST_PAYLOAD_LEN,
  HAPTIC_COMMAND_PAYLOAD_LEN
} from "@gl30/protocol";

const textEncoder = new TextEncoder();

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
    textureId: 4
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
  assert.equal(HAPTIC_COMMAND_PAYLOAD_LEN, 64);
  assert.equal(MOTOR_STATE_SLOW_PAYLOAD_LEN, 64);

  const hapticPayload = encodeHapticCommand(makeHapticCommand());
  const hapticFrame = encodeFrame(FRAME_VERSION, 0x10, hapticPayload, {
    timestampUs: 0x1_0000_0000n + 7n
  });
  assert.equal(hapticFrame.length, 24 + HAPTIC_COMMAND_PAYLOAD_LEN);
  assert.equal(hapticFrame.length, 88);

  const decoded = decodeMotorStateFast(encodeMotorStateFast(makeMotorState({ logicalPosition: -2147483648 })));
  assert.equal(decoded.logicalPosition, -2147483648);

  const slowPayload = encodeMotorStateSlow(makeMotorStateSlow());
  assert.equal(slowPayload.length, 64);
  const decodedSlow = decodeMotorStateSlow(slowPayload);
  const regenSlowPayload = encodeMotorStateSlow(decodedSlow);
  assert.deepStrictEqual(regenSlowPayload, slowPayload);
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
