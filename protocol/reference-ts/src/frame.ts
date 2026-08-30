import { crc32c } from "./crc32c.js";
import {
  HAPTIC_COMMAND_PAYLOAD_LEN,
  MOTOR_STATE_FAST_PAYLOAD_LEN,
  MOTOR_STATE_SLOW_PAYLOAD_LEN,
  encodeHapticCommand,
  encodeMotorStateFast,
  decodeHapticCommand,
  decodeMotorStateFast,
  encodeMotorStateSlow,
  decodeMotorStateSlow,
  HapticCommand,
  MotorStateFast,
  MotorStateSlow
} from "./payloads.js";

export const FRAME_SYNC = 0xA55A;
export const FRAME_VERSION = 1;
export const MAX_PAYLOAD = 4096;

export type FrameType = 0x01 | 0x02 | 0x10 | number;

export interface V6Frame {
  version: number;
  type: FrameType;
  payloadLen: number;
  flags: number;
  sequence: number;
  timestampUs: bigint;
  payload: Uint8Array;
  crc32c: number;
}

export interface DecodeResult<T = V6Frame> {
  frame: T;
}

const HEADER_BYTES_NO_SYNC = 1 + 1 + 2 + 2 + 4 + 8;
const FRAME_HEADER_BYTES = 2 + HEADER_BYTES_NO_SYNC;
const FRAME_CRC_BYTES = 4;
const PAYLOAD_OFFSET = FRAME_HEADER_BYTES;
const FRAME_OVERHEAD = FRAME_HEADER_BYTES + FRAME_CRC_BYTES;

export class FrameError extends Error {
  public readonly code: "bad_sync" | "bad_length" | "bad_crc" | "bad_payload_len";
  constructor(code: FrameError["code"], message: string) {
    super(message);
    this.name = "FrameError";
    this.code = code;
  }
}

const KNOWN_STATIC_LEN_BY_TYPE: Record<number, number | null> = {
  0x01: MOTOR_STATE_FAST_PAYLOAD_LEN,
  0x02: MOTOR_STATE_SLOW_PAYLOAD_LEN,
  0x10: HAPTIC_COMMAND_PAYLOAD_LEN
};

export function encodeFrame(
  version: number,
  type: FrameType,
  payload: Uint8Array,
  options?: {
    sequence?: number;
    timestampUs?: bigint;
    flags?: number;
  }
): Uint8Array {
  const payloadLen = payload.length;
  if (payloadLen > MAX_PAYLOAD) {
    throw new FrameError("bad_length", `payload_len exceeds MAX_PAYLOAD (${payloadLen})`);
  }
  if (KNOWN_STATIC_LEN_BY_TYPE[type] != null && payloadLen !== KNOWN_STATIC_LEN_BY_TYPE[type]) {
    throw new FrameError("bad_length", `payload length mismatch for type 0x${type.toString(16)}`);
  }
  if (typeof options?.timestampUs !== "bigint") {
    throw new Error("timestampUs is required as bigint");
  }
  const flags = options?.flags ?? 0;
  const sequence = options?.sequence ?? 0;

  const frameLen = FRAME_OVERHEAD + payloadLen;
  const out = new Uint8Array(frameLen);
  const dv = new DataView(out.buffer);

  dv.setUint16(0, FRAME_SYNC, true);
  dv.setUint8(2, version & 0xff);
  dv.setUint8(3, type & 0xff);
  dv.setUint16(4, payloadLen, true);
  dv.setUint16(6, flags & 0xffff, true);
  dv.setUint32(8, sequence >>> 0, true);
  dv.setBigUint64(12, options.timestampUs as bigint, true);
  out.set(payload, PAYLOAD_OFFSET);
  const bodyForCrc = out.slice(2, PAYLOAD_OFFSET + payloadLen);
  const crc = crc32c(bodyForCrc);
  dv.setUint32(PAYLOAD_OFFSET + payloadLen, crc >>> 0, true);
  return out;
}

export function decodeFrame(frame: Uint8Array): V6Frame {
  if (frame.length < FRAME_OVERHEAD) {
    throw new FrameError("bad_length", "frame shorter than header+crc");
  }
  const dv = new DataView(frame.buffer, frame.byteOffset, frame.byteLength);
  const sync = dv.getUint16(0, true);
  if (sync !== FRAME_SYNC) {
    throw new FrameError("bad_sync", `sync mismatch: ${sync}`);
  }
  const version = dv.getUint8(2);
  const type = dv.getUint8(3);
  const payloadLen = dv.getUint16(4, true);
  if (payloadLen > MAX_PAYLOAD) {
    throw new FrameError("bad_length", `payload_len > MAX_PAYLOAD: ${payloadLen}`);
  }
  const expectedLen = FRAME_OVERHEAD + payloadLen;
  if (frame.length !== expectedLen) {
    throw new FrameError("bad_length", `frame length mismatch (expected ${expectedLen}, got ${frame.length})`);
  }
  if (KNOWN_STATIC_LEN_BY_TYPE[type] != null && payloadLen !== KNOWN_STATIC_LEN_BY_TYPE[type]) {
    throw new FrameError("bad_payload_len", `payload length mismatch for type 0x${type.toString(16)}`);
  }
  const flags = dv.getUint16(6, true);
  const sequence = dv.getUint32(8, true);
  const timestampUs = dv.getBigUint64(12, true);
  const payload = frame.slice(PAYLOAD_OFFSET, PAYLOAD_OFFSET + payloadLen);
  const crcInFrame = dv.getUint32(PAYLOAD_OFFSET + payloadLen, true);
  const crcCalc = crc32c(frame.slice(2, PAYLOAD_OFFSET + payloadLen));
  if (crcInFrame !== crcCalc) {
    throw new FrameError("bad_crc", `crc mismatch (in frame=${crcInFrame}, calc=${crcCalc})`);
  }
  return { version, type, payloadLen, flags, sequence, timestampUs, payload, crc32c: crcInFrame };
}

export function encodePayloadByType(
  type: FrameType,
  payload: MotorStateFast | MotorStateSlow | HapticCommand
): Uint8Array {
  if (type === 0x01) {
    return encodeMotorStateFast(payload as MotorStateFast);
  }
  if (type === 0x10) {
    return encodeHapticCommand(payload as HapticCommand);
  }
  if (type === 0x02) {
    return encodeMotorStateSlow(payload as MotorStateSlow);
  }
  throw new Error(`unsupported payload type 0x${type.toString(16)}`);
}

export function decodePayloadByType(
  type: FrameType,
  payload: Uint8Array
): MotorStateFast | MotorStateSlow | HapticCommand {
  if (type === 0x01) {
    return decodeMotorStateFast(payload);
  }
  if (type === 0x10) {
    return decodeHapticCommand(payload);
  }
  if (type === 0x02) {
    return decodeMotorStateSlow(payload);
  }
  throw new Error(`unsupported payload type 0x${type.toString(16)}`);
}
