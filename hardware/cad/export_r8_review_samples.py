"""R8 explicit unpowered sample list; STEP/STL/3MF, never slicing or G-code."""
from pathlib import Path
import hashlib
import json
import os
import sys

import cadquery as cq
from v7_product_model import _shape
from export_r7_print_fit import orient_for_print, geometry, _assert_geometry

ROOT = Path(__file__).resolve().parents[2]
MODEL = ROOT / 'output/models/GL30_FULL_R8'
OUT = ROOT / 'output/print/GL30_R8_REVIEW_SAMPLES'
sys.path.insert(0, str(ROOT / 'outputs/r7-dfm-review'))
from verify_r7_meshes import _mesh_record, _compare_meshes, _verify_step

# key, source name, quantity, orientation, manufacture class, Chinese purpose
# Class A: ordinary unpowered printed trial; B: precision/thin metal size proxy;
# C: dummy/optical shape only. All are samples, not loaded hardware acceptance.
SPECS = [
 ('01_housing','housing_104x98_rounded',1,'world','A','外壳；新增后键固定座、底部嵌件孔及内部避让'),
 ('02_bottom','bottom_cover',1,'world','A','底盖；沉头螺钉、导轨背板与电池绑带槽'),
 ('03_press_plate','press_base_plate',1,'local','A','承力架；轴承止口、L转接板和电机张紧平台'),
 ('04_bearing_cartridge','turned_bearing_cartridge',1,'local_top','B','轴承筒；正式件建议车削，塑料只校空间和安装孔'),
 ('05_outer_spacer','bearing_outer_race_spacer_7mm',1,'local','B','7 mm轴承外圈隔套；正式件金属定长'),
 ('06_outer_retainer','bearing_top_retainer_0p6mm',1,'local','B','0.6 mm薄压盖；尺寸样，正式件激光切割/精整'),
 ('07_motor_slider','motor_tension_slide_plate',1,'local_top','A','电机端面安装滑座；3后孔、线缆槽和±0.5张紧长槽'),
 ('08_guide_support','fixed_rail_spine_and_floor_foot',1,'rail_side','A','连续导轨支架；轨道孔、底脚、释放止挡和开关固定座'),
 ('09_screen_tube','fixed_screen_tube_8x6',1,'local','B','Ø8/Ø6×58.7固定屏柱；金属直管端部加工M8×0.5，塑料不承载屏幕'),
 ('10_tube_clamp_left','fixed_screen_clamp_left',1,'screen_clamp_left','A','屏柱分体夹座左半'),
 ('11_tube_clamp_right','fixed_screen_clamp_right',1,'screen_clamp_right','A','屏柱分体夹座右半'),
 ('12_board_tray','PCB_flat_support_tray',1,'world','A','电路板托盘；随A/B右缘和后服务孔让位'),
 ('13_board_post_x2','PCB_support_post_25mm_-39_43',2,'world','B','25 mm板柱；优先购买金属隔柱，两只'),
 ('14_grip','rotating_ring',1,'grip','A','收腰握环；三颗螺钉连接三臂架，顶部整合遮缝唇边'),
 ('15_side_key_x4','button_right_1_cap_and_stem',4,'side_key','A','侧键帽；相同尺寸四只，左右安装方向相反'),
 ('16_rear_key','rear_power_button_cap',1,'power_key','A','后电源键；新增防脱肩'),
 ('17_diffuser_dummy','continuous_low_light_diffuser',1,'local','C','连续光导环；仅检查形状，不代表光学均匀性'),
 ('18_left_carrier','switch_left_pair_removable_carrier',1,'carrier','A','左双键支座；横梁让出0.4 mm键帽行程'),
 ('19_right_carrier','switch_right_pair_removable_carrier',1,'carrier','A','右双键支座；横梁让出0.4 mm键帽行程'),
 ('20_output_shaft','rotary_output_shaft',1,'local','B','Ø12/Ø8.8输出轴；底轴肩、顶部M12×0.75，正式件车削'),
 ('21_spider','rotating_arm_spider',1,'local','A','三臂架；Ø12.1孔，单独可拆，与输出轴间无重复材料'),
 ('22_screen_tray','internal_screen_tray_and_three_spacers',1,'local','B','薄屏幕托盘；双锁环固定，螺钉按实际屏幕孔确认'),
 ('23_rear_switch_holder','rear_power_switch_holder',1,'world','A','后开关座；两颗M2固定到壳体'),
 ('24_board_spacer_x4',None,4,'world','B','13.6 mm板间隔管；优先金属管切段，四只'),
 ('25_press_stop_x3',None,3,'local','B','Ø3×5.15硬止挡；三只，实际行程实测配厚'),
 ('26_inner_top_spacer','upper_shaft_inner_race_spacer',1,'local','B','1.3 mm内圈上隔垫；正式件金属，禁止用软垫调预紧'),
 ('27_board_A_dummy','PCB_A_1p2mm_outline',1,'world','C','A板形状假件；不是已布线/可制板文件'),
 ('28_board_B_dummy','PCB_B_1p2mm_outline',1,'world','C','B板形状假件；不是已布线/可制板文件'),
 ('29_board_C_dummy','PCB_C_24LED_annulus_service_lobe',1,'local','C','C灯板形状假件；无板上器件模型'),
 ('30_battery_dummy','F_custom_3S_battery_complete_pack_74x24x21',1,'world','C','74×24×21无电电池假块；纸板/泡沫也可'),
 ('31_carriage_adapter','carriage_to_plate_L_adapter',1,'local','A','MGN7滑块主安装面L转接件；四横向M2、两底部M2'),
 ('32_floor_backplate','guide_floor_backplate_1p5mm',1,'world','B','1.5 mm导轨底部背板；正式件平板加工'),
 ('33_inner_middle_spacer','bearing_inner_race_spacer_7mm',1,'local','B','7 mm内圈隔套；与外圈隔套独立'),
 ('34_tube_lockring_x2','screen_tube_M8x0p5_lower_lockring',2,'local','B','M8×0.5薄锁环；两只，网格没有螺旋牙，仅尺寸样'),
 ('35_output_locknut','output_M12x0p75_slotted_locknut',1,'local','B','M12×0.75输出锁母；网格没有螺旋牙，正式件加工'),
 ('36_motor_hub','motor_pulley_hub',1,'local','B','电机转子端面转接毂；4原厂孔、定位段及M14×0.75'),
 ('37_motor_locknut','motor_pulley_locknut_M14x0p75',1,'local','B','M14×0.75带轮锁母；仅尺寸样'),
 ('38_switch_leaf','ring_press_compliant_actuator',1,'local','B','0.4 mm弹片；正式件SUS301候选，塑料不模拟弹力'),
]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    report = json.loads((MODEL / 'verification.json').read_text(encoding='utf-8'))
    assert report['all_specified_geometry_checks_pass'], 'R8 geometry checks have not passed'
    for name, expected in report['source_hashes'].items():
        assert digest(ROOT / name) == expected, name
    assembly_path = MODEL / 'GL30_FULL_R8_ASSEMBLY.step'
    assembly = cq.Assembly.load(str(assembly_path))
    parts = {n: _shape(v.obj).located(v.loc) for n, v in assembly.objects.items() if v.obj is not None}
    manifest = {'status':'R8_UNPOWERED_SIZE_SAMPLES', 'units':'mm', 'source_hashes':report['source_hashes'],
                'assembly_sha256':digest(assembly_path), 'models':{}, 'sliced':False,
                'gcode_delivered':False, 'physical_release':False}
    validation = {'models':{}}
    rows=[]
    for key,name,qty,mode,kind,note in SPECS:
        if name is None:
            prefix='PCB_stack_spacer_' if key.startswith('24_') else 'press_stop_'
            found=sorted(n for n in parts if n.startswith(prefix))
            assert len(found)==qty, (prefix, found)
            name=found[0]
        source=parts[name]
        placed,ops=orient_for_print(source,mode)
        geo=geometry(placed)
        _assert_geometry(key,geo,print_ready=True)
        files={}
        for ext in ('stl','3mf','step'):
            path=OUT/kind/ext/(key+'.'+ext)
            path.parent.mkdir(parents=True,exist_ok=True)
            cq.exporters.export(placed,str(path),tolerance=.025,angularTolerance=.08)
            files[ext]={'file':path.relative_to(OUT).as_posix(),'sha256':digest(path),'bytes':path.stat().st_size}
        stl=_mesh_record(OUT/files['stl']['file'],'stl')
        mf=_mesh_record(OUT/files['3mf']['file'],'3mf')
        comparison=_compare_meshes(stl['metrics'],mf['metrics'],geo)
        step=_verify_step(OUT/files['step']['file'],geo)
        assert stl['passed'] and mf['passed'] and step['passed'] and all(v for k,v in comparison.items() if k.endswith(('_match','_zero'))), key
        validation['models'][key]={'stl':stl,'3mf':mf,'step':step,'comparison':comparison,'passed':True}
        manifest['models'][key]={'source_part':name,'quantity':qty,'class':kind,'note':note,'files':files,'cad':geo,'transform_sequence':ops}
        rows.append(f'| {key} | {kind} | {qty} | {note} |')
        print('verified '+key,flush=True)
    validation['all_checks_pass']=True
    manifest['types']=len(manifest['models'])
    manifest['physical_sample_count_if_all_made']=sum(r['quantity'] for r in manifest['models'].values())
    for filename,data in (('geometry_manifest.json',manifest),('mesh_readback.json',validation)):
        (OUT/filename).write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (OUT/'先读_打印数量.md').write_text('# R8 无电试装样件\n\n单位 mm，100% 导入。模型已经摆放至 Z=0；这不证明免支撑、层间强度或配合。自行切片。\n\n'
      '**A：常规打印试装件；B：精密/薄金属件尺寸样，正式装配要金属件或采购件；C：板、电池、光环假件。** 不必把全部样件打印一遍。先做侧键组（15×4、18、19）、导轨按压组（02、03、08、31、32、25×3）与输出轴组（04、05、06、20、21、26、33、35）；共享件只做一次。屏幕先用假件配合。\n\n'
      '不打印电机、导轨、轴承、弹簧、开关、电子器件、紧固件、绑带或带齿。皮带轮STEP是齿系包络，不能据此打印功能同步齿。\n\n'
      '| 文件编号 | 类别 | 整机用量 | 用途 |\n| --- | --- | ---: | --- |\n'+'\n'.join(rows)+'\n',encoding='utf-8')
    print(json.dumps({'out':str(OUT),'types':manifest['types'],'samples_if_all_made':manifest['physical_sample_count_if_all_made'],'readback_pass':True},ensure_ascii=False),flush=True)


if __name__=='__main__':
    try:main();code=0
    except Exception:
        import traceback;traceback.print_exc();code=1
    sys.stdout.flush();sys.stderr.flush();os._exit(code)
