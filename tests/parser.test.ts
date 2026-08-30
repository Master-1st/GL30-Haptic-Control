import { test } from "node:test";
import assert from "node:assert/strict";
import {
  V6StreamParser,
  encodeFrame,
  encodeMotorStateFast,
  encodeHapticCommand,
  HAPTIC_COMMAND_PAYLOAD_LEN,
  FRAME_VERSION
} from "@gl30/protocol";

function concat(chunks: readonly Uint8Array[]): Uint8Array {
  const total = chunks.reduce((sum, c) => sum + c.length, 0);
  const out = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) {
    out.set(chunk, offset);
    offset += chunk.length;
  }
  return out;
}

function makeMotorPayload() {
  return encodeMotorStateFast({
    angleRad: 0.1,
    velocityRadPerSec: 0.2,
    accelerationRadPerSec2: 0.3,
    iqRefA: 0.4,
    iqMeasA: 0.5,
    idMeasA: 0.6,
    torqueCmdNm: 0.7,
    torqueEstNm: 0.8,
    busVoltageV: 7.2,
    busCurrentA: 0.05,
    logicalPosition: 12,
    subPosition: 0.9,
    motorState: 1,
    faultBits: 0,
    warningBits: 0,
    isrCycles: 1,
    encoderStatus: 2,
    droppedCmds: 3,
    reserved: 0
  });
}

function makeHapticPayload() {
  return encodeHapticCommand({
    profileId: 1,
    commandNonce: 2,
    targetPositionRad: 0.1,
    targetVelocityRadS: 0.01,
    detentWidthRad: 0.5,
    detentStrengthNm: 0.01,
    endstopMinRad: -0.2,
    endstopMaxRad: 0.2,
    endstopStrengthNm: 0.01,
    dampingNmPerRadS: 0.0002,
    inertiaKgM2: 0.0002,
    frictionNm: 0.0001,
    userTorqueLimitNm: 0.01,
    activeSpeedLimitRadS: 0.9,
    modeFlags: 0,
    textureId: 0
  });
}

test("stream parser reports bad CRC", () => {
  const parser = new V6StreamParser();
  const frame = encodeFrame(FRAME_VERSION, 0x10, makeHapticPayload(), { timestampUs: 1000n });
  const badCrc = new Uint8Array(frame);
  badCrc[badCrc.length - 1] ^= 0x01;
  const out = parser.feed(badCrc);
  assert.equal(out.frames.length, 0);
  assert.equal(out.errors.length, 1);
  assert.equal(out.errors[0].code, "bad_crc");
});

test("stream parser reports known-type fixed payload mismatch", () => {
  const parser = new V6StreamParser();
  const frame = encodeFrame(FRAME_VERSION, 0x01, makeMotorPayload(), { timestampUs: 1000n });
  const bad = new Uint8Array(frame);
  const view = new DataView(bad.buffer, bad.byteOffset, bad.byteLength);
  view.setUint16(4, HAPTIC_COMMAND_PAYLOAD_LEN - 1, true);
  const out = parser.feed(bad);
  assert.equal(out.frames.length, 0);
  assert.equal(out.errors[0].code, "bad_payload_len");
});

test("stream parser survives random chunks and cross-chunk 0x5a 0xa5 resync", () => {
  const parser = new V6StreamParser();
  const good = encodeFrame(FRAME_VERSION, 0x10, makeHapticPayload(), {
    sequence: 0x01020304,
    timestampUs: 8000n,
    flags: 0xaaaa
  });
  const chunks: Uint8Array[] = [
    new Uint8Array([0x10, 0x20, 0x5a]),
    concat([new Uint8Array([0xa5]), good.slice(2, 28)]),
    good.slice(28),
    new Uint8Array([0x11, 0x22, 0x33])
  ];

  const frames = [];
  const errors = [];
  for (const chunk of chunks) {
    const out = parser.feed(chunk);
    frames.push(...out.frames);
    errors.push(...out.errors);
  }

  assert.equal(frames.length, 1);
  assert.equal(frames[0].type, 0x10);
  assert.equal(errors.length, 0);
});
