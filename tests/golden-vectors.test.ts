import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { decodeFrame, crc32c } from "@gl30/protocol";

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

  const [motorFrameHex, slowFrameHex, hapticFrameHex] = data.vectors.map((entry) => entry.frameHex);
  assert.equal(data.vectors.length, 3);

  const motorFrame = decodeFrame(hexToBytes(motorFrameHex));
  const slowFrame = decodeFrame(hexToBytes(slowFrameHex));
  const hapticFrame = decodeFrame(hexToBytes(hapticFrameHex));

  assert.equal(motorFrame.payloadLen, 68);
  assert.equal(motorFrame.payload.length, 68);
  assert.equal(motorFrame.type, 0x01);
  assert.equal(motorFrame.version, 1);

  assert.equal(slowFrame.payloadLen, 64);
  assert.equal(slowFrame.payload.length, 64);
  assert.equal(slowFrame.type, 0x02);
  assert.equal(slowFrame.version, 1);

  assert.equal(hapticFrame.payloadLen, 64);
  assert.equal(hapticFrame.payload.length, 64);
  assert.equal(hapticFrame.type, 0x10);

  assert.equal(motorFrame.timestampUs > 0x1_0000_0000n, true);
  assert.equal(slowFrame.timestampUs > 0x1_0000_0000n, true);
  assert.equal(hapticFrame.timestampUs > 0x1_0000_0000n, true);

  assert.equal(motorFrame.payloadLen + 24, 92);
  assert.equal(slowFrame.payloadLen + 24, 88);
  assert.equal(hapticFrame.payloadLen + 24, 88);

  const motorEntry = data.vectors.find((entry) => entry.type === "0x01") as VectorEntry;
  const slowEntry = data.vectors.find((entry) => entry.type === "0x02") as VectorEntry;
  const hapticEntry = data.vectors.find((entry) => entry.type === "0x10") as VectorEntry;
  assert.equal(motorEntry.payloadBytes, motorFrame.payload.length);
  assert.equal(slowEntry.payloadBytes, slowFrame.payload.length);
  assert.equal(hapticEntry.payloadBytes, hapticFrame.payload.length);
  assert.equal(motorEntry.payloadBytes, 68);
  assert.equal(slowEntry.payloadBytes, 64);
  assert.equal(hapticEntry.payloadBytes, 64);

  assert.equal(`0x${motorFrame.crc32c.toString(16).padStart(8, "0")}`, motorEntry.crc32c);
  assert.equal(`0x${slowFrame.crc32c.toString(16).padStart(8, "0")}`, slowEntry.crc32c);
  assert.equal(`0x${hapticFrame.crc32c.toString(16).padStart(8, "0")}`, hapticEntry.crc32c);
  assert.equal(motorEntry.frameBytes, 92);
  assert.equal(slowEntry.frameBytes, 88);
  assert.equal(hapticEntry.frameBytes, 88);
});
