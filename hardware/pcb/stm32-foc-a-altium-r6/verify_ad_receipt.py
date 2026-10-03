"""Compare Altium's actual compiled pin/net output with original design input."""
from collections import Counter, defaultdict
import hashlib
import json
import re
from pathlib import Path

HERE=Path(__file__).resolve().parent
OUT=HERE.parents[2]/'outputs/a-schematic-altium-20260927/verification'
receipt=OUT/'ad_native_receipt_r6.txt'
raw=receipt.read_bytes()
encoding='utf-16' if raw[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig'
lines=raw.decode(encoding).splitlines()
assert 'status=complete' in lines
assert 'revision=A-SCH-AD-R6-USER-LIB-NO-TP' in lines
assert 'project='+str(HERE/'GL30_A_STM32_FOC_NO_TP.PrjPcb') in lines
data=json.loads((HERE.parent/'stm32-foc-a/design_input.json').read_text(encoding='utf-8'))
expected={c['ref']:dict(c['pins']) for c in data['components'] if c['pins']}
expected['D1']={'1':expected['D1']['K'],'2':expected['D1']['A']}
# User-approved R6 replacement of the ten-pin SWD connector.
expected['J4']={'1':'3V3','2':'SWDIO','3':'GND','4':'SWCLK','5':'NRST'}
for p in ('3','4','33'):expected['U1'][p]='NC'
actual={};errors=[];nets=defaultdict(set);sheets={};violations=[];models={}
for line in lines:
    cells=line.split('\t')
    if cells[0]=='PIN':
        _,ref,pin,net,sheet=cells
        key=(ref,pin)
        if key in actual:errors.append({'duplicate_compiled_pin':key})
        actual[key]=net;nets[net].add(key)
    elif cells[0]=='SHEET':sheets[cells[1]]=int(cells[2])
    elif cells[0]=='VIOLATION':
        violations.append(dict(level=int(cells[1]),rule=cells[2],detail=' '.join(cells[3:])))
    elif cells[0]=='MODEL':models[cells[1]]=cells[2]
want_keys={(r,p) for r,c in expected.items() for p in c}
if set(actual)!=want_keys:
    errors.append({'pin_set_difference':{'missing':sorted(want_keys-set(actual)),'extra':sorted(set(actual)-want_keys)}})
for ref,pins in expected.items():
    for pin,want in pins.items():
        key=(ref,pin);got=actual.get(key)
        if want=='NC':
            if got is None or nets[got]!={key}:errors.append({'invalid_nc':key,'actual':got})
        elif got!=want:errors.append({'wrong_native_net':key,'want':want,'got':got})
assert len(sheets)==7 and sum(sheets.values())==146,sheets
assert not any(r.startswith('TP') for r,p in actual)
compile_return=next(line.split('=',1)[1] for line in lines if line.startswith('compile_return='))
levels=Counter(v['level'] for v in violations)
mapping_path=HERE/'user_footprint_mapping.json'
if mapping_path.exists():
    assignments=json.loads(mapping_path.read_text(encoding='utf-8'))['assignments']
    if {r for r,n in models.items() if n}!=set(assignments):
        errors.append({'native_model_reference_set_mismatch':True})
    for ref,entry in assignments.items():
        if models.get(ref)!=entry['footprint']:
            errors.append({'native_footprint_mismatch':ref,'expected':entry['footprint'],'actual':models.get(ref)})
    unresolved=set()
    for violation in violations:
        if violation['rule']=='Missing component models':
            match=re.search(r'Component\s+([A-Z]+\d+)\b',violation['detail'])
            if match:unresolved.add(match.group(1))
            else:errors.append({'unparsed_missing_model_warning':violation['detail']})
    if unresolved!=set(expected)-set(assignments):
        errors.append({'unexpected_unresolved_models':sorted(unresolved),'expected':sorted(set(expected)-set(assignments))})
result=dict(status='PASS_ALTIUM_COMPILED_CONNECTIVITY' if not errors else 'FAIL',
    tool='Altium Designer 26.10.1.5, Workspace Manager DM_Compile',
    receipt_sha256=hashlib.sha256(raw).hexdigest(),compile_return=compile_return,
    component_references=len({r for r,p in actual}),physical_pins=len(actual),
    connected_pins=sum(n!='NC' for c in expected.values() for n in c.values()),
    explicit_nc_pins=sum(n=='NC' for c in expected.values() for n in c.values()),
    named_nets=len({n for c in expected.values() for n in c.values() if n!='NC'}),
    testpoints=0,sheets=sheets,
    linked_footprints={r:n for r,n in models.items() if n},
    erc_errors=sum(n for level,n in levels.items() if level>=2),erc_warnings=levels[1],
    erc_rules=dict(Counter(v['rule'] for v in violations)),violations=violations,errors=errors,
    files_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(HERE.glob('*.SchDoc'))},
    boundary='Native application connectivity/compile evidence. Warnings retained; not PCB or physical acceptance.')
(OUT/'ad_acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k not in ('violations','files_sha256','sheets')},ensure_ascii=False))
raise SystemExit(bool(errors) or result['erc_errors']>0)
