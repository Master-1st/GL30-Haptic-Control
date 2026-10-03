"""Export existing R7.1 geometry omitted from the first 19 fit samples."""
from pathlib import Path
import json, os, sys
import cadquery as cq
import export_r7_print_fit as fit
from v7_product_model import _shape

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'outputs/r7-dfm-review'))
from verify_r7_meshes import _mesh_record, _compare_meshes, _verify_step

MODEL=ROOT/'output/models/GL30_FULL_R7_1'
OUT=ROOT/'output/print/GL30_R7_1_ASSEMBLY_TRIAL/supplementary'

def main():
    verification, source_hash, assembly_hash=fit._load_gates(model_root=MODEL)
    assembly=cq.Assembly.load(str(MODEL/'GL30_FULL_R7_ASSEMBLY.step'))
    parts={n:_shape(v.obj).located(v.loc) for n,v in assembly.objects.items() if v.obj is not None}
    spacers=sorted(n for n in parts if n.startswith('PCB_stack_spacer_'))
    stops=sorted(n for n in parts if n.startswith('press_stop_'))
    assert len(spacers)==4 and len(stops)==3
    specs=[
      ('20_output_shaft','rotary_output_shaft',1,'local','输出空心轴尺寸样；轴承配合、挡圈槽及端部连接未定'),
      ('21_rotor_spider','rotating_arm_spider',1,'local','三臂架尺寸样；与输出轴、旋钮的连接未定'),
      ('22_screen_tray','internal_screen_tray_and_three_spacers',1,'local','固定屏幕托盘；管端固定方式和屏幕螺钉待细化'),
      ('23_rear_switch_holder','rear_power_switch_holder',1,'world','后电源开关座；与外壳固定方式待细化'),
      ('24_PCB_spacer_x4',spacers[0],4,'world','13.6 mm板间隔管尺寸样；金属件可直管定长切割'),
      ('25_press_stop_x3',stops[0],3,'local','3×5.15 mm按压止挡尺寸样；固定及0.35 mm行程实测后确定'),
      ('26_inner_race_washer','upper_shaft_inner_race_spacer',1,'local','0.7 mm内圈隔垫尺寸样；金属垫片厚度与预紧待匹配'),
      ('27_PCB_A_dummy','PCB_A_1p2mm_outline',1,'world','1.2 mm假板；检查轮廓与安装孔，不代表布线成品'),
      ('28_PCB_B_dummy','PCB_B_1p2mm_outline',1,'world','1.2 mm假板；检查轮廓与安装孔，不代表布线成品'),
      ('29_PCB_C_dummy','PCB_C_24LED_annulus_service_lobe',1,'local','1.2 mm灯环假板；检查止口、让位与孔位，不含LED'),
      ('30_battery_dummy','F_custom_3S_battery_complete_pack_74x24x21',1,'world','74×24×21 mm无电占位块；也可用纸板/泡沫代替'),
    ]
    for ext in ('stl','3mf','step'):(OUT/ext).mkdir(parents=True,exist_ok=True)
    manifest={'status':'R7_1_SUPPLEMENTARY_UNPOWERED_SIZE_SAMPLES','model_source_sha256':source_hash,
              'source_assembly_sha256':assembly_hash,'expected_model_count':11,
              'sliced':False,'printer_contacted':False,'gcode_delivered':False,'models':{}}
    review={'source_sha256':source_hash,'models':{},'all_checks_pass':False}
    rows=[]
    for key,name,qty,mode,note in specs:
        source=parts[name]
        fit._assert_matches_verification(name,source,verification)
        placed,ops=fit.orient_for_print(source,mode)
        geo=fit.geometry(placed);fit._assert_geometry(key,geo,print_ready=True)
        files={}
        for ext in ('stl','3mf','step'):
            path=OUT/ext/(key+'.'+ext)
            cq.exporters.export(placed,str(path),tolerance=.025,angularTolerance=.08)
            files[ext]={'file':path.relative_to(OUT).as_posix(),'sha256':fit.digest(path),'bytes':path.stat().st_size}
        stl=_mesh_record(OUT/files['stl']['file'],'stl')
        mf=_mesh_record(OUT/files['3mf']['file'],'3mf')
        compare=_compare_meshes(stl['metrics'],mf['metrics'],geo)
        step=_verify_step(OUT/files['step']['file'],geo)
        comparisons=all(v for k,v in compare.items() if k.endswith(('_match','_zero')))
        assert stl['passed'] and mf['passed'] and comparisons and step['passed'], key
        manifest['models'][key]={'source_part':name,'purpose':note,'print_quantity':qty,'cad':geo,'files':files,'transform_sequence':ops}
        review['models'][key]={'stl':stl,'3mf':mf,'comparison':compare,'step':step,'passed':True}
        dims=' × '.join(f'{d:.2f}' for d in geo['envelope_mm'])
        rows.append(f'| {key} | {qty} | {dims} | {note} |')
    assert fit.digest(fit.SOURCE_FILE)==source_hash
    manifest['all_checks_pass']=True;review['all_checks_pass']=True
    for filename,data in [('geometry_manifest.json',manifest),('mesh_review.json',review)]:
        (OUT/filename).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (OUT/'README_CN.md').write_text('# R7.1 补充分件\n\n补出原整机已有的11种几何，供无电尺寸和空间试装。mm、100%导入；薄金属件及回转件的塑料样不能证明加载、手感或轴承配合。尚未补齐的固定接口在清单中逐项标明。\n\n| 文件 | 数量 | 摆放包络 mm | 用途和限制 |\n| --- | ---: | --- | --- |\n'+'\n'.join(rows)+'\n',encoding='utf-8')
    print('PASS: 11 supplementary models, 33 format files',flush=True)

if __name__=='__main__':
    try:main();code=0
    except Exception:
        import traceback;traceback.print_exc();code=2
    sys.stdout.flush();sys.stderr.flush();os._exit(code)
