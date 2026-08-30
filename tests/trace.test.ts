import { test } from "node:test";
import assert from "node:assert/strict";
import { TraceBuffer } from "@gl30/core";

function buildTraceSample(index: number): number[] {
  return [
    index,
    -index,
    (index % 20) - 10,
    (index * 3) % 11 - 5,
    index % 13,
    (index % 16) - 8
  ];
}

function expectedStats(samples: number[][], start: number, end: number): {
  min: number[];
  max: number[];
  mean: number[];
  rms: number[];
} {
  const cols = 6;
  const count = end - start;
  const min = new Array(cols).fill(Number.POSITIVE_INFINITY);
  const max = new Array(cols).fill(Number.NEGATIVE_INFINITY);
  const meanAcc = new Array(cols).fill(0);
  const rmsAcc = new Array(cols).fill(0);
  for (let i = start; i < end; i++) {
    const sample = samples[i];
    for (let c = 0; c < cols; c++) {
      const v = sample[c];
      if (v < min[c]) min[c] = v;
      if (v > max[c]) max[c] = v;
      meanAcc[c] += v;
      rmsAcc[c] += v * v;
    }
  }
  const mean = new Array(cols).fill(0);
  const rms = new Array(cols).fill(0);
  for (let c = 0; c < cols; c++) {
    mean[c] = meanAcc[c] / count;
    rms[c] = Math.sqrt(rmsAcc[c] / count);
  }
  return { min, max, mean, rms };
}

function assertAlmostEqualArray(actual: number[], expected: number[], eps: number) {
  assert.equal(actual.length, expected.length);
  for (let i = 0; i < actual.length; i++) {
    assert.ok(Math.abs(actual[i] - expected[i]) <= eps);
  }
}

function collectTraceDownsample(samples: number[][]): {
  bins: number;
  min: number[][];
  max: number[][];
  mean: number[][];
  rms: number[][];
} {
  const trace = new TraceBuffer();
  for (const sample of samples) {
    trace.write(sample);
  }
  const downsampled = trace.downsample20();
  return downsampled;
}

function overflowOrderedIndex(position: number): number {
  const source = (position + 20) % 4096;
  return source < 20 ? source + 4096 : source;
}

test("trace buffer stores 4096x6 int16 samples and downsample produces 205 bins", () => {
  const samples: number[][] = [];
  const trace = new TraceBuffer();
  for (let i = 0; i < 4096; i++) {
    const sample = buildTraceSample(i);
    samples.push(sample);
    trace.write(sample);
  }

  const raw = trace.readAll();
  assert.equal(raw.length, 4096 * 6);
  assert.equal(raw.byteLength, 49152);

  const downsample = trace.downsample20();
  assert.equal(downsample.bins, 205);
  assert.equal(downsample.min.length, 205);
  assert.equal(downsample.max.length, 205);
  assert.equal(downsample.mean.length, 205);
  assert.equal(downsample.rms.length, 205);

  const firstWindow = expectedStats(samples, 0, 20);
  assertAlmostEqualArray(downsample.min[0], firstWindow.min, 1e-12);
  assertAlmostEqualArray(downsample.max[0], firstWindow.max, 1e-12);
  assertAlmostEqualArray(downsample.mean[0], firstWindow.mean, 1e-12);
  assertAlmostEqualArray(downsample.rms[0], firstWindow.rms, 1e-12);

  const lastWindow = expectedStats(samples, 4080, 4096);
  assertAlmostEqualArray(downsample.min[204], lastWindow.min, 1e-12);
  assertAlmostEqualArray(downsample.max[204], lastWindow.max, 1e-12);
  assertAlmostEqualArray(downsample.mean[204], lastWindow.mean, 1e-12);
  assertAlmostEqualArray(downsample.rms[204], lastWindow.rms, 1e-12);
});

test("trace buffer preserves oldest-to-newest ordering after overwrite and downsample", () => {
  const trace = new TraceBuffer();
  for (let i = 0; i < 4116; i++) {
    trace.write(buildTraceSample(i));
  }

  const raw = trace.readAll();
  const expectedFirst = buildTraceSample(20);
  const expectedLast = buildTraceSample(4096 + 19);
  const firstSample = Array.from(raw.slice(0, 6));
  const lastSample = Array.from(raw.slice(raw.length - 6, raw.length));
  assert.deepStrictEqual(firstSample, expectedFirst);
  assert.deepStrictEqual(lastSample, expectedLast);

  const downsample = trace.downsample20();
  const orderedSamples: number[][] = [];
  for (let i = 0; i < 4096; i++) {
    orderedSamples.push(buildTraceSample(overflowOrderedIndex(i)));
  }

  const firstWindow = expectedStats(orderedSamples, 0, 20);
  const lastWindow = expectedStats(orderedSamples, 4080, 4096);
  assertAlmostEqualArray(downsample.min[0], firstWindow.min, 1e-12);
  assertAlmostEqualArray(downsample.max[0], firstWindow.max, 1e-12);
  assertAlmostEqualArray(downsample.mean[0], firstWindow.mean, 1e-12);
  assertAlmostEqualArray(downsample.rms[0], firstWindow.rms, 1e-12);
  assertAlmostEqualArray(downsample.min[204], lastWindow.min, 1e-12);
  assertAlmostEqualArray(downsample.max[204], lastWindow.max, 1e-12);
  assertAlmostEqualArray(downsample.mean[204], lastWindow.mean, 1e-12);
  assertAlmostEqualArray(downsample.rms[204], lastWindow.rms, 1e-12);
});

test("replaying same input trace writes and downsample deterministically", () => {
  const samples: number[][] = [];
  for (let i = 0; i < 4096; i++) {
    samples.push(buildTraceSample(i));
  }

  const first = collectTraceDownsample(samples);
  const second = collectTraceDownsample(samples);

  assert.equal(first.bins, second.bins);
  assert.deepStrictEqual(first.min, second.min);
  assert.deepStrictEqual(first.max, second.max);
  assert.deepStrictEqual(first.mean, second.mean);
  assert.deepStrictEqual(first.rms, second.rms);
});
