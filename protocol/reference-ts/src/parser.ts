import { decodeFrame, FrameError } from "./frame.js";

export interface ParseEvent {
  frame?: ReturnType<typeof decodeFrame>;
  error?: FrameError;
  consumed: number;
}

export interface StreamParserResult {
  frames: ReturnType<typeof decodeFrame>[];
  errors: Array<{ code: FrameError["code"]; message: string; offset: number }>;
  processed: number;
  syncOffset: number;
}

export class V6StreamParser {
  private buffer = new Uint8Array(0);
  private readonly sync = new Uint8Array([0x5a, 0xa5]);
  private totalProcessed = 0;
  private syncOffset = 0;

  feed(chunk: Uint8Array): StreamParserResult {
    const merged = new Uint8Array(this.buffer.length + chunk.length);
    merged.set(this.buffer, 0);
    merged.set(chunk, this.buffer.length);
    let cursor = 0;
    const frames: ReturnType<typeof decodeFrame>[] = [];
    const errors: Array<{ code: FrameError["code"]; message: string; offset: number }> = [];

    while (cursor < merged.length) {
      const remaining = merged.length - cursor;
      if (remaining < 2) {
        break;
      }
      const syncIndex = this.indexOfSync(merged, cursor);
      if (syncIndex === -1) {
        if (merged[merged.length - 1] === this.sync[0]) {
          this.syncOffset += merged.length - 1 - cursor;
          cursor = merged.length - 1;
        } else {
          this.syncOffset += remaining;
          cursor = merged.length;
        }
        break;
      }
      this.syncOffset += syncIndex - cursor;
      cursor = syncIndex;
      if (merged.length - cursor < 24) {
        break;
      }
      const payloadLen = merged[cursor + 4] | (merged[cursor + 5] << 8);
      if (payloadLen > 4096 || payloadLen < 0) {
        errors.push({
          code: "bad_payload_len",
          message: `payload too large at offset ${cursor + this.totalProcessed + syncIndex - cursor}: ${payloadLen}`,
          offset: cursor
        });
        cursor += 1;
        continue;
      }
      const totalLen = 24 + payloadLen;
      if (merged.length - cursor < totalLen) {
        break;
      }
      const slice = merged.slice(cursor, cursor + totalLen);
      try {
        const frame = decodeFrame(slice);
        frames.push(frame);
      } catch (e) {
        if (e instanceof FrameError) {
          errors.push({ code: e.code, message: e.message, offset: cursor });
          // discard the sync byte only for resync recovery
          cursor += 1;
          continue;
        }
        throw e;
      }
      cursor += totalLen;
    }

    this.totalProcessed += cursor;
    this.buffer = merged.slice(cursor);
    return {
      frames,
      errors,
      processed: cursor,
      syncOffset: this.syncOffset
    };
  }

  private indexOfSync(data: Uint8Array, start: number): number {
    for (let i = start; i < data.length - 1; i++) {
      if (data[i] === this.sync[0] && data[i + 1] === this.sync[1]) {
        return i;
      }
    }
    return -1;
  }
}
