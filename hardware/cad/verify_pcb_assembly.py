"""Read back the exported assembly and mechanical parts, without rebuilding."""
import hashlib
import json
import os
from pathlib import Path
import sys
import cadquery as cq

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/models/GL30_PCB_1P2'

def main():
    data=json.loads((OUT/'layout.json').read_text(encoding='utf-8'))
    assert all(data['checks'].values()),data['checks']
    out={'checks':{},'source_hashes':{}}
    for rel,source in data['source_files'].items():
        actual=hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()
        assert actual==source['sha256'],rel
        out['source_hashes'][rel]=actual
    assembly=cq.importers.importStep(str(OUT/'GL30_PCB_1P2_ASSEMBLY.step')).val()
    box=assembly.BoundingBox()
    expected_count=sum(v['solids'] for v in data['parts'].values())
    expected_volume=sum(v['volume_mm3'] for v in data['parts'].values())
    actual_count=len(assembly.Solids());actual_volume=assembly.Volume()
    assert actual_count==expected_count,(actual_count,expected_count)
    assert abs(actual_volume-expected_volume)<max(1e-3,expected_volume*1e-7)
    for axis in 'xyz':
        for extreme in ('min','max'):
            key=axis+extreme
            expected=(min if extreme=='min' else max)(v['bbox_mm'][key] for v in data['parts'].values())
            assert abs(getattr(box,key)-expected)<1e-4,(key,getattr(box,key),expected)
    out['assembly']={'solids':actual_count,'volume_mm3':actual_volume,
                     'bbox_mm':{k:getattr(box,k) for k in ('xlen','ylen','zlen','xmin','xmax','ymin','ymax','zmin','zmax')}}
    out['checks']['assembly_count_volume_bbox_roundtrip']=True
    # Check the full reserved battery growth space, including integral ledges.
    battery_space=cq.Workplane('XY').box(107,26,24,centered=(True,True,False)).translate((0,32,4.5)).val()
    out['exported_parts']={}
    for name in ('housing','bridge','bottom_cover_with_battery_tray'):
        part=cq.importers.importStep(str(OUT/'parts'/f'{name}.step')).val()
        assert part.isValid() and len(part.Solids())==1,name
        overlap=part.intersect(battery_space).Volume()
        assert overlap<1e-5,(name,overlap)
        out['exported_parts'][name]={'valid':True,'single_solid':True,'battery_space_overlap_mm3':overlap}
    out['checks']['new_structure_valid_and_battery_space_clear']=True
    out['checks']['vendor_sources_unchanged']=True
    out['limit']='Official screen geometry validity is inherited and documented. Assembly round-trip checks count, volume and envelope, not a whole-assembly manufacturing approval.'
    (OUT/'assembly-readback.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(out,ensure_ascii=False,indent=2),flush=True)

if __name__=='__main__':
    code=0
    try:main()
    except Exception:
        import traceback;traceback.print_exc();code=1
    sys.stdout.flush();sys.stderr.flush();os._exit(code)
