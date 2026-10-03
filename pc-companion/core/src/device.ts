import type { HapticCommand as HapticCommandType, MotorStateFast, V6Frame } from "@gl30/protocol";
import { decodePayloadByType, FRAME_VERSION, MAX_PAYLOAD } from "@gl30/protocol";
import { clampHapticCommand } from "./command.js";
import { SafetySupervisor } from "./safety.js";
import { TraceBuffer } from "./trace.js";

const ZERO_COMMAND: HapticCommandType = {
  profileId: 0,
  commandNonce: 0,
  targetPositionRad: 0,
  targetVelocityRadS: 0,
  detentWidthRad: 0,
  detentStrengthNm: 0,
  endstopMinRad: 0,
  endstopMaxRad: 0,
  endstopStrengthNm: 0,
  dampingNmPerRadS: 0,
  inertiaKgM2: 0,
  frictionNm: 0,
  userTorqueLimitNm: 0,
  activeSpeedLimitRadS: 0,
  modeFlags: 0,
  textureId: 0,
  leaseGeneration: 0n
};

export interface DeviceTickTelemetry {
  timestampUs: bigint;
  safetyState: "NORMAL" | "WARN" | "SAFE_ZERO_REQUESTED" | "COMM_LOST";
  commandNonce: number;
  allowTorque: boolean;
  voltage: number;
  current: number;
}

export interface ApplyResult {
  applied: boolean;
  dropped: boolean;
  reason?: string;
  warnings: string[];
}

export class VirtualDevice {
  private readonly safety = new SafetySupervisor();
  private readonly trace = new TraceBuffer();
  private currentCommand: HapticCommandType = ZERO_COMMAND;
  private sequence = 0;

  applyFrame(frame: V6Frame, receivedAtUs?: bigint): ApplyResult {
    const receiveTime = receivedAtUs ?? frame.timestampUs;
    const warnings: string[] = [];
    if (frame.version !== FRAME_VERSION) {
      this.safety.applyFrame({ isFrameValid: false, frameVersion: frame.version, frameType: frame.type, nowUs: receiveTime });
      return { applied: false, dropped: true, reason: "unsupported version", warnings };
    }
    if (frame.type !== 0x10) {
      this.safety.applyFrame({ isFrameValid: false, frameVersion: frame.version, frameType: frame.type, nowUs: receiveTime });
      return { applied: false, dropped: true, reason: `unsupported type 0x${frame.type.toString(16)}`, warnings };
    }
    if (frame.payloadLen > MAX_PAYLOAD) {
      this.safety.applyFrame({ isFrameValid: false, frameVersion: frame.version, frameType: frame.type, nowUs: receiveTime });
      return { applied: false, dropped: true, reason: "payload exceed max", warnings };
    }

    const payload = decodePayloadByType(frame.type, frame.payload) as HapticCommandType;
    const clamped = clampHapticCommand(payload);
    if (!clamped.valid) {
      warnings.push(...clamped.warnings);
      this.safety.applyFrame({ isFrameValid: false, frameVersion: frame.version, frameType: frame.type, nowUs: receiveTime });
      return { applied: false, dropped: true, reason: "invalid command values", warnings };
    }
    if (clamped.warnings.length > 0) {
      warnings.push(...clamped.warnings);
    }
    this.currentCommand = clamped.clamped;
    this.sequence += 1;
    this.safety.applyFrame({ isFrameValid: true, nowUs: receiveTime, frameVersion: frame.version, frameType: frame.type });
    if (clamped.warnings.length > 0) {
      return { applied: true, dropped: false, reason: "hard-clamped", warnings };
    }
    return { applied: true, dropped: false, warnings };
  }

  pushTrace(sample: number[]): void {
    this.trace.write(sample);
  }

  tick(nowUs: bigint): DeviceTickTelemetry {
    const safe = this.safety.tick(nowUs);
    const allowTorque = safe.state === "NORMAL" || safe.state === "WARN";
    let commanded = this.currentCommand;
    if (safe.state === "SAFE_ZERO_REQUESTED" || safe.state === "COMM_LOST") {
      commanded = { ...ZERO_COMMAND, commandNonce: commanded.commandNonce + 1 };
    }
    const baseV = 7.2 + 0.08 * Math.sin(Number(nowUs) / 3500);
    const baseI = Math.abs(commanded.targetVelocityRadS) * 0.02 + 0.13;
    return {
      timestampUs: nowUs,
      safetyState: safe.state,
      commandNonce: commanded.commandNonce,
      allowTorque,
      voltage: Number(baseV.toFixed(4)),
      current: Number(baseI.toFixed(6))
    };
  }

  getSafetyStatus() {
    return this.safety.status;
  }

  getTraceDownsample() {
    return this.trace.downsample20();
  }

  getTraceStatus() {
    return this.trace.status;
  }
}

export function makeNoOpTelemetry(): MotorStateFast {
  return {
    angleRad: 0,
    velocityRadPerSec: 0,
    accelerationRadPerSec2: 0,
    iqRefA: 0,
    iqMeasA: 0,
    idMeasA: 0,
    torqueCmdNm: 0,
    torqueEstNm: 0,
    busVoltageV: 0,
    busCurrentA: 0,
    logicalPosition: 0,
    subPosition: 0,
    motorState: 0,
    faultBits: 0,
    warningBits: 0,
    isrCycles: 0,
    encoderStatus: 0,
    droppedCmds: 0,
    reserved: 0
  };
}
