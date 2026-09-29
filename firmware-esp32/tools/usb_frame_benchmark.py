"""Measure completed raster submissions, with quiet capture and a later USB dump.

Uses software ROTATE inputs; it never arms or commands the motor. No per-frame
STATUS polling during capture. Trace times are board monotonic microseconds.
"""
import argparse
import json
from pathlib import Path
import time
import serial
from serial.tools import list_ports

MENU_COUNT=8

def percentiles(values):
    values=sorted(values)
    return {f'p{p}':values[min(len(values)-1,(len(values)*p+99)//100-1)] for p in (50,95,99)} | {'max':max(values)}


def run(port, seconds, runs, folder):
    device=next((p for p in list_ports.comports() if p.device.upper()==port.upper()),None)
    if device is None or (device.vid,device.pid)!=(0x303A,0x1001):
        raise RuntimeError('Select the Espressif USB Serial/JTAG port')
    folder.mkdir(parents=True,exist_ok=True)
    connection=serial.Serial(port=None,baudrate=115200,timeout=.002)
    connection.port=port; connection.dtr=connection.rts=False
    raw=bytearray(); partial=bytearray(); lines=[]
    def send(command): connection.write((command+'\n').encode())
    def read():
        chunk=connection.read(connection.in_waiting or 1)
        raw.extend(chunk); partial.extend(chunk)
        while b'\n' in partial:
            line,_,rest=partial.partition(b'\n'); partial[:]=rest
            lines.append(line.decode('utf-8',errors='replace'))
    def wait_for(prefix,deadline_seconds=8):
        deadline=time.monotonic()+deadline_seconds
        while time.monotonic()<deadline:
            for i,line in enumerate(lines):
                if line.startswith(prefix): return lines.pop(i)[len(prefix):]
            read()
        raise RuntimeError('Timeout waiting for '+prefix)
    def status():
        send('STATUS'); value=json.loads(wait_for('GL30_STATUS '))
        assert not value['motor_armed'] and value['motor_tx']==0, value
        assert value['frame_errors']==value['touch_errors']==value['input_queue_drops']==0,value
        assert value['ui']['display_error']==0,value
        return value
    def delay(seconds):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline: read()
    def click():
        send('BUTTON 1'); delay(.1); send('BUTTON 0'); delay(.5)
    def back():
        for value in (1,0,1,0): send(f'BUTTON {value}'); delay(.08)
        delay(.45)
    results=[]
    connection.open()
    try:
        initial=status()
        if initial['ui']['off']: click(); initial=status()
        while initial['ui']['page']!=0: back(); initial=status()
        click(); menu=status(); assert menu['ui']['page']==1
        # A countdown intentionally keeps running behind the menu. Pause it
        # via ordinary UI input so a DONE notification cannot end this workload.
        if menu['ui']['phase']==1:
            send(f"ROTATE {-menu['ui']['menu']}"); delay(.4); click()
            timer=status()
            if timer['ui']['phase']==1: click()
            assert status()['ui']['phase']!=1
            back(); menu=status(); assert menu['ui']['page']==1
        for run_index in range(runs):
            # A previous trace dump is deliberately outside the measured
            # interval and may receive real touch input. Re-establish the menu
            # before every run instead of assuming post-dump UI state.
            current=status()
            if current['ui']['page']!=1:
                while current['ui']['page']!=0:
                    back(); current=status()
                click(); current=status(); assert current['ui']['page']==1
            # Warm the complete renderer/DMA path before arming trace capture.
            for _ in range(38): send('ROTATE 1'); delay(.08)
            before=status(); send('TRACE START')
            start_us=int(wait_for('GL30_TRACE_START '))
            begin=time.monotonic(); next_rotate=begin; total=0; sent=[]
            while time.monotonic()-begin<seconds:
                now=time.monotonic()
                if now>=next_rotate:
                    step=1 if now-begin<seconds/2 else -1
                    send(f'ROTATE {step}'); total+=step
                    sent.append([round(now-begin,6),step]); next_rotate+=.08
                read()
            send('TRACE STOP'); stop_us=int(wait_for('GL30_TRACE_STOP '))
            after=status()
            # Validate workload state immediately at capture stop. A long USB
            # trace dump can take many seconds; touch/knob input after STOP is
            # outside the measured interval and must not invalidate the trace.
            assert after['ui']['page']==1, 'A different page invalidates a menu capture'
            assert after['ui']['menu']==(before['ui']['menu']+total)%MENU_COUNT
            assert after['touch_presses']==before['touch_presses'], 'Touch input during capture invalidates this run'
            send('TRACE DUMP')
            header=json.loads(wait_for('GL30_TRACE_HEADER '))
            wait_for('GL30_TRACE_END',30)
            assert b'watchdog' not in raw, 'A watchdog event invalidates this run; see raw.log'
            frames=[json.loads(line[len('GL30_FRAME '):]) for line in lines if line.startswith('GL30_FRAME ')]
            lines[:]=[line for line in lines if not line.startswith('GL30_FRAME ')]
            assert header['overflow']==0 and len(frames)==header['count']>1,header
            assert len({f['id'] for f in frames})==len(frames)
            assert len({f['raster_id'] for f in frames})==len(frames)
            expected_strips=frames[0]['strips']
            assert 1<=expected_strips<=466
            for f in frames:
                assert f['success'] and f['bytes']==466*466*2 and f['strips']==expected_strips,f
                assert f['begin_us']<=f['draw_begin_us']<=f['draw_end_us']<=f['submit_us']<=f['start_us']<=f['dma_done_us']<=f['consumed_us'],f
                assert start_us<=f['submit_us'] and f['dma_done_us']<=stop_us,f
            for a,b in zip(frames,frames[1:]):
                assert a['id']<b['id'] and a['raster_id']<b['raster_id'] and a['dma_done_us']<b['dma_done_us']
            delay(.4); settled=status()
            # settled is diagnostic only: post-STOP human input is not part of
            # the captured workload. The next run, if any, re-establishes menu.
            duration=(stop_us-start_us)/1e6
            intervals=[b['dma_done_us']-a['dma_done_us'] for a,b in zip(frames,frames[1:])]
            blocks=[]
            for block in range(int(duration//10)):
                left=start_us+block*10_000_000; right=left+10_000_000
                blocks.append(sum(left<=f['dma_done_us']<right for f in frames)/10)
            summary={
                'run':run_index+1,'seconds':duration,'completed_raster_frames':len(frames),
                # Unique IDs do not prove different pixels when comparison is skipped.
                'pixel_uniqueness_verified':False,
                'metric_schema':after.get('metric_schema',1),
                'frame_interval_ms':after.get('frame_interval_ms'),
                'target_fps':after.get('target_fps'),
                'frame_period_us':after.get('frame_period_us'),
                'frame_deadline_drops_delta':after.get('frame_deadline_drops',0)-before.get('frame_deadline_drops',0),
                'panel_fps':len(frames)/duration,
                'strip_count':expected_strips,
                'interval_fps':(len(frames)-1)*1e6/(frames[-1]['dma_done_us']-frames[0]['dma_done_us']),
                'ten_second_fps':blocks,'completion_interval_us':percentiles(intervals),
                'raster_us':percentiles([f['draw_end_us']-f['draw_begin_us'] for f in frames]),
                'send_wall_us':percentiles([f['dma_done_us']-f['submit_us'] for f in frames]),
                'raster_start_to_dma_us':percentiles([f['dma_done_us']-f['begin_us'] for f in frames]),
                'source_age_at_submit_ms':percentiles([f['submit_us']//1000-f['capture_ms'] for f in frames]),
                'clear_us':percentiles([f['clear_us'] for f in frames]),
                'compare_us':percentiles([f['compare_us'] for f in frames]),
                'copy_us':percentiles([f['copy_us'] for f in frames]),
                'io_submit_us':percentiles([f['io_submit_us'] for f in frames]),
                'wait_us':percentiles([f['wait_us'] for f in frames]),
                'motor_fast_delta':after['motor_rx_fast']-before['motor_rx_fast'],
                'motor_haptic_delta':after['motor_rx_haptic']-before['motor_rx_haptic'],
                'motor_tx':after['motor_tx'],'input_drops':after['input_queue_drops'],
                'frame_errors':after['frame_errors'],'motor_poll_max_us':after['motor_poll_max_us'],
                'internal_free':after['internal_free'],'psram_free':after['psram_free'],
                'stack_free':after['stack_free'],
                'display_stack_free':after['display_stack_free'],
                'dma_largest_free':after['dma_largest_free'],
            }
            if after.get('metric_schema',1)>=2:
                for metric in ('copy_submit_us','copy_wait_us','copy_span_us'):
                    summary[metric]=percentiles([f[metric] for f in frames])
                summary['copy_worker_submit_plus_wait_us']=percentiles([
                    f['copy_submit_us']+f['copy_wait_us'] for f in frames])
                summary['compare_calls_delta']=after['compare_calls']-before['compare_calls']
                summary['skipped_compares_delta']=after['skipped_compares']-before['skipped_compares']
            result=dict(summary=summary,trace_header=header,before=before,after=after,
                        settled=settled,host_rotate_events=sent,frames=frames)
            (folder/f'run-{run_index+1}.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
            results.append(summary); print(json.dumps(summary,ensure_ascii=False),flush=True)
        # Cleanup is outside the measured interval. Post-dump real touch input
        # may have opened an app/settings page, so unwind deterministically
        # instead of assuming one BACK always reaches Home.
        final=status()
        for _ in range(4):
            if final['ui']['page']==0: break
            back(); final=status()
        assert final['ui']['page']==0, final['ui']
    finally:
        connection.close(); (folder/'raw.log').write_bytes(raw)
        (folder/'summary.json').write_text(json.dumps(results,indent=2),encoding='utf-8')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--seconds',type=float,default=60)
    parser.add_argument('--runs',type=int,default=3)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if not 10<=args.seconds<=60: parser.error('Use 10..60 seconds per capture')
    if not 1<=args.runs<=3: parser.error('Use 1..3 independent captures')
    run(args.port,args.seconds,args.runs,args.output)
