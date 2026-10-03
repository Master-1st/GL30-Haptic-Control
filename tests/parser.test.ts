import { test } from "node:test";
import assert from "node:assert/strict";
import {
  crc32c,
  V6StreamParser,
  encodeFrame,
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
    textureId: 0,
    leaseGeneration: 0x1_0000_0001n
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
  const legacyLengthPayload = makeHapticPayload().subarray(0, HAPTIC_COMMAND_PAYLOAD_LEN - 8);
  const oldCommandFrame = encodeFrame(FRAME_VERSION, 0x22, legacyLengthPayload, {
    timestampUs: 1000n
  });
  oldCommandFrame[3] = 0x10;
  const view = new DataView(oldCommandFrame.buffer, oldCommandFrame.byteOffset, oldCommandFrame.byteLength);
  view.setUint32(oldCommandFrame.length - 4,
    crc32c(oldCommandFrame.subarray(2, oldCommandFrame.length - 4)), true);
  const out = parser.feed(oldCommandFrame);
  assert.equal(out.frames.length, 0);
  assert.equal(out.errors.length, 1);
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
