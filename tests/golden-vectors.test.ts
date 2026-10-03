import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import {
  crc32c,
  decodeControlLease,
  decodeFrame,
  decodeHapticCommand,
  decodeHapticState
} from "@gl30/protocol";

function hexToBytes(hex: string): Uint8Array {
  return new Uint8Array(Buffer.from(hex, "hex"));
}

type VectorEntry = {
  name: string;
  type: string;
  payloadBytes: number;
  frameBytes: number;
  timestampUs: string;
  crc32c: string;
  frameHex: string;
};

type GoldenVectors = {
  crc32cKnownVector: {
    inputUtf8: string;
    expected: string;
  };
  vectors: VectorEntry[];
};

test("load and verify golden vectors determinism constraints", () => {
  const content = readFileSync(join(process.cwd(), "protocol", "generated", "v1.json"), "utf8");
  const data = JSON.parse(content) as GoldenVectors;

  assert.equal(data.crc32cKnownVector.inputUtf8, "123456789");
  assert.equal(data.crc32cKnownVector.expected.toLowerCase(), "0xe3069283");
  assert.equal(data.crc32cKnownVector.expected.toLowerCase(), `0x${crc32c(Buffer.from(data.crc32cKnownVector.inputUtf8)).toString(16).padStart(8, "0")}`);

  assert.equal(data.vectors.length, 5);
  const frameFor = (name: string) => {
    const entry = data.vectors.find((item) => item.name === name);
    assert.ok(entry, `missing ${name} vector`);
    const frame = decodeFrame(hexToBytes(entry.frameHex));
    assert.equal(frame.type, Number(entry.type));
    assert.equal(frame.payloadLen, entry.payloadBytes);
    assert.equal(frame.payloadLen + 24, entry.frameBytes);
    assert.equal(`0x${frame.crc32c.toString(16).padStart(8, "0")}`, entry.crc32c);
    assert.ok(frame.timestampUs > 0x1_0000_0000n);
    return { entry, frame };
  };
  const { entry: motorEntry, frame: motorFrame } = frameFor("motor-state-fast");
  const { entry: slowEntry, frame: slowFrame } = frameFor("motor-state-slow");
  const { entry: commandEntry, frame: commandFrame } = frameFor("haptic-command");
  const { entry: stateEntry, frame: stateFrame } = frameFor("haptic-state");
  const { entry: leaseEntry, frame: leaseFrame } = frameFor("control-lease-acquire");

  assert.equal(motorFrame.payloadLen, 68);
  assert.equal(motorFrame.payload.length, 68);
  assert.equal(motorFrame.type, 0x01);
  assert.equal(motorFrame.version, 1);

  assert.equal(slowFrame.payloadLen, 64);
  assert.equal(slowFrame.payload.length, 64);
  assert.equal(slowFrame.type, 0x02);
  assert.equal(slowFrame.version, 1);

  assert.equal(commandFrame.payloadLen, 72);
  assert.equal(commandFrame.payload.length, 72);
  assert.equal(commandFrame.type, 0x10);
  assert.equal(stateFrame.payloadLen, 44);
  assert.equal(stateFrame.payload.length, 44);
  assert.equal(stateFrame.type, 0x06);
  assert.equal(leaseFrame.payloadLen, 24);
  assert.equal(leaseFrame.payload.length, 24);
  assert.equal(leaseFrame.type, 0x15);

  assert.equal(motorFrame.payloadLen + 24, 92);
  assert.equal(slowFrame.payloadLen + 24, 88);
  assert.equal(motorEntry.payloadBytes, motorFrame.payload.length);
  assert.equal(slowEntry.payloadBytes, slowFrame.payload.length);
  assert.equal(motorEntry.payloadBytes, 68);
  assert.equal(slowEntry.payloadBytes, 64);
  assert.equal(commandEntry.payloadBytes, 72);
  assert.equal(stateEntry.payloadBytes, 44);
  assert.equal(leaseEntry.payloadBytes, 24);
  assert.equal(commandEntry.frameBytes, 96);
  assert.equal(stateEntry.frameBytes, 68);
  assert.equal(leaseEntry.frameBytes, 48);
  assert.equal(decodeHapticCommand(commandFrame.payload).leaseGeneration, 0x1_0000_0001n);
  assert.equal(decodeHapticState(stateFrame.payload).leaseGeneration, 0x2_0000_0003n);
  assert.deepEqual(decodeControlLease(leaseFrame.payload), {
    action: 1,
    zeroNonce: 0x12345678,
    currentGeneration: 0x1_0000_0001n,
    nextGeneration: 0x2_0000_0002n
  });
});
