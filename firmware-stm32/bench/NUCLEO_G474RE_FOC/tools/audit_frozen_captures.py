"""Offline independent audit of frozen bench RAM/MAP/CSV. Never touches hardware.
Reader success means a consistent upload, not successful motor operation.
Only the retained 512 ADC / 2048 encoder samples are waveform evidence.
"""
from pathlib import Path
import argparse,csv,hashlib,json,math,re,statistics,struct
from datetime import datetime,timezone
ROOT=Path(__file__).resolve().parents[4]; BASE=0x20000000

def load(p): return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest().upper()
def audit(build, folder):
    manifest=load(build/'symbol-manifest.json');fw=manifest['Firmware'];tag=fw.split('_',1)[1]
    mp=build/f'NUCLEO_G474RE_FOC_{tag}.map';binary=build/f'NUCLEO_G474RE_FOC_{tag}.bin'
    assert sha(mp)==manifest['MapSHA256'] and sha(binary)==manifest['BinarySHA256'], 'artifact hash mismatch'
    symbols={m[1]:(int(m[2],16),int(m[3])) for m in re.finditer(r'^\s+(g_\S+)\s+(0x[0-9a-fA-F]+)\s+Data\s+(\d+)\s+\S+\.o',mp.read_text(),re.M)}
    raw=(folder/'ram.bin').read_bytes();second=(folder/'ram-after.bin').read_bytes()
    def block(name, data=raw):
        a,n=symbols[name];o=a-BASE;assert 0<=o and o+n<=len(data), f'missing RAM bytes for {name}'
        return data[o:o+n]
    def get(name,fmt):
        v=struct.unpack_from('<'+fmt,block(name));return v[0] if len(v)==1 else list(v)
    for name in ['g_current_capture','g_current_capture_count','g_current_capture_total','g_current_capture_write','g_gate','g_zero','g_zero_sum','g_zero_count','g_trip_captured']:
        assert block(name)==block(name,second), 'frozen data changed: '+name
    reader=load(folder/'validation.json')
    assert reader['Result']=='FROZEN_CAPTURE_READ_PASS' and reader['BinarySHA256']==manifest['BinarySHA256']
    mode,fault,health=get('g_gate','III'); assert mode in (0,1,3,5) and not get('g_preparing','B')
    total=get('g_current_capture_total','I');count=get('g_current_capture_count','H');write=get('g_current_capture_write','H')
    assert count==min(total,512) and write==total%512
    zeros=get('g_zero','3f');zc=get('g_zero_count','I');zs=get('g_zero_sum','3I');zero_sync=get('g_zero_sync_failed','B')
    assert all(math.isfinite(v) for v in zeros)
    zero_matches=bool(zc==512 and all(abs(a/zc-b)<0.00013 for a,b in zip(zs,zeros)))
    rows=[]
    for i in range(count):
        slot=(write-count+i)%512;us,a,b,c,v,t,f=struct.unpack_from('<I6H',block('g_current_capture'),slot*16)
        amps=[(x-z)*3.3/(4095*0.6) for x,z in zip([a,b,c],zeros)]
        rows.append(dict(Index=i,Slot=slot,Us=us,RawA=a,RawB=b,RawC=c,RawVm=v,Timer=t,Flags=f,Sync=f&1,MOE=(f>>1)&1,NFault=(f>>2)&1,CSActive=(f>>3)&1,SPIBusy=(f>>7)&1,Mode=(f>>4)&7,Ia=amps[0],Ib=amps[1],Ic=amps[2],SumA=sum(amps),VmV=v*3.3/4095*(81040/6040)))
    csv_rows=list(csv.DictReader((folder/'samples.csv').open(encoding='utf-8-sig',newline=''))) if count else []
    assert len(csv_rows)==count
    for a,b in zip(rows,csv_rows):
        for k,v in a.items(): assert math.isclose(v,float(b[k]),rel_tol=1e-10,abs_tol=1e-10), f'CSV mismatch {k}'
    gap=[(b['Us']-a['Us'])&0xffffffff for a,b in zip(rows,rows[1:])]
    enc=[];enc_summary=None
    if 'g_enc_capture' in symbols and symbols['g_enc_capture'][0]-BASE+symbols['g_enc_capture'][1]<=len(raw):
        ec=get('g_enc_capture_count','H');et=get('g_enc_capture_total','I');ew=get('g_enc_capture_write','H')
        assert ec==min(et,2048) and ew==et%2048
        if not get('g_enc_capture_running','B'):
            assert block('g_enc_capture')==block('g_enc_capture',second)
            enc=[struct.unpack_from('<IHH',block('g_enc_capture'),((ew-ec+i)%2048)*8) for i in range(ec)]
            eg=[(b[0]-a[0])&0xffffffff for a,b in zip(enc,enc[1:])]
            deltas=[((b[1]-a[1]+8192)%16384-8192)*2*math.pi/16384 for a,b in zip(enc,enc[1:]) if a[1]<16384 and b[1]<16384]
            enc_summary={'Count':ec,'Total':et,'GapUsMin':min(eg,default=None),'GapUsMax':max(eg,default=None),'Invalid':sum(a[1]>=16384 for a in enc),'TailNetRad':sum(deltas),'TailOnly':True}
    trip=None
    if get('g_trip_captured','B'):
        vals=get('g_trip_adc','III4HB3x')
        trip={'Fault':get('g_trip_fault','I'),'Sample':vals[0],'Us':vals[1],'Raw':vals[3:7],'Sync':vals[7],'Outputs':get('g_trip_outputs','I')}
        trip['TailMatches']=bool(rows and rows[-1]['Us']==vals[1] and [rows[-1][k] for k in ['RawA','RawB','RawC','RawVm']]==vals[3:7])
        assert reader['Trip']['Captured']==1 and reader['Trip']['Fault']==trip['Fault']
    else: assert reader['Trip']['Captured']==0 # sample=0 in an uncaptured struct is NOT a fault
    loop_names=['sample','us','entry','mode','observer','stage','acquired','observed','gated','trajectory','foc','total','reason','stop','counter','down']
    timing=dict(zip(loop_names,get('g_loop_timing','16I')))
    n=get('g_align_current_count','I');stage=get('g_align_stage','I')
    align={'Stage':stage,'StaticCount':n,'MeanIdA':get('g_align_current_sum','f')/n if n else None,'MeanVdV':get('g_align_voltage_sum','f')/n if n else None,'Direction':get('g_direction','f'),'ZeroRad':get('g_zero_angle','f'),'OriginRad':get('g_align_origin','f')}
    if stage==2:
        assert n and .28<=align['MeanIdA']<=.52 and .05<=align['MeanVdV']<=.6 and abs(align['Direction'])==1
    approx_iq=[]
    if stage==2 and enc:
        for row in rows:
            if row['Mode']!=4 or not row['MOE']:continue
            previous=[e for e in enc if e[1]<16384 and ((row['Us']-e[0])&0xffffffff)<=750]
            if not previous:continue
            e=min(previous,key=lambda e:(row['Us']-e[0])&0xffffffff)
            theta=align['Direction']*7*(e[1]*2*math.pi/16384-align['ZeroRad'])
            a,b,c=[row[k] for k in ['Ia','Ib','Ic']]
            x=.995832*a-.028199*b-.014988*c;y=.037737*a+1.007723*b-.033757*c;z=.009226*a+.029805*b+1.003268*c
            approx_iq.append(-math.sin(theta)*(2*x-y-z)/3+math.cos(theta)*(y-z)/math.sqrt(3))
    summary={'Count':count,'Total':total,'TailSpanUs':sum(gap),'GapUsMin':min(gap,default=None),'GapUsMax':max(gap,default=None),'NotSynchronized':sum(not r['Sync'] for r in rows),'MaxAbsPhaseA':max((abs(r[k]) for r in rows for k in ['Ia','Ib','Ic']),default=None),'VmMinV':min((r['VmV'] for r in rows),default=None),'VmMaxV':max((r['VmV'] for r in rows),default=None),'MOESamples':sum(r['MOE'] for r in rows),'ActivePhaseLimitExceedances':sum(r['MOE'] and max(abs(r[k]) for k in ['Ia','Ib','Ic'])>(.65 if r['Mode']==2 else .35) for r in rows),'TailOnly':total>count}
    assert reader['Count']==count and reader['Total']==total and reader['Summary']['NotSynchronized']==summary['NotSynchronized']
    if count:assert math.isclose(reader['Summary']['MaxAbsPhaseA'],summary['MaxAbsPhaseA'],abs_tol=1e-10)
    if total and not fault: assert zero_matches and not zero_sync
    approx={'Samples':len(approx_iq),'LastHalfMeanMa':statistics.mean(approx_iq[len(approx_iq)//2:])*1000 if approx_iq else None,'Method':'RAW_ADC + CSA matrix + latest prior encoder; approximate, not bit-exact ISR observer or torque metrology'}
    return {'Folder':str(folder),'EvidenceResult':'INDEPENDENT_RAM_CSV_CONSISTENCY_PASS','FunctionalVerdict':'FAULT_RECORDED' if fault else 'NO_FAULT_IN_FROZEN_CAPTURE','RAMSHA256':sha(folder/'ram.bin'),'Gate':{'Mode':mode,'Fault':fault,'Health':health},'ZeroMatchesSumCount':zero_matches,'ADC':summary,'Encoder':enc_summary,'Alignment':align,'Timing':timing,'FirstTrip':trip,'ApproxIQ':approx}

def main():
    if not __debug__: raise RuntimeError("Do not disable audit validation with python -O")
    ap=argparse.ArgumentParser();ap.add_argument('--build',required=True);ap.add_argument('--out',required=True);ap.add_argument('--captures',required=True);args=ap.parse_args()
    b=(ROOT/args.build).resolve();out=(ROOT/args.out).resolve();assert b.is_relative_to(ROOT) and out.is_relative_to(ROOT) and not out.exists()
    results=[]
    captures=(ROOT/args.captures).resolve();assert captures.is_relative_to(ROOT)
    for p in sorted(captures.rglob('ram-after.bin')):
        try:results.append(audit(b,p.parent))
        except Exception as e:results.append({'Folder':str(p.parent),'EvidenceResult':'FAIL','Error':repr(e)})
    report={'Utc':datetime.now(timezone.utc).isoformat(),'Result':'OFFLINE_AUDIT_PASS' if results and all(r['EvidenceResult']!='FAIL' for r in results) else 'FAIL','Build':str(b),'CaptureCount':len(results),'HardwareAccess':False,'Limitations':['Waveform buffers are tails, not full-duration recordings','No external supply current, torque, thermal or tactile measurements','Fault captures remain failures even when upload consistency passes'],'Captures':results}
    out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='Captures'}));print(json.dumps([r for r in results if r['EvidenceResult']=='FAIL']))
    return 0 if report['Result']=='OFFLINE_AUDIT_PASS' else 1
if __name__=='__main__':raise SystemExit(main())
