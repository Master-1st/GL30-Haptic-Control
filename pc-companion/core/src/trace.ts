export interface DownsampleRow {
  bins: number;
  min: number[][];
  max: number[][];
  mean: number[][];
  rms: number[][];
}

export interface TraceStore {
  samples: Int16Array[];
  writePos: number;
  length: number;
}

export class TraceBuffer {
  private readonly rows = 4096;
  private readonly cols = 6;
  private readonly raw: Int16Array;
  private cursor = 0;
  private totalWritten = 0;
  private overflowCount = 0;

  constructor() {
    this.raw = new Int16Array(this.rows * this.cols);
  }

  write(sample: readonly number[]): void {
    if (sample.length !== this.cols) {
      throw new Error(`trace sample必须是6通道（当前${sample.length}）`);
    }
    const base = this.cursor * this.cols;
    for (let c = 0; c < this.cols; c++) {
      const value = sample[c];
      this.raw[base + c] = clampInt16(value);
    }
    this.cursor = (this.cursor + 1) % this.rows;
    if (this.totalWritten < this.rows) this.totalWritten += 1;
    else this.overflowCount += 1;
  }

  readAll(): Int16Array {
    const ordered = new Int16Array(this.totalWritten * this.cols);
    const start = this.totalWritten < this.rows ? 0 : this.cursor;
    for (let i = 0; i < this.totalWritten; i++) {
      const row = (start + i) % this.rows;
      const source = row * this.cols;
      const target = i * this.cols;
      for (let c = 0; c < this.cols; c++) {
        ordered[target + c] = this.raw[source + c];
      }
    }
    return ordered;
  }

  downsample20(): DownsampleRow {
    const window = 20;
    const total = this.totalWritten || 1;
    const used = Math.min(total, this.rows);
    const start = this.totalWritten < this.rows ? 0 : this.cursor;
    const bins = Math.ceil(used / window);
    const min: number[][] = [];
    const max: number[][] = [];
    const mean: number[][] = [];
    const rms: number[][] = [];

    for (let b = 0; b < bins; b++) {
      const minRow = new Array(this.cols).fill(Number.POSITIVE_INFINITY);
      const maxRow = new Array(this.cols).fill(Number.NEGATIVE_INFINITY);
      const meanAcc = new Array(this.cols).fill(0);
      const rmsAcc = new Array(this.cols).fill(0);
      let count = 0;
      const binStart = b * window;
      const end = Math.min(used, (b + 1) * window);
      for (let i = binStart; i < end; i++) {
        const row = (start + i) % this.rows;
        const base = row * this.cols;
        for (let c = 0; c < this.cols; c++) {
          const v = this.raw[base + c];
          if (v < minRow[c]) minRow[c] = v;
          if (v > maxRow[c]) maxRow[c] = v;
          meanAcc[c] += v;
          rmsAcc[c] += v * v;
        }
        count += 1;
      }
      const meanRow = new Array(this.cols).fill(0);
      const rmsRow = new Array(this.cols).fill(0);
      for (let c = 0; c < this.cols; c++) {
        meanRow[c] = count ? meanAcc[c] / count : 0;
        rmsRow[c] = count ? Math.sqrt(rmsAcc[c] / count) : 0;
      }
      min.push(minRow);
      max.push(maxRow);
      mean.push(meanRow);
      rms.push(rmsRow);
    }
    return { bins, min, max, mean, rms };
  }

  get status() {
    return {
      rows: this.rows,
      cols: this.cols,
      totalWritten: this.totalWritten,
      overflowCount: this.overflowCount
    };
  }
}

function clampInt16(v: number): number {
  if (!Number.isFinite(v)) return 0;
  return Math.max(-32768, Math.min(32767, Math.trunc(v)));
}
