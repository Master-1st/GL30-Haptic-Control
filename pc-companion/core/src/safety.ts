import { SafeState, SafetyTickResult } from "./types.js";

export const SAFE_THRESHOLDS = {
  WARN_MS: 10,
  SAFE_ZERO_REQUESTED_MS: 20,
  COMM_LOST_MS: 100
} as const;

export interface SupervisorInput {
  nowUs: bigint;
  isFrameValid: boolean;
  frameVersion?: number;
  frameType?: number;
}

export class SafetySupervisor {
  private lastValidFrameUs: bigint | null = null;
  private state: SafeState = "NORMAL";
  private readonly warnMs: number;
  private readonly safeZeroMs: number;
  private readonly commLostMs: number;
  public crcError = 0;
  public badLength = 0;
  public unknownVersion = 0;
  public unknownType = 0;

  constructor(thresholds?: Partial<typeof SAFE_THRESHOLDS>) {
    this.warnMs = thresholds?.WARN_MS ?? SAFE_THRESHOLDS.WARN_MS;
    this.safeZeroMs = thresholds?.SAFE_ZERO_REQUESTED_MS ?? SAFE_THRESHOLDS.SAFE_ZERO_REQUESTED_MS;
    this.commLostMs = thresholds?.COMM_LOST_MS ?? SAFE_THRESHOLDS.COMM_LOST_MS;
  }

  reportBadCrc() {
    this.crcError += 1;
  }
  reportBadLength() {
    this.badLength += 1;
  }
  reportUnknownVersion() {
    this.unknownVersion += 1;
  }
  reportUnknownType() {
    this.unknownType += 1;
  }
  applyFrame(input: Omit<SupervisorInput, "hadFrame">): void {
    if (!input.isFrameValid) {
      if (input.frameVersion !== undefined && input.frameVersion !== 1) {
        this.reportUnknownVersion();
      }
      if (input.frameType !== undefined && ![0x01, 0x10].includes(input.frameType)) {
        this.reportUnknownType();
      }
    } else {
      this.lastValidFrameUs = input.nowUs;
      this.state = "NORMAL";
    }
  }

  tick(nowUs: bigint): SafetyTickResult {
    const warnings: string[] = [];
    let elapsedMs = Number.MAX_SAFE_INTEGER;
    let current = this.state;
    if (this.lastValidFrameUs !== null) {
      const deltaMs = Number(nowUs - this.lastValidFrameUs) / 1000;
      elapsedMs = deltaMs;
      if (deltaMs >= this.commLostMs) {
        current = "COMM_LOST";
      } else if (deltaMs >= this.safeZeroMs) {
        current = "SAFE_ZERO_REQUESTED";
      } else if (deltaMs >= this.warnMs) {
        current = "WARN";
      } else {
        current = "NORMAL";
      }
      if (deltaMs >= this.commLostMs) {
        warnings.push(`通信超时 >=${this.commLostMs}ms，进入COMM_LOST`);
      } else if (deltaMs >= this.safeZeroMs) {
        warnings.push(`无新有效命令 >=${this.safeZeroMs}ms，进入SAFE_ZERO_REQUESTED`);
      } else if (deltaMs >= this.warnMs) {
        warnings.push(`接收间隔>=${this.warnMs}ms，进入WARN`);
      }
    }
    this.state = current;
    return {
      state: current,
      elapsedMs,
      warnings,
      unknownVersion: this.unknownVersion,
      unknownType: this.unknownType,
      crcError: this.crcError,
      badLength: this.badLength
    };
  }

  get status(): SafeState {
    return this.state;
  }
}
