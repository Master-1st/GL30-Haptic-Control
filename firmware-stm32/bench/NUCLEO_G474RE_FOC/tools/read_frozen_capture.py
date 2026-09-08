"""Read frozen HAPTIC25 ADC history using SWD HOTPLUG; never arm or halt."""
from pathlib import Path
from datetime import datetime, timezone
import argparse, csv, hashlib, json, re, struct, subprocess

ROOT = Path(__file__).resolve().parents[4]
BUILD = None
CLI = None
BASE = 0x20000000

def load_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def upload(out, label, address, size):
    dest = out / (label + '.bin')
    assert not dest.exists(), 'Do not overwrite evidence'
    p = subprocess.run([str(CLI), '-c', 'port=SWD', 'mode=HOTPLUG', 'freq=1000',
                        '-u', hex(address), str(size), str(dest)],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=40)
    (out / (label + '.log')).write_bytes(p.stdout)
    if p.returncode != 0 or not dest.exists() or dest.stat().st_size != size:
        raise RuntimeError('SWD upload failed: ' + label)
    return dest.read_bytes()

def decode_rows(raw, write, count, zeros):
    assert 0 <= write < 512 and 0 <= count <= 512
    rows = []
    for index in range(count):
        slot = (write - count + index) % 512
        us, a, b, c, vm, timer, flags = struct.unpack_from('<I6H', raw, slot * 16)
        amps = [(value-zero)*3.3/4095/0.6 for value, zero in zip((a,b,c), zeros)]
        rows.append(dict(Index=index, Slot=slot, Us=us, RawA=a, RawB=b, RawC=c,
                         RawVm=vm, Timer=timer, Flags=flags, Sync=flags & 1,
                         MOE=(flags >> 1) & 1, NFault=(flags >> 2) & 1, CSActive=(flags >> 3) & 1, SPIBusy=(flags >> 7) & 1,
                         Mode=(flags >> 4) & 7, Ia=amps[0], Ib=amps[1], Ic=amps[2],
                         SumA=sum(amps), VmV=vm*3.3/4095*(75000+6040)/6040))
    return rows

def main():
    if not __debug__: raise RuntimeError("Do not disable capture validation with python -O")
    global BUILD, CLI
    ap=argparse.ArgumentParser(); ap.add_argument('--out',required=True)
    ap.add_argument('--build-dir',required=True); ap.add_argument('--programmer',required=True)
    args=ap.parse_args(); BUILD=Path(args.build_dir).resolve(); CLI=Path(args.programmer).resolve()
    out=Path(args.out).resolve()
    assert out.is_relative_to(ROOT) and not out.exists(), 'New workspace evidence directory required'
    out.mkdir(parents=True)
    result={'Utc':datetime.now(timezone.utc).isoformat(), 'Result':'INCOMPLETE',
            'DebugMode':'HOTPLUG upload only; no halt/reset/register write', 'OutputCommandSent':False}
    try:
        manifest=load_json(BUILD/'symbol-manifest.json')
        flash=load_json(BUILD/'hardware/flash-validation.json')
        assert flash['Result']=='PASS' and flash['Firmware']==manifest['Firmware']
        assert flash['ReadbackSHA256']==manifest['BinarySHA256']
        mp=BUILD/'NUCLEO_G474RE_FOC_HAPTIC25.map'
        assert hashlib.sha256(mp.read_bytes()).hexdigest().upper()==manifest['MapSHA256']
        symbols={m[1]:(int(m[2],16),int(m[3])) for m in re.finditer(
            r'^\s+(g_\S+)\s+(0x[0-9a-fA-F]+)\s+Data\s+(\d+)\s+\S+\.o', mp.read_text(), re.M)}
        assert symbols['g_current_capture'][1]==8192
        tim=upload(out,'tim1-before',0x40012c20,40)
        gpio={x:upload(out,'gpio'+x+'-before',a,8) for x,a in [('a',0x48000010),('b',0x48000410),('c',0x48000810)]}
        ccer=struct.unpack_from('<I',tim,0)[0]; bdtr=struct.unpack_from('<I',tim,36)[0]
        inputs_low=all((struct.unpack_from('<I',gpio[x],offset)[0]&mask)==0
                       for x,mask in [('a',0x700),('b',0xe000)] for offset in (0,4))
        pins={'MOE':(bdtr>>15)&1,'ChannelEnables':ccer&0x555,'SixPWMInputsLow':inputs_low,
              'DrvOff':(struct.unpack_from('<I',gpio['c'],0)[0]>>8)&1}
        result['PinsBefore']=pins
        assert not pins['MOE'] and not pins['ChannelEnables'] and inputs_low, 'Hardware must already be output-off'
        size=max(a+n for a,n in symbols.values() if BASE <= a < BASE+65536)-BASE
        assert 0 < size <= 65536
        ram=upload(out,'ram',BASE,size)
        def get(name, fmt):
            address,n=symbols[name]; assert struct.calcsize(fmt)<=n
            values=struct.unpack_from(fmt,ram,address-BASE)
            return values[0] if len(values)==1 else list(values)
        mode,faults,health=struct.unpack_from('<III',ram,symbols['g_gate'][0]-BASE)
        assert mode in (0,1,3,5) and not get('g_preparing','<B'), 'Capture must be frozen'
        write=get('g_current_capture_write','<H'); count=get('g_current_capture_count','<H')
        total=get('g_current_capture_total','<I')
        assert count==min(total,512) and write==total%512
        zero_count=get('g_zero_count','<I')
        zeros=get('g_zero','<3f')
        result.update(Firmware=manifest['Firmware'],BinarySHA256=manifest['BinarySHA256'],MapSHA256=manifest['MapSHA256'],
                      Symbols={k:dict(Address=hex(v[0]),Bytes=v[1]) for k,v in symbols.items() if k.startswith(('g_current_capture','g_zero','g_trip')) or k=='g_gate'},
                      Gate={'Mode':mode,'Faults':faults,'Health':health},Write=write,Count=count,Total=total,
                      IdleOutlierTriggered=get('g_current_capture_idle_trip','<B'),Zero= zeros,ZeroCount=zero_count,ZeroMin=[get('g_zero_min.'+str(i),'<H') for i in range(3)],
                      ZeroMax=get('g_zero_max','<3H'),ZeroSum=get('g_zero_sum','<3I'),
                      ZeroSyncFailed=get('g_zero_sync_failed','<B'),ZeroAdcBadStart=get('g_zero_adc_bad_start','<I'))
        offset=symbols['g_current_capture'][0]-BASE
        history=ram[offset:offset+8192]
        ram2=upload(out,'ram-after',BASE,size)
        for name in ('g_current_capture','g_current_capture_write','g_current_capture_count','g_current_capture_total','g_gate'):
            a,n=symbols[name]; assert ram[a-BASE:a-BASE+n]==ram2[a-BASE:a-BASE+n], 'Frozen state changed: '+name
        rows=decode_rows(history,write,count,zeros)
        if rows:
            with (out/'samples.csv').open('w',newline='',encoding='utf-8') as f:
                w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
        trip_capture=get('g_trip_captured','<B')
        trip_values=get('g_trip_adc','<III4HB3x')
        trip=dict(Captured=trip_capture,Fault=get('g_trip_fault','<I'),Sample=trip_values[0],Us=trip_values[1],
                  Bad=trip_values[2],Raw=trip_values[3:7],Sync=trip_values[7],Zero=get('g_trip_zero','<3f'),
                  Outputs=get('g_trip_outputs','<I'),MoveRad=get('g_trip_align_move','<f'),
                  VelocityRadS=get('g_trip_velocity','<f'),FieldRad=get('g_trip_field','<f'))
        if rows and trip_capture:
            last=rows[-1];trip['MatchesLastSample']=last['Us']==trip['Us'] and [last['RawA'],last['RawB'],last['RawC'],last['RawVm']]==trip['Raw']
            if trip['Fault']==16: assert trip['MatchesLastSample'], 'Current trip frame absent from history'
        result['Trip']=trip
        gaps=[(b['Us']-a['Us'])&0xffffffff for a,b in zip(rows,rows[1:])]
        result['Summary']={'GapUsMin':min(gaps) if gaps else None,'GapUsMax':max(gaps) if gaps else None,
            'NotSynchronized':sum(not row['Sync'] for row in rows),'MaxAbsPhaseA':max((abs(row[k]) for row in rows for k in ('Ia','Ib','Ic')),default=0),
            'MaxAbsSumA':max((abs(row['SumA']) for row in rows),default=0),'AbsSumOver0_1A':sum(abs(row['SumA'])>0.1 for row in rows),
            'First':rows[0] if rows else None,'Last':rows[-1] if rows else None}
        result['Result']='FROZEN_CAPTURE_READ_PASS'
    except Exception as e:
        result['Result']='FAIL'; result['Error']=repr(e); raise
    finally:
        result['CompletedUtc']=datetime.now(timezone.utc).isoformat()
        (out/'validation.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({k:result[k] for k in ('Result','Count','Total','Gate','Summary','Trip')},indent=2))

if __name__=='__main__': main()
