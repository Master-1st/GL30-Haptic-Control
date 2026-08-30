import { test } from "node:test";
import assert from "node:assert/strict";
import {
  VirtualDevice,
  SafetySupervisor
} from "@gl30/core";
import {
  FRAME_VERSION,
  encodeFrame,
  encodeMotorStateFast,
  encodeHapticCommand,
  decodeFrame
} from "@gl30/protocol";

function hapticFrame(profileNonce: number, options: { timestampUs: bigint; sequence?: number; version?: number }) {
  const payload = encodeHapticCommand({
    profileId: 100,
    commandNonce: profileNonce,
    targetPositionRad: 0.11,
    targetVelocityRadS: 0,
    detentWidthRad: 0.2,
    detentStrengthNm: 0.01,
    endstopMinRad: -0.2,
    endstopMaxRad: 0.2,
    endstopStrengthNm: 0.01,
    dampingNmPerRadS: 0.0002,
    inertiaKgM2: 0.0001,
    frictionNm: 0.001,
    userTorqueLimitNm: 0.03,
    activeSpeedLimitRadS: 1.2,
    modeFlags: 0x1,
    textureId: 2
  });
  return decodeFrame(
    encodeFrame(options.version ?? FRAME_VERSION, 0x10, payload, {
      timestampUs: options.timestampUs,
      sequence: options.sequence ?? 0
    })
  );
}

function randomMotorFrameAt(version: number, timestampUs: bigint) {
  return decodeFrame(
    encodeFrame(version, 0x22, encodeMotorStateFast({
      angleRad: 0.2,
      velocityRadPerSec: 0.1,
      accelerationRadPerSec2: 0.1,
      iqRefA: 0.1,
      iqMeasA: 0.1,
      idMeasA: 0.1,
      torqueCmdNm: 0,
      torqueEstNm: 0,
      busVoltageV: 7.2,
      busCurrentA: 0.1,
      logicalPosition: 0,
      subPosition: 0,
      motorState: 1,
      faultBits: 0,
      warningBits: 0,
      isrCycles: 1,
      encoderStatus: 1,
      droppedCmds: 0,
      reserved: 0
    }), {
      timestampUs
    })
  );
}

test("unknown version/type does not execute command or refresh state and counters are not inflated", () => {
  const device = new VirtualDevice();
  const first = device.applyFrame(hapticFrame(77, { timestampUs: 0n }));
  assert.equal(first.applied, true);
  assert.equal(first.reason, undefined);

  const afterGood = device.tick(1_000n);
  assert.equal(afterGood.commandNonce, 77);

  const versionFrame = hapticFrame(88, {
    version: 2,
    timestampUs: 95_000n
  });
  const badVersion = device.applyFrame(versionFrame);
  assert.equal(badVersion.applied, false);
  assert.equal(badVersion.reason, "unsupported version");

  const typeFrame = randomMotorFrameAt(FRAME_VERSION, 96_000n);
  const badType = device.applyFrame(typeFrame);
  assert.equal(badType.applied, false);
  assert.equal(badType.reason, "unsupported type 0x22");

  const afterBad = device.tick(150_000n);
  assert.equal(afterBad.safetyState, "COMM_LOST");
  assert.equal(afterBad.commandNonce, 78);
  assert.equal(afterBad.allowTorque, false);

  const stats = device.getSafetyStatus();
  assert.equal(stats, "COMM_LOST");
});

test("received timestamp is ignored for timeout; timeout uses receive time and can recover from COMM_LOST", () => {
  const device = new VirtualDevice();
  const oldTimestampFrame = hapticFrame(1, { timestampUs: 0n, sequence: 1 });
  const appliedOld = device.applyFrame(oldTimestampFrame, 100_000n);
  assert.equal(appliedOld.applied, true);

  const initial = device.tick(101_000n);
  assert.equal(initial.safetyState, "NORMAL");

  const warn = device.tick(110_000n);
  assert.equal(warn.safetyState, "WARN");
  assert.equal(warn.allowTorque, true);

  const safeZero = device.tick(120_000n);
  assert.equal(safeZero.safetyState, "SAFE_ZERO_REQUESTED");
  assert.equal(safeZero.allowTorque, false);

  const lost = device.tick(205_000n);
  assert.equal(lost.safetyState, "COMM_LOST");
  assert.equal(lost.allowTorque, false);

  const recoveredFrame = hapticFrame(2, { timestampUs: 0n, sequence: 2 });
  const recovered = device.applyFrame(recoveredFrame, 205_000n);
  assert.equal(recovered.applied, true);

  const afterRecover = device.tick(205_000n);
  assert.equal(afterRecover.safetyState, "NORMAL");
  assert.equal(afterRecover.allowTorque, true);
});

test("virtual time transitions at 10ms/WARN, 20ms/SAFE_ZERO_REQUESTED, 100ms/COMM_LOST", () => {
  const device = new VirtualDevice();
  device.applyFrame(hapticFrame(12, { timestampUs: 0n }));

  const warn = device.tick(10_000n);
  assert.equal(warn.safetyState, "WARN");
  assert.equal(warn.allowTorque, true);

  const safeZero = device.tick(20_000n);
  assert.equal(safeZero.safetyState, "SAFE_ZERO_REQUESTED");
  assert.equal(safeZero.allowTorque, false);
  assert.equal(safeZero.commandNonce, 13);

  const commLost = device.tick(100_000n);
  assert.equal(commLost.safetyState, "COMM_LOST");
  assert.equal(commLost.allowTorque, false);
});

test("SafetySupervisor counts unknown version/type once", () => {
  const safety = new SafetySupervisor();
  safety.applyFrame({ isFrameValid: false, frameVersion: 0, frameType: FRAME_VERSION, nowUs: 1_000n });
  safety.applyFrame({ isFrameValid: false, frameVersion: FRAME_VERSION, frameType: 0, nowUs: 2_000n });

  const status = safety.tick(10_000n) as {
    unknownVersion: number;
    unknownType: number;
  };
  assert.equal(status.unknownVersion, 1);
  assert.equal(status.unknownType, 1);
});
