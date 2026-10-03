"""Reconstruct connections from saved native SchDoc primitives, not the writer.

Finite graph/file validation only. This does not run Altium's own compiler/ERC.
"""
from collections import defaultdict
import csv
import hashlib
import json
from pathlib import Path

from altium_monkey import AltiumSchDoc
from altium_monkey.altium_prjpcb import AltiumPrjPcb, NetIdentifierScope

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
OUT=ROOT/'outputs/a-schematic-altium-20260927/verification'
data=json.loads((HERE.parent/'stm32-foc-a/design_input.json').read_text(encoding='utf-8'))
expected={c['ref']:dict(c['pins']) for c in data['components'] if c['pins']}
expected['D1']={'1':expected['D1']['K'],'2':expected['D1']['A']}
# User-approved R6 replacement of the ten-pin SWD connector.
expected['J4']={'1':'3V3','2':'SWDIO','3':'GND','4':'SWCLK','5':'NRST'}
# Only externally unused nets disappear with their test points. These pads
# are deliberately NC in the AD deliverable, not forgotten connections.
for pin in ('3','4','33'):expected['U1'][pin]='NC'

def xy(p):
    # Integer native fractional units: 100,000 units per 10 mil. Do not drop
    # fractions, which would miss real pin-to-wire disconnections.
    return p.x*100000+p.x_frac,p.y*100000+p.y_frac

class Connections:
    def __init__(self, points):self.parent={p:p for p in points}
    def root(self,p):
        if self.parent[p]!=p:self.parent[p]=self.root(self.parent[p])
        return self.parent[p]
    def join(self,a,b):self.parent[self.root(a)]=self.root(b)

errors=[];actual={};nc=[];records=[];references=set();all_ids=set();net_members=defaultdict(list)
files=sorted(HERE.glob('A0[1-7]*.SchDoc'))
project_path=HERE/'GL30_A_STM32_FOC_NO_TP.PrjPcb'
project=AltiumPrjPcb(project_path)
assert project.net_identifier_scope==NetIdentifierScope.GLOBAL
assert {d['path'] for d in project.documents if d['path'].lower().endswith('.schdoc')}=={p.name for p in files}
assert all(Path(d['path']).suffix.lower() in ('.schdoc','.pcblib') for d in project.documents)
assert all((HERE/d['path']).is_file() for d in project.documents)
for path in files:
    raw=path.read_bytes()
    assert raw[:8]==bytes.fromhex('d0cf11e0a1b11ae1'),path
    d=AltiumSchDoc(path)
    endpoints={};wire_segments=[];points=set();labels=[]
    for c in d.components:
        ds=[p.text for p in c.children if getattr(p,'name','')=='Designator']
        assert len(ds)==1,(path,ds)
        ref=ds[0]; references.add(ref)
        assert not ref.startswith('TP'),ref
        for pin in c.pins:
            ep=(ref,pin.designator);pos=xy(pin.get_hot_spot())
            if ep in endpoints:errors.append({'duplicate_pin_on_sheet':ep})
            endpoints[ep]=pos;points.add(pos)
    for w in d.wires:
        ps=[xy(p) for p in w.points];points.update(ps)
        wire_segments.extend(zip(ps,ps[1:]))
    for lab in d.net_labels:
        pos=xy(lab.location);labels.append((pos,lab.text));points.add(pos)
    for j in d.junctions:points.add(xy(j.location))
    ncpoints={xy(n.location) for n in d.no_ercs}
    points.update(ncpoints)
    graph=Connections(points)
    for a,b in wire_segments:
        assert a[0]==b[0] or a[1]==b[1],('nonorthogonal',a,b)
        for p in points:
            if ((a[0]==b[0]==p[0] and min(a[1],b[1])<=p[1]<=max(a[1],b[1])) or
                (a[1]==b[1]==p[1] and min(a[0],b[0])<=p[0]<=max(a[0],b[0]))):
                graph.join(a,p)
        graph.join(a,b)
    names=defaultdict(set)
    for pos,name in labels:names[graph.root(pos)].add(name)
    clusters=defaultdict(set)
    for ep,pos in endpoints.items():clusters[graph.root(pos)].add(ep)
    for ep,pos in endpoints.items():
        got=names[graph.root(pos)];want=expected.get(ep[0],{}).get(ep[1])
        if ep in actual:errors.append({'duplicate_physical_pin':ep})
        actual[ep]=sorted(got)
        if want=='NC':
            nc.append(ep)
            if got or pos not in ncpoints or clusters[graph.root(pos)]!={ep}:
                errors.append({'invalid_explicit_nc':ep,'nets':sorted(got)})
        elif got!={want}:errors.append({'endpoint_mismatch':ep,'expected':want,'actual':sorted(got)})
        else:net_members[want].append(ep)
    for obj in d.objects:
        uid=getattr(obj,'unique_id',None)
        if uid:
            if uid in all_ids:errors.append({'duplicate_unique_id':uid})
            all_ids.add(uid)
    records.append(dict(file=path.name,sha256=hashlib.sha256(raw).hexdigest(),components=len(d.components),
                        pins=len(endpoints),wires=len(d.wires),net_labels=len(d.net_labels),explicit_nc=len(ncpoints)))

expected_pins={(r,p) for r,c in expected.items() for p in c}
if set(actual)!=expected_pins:errors.append({'pin_set_difference':{'missing':sorted(expected_pins-set(actual)),'extra':sorted(set(actual)-expected_pins)}})
if references!=set(expected):errors.append({'component_set_difference':{'missing':sorted(set(expected)-references),'extra':sorted(references-set(expected))}})
assert len(files)==7
with (HERE/'BOM_no_testpoints.csv').open(encoding='utf-8-sig',newline='') as f:bom=list(csv.DictReader(f))
assert {r['Reference'] for r in bom}==references
result=dict(status='PASS_NATIVE_CONNECTIVITY_NO_TESTPOINTS' if not errors else 'FAIL',
    sheets=len(files),component_references=len(references),connected_pins=len(actual)-len(nc),
    physical_pins=len(actual),explicit_nc_pins=len(nc),named_nets=len(net_members),testpoints=0,
    checks=['native project reopened with seven relative sheet paths and GLOBAL net scope',
            'native OLE files reopened','physical pins reconstructed from actual saved coordinates and wires',
            'global net labels compared to independent original design input','BOM reference set matches native sheets'],
    project_sha256=hashlib.sha256(project_path.read_bytes()).hexdigest(),records=records,errors=errors,
    boundary='Independent file-level graph check, not Altium application compilation/ERC, PCB DRC or hardware acceptance')
(OUT/'connectivity.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'native_netlist.json').write_text(json.dumps(dict(sorted(net_members.items())),ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:result[k] for k in ['status','sheets','component_references','physical_pins','connected_pins','explicit_nc_pins','named_nets','testpoints','errors']},ensure_ascii=False))
raise SystemExit(bool(errors))
