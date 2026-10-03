"""Export separate R6 fit samples; no slicer profile, G-code or printer access."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import sys

import cadquery as cq
import hidden_screen_study as core


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'output/models/GL30_FULL_R6'
OUT = ROOT / 'output/print/GL30_R6_PRINT_FIT'

# Only designed single parts enter the print pack. Bearings, motors, battery,
# electronics, metal shafts and optical/paint simulations are not fused in.
MODELS = [
    ('01_housing', 'housing_104x98_rounded', '底座外壳', 1, 'world', '底部开口朝下；内顶面和侧键孔需要支撑，支撑从底部移除'),
    ('02_bottom_cover', 'bottom_cover', '底盖与电池定位座', 1, 'world', '平底贴平台，弹簧座朝上'),
    ('03_load_frame', 'one_piece_press_yoke_and_motor_saddle', '内部承力支架尺寸样', 1, 'local', '承力底板放平；轴承孔保持名义尺寸，打印后测量配合'),
    ('04_motor_slider', 'motor_tension_slide_plate', '电机滑座尺寸样', 1, 'local', '两个脚面贴平台，薄悬臂检查支撑'),
    ('05_rail_support', 'fixed_rail_spine_and_floor_foot', '固定导轨支架尺寸样', 1, 'world', '底脚贴平台；倾斜和悬伸部分检查支撑'),
    ('06_screen_post', 'fixed_hollow_screen_post_and_foot', '隐藏固定屏柱支架尺寸样', 1, 'world', '底脚贴平台；内通道清理后再穿线'),
    ('07_board_support', 'PCB_stack_bridge_and_rear_supports', '电路板支撑架', 1, 'world', '按已摆放方向导入；横桥及悬伸部位检查支撑'),
    ('08_grip_ring', 'rotating_ring', '收腰旋钮握持样', 1, 'grip', '上端面朝下，避免在内侧台阶搭大面积悬空桥'),
    ('09_side_key_x4', 'button_right_1_cap_and_stem', '侧键帽', 4, 'side_key', '外按键面贴平台；左右及前后共四只相同键帽'),
    ('10_power_key', 'rear_power_button_cap', '后电源键帽', 1, 'power_key', '圆形按键面贴平台，顶杆朝上'),
    ('11_light_ring_fit', 'continuous_low_light_diffuser', '光导环尺寸样', 1, 'local', '环面放平；只测安装空间，不代表光学扩散性能'),
    ('12_left_switch_carrier', 'switch_left_pair_removable_carrier', '左侧双键可拆支座', 1, 'carrier', '横梁朝平台，先放键帽再用两颗 M2×4 固定此支座'),
    ('13_right_switch_carrier', 'switch_right_pair_removable_carrier', '右侧双键可拆支座', 1, 'carrier', '横梁朝平台，先放键帽再用两颗 M2×4 固定此支座'),
]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def geometry(shape: cq.Shape) -> dict:
    box = shape.BoundingBox()
    return {'one_solid': len(shape.Solids()) == 1, 'valid': shape.isValid(),
            'positive_volume': shape.Volume() > 1e-6,
            'on_bed': abs(box.zmin) < 2e-5,
            'fits_h2d_single_nozzle_envelope': box.xlen < 325 and box.ylen < 320 and box.zlen < 325,
            'envelope_mm': [box.xlen, box.ylen, box.zlen],
            'bounds_mm': [box.xmin, box.xmax, box.ymin, box.ymax, box.zmin, box.zmax],
            'volume_mm3': shape.Volume()}


def orient_for_print(shape: cq.Shape, mode: str) -> tuple[cq.Shape, list[dict]]:
    operations = []
    if mode in ('local', 'grip'):
        delta = tuple(-value for value in core.CENTER)
        shape = shape.translate(delta).rotate((0, 0, 0), (1, 0, 0), -26)
        operations.extend([{'translate_mm': delta}, {'axis': 'X', 'rotate_deg': -26}])
    if mode == 'grip':
        shape = shape.rotate((0, 0, 0), (1, 0, 0), 180)
        operations.append({'axis': 'X', 'rotate_deg': 180})
    if mode == 'carrier':
        shape = shape.rotate((0, 0, 0), (1, 0, 0), 180)
        operations.append({'axis': 'X', 'rotate_deg': 180})
    if mode == 'side_key':
        shape = shape.rotate((0, 0, 0), (0, 1, 0), 90)
        operations.append({'axis': 'Y', 'rotate_deg': 90})
    if mode == 'power_key':
        shape = shape.rotate((0, 0, 0), (1, 0, 0), -90)
        operations.append({'axis': 'X', 'rotate_deg': -90})
    box = shape.BoundingBox()
    delta = (-(box.xmin + box.xmax)/2, -(box.ymin + box.ymax)/2, -box.zmin)
    shape = shape.translate(delta)
    operations.append({'translate_mm': delta})
    return shape, operations


def main() -> int:
    source_file = ROOT / 'hardware/cad/full_knob_assembly.py'
    verification = json.loads((SOURCE / 'verification.json').read_text(encoding='utf8'))
    assert verification['all_checks_pass'], 'Resolve R6 assembly checks before printing export'
    assert verification['model_source_sha256'] == digest(source_file), 'R6 exports are stale'
    for extension in ('stl', '3mf', 'step'):
        (OUT / extension).mkdir(parents=True, exist_ok=True)
    manifest = {'status': 'R6_SEPARATE_UNPOWERED_FIT_SAMPLES', 'units': 'mm',
                'model_source_sha256': digest(source_file), 'exporter_sha256': digest(Path(__file__)),
                'source_assembly_sha256': digest(SOURCE / 'GL30_FULL_R6_ASSEMBLY.step'),
                'expected_model_count': len(MODELS), 'gcode_delivered': False,
                'material': 'PETG suggested for fit samples; brand and shrinkage not assumed',
                'source_geometry_modified': False, 'models': {}}
    rows = []
    for key, source_name, label, quantity, mode, note in MODELS:
        path = SOURCE / 'parts' / (source_name + '.step')
        original = cq.importers.importStep(str(path)).val()
        assert len(original.Solids()) == 1 and original.isValid(), source_name
        shape, operations = orient_for_print(original, mode)
        checks = geometry(shape)
        assert all(checks[n] for n in ('one_solid', 'valid', 'positive_volume', 'on_bed', 'fits_h2d_single_nozzle_envelope')), (key, checks)
        assert abs(shape.Volume() - original.Volume()) < max(1e-4, original.Volume()*1e-8)
        files = {}
        for extension in ('stl', '3mf', 'step'):
            target = OUT / extension / f'{key}.{extension}'
            cq.exporters.export(shape, str(target), tolerance=.025, angularTolerance=.08)
            files[extension] = {'file': target.relative_to(OUT).as_posix(), 'sha256': digest(target), 'bytes': target.stat().st_size}
        manifest['models'][key] = {'source_part': source_name, 'source_step_sha256': digest(path),
                                   'purpose': label, 'print_quantity': quantity,
                                   'source_cad': geometry(original), 'cad': checks,
                                   'transform_sequence': operations, 'orientation_note': note, 'files': files}
        dims = ' × '.join(f'{value:.2f}' for value in checks['envelope_mm'])
        rows.append(f'| {key} | {label} | {quantity} | {dims} | {note} |')
        print(f'{key}: valid single solid; {dims} mm', flush=True)
    assert digest(source_file) == manifest['model_source_sha256'], 'CAD changed during export'
    (OUT / 'geometry_manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    readme = '\n'.join([
        '# GL30 R6 支架与外壳打印测试包', '',
        '所有文件单位为 **mm**，按 **100%** 导入。STL、3MF、STEP 是同一零件的三种格式，任选一种，勿把三种格式重复打印。3MF 只有几何，没有切片配置或 G-code。', '',
        '**先打印 01 外壳、02 底盖、03 承力支架。** 其余按试装需要打印；09 键帽共四只，其余各一只。08 可单独测试收腰握持感。', '',
        '这是分件的无电尺寸/空间试装包。原本的金属承力架在这里输出塑料尺寸样；电机、同步轮、轴承、滑轨、弹簧、轴和 PCB 均未混入打印网格。实际电机固定接口、精密配合和紧固细节仍未冻结，不能据此完成带电加载测试。', '',
        '| 文件名 | 零件 | 数量 | 已摆放打印包络 mm | 必要说明 |',
        '| --- | --- | ---: | --- | --- |', *rows, '',
        '先在空壳中从内侧放入四个键帽，再装左右双键支座及开关，每侧两颗 M2×4 从底部向上锁紧，之后再装电机与电路板。两条支座各有两个 Ø2.4 通孔，对应壳体 Ø1.7 导孔；螺纹配合按实际打印样件确认。', '',
        '已检查的名义放入路径：键帽先比最终位置低 1 mm，向侧壁靠近至距终位 3 mm，再抬正并推出到位；双键支座先低 3 mm 横移到位，再抬起固定。这样绕开固定灯框，不能在电机已装好后硬推支座。', '',
        '推荐用普通 PETG 试装；沿用你已验证的材料配置。外壳底部保持开放供清理内支撑，切片时检查悬空面和支撑可达性。小孔按名义几何保留，先测实际孔径再修孔，勿强压轴承或硬挤电池。字样在外壳上为 0.25 mm 凹刻，浅灰填色另做。', '',
        '光导尺寸样的两侧名义间隙各 0.1 mm，普通 FDM 不一定直接达到；它只用于试尺寸，最终扩散材料和配合以样件为准。', '',
        '几何和网格检查随包提供，均不替代真实打印、收缩、装配可达性、按压刚度、手感、光学和热验证。', '',
    ])
    (OUT / 'README_CN.md').write_text(readme, encoding='utf8')
    print(OUT, flush=True)
    return 0


if __name__ == '__main__':
    try:
        result = main()
    except Exception:
        import traceback
        traceback.print_exc()
        result = 1
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(result)
