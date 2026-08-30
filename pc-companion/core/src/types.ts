import type { HapticCommand } from "@gl30/protocol";

export type SafeState = "NORMAL" | "WARN" | "SAFE_ZERO_REQUESTED" | "COMM_LOST";

export interface HapticCommandApplied {
  original: HapticCommand;
  clamped: HapticCommand;
  warnings: string[];
  valid: boolean;
}

export interface CommandGateTelemetry {
  applied: boolean;
  dropped: boolean;
  limitWarnings: number;
}

export interface SafetyTickResult {
  state: SafeState;
  elapsedMs: number;
  warnings: string[];
  unknownVersion: number;
  unknownType: number;
  crcError: number;
  badLength: number;
}
