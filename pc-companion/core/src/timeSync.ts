export interface SyncSample {
  localUsSend: bigint;
  localUsRecv: bigint;
  remoteUsReq: bigint;
  remoteUsResp: bigint;
}

export interface TimeSyncEstimate {
  offsetUs: bigint;
  rttUs: bigint;
}

export class TimeSyncEstimator {
  private static readonly FILTER_ALPHA_DENOMINATOR = 5n;
  private offsetSmoothed: bigint | null = null;
  private rttSmoothed: bigint | null = null;
  private readonly filterAlpha = TimeSyncEstimator.FILTER_ALPHA_DENOMINATOR;

  addSample(sample: SyncSample): TimeSyncEstimate {
    if (sample.localUsRecv < sample.localUsSend) {
      throw new Error("time sync error: local receive time earlier than local send time");
    }
    if (sample.remoteUsResp < sample.remoteUsReq) {
      throw new Error("time sync error: remote response earlier than remote request time");
    }
    const rtt = (sample.localUsRecv - sample.localUsSend) - (sample.remoteUsResp - sample.remoteUsReq);
    if (rtt < 0n) {
      throw new Error("time sync error: negative RTT");
    }
    const offset = (sample.remoteUsReq + sample.remoteUsResp - sample.localUsSend - sample.localUsRecv) / 2n;
    const previousOffset = this.offsetSmoothed;
    const previousRtt = this.rttSmoothed;
    if (previousOffset === null || previousRtt === null) {
      this.offsetSmoothed = offset;
      this.rttSmoothed = rtt;
    } else {
      this.offsetSmoothed = this.lowPass(previousOffset, offset, this.filterAlpha);
      this.rttSmoothed = this.lowPass(previousRtt, rtt, this.filterAlpha);
    }
    return { offsetUs: this.offsetSmoothed, rttUs: this.rttSmoothed };
  }

  estimateRemoteTime(localTimeUs: bigint): bigint {
    if (this.offsetSmoothed === null) return localTimeUs;
    return localTimeUs + this.offsetSmoothed;
  }

  private lowPass(prev: bigint, next: bigint, alpha: bigint): bigint {
    return (prev * (alpha - 1n) + next) / alpha;
  }
}
