import { test } from "node:test";
import assert from "node:assert/strict";
import { TimeSyncEstimator } from "@gl30/core";

test("TimeSyncEstimator removes remote processing time from RTT and computes signed offset", () => {
  const estimator = new TimeSyncEstimator();
  const estimate = estimator.addSample({
    localUsSend: 0n,
    localUsRecv: 120n,
    remoteUsReq: 40n,
    remoteUsResp: 75n
  });
  assert.equal(estimate.rttUs, 85n);
  assert.equal(estimate.offsetUs, -2n);
});

test("TimeSyncEstimator rejects invalid time ordering and negative RTT", () => {
  const estimator = new TimeSyncEstimator();
  assert.throws(() => {
    estimator.addSample({
      localUsSend: 40n,
      localUsRecv: 20n,
      remoteUsReq: 75n,
      remoteUsResp: 120n
    });
  });
  assert.throws(() => {
    estimator.addSample({
      localUsSend: 0n,
      localUsRecv: 120n,
      remoteUsReq: 130n,
      remoteUsResp: 100n
    });
  });
  assert.throws(() => {
    estimator.addSample({
      localUsSend: 0n,
      localUsRecv: 120n,
      remoteUsReq: 75n,
      remoteUsResp: 210n
    });
  });
});

test("TimeSyncEstimator keeps precision for > 2^53 bigint inputs after smoothing", () => {
  const estimator = new TimeSyncEstimator();
  const base = 1n << 56n;
  const first = estimator.addSample({
    localUsSend: base + 1n,
    localUsRecv: base + 65n,
    remoteUsReq: base + 130n,
    remoteUsResp: base + 160n
  });
  assert.equal(first.rttUs, 34n);
  assert.equal(first.offsetUs, 112n);

  const second = estimator.addSample({
    localUsSend: base + 129n,
    localUsRecv: base + 257n,
    remoteUsReq: base + 900n,
    remoteUsResp: base + 950n
  });
  assert.equal(second.rttUs, 42n);
  assert.equal(second.offsetUs, 236n);
});
