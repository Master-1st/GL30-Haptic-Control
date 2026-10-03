"""Check KiCad's exported connectivity against the independent design input.

This is a file-level acceptance check, not an electrical simulation or PCB DRC.
It reads KiCad XML, never the generator's PLACED connectivity dictionary.
"""
import argparse, hashlib, json, re, xml.etree.ElementTree as ET
from pathlib import Path

HERE=Path(__file__).resolve().parent
SOURCE=HERE/'design_input.json'

def verify(netlist,erc,output):
    source=json.loads(SOURCE.read_text(encoding='utf-8'))
    components={c['ref']:dict(c['pins']) for c in source['components'] if c['pins']}
    components['D1']={'1':components['D1']['K'],'2':components['D1']['A']}
    components.update({t['ref']:{'1':t['net']} for t in source['testpoints']})
    root=ET.parse(netlist).getroot()
    actual_refs={c.attrib['ref'] for c in root.findall('./components/comp')}
    errors=[]
    if set(components)!=actual_refs:errors.append({'reference_set_mismatch':{'missing':sorted(set(components)-actual_refs),'extra':sorted(actual_refs-set(components))}})
    actual={};nets={}
    for net in root.findall('./nets/net'):
        name=net.attrib['name'];members=set()
        for node in net.findall('node'):
            ref=node.attrib['ref'];pin=node.attrib['pin']
            if ref.startswith('#'):continue
            k=(ref,pin)
            if k in actual:errors.append({'pin_in_multiple_nets':list(k)})
            actual[k]=name;members.add(k)
        if members:nets[name]=members
    expected={};nc=[]
    for ref,pins in components.items():
        for pin,net in pins.items():
            if net=='NC':nc.append((ref,pin));continue
            expected[(ref,pin)]=net
    for k,net in expected.items():
        if actual.get(k)!=net:errors.append({'endpoint_mismatch':list(k),'expected':net,'actual':actual.get(k)})
    for k in nc:
        name=actual.get(k)
        if name is not None and (not name.startswith('unconnected-') or nets[name]!={k}):errors.append({'nc_has_connection':list(k),'net':name})
    extras=set(actual)-set(expected)-set(nc)
    if extras:errors.append({'unexpected_pins':sorted(extras)})
    checked_footprints=0;pending_footprints=[]
    for comp in root.findall('./components/comp'):
        ref=comp.attrib['ref'];fp=comp.findtext('footprint')
        if not fp:pending_footprints.append(ref);continue
        assert fp.startswith('GL30:'),fp
        path=HERE/'GL30.pretty'/(fp.split(':',1)[1]+'.kicad_mod')
        if not path.is_file():errors.append({'missing_footprint':str(path)});continue
        pads={m.group(1) or m.group(2) for m in re.finditer(r'\(pad\s+(?:"([^"]*)"|([^\s()]+))',path.read_text(encoding='utf-8'))}
        missing=set(components[ref])-pads
        if missing:errors.append({'footprint_missing_physical_pins':ref,'pins':sorted(missing)})
        checked_footprints+=1
    report=json.loads(erc.read_text(encoding='utf-8'))
    violations=[v for s in report['sheets'] for v in s['violations']]
    if violations:errors.append({'erc_violations':violations})
    critical={net:sorted(nets.get(net,set())) for net in ['MOTOR_BUS','3V3','3V3_ESP','HARD_FAULT_N','DRV_NFAULT_N','MOTOR_PWR_EN_MCU','MOTOR_PWR_EN','DRV_NSLEEP','VBUS_DIV11','DRV_CP','DRV_CPH','DRV_CPL','MOTOR_U','MOTOR_V','MOTOR_W']}
    result=dict(status='PASS_EXPORTED_NETLIST_AND_ERC' if not errors else 'FAIL',
        kicad_version=report.get('kicad_version'),schematic_sheets=len(report['sheets']),
        component_count=len(actual_refs),connected_pin_count=len(expected),explicit_nc_pin_count=len(nc),
        named_net_count=len(set(expected.values())),erc_violations=len(violations),
        erc_default_ignored_checks=report.get('ignored_checks'),
        footprints_with_pin_numbers_checked=checked_footprints,
        unassigned_footprints=pending_footprints,
        source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        netlist_sha256=hashlib.sha256(netlist.read_bytes()).hexdigest(),
        critical_nets=critical,errors=errors,
        boundary='No PCB DRC, analog/timing simulation, power sequencing waveform or physical hardware qualification.')
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:result[k] for k in ['status','schematic_sheets','component_count','connected_pin_count','explicit_nc_pin_count','named_net_count','erc_violations','errors']},ensure_ascii=False))
    return 0 if not errors else 1

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--netlist',type=Path,required=True);p.add_argument('--erc',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();raise SystemExit(verify(a.netlist,a.erc,a.output))
