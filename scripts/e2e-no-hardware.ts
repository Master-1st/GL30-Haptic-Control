import {
  encodeFrame,
  encodeHapticCommand,
  HapticCommand,
  V6StreamParser,
  FRAME_VERSION,
  MAX_PAYLOAD
} from "@gl30/protocol";
import { parseProfile, loadExampleProfiles } from "@gl30/profile";
import { TimeSyncEstimator, VirtualDevice } from "@gl30/core";
import { mkdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";

interface E2ESummary {
  mode: "SIM_ONLY";
  profileValidation: {
    total: number;
    valid: number;
    invalid: number;
    details: Array<{ index: number; valid: boolean; issues: string[] }>;
  };
  transport: {
    totalFramesParsed: number;
    totalErrors: number;
    badFrameErrors: Array<{ code: string; offset: number; message: string }>;
    unknownTypeCount: number;
    unknownVersionCount: number;
    totalFramesInjected: number;
  };
  command: {
    totalDecodedFrames: number;
    applied: number;
    dropped: number;
    hardClampWarnings: number;
  };
  runtime: {
    sampledTelemetry: number;
    timeoutState: string;
    allowTorqueFalseCount: number;
    warnCount: number;
    safeZeroCount: number;
    commLostCount: number;
    stateTimeline: Array<{ timestampUs: string; state: string }>;
    timeSync: Array<{ localUsSend: string; offsetUs: string; rttUs: string }>;
  };
  trace: {
    samples: number;
    downsampled: {
      bins: number;
      min: number[][];
      max: number[][];
      mean: number[][];
      rms: number[][];
    };
  };
}

type TransportIssue = {
  code: string;
  offset: number;
  message: string;
};

interface ProfileLike {
  haptic: {
    detent: {
      widthDeg: number;
      strengthmNm: number;
      snapRatio: number;
    };
    damping: number;
    friction: number;
    inertia: number;
    endstops: {
      minPosition?: number;
      maxPosition?: number;
      strengthmNm: number;
    };
  };
}

const profileSources = loadExampleProfiles();
const validationDetails: Array<{ index: number; valid: boolean; issues: string[] }> = [];
const validProfiles: unknown[] = [];
let validCount = 0;

for (let i = 0; i < profileSources.length; i++) {
  const parsed = parseProfile(profileSources[i]);
  if (parsed.valid) {
    validCount += 1;
    validProfiles.push(profileSources[i]);
  }
  validationDetails.push({
    index: i,
    valid: parsed.valid,
    issues: parsed.errors.map((e) => `${e.path}: ${e.message}`)
  });
}

if (validProfiles.length === 0) {
  throw new Error("无有效 Profile，E2E 无法继续");
}

const profile = validProfiles[0] as ProfileLike;
// Synthetic SIM_ONLY generation; it is not an acquired STM32 control lease.
const SIM_ONLY_LEASE_GENERATION = 1n;

let rngState = 0x12345678;
function nextUInt32(): number {
  rngState ^= (rngState << 13) & 0xffffffff;
  rngState ^= (rngState >>> 17);
  rngState ^= (rngState << 5) & 0xffffffff;
  return rngState >>> 0;
}
function nextFloat(): number {
  return nextUInt32() / 0x1_0000_0000;
}

function buildCommandFromProfile(profileLike: ProfileLike, nonce: number): HapticCommand {
  const h = profileLike.haptic;
  return {
    profileId: 0x11112233,
    commandNonce: nonce,
    targetPositionRad: 0,
    targetVelocityRadS: 0,
    detentWidthRad: (h.detent.widthDeg * Math.PI) / 180,
    detentStrengthNm: h.detent.strengthmNm / 1000,
    endstopMinRad: (h.endstops.minPosition ?? 0) * (Math.PI / 180),
    endstopMaxRad: (h.endstops.maxPosition ?? 0) * (Math.PI / 180),
    endstopStrengthNm: h.endstops.strengthmNm / 1000,
    dampingNmPerRadS: h.damping,
    inertiaKgM2: h.inertia,
    frictionNm: h.friction,
    userTorqueLimitNm: 0,
    activeSpeedLimitRadS: 0,
    modeFlags: 0x0001,
    textureId: 1,
    leaseGeneration: SIM_ONLY_LEASE_GENERATION
  };
}

function splitPacketChunks(data: Uint8Array): Uint8Array[] {
  const chunks: Uint8Array[] = [];
  let cursor = 0;
  while (cursor < data.length) {
    const remain = data.length - cursor;
    const seg = Math.min(remain, 1 + Math.floor(nextFloat() * 10));
    chunks.push(data.slice(cursor, cursor + seg));
    cursor += seg;
  }
  return chunks;
}

function makeNoiseChunk(length: number): Uint8Array {
  const noiseLen = Math.max(0, length);
  const noise = new Uint8Array(noiseLen);
  for (let i = 0; i < noiseLen; i++) {
    noise[i] = Math.floor(nextFloat() * 256);
  }
  return noise;
}

function emitChunkedFrame(parser: V6StreamParser, frame: Uint8Array): {
  frames: ReturnType<V6StreamParser["feed"]>["frames"];
  errors: ReturnType<V6StreamParser["feed"]>["errors"];
  processed: number;
  syncOffset: number;
} {
  const chunks = splitPacketChunks(frame);
  const result: ReturnType<V6StreamParser["feed"]> = {
    frames: [],
    errors: [],
    processed: 0,
    syncOffset: 0
  };
  for (const c of chunks) {
    const partial = parser.feed(c);
    result.frames.push(...partial.frames);
    result.errors.push(...partial.errors);
    result.processed += partial.processed;
    result.syncOffset = partial.syncOffset;
  }
  return result;
}

function makeBadCrcFrame(atUs: bigint, sequence: number): Uint8Array {
  const base = buildCommandFromProfile(profile, sequence);
  const frame = encodeFrame(
    FRAME_VERSION,
    0x10,
    encodeHapticCommand(base),
    { sequence, timestampUs: atUs, flags: 0 }
  );
  frame[frame.length - 1] = frame[frame.length - 1] ^ 0xff;
  return frame;
}

const device = new VirtualDevice();
const parser = new V6StreamParser();
const timeSyncEstimator = new TimeSyncEstimator();

let unknownTypeCount = 0;
let badCrcCount = 0;
let hardClampWarnings = 0;
let totalFramesInjected = 0;
let applied = 0;
let dropped = 0;
let parsedFrameCount = 0;
const transportErrors: TransportIssue[] = [];
const stateTimeline: Array<{ timestampUs: string; state: string }> = [];
const timeSyncTimeline: Array<{ localUsSend: string; offsetUs: string; rttUs: string }> = [];
let warnCount = 0;
let safeZeroCount = 0;
let commLostCount = 0;
let unknownVersionCount = 0;
let allowTorqueFalseCount = 0;

let commandSequence = 0;
const validPacketSteps = new Set([0n, 5_000n, 10_000n]);
const corruptPacketStep = 120_000n;
const unknownTypeStep = 140_000n;
const unknownVersionStep = 160_000n;

for (let step = 0; step < 4096; step++) {
    const nowUs = BigInt(step) * 500n;
  const events: Uint8Array[] = [];
  if (validPacketSteps.has(nowUs)) {
    const command = buildCommandFromProfile(profile, commandSequence);
    const encoded = encodeFrame(
      FRAME_VERSION,
      0x10,
      encodeHapticCommand(command),
      { sequence: commandSequence, timestampUs: nowUs, flags: 0 }
    );
    events.push(encoded);
    commandSequence += 1;
  }
  if (nowUs === unknownTypeStep) {
    const unknownPayload = new Uint8Array(4);
    unknownPayload.fill(0x11);
    events.push(encodeFrame(FRAME_VERSION, 0x99, unknownPayload, { sequence: 123, timestampUs: nowUs, flags: 0 }));
  }
  if (nowUs === corruptPacketStep) {
    events.push(makeBadCrcFrame(nowUs, 124));
  }
  if (nowUs === unknownVersionStep) {
    const command = buildCommandFromProfile(profile, 125);
    events.push(
      encodeFrame(
        FRAME_VERSION + 1,
        0x10,
        encodeHapticCommand(command),
        { sequence: 125, timestampUs: nowUs, flags: 0 }
      )
    );
  }

    for (const f of events) {
      totalFramesInjected += 1;
      const res = emitChunkedFrame(parser, f);
      transportErrors.push(...res.errors.map((e): TransportIssue => ({ code: e.code, offset: e.offset, message: e.message })));
      parsedFrameCount += res.frames.length;
      badCrcCount += res.errors.filter((e) => e.code === "bad_crc").length;
      unknownTypeCount += res.frames.filter((frame) => frame.type !== 0x10 && frame.type !== 0x01).length;
      for (const frame of res.frames) {
        const apply = device.applyFrame(frame, nowUs);
        if (apply.applied) {
          applied += 1;
        } else {
          dropped += 1;
          if (apply.reason?.includes("unsupported version")) {
            unknownVersionCount += 1;
          }
        }
        hardClampWarnings += apply.warnings.length > 0 ? 1 : 0;
      }
      if (nextFloat() < 0.5) {
        const noiseChunk = makeNoiseChunk(1 + Math.floor(nextFloat() * 4));
        const noiseRes = parser.feed(noiseChunk);
        transportErrors.push(...noiseRes.errors.map((e): TransportIssue => ({ code: e.code, offset: e.offset, message: e.message })));
        parsedFrameCount += noiseRes.frames.length;
      }
    }

  if (step % 250 === 0) {
    const localUsSend = nowUs;
    const remoteUsReq = nowUs + 40n;
    const remoteUsResp = nowUs + 75n;
    const localUsRecv = nowUs + 120n;
    const est = timeSyncEstimator.addSample({
      localUsSend,
      localUsRecv,
      remoteUsReq,
      remoteUsResp
    });
    timeSyncTimeline.push({
      localUsSend: localUsSend.toString(),
      offsetUs: est.offsetUs.toString(),
      rttUs: est.rttUs.toString()
    });
  }
}

const telemetryBuffer: Array<[number, number, number, number, number, number]> = [];
for (let step = 0; step < 4096; step++) {
  const nowUs = BigInt(step) * 500n;
  const tick = device.tick(nowUs);
  const stateChanged = stateTimeline.length === 0 || stateTimeline[stateTimeline.length - 1].state !== tick.safetyState;
  if (stateChanged) {
    stateTimeline.push({ timestampUs: nowUs.toString(), state: tick.safetyState });
  }

  if (tick.safetyState === "WARN") warnCount += 1;
  if (tick.safetyState === "SAFE_ZERO_REQUESTED") safeZeroCount += 1;
  if (tick.safetyState === "COMM_LOST") commLostCount += 1;
  if (!tick.allowTorque) allowTorqueFalseCount += 1;

  const stateCode =
    tick.safetyState === "NORMAL" ? 0 :
    tick.safetyState === "WARN" ? 1 :
    tick.safetyState === "SAFE_ZERO_REQUESTED" ? 2 : 3;

  const sample = [
    Math.trunc(tick.voltage * 1000),
    Math.trunc(tick.current * 1000),
    stateCode,
    tick.commandNonce,
    step % 4096,
    Number(nowUs % 10000n)
  ] as [number, number, number, number, number, number];
  device.pushTrace(sample);
  telemetryBuffer.push(sample);
}

const lastSafety = device.tick(4096n * 500n);
const downsampled = device.getTraceDownsample();
const finalSummary: E2ESummary = {
  mode: "SIM_ONLY",
  profileValidation: {
    total: profileSources.length,
    valid: validCount,
    invalid: profileSources.length - validCount,
    details: validationDetails
  },
  transport: {
    totalFramesParsed: parsedFrameCount,
    totalErrors: transportErrors.length,
    badFrameErrors: transportErrors,
    unknownTypeCount,
    unknownVersionCount,
    totalFramesInjected
  },
  command: {
    totalDecodedFrames: parsedFrameCount,
    applied,
    dropped,
    hardClampWarnings
  },
  runtime: {
    sampledTelemetry: telemetryBuffer.length,
    timeoutState: lastSafety.safetyState,
    allowTorqueFalseCount,
    warnCount,
    safeZeroCount,
    commLostCount,
    stateTimeline,
    timeSync: timeSyncTimeline
  },
  trace: {
    samples: device.getTraceStatus().totalWritten,
    downsampled
  }
};

const markdown = `# GL30 AMOLED V7 SIM_ONLY 报告

## 结论

- SIM_ONLY: 是
- Profile 有效数：${finalSummary.profileValidation.valid}/${finalSummary.profileValidation.total}
- 解析到的有效帧：${finalSummary.transport.totalFramesParsed}
- 命令应用：${finalSummary.command.applied} 个通过、${finalSummary.command.dropped} 个丢弃
- 采样遥测：${finalSummary.runtime.sampledTelemetry} 组（2kHz 虚拟时间，步长 500us）
- 通信状态：${finalSummary.runtime.timeoutState}

## 统计

- bad_crc：${badCrcCount}
- unknown_type：${unknownTypeCount}
- allow_torque_false：${finalSummary.runtime.allowTorqueFalseCount}
- Trace 降采样（20:1）：${finalSummary.trace.downsampled.bins} × 6（每行含 min/max/mean/rms）
`;

const artifactDir = resolve(process.cwd(), "artifacts", "sim");
mkdirSync(artifactDir, { recursive: true });
writeFileSync(resolve(artifactDir, "latest.json"), `${JSON.stringify(finalSummary, null, 2)}\n`, "utf8");
writeFileSync(resolve(artifactDir, "latest.md"), markdown, "utf8");

console.log("=== GL30 V7 e2e-no-hardware（SIM_ONLY）===");
console.log(`Profile: ${finalSummary.profileValidation.valid}/${finalSummary.profileValidation.total} valid`);
console.log(`Transport: ${finalSummary.transport.totalFramesParsed} parsed, ${finalSummary.transport.totalErrors} CRC/length errors`);
console.log(`Command: ${finalSummary.command.applied} applied, ${finalSummary.command.dropped} dropped`);
console.log(`Safety: ${finalSummary.runtime.timeoutState}, allowTorque=false ${finalSummary.runtime.allowTorqueFalseCount} samples`);
console.log(`Trace: ${finalSummary.trace.samples}x6 int16 -> ${finalSummary.trace.downsampled.bins} bins`);
console.log(`Artifacts: ${artifactDir}`);
