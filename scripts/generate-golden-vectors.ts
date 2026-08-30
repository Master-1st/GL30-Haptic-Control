import { mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import {
  FRAME_VERSION,
  crc32c,
  decodeFrame,
  encodeFrame,
  encodeHapticCommand,
  encodeMotorStateFast,
  encodeMotorStateSlow
} from "@gl30/protocol";

function hex(data: Uint8Array): string {
  return Buffer.from(data).toString("hex");
}

function vector(name: string, type: number, payload: Uint8Array, sequence: number, timestampUs: bigint, flags: number) {
  const frame = encodeFrame(FRAME_VERSION, type, payload, { sequence, timestampUs, flags });
  const decoded = decodeFrame(frame);
  return {
    name,
    type: `0x${type.toString(16).padStart(2, "0")}`,
    sequence,
    timestampUs: timestampUs.toString(),
    flags: `0x${flags.toString(16).padStart(4, "0")}`,
    payloadBytes: payload.length,
    frameBytes: frame.length,
    crc32c: `0x${decoded.crc32c.toString(16).padStart(8, "0")}`,
    payloadHex: hex(payload),
    frameHex: hex(frame)
  };
}

const motorPayload = encodeMotorStateFast({
  angleRad: 1.25,
  velocityRadPerSec: -0.5,
  accelerationRadPerSec2: 0.01,
  iqRefA: 0.2,
  iqMeasA: 0.21,
  idMeasA: 0.22,
  torqueCmdNm: 0.03,
  torqueEstNm: 0.029,
  busVoltageV: 7.2,
  busCurrentA: 0.12,
  logicalPosition: -123456789,
  subPosition: 0.33,
  motorState: 5,
  faultBits: 1,
  warningBits: 2,
  isrCycles: 999,
  encoderStatus: 11,
  droppedCmds: 3,
  reserved: 0
});

const hapticPayload = encodeHapticCommand({
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
});

const slowPayload = encodeMotorStateSlow({
  busVoltageV: 12.1,
  busCurrentA: -0.35,
  busPowerW: 4.235,
  energyJ: 123.5,
  chargeC: -2.25,
  inaDieTemperatureC: 31.5,
  motorTemperatureC: 42.25,
  ambientLux: 318.75,
  uptimeMs: 654321,
  inaDiag: 0x0001,
  sensorStatus: 0x0000000f,
  inaI2cErrors: 2,
  vemlI2cErrors: 3,
  telemetryDrops: 4,
  encoderCrcErrors: 5,
  focDeadlineMisses: 6
});

const output = {
  format: "GL30_AMOLED_V7_PROTOCOL_V1_GOLDEN_VECTORS",
  protocolVersion: 1,
  evidence: "SIM_ONLY_REFERENCE",
  frameLayout: {
    byteOrder: "little-endian",
    payloadOffset: 20,
    crcBytes: 4,
    crcCoverage: "version through payload; sync excluded"
  },
  crc32cKnownVector: {
    inputUtf8: "123456789",
    expected: `0x${crc32c(new TextEncoder().encode("123456789")).toString(16).padStart(8, "0")}`
  },
  vectors: [
    vector("motor-state-fast", 0x01, motorPayload, 0x11223344, 0x1_0000_0001n, 0x55aa),
    vector("motor-state-slow", 0x02, slowPayload, 0x11223345, 0x1_0000_0002n, 0x0000),
    vector("haptic-command", 0x10, hapticPayload, 0x89abcdef, 0x1_0000_0003n, 0x0003)
  ]
};

const outputDir = resolve(process.cwd(), "protocol", "generated");
mkdirSync(outputDir, { recursive: true });
const outputPath = resolve(outputDir, "v1.json");
writeFileSync(outputPath, `${JSON.stringify(output, null, 2)}\n`, "utf8");
console.log(`Generated deterministic protocol vectors: ${outputPath}`);
