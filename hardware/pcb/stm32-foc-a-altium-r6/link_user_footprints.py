"""Link reviewed user-library footprints without altering circuit geometry."""
from collections import defaultdict
import csv
import hashlib
import json
from pathlib import Path
import shutil

from altium_monkey import AltiumSchDoc, AltiumPcbLib
from altium_monkey.altium_prjpcb import AltiumPrjPcb
from altium_monkey.altium_record_sch__implementation import (
    AltiumSchImplementationList, AltiumSchImplementation,
    AltiumSchMapDefinerList, AltiumSchImplParams)

HERE=Path(__file__).resolve().parent
OUT=HERE.parents[2]/'outputs/a-schematic-altium-20260927/verification'
MAP_PATH=HERE/'user_footprint_mapping.json'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    mapping=json.loads(MAP_PATH.read_text(encoding='utf-8'))
    assignments=mapping['assignments']
    assert assignments
    with (HERE/'BOM_no_testpoints.csv').open(encoding='utf-8-sig',newline='') as f:
        rows=list(csv.DictReader(f))
    byref={r['Reference']:r for r in rows}
    assert set(assignments)<=set(byref)
    expected=json.loads((HERE.parent/'stm32-foc-a/design_input.json').read_text(encoding='utf-8'))
    pinsets={c['ref']:set(c['pins']) for c in expected['components'] if c['pins']}
    pinsets['D1']={'1','2'}
    pinsets['J4']={'1','2','3','4','5'}
    libraries={}
    for entry in assignments.values():
        src=Path(entry['library_path'])
        assert src.is_file() and src.suffix.lower()=='.pcblib'
        if str(src) not in libraries:
            parsed=AltiumPcbLib.from_file(src)
            libraries[str(src)]={'path':src,'sha256':sha(src),'parsed':parsed,
                                 'name':entry['project_library']}
        assert libraries[str(src)]['name']==entry['project_library']
    assert len({v['name'].casefold() for v in libraries.values()})==len(libraries)
    checked=[]
    for ref,entry in assignments.items():
        lib=libraries[entry['library_path']]['parsed']
        candidates=[f for f in lib.footprints if f.name==entry['footprint']]
        assert len(candidates)==1,(ref,entry)
        fp=candidates[0]
        pads={str(p.designator) for p in fp.pads}
        assert pinsets[ref]<=pads,(ref,sorted(pinsets[ref]-pads))
        checked.append({'reference':ref,'footprint':fp.name,'project_library':entry['project_library'],
                        'pins':sorted(pinsets[ref]),'pads':sorted(pads),'basis':entry['basis']})
    for item in libraries.values():
        dest=HERE/item['name']
        assert dest.parent==HERE and dest!=item['path']
        shutil.copy2(item['path'],dest)
        assert sha(dest)==item['sha256']

    # Caller must close these clean generated files in AD before this step.
    receipts=[]
    staging=OUT/'linked_sheets_staging'
    staging.mkdir(exist_ok=True)
    for path in sorted(HERE.glob('A0[1-7]*.SchDoc')):
        doc=AltiumSchDoc(path)
        before_svg=doc.to_svg()
        refs=[]
        for comp in doc.components:
            ref=next(p.text for p in comp.children if getattr(p,'name','')=='Designator')
            if ref not in assignments:continue
            entry=assignments[ref]
            existing=[c for c in comp.children if isinstance(c,AltiumSchImplementationList)]
            if existing:
                models=[m for container in existing for m in container.children
                        if isinstance(m,AltiumSchImplementation)]
                assert len(models)==1,(ref,'multiple existing models')
                if models[0].model_name!=entry['footprint']:
                    assert models[0].model_name==entry.get('replaces_footprint'),(ref,'unreviewed model replacement')
                    record=models[0].serialize_to_record()
                    record.update({'ModelName':entry['footprint'],
                        'ModelDatafile0':entry['project_library'],
                        'ModelDatafileEntity0':entry['footprint']})
                    models[0].parse_from_record(record)
                    comp.footprint=entry['footprint']
                assert models[0].datafile_entity==entry['footprint']
                assert models[0].serialize_to_record().get('ModelDatafile0')==entry['project_library']
                refs.append(ref)
                continue
            comp.footprint=entry['footprint']
            marker=AltiumSchImplementationList();doc.add_object(marker,owner=comp)
            impl=AltiumSchImplementation()
            # Match native user-library records: Datafile0 names the library;
            # DatafileEntity0 names the footprint inside that library.
            impl.parse_from_record({'RECORD':'45','ModelName':entry['footprint'],
                'ModelType':'PCBLIB','IsCurrent':'T','DatafileCount':'1',
                'ModelDatafile0':entry['project_library'],
                'ModelDatafileEntity0':entry['footprint'],'ModelDatafileKind0':'PCBLIB'})
            doc.add_object(impl,owner=marker)
            doc.add_object(AltiumSchMapDefinerList(),owner=impl)
            doc.add_object(AltiumSchImplParams(),owner=impl)
            for p in comp.children:
                if getattr(p,'name','')=='PCB_Model_Status':p.text='USER_LIBRARY_LINKED_PIN_NUMBERS_CHECKED'
            refs.append(ref)
        staged=staging/path.name
        assert doc.save(staged)
        reread=AltiumSchDoc(staged)
        assert reread.to_svg()==before_svg,('visible circuit changed',path.name)
        receipts.append({'file':path.name,'sha256':sha(staged),'linked_instances':refs,'visible_graphics_unchanged':True})
    for item in receipts:shutil.copy2(staging/item['file'],HERE/item['file'])

    project_path=HERE/'GL30_A_STM32_FOC_NO_TP.PrjPcb'
    project=AltiumPrjPcb(project_path)
    known={d['path'] for d in project.documents}
    for name in sorted(v['name'] for v in libraries.values()):
        if name not in known:project.add_document(name)
    project.set_parameter('Revision',mapping['revision'])
    project.save(project_path)
    (OUT/'user_library_paths.txt').write_text('\n'.join(str(HERE/v['name']) for v in libraries.values()),encoding='utf-8')
    for row in rows:
        entry=assignments.get(row['Reference'])
        row['Altium_PCB_Model']=entry['footprint'] if entry else 'Not assigned - needs confirmation'
        row['Altium_PCB_Library']=entry['project_library'] if entry else ''
        row['User_Library_Source']=entry['library_path'] if entry else ''
        row['Model_match_basis']=entry['basis'] if entry else ''
    with (HERE/'BOM_no_testpoints.csv').open('w',encoding='utf-8-sig',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    for item in libraries.values():assert sha(item['path'])==item['sha256'],'User source library changed'
    result={'status':'USER_LIBRARY_LINKS_WRITTEN_PENDING_AD_CHECK','assigned_refs':len(assignments),
            'unassigned_refs':sorted(set(byref)-set(assignments)),
            'source_libraries':[{'path':str(v['path']),'sha256':v['sha256'],'project_copy':v['name'],
                                 'byte_identical':True} for v in libraries.values()],
            'pin_to_pad_checks':checked,'sheets':receipts,
            'boundary':'Reuse user library; pin coverage and byte identity checked, not full land-pattern qualification.'}
    (OUT/'user_library_link_check.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'assigned_refs':len(assignments),'unassigned_refs':result['unassigned_refs'],
                      'libraries':len(libraries),'visible_graphics_unchanged':True},ensure_ascii=False))

if __name__=='__main__':main()
