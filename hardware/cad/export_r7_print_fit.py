"""Export the explicit R7 cost-down fit-sample whitelist.

The script only exports geometry after the R7 verification report and source
hash agree.  It does not slice, create G-code, contact a printer, or claim
that a mesh is printable without checking the exported files separately.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

import cadquery as cq
import hidden_screen_study as core


ROOT = Path(__file__).resolve().parents[2]
SOURCE_FILE = ROOT / "hardware/cad/full_knob_assembly.py"
MODEL_ROOT = ROOT / "output/models/GL30_FULL_R7"
PARTS_ROOT = MODEL_ROOT / "parts"
VERIFICATION_FILE = MODEL_ROOT / "verification.json"
ASSEMBLY_FILE = MODEL_ROOT / "GL30_FULL_R7_ASSEMBLY.step"
DEFAULT_OUT = ROOT / "output/print/GL30_R7_COST_DOWN"

MAX_ENVELOPE_MM = (325.0, 320.0, 325.0)
VOLUME_TOLERANCE = 1.0e-8


# key, source STEP stem, Chinese label, quantity, orientation mode, note
# The list is intentionally explicit.  A later CAD change must update this
# whitelist instead of silently exporting every named PARTS entry.
MODELS = (
    ("01_housing", "housing_104x98_rounded", "外壳", 1, "world", "底部开口朝下；侧键孔和光框接口随壳体保留，支撑可达性需切片确认"),
    ("02_bottom_cover", "bottom_cover", "底盖", 1, "world", "平底贴平台，弹簧座和电池定位结构朝上"),
    ("03_press_base", "press_base_plate", "平底承力架尺寸样", 1, "local", "局部承力面朝下；三颗 M3、Ø18.2 定位孔和轴承接口仅作无电试装"),
    ("04_bearing_cartridge", "turned_bearing_cartridge", "轴承筒尺寸样", 1, "local_top", "大端法兰贴平台，减少外侧悬空；内孔台阶仍检查局部支撑，止口 Ø18×1.2"),
    ("05_bearing_spacer", "bearing_outer_race_spacer_7mm", "7 mm 外圈间隔环", 1, "local", "环面放平，作为外圈间隔尺寸样"),
    ("06_bearing_retainer", "bearing_top_retainer_0p6mm", "顶部薄压环", 1, "local", "薄环局部轴向打印；孔位只作无电装配检查"),
    ("07_motor_slider", "motor_tension_slide_plate", "电机滑座", 1, "local_top", "局部上端面朝下，腿部向上；脚端小悬挑需检查，定子接口未冻结"),
    ("08_rail_support", "fixed_rail_spine_and_floor_foot", "固定导轨支架", 1, "rail_side", "世界 Y+90；原 +X 面朝床，定位面朝上，残留下悬需切片确认"),
    ("09_screen_tube", "fixed_screen_tube_8x6", "8×6×57.1 屏柱尺寸样", 1, "local", "优先购买/截取标准金属直管；STL/3MF/STEP 只作尺寸样"),
    ("10_screen_clamp_left", "fixed_screen_clamp_left", "左屏柱夹座", 1, "screen_clamp_left", "世界 Y-90；左侧外平面朝床，底部 M2 与横向 M3 接口保留"),
    ("11_screen_clamp_right", "fixed_screen_clamp_right", "右屏柱夹座", 1, "screen_clamp_right", "世界 Y+90；右侧外平面朝床，底部 M2 与横向 M3 接口保留"),
    ("12_PCB_tray", "PCB_flat_support_tray", "PCB 平托盘", 1, "world", "2.2 mm 平托盘，板孔/板间距基准不改，平面贴平台"),
    ("13_PCB_post_x2", "PCB_support_post_25mm_-39_43", "25 mm PCB 直隔柱", 2, "world", "优先购买标准金属隔柱；网格仅作尺寸样，数量两只"),
    ("14_grip_ring", "rotating_ring", "收腰旋钮握持样", 1, "grip", "局部轴向后翻 180°，环面放平，不能据此宣称触感或疲劳通过"),
    ("15_side_key_x4", "button_right_1_cap_and_stem", "侧键帽", 4, "side_key", "外按键面贴平台；同一尺寸样按四只计，侧键接口位置不改"),
    ("16_power_key", "rear_power_button_cap", "后电源键帽", 1, "power_key", "圆形按键面贴平台，顶杆朝上"),
    ("17_light_ring_fit", "continuous_low_light_diffuser", "光导环尺寸样", 1, "local", "局部轴向放平；只检查光框安装空间，不代表光学性能"),
    ("18_left_switch_carrier", "switch_left_pair_removable_carrier", "左侧双键可拆支座", 1, "carrier", "横梁朝平台；先装键帽，再按既定 M2 接口固定"),
    ("19_right_switch_carrier", "switch_right_pair_removable_carrier", "右侧双键可拆支座", 1, "carrier", "横梁朝平台；先装键帽，再按既定 M2 接口固定"),
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def geometry(shape: cq.Shape) -> dict[str, object]:
    box = shape.BoundingBox()
    envelope = [float(box.xlen), float(box.ylen), float(box.zlen)]
    bounds = [
        float(box.xmin),
        float(box.xmax),
        float(box.ymin),
        float(box.ymax),
        float(box.zmin),
        float(box.zmax),
    ]
    return {
        "one_solid": len(shape.Solids()) == 1,
        "valid": bool(shape.isValid()),
        "positive_volume": float(shape.Volume()) > 1.0e-6,
        "on_bed": abs(float(box.zmin)) < 2.0e-5,
        "fits_single_nozzle_envelope": all(
            size < limit for size, limit in zip(envelope, MAX_ENVELOPE_MM)
        ),
        "envelope_mm": envelope,
        "bounds_mm": bounds,
        "volume_mm3": float(shape.Volume()),
    }


def _assert_geometry(name: str, checks: dict[str, object], *, print_ready: bool) -> None:
    required = ["one_solid", "valid", "positive_volume", "fits_single_nozzle_envelope"]
    if print_ready:
        required.append("on_bed")
    failed = [key for key in required if checks.get(key) is not True]
    if failed:
        raise RuntimeError(f"{name}: geometry checks failed: {failed}; {checks}")


def orient_for_print(shape: cq.Shape, mode: str) -> tuple[cq.Shape, list[dict[str, object]]]:
    """Apply only deterministic placement rotations, then center XY and set Z=0."""
    operations: list[dict[str, object]] = []
    if mode in ("local", "grip", "local_top"):
        delta = tuple(-float(value) for value in core.CENTER)
        shape = shape.translate(delta).rotate((0, 0, 0), (1, 0, 0), -26)
        operations.extend([{"translate_mm": list(delta)}, {"axis": "X", "rotate_deg": -26.0}])
    if mode in ("grip", "local_top"):
        shape = shape.rotate((0, 0, 0), (1, 0, 0), 180)
        operations.append({"axis": "X", "rotate_deg": 180.0})
    if mode == "carrier":
        shape = shape.rotate((0, 0, 0), (1, 0, 0), 180)
        operations.append({"axis": "X", "rotate_deg": 180.0})
    if mode == "side_key":
        shape = shape.rotate((0, 0, 0), (0, 1, 0), 90)
        operations.append({"axis": "Y", "rotate_deg": 90.0})
    if mode == "power_key":
        shape = shape.rotate((0, 0, 0), (1, 0, 0), -90)
        operations.append({"axis": "X", "rotate_deg": -90.0})
    if mode == "rail_side":
        # +X -> -Z under world Y+90, so the original +X face is on the bed.
        shape = shape.rotate((0, 0, 0), (0, 1, 0), 90)
        operations.append({"axis": "Y", "rotate_deg": 90.0, "purpose": "原 +X 面朝床"})
    if mode == "screen_clamp_left":
        shape = shape.rotate((0, 0, 0), (0, 1, 0), -90)
        operations.append({"axis": "Y", "rotate_deg": -90.0, "purpose": "左侧外平面朝床"})
    if mode == "screen_clamp_right":
        shape = shape.rotate((0, 0, 0), (0, 1, 0), 90)
        operations.append({"axis": "Y", "rotate_deg": 90.0, "purpose": "右侧外平面朝床"})
    if mode not in {
        "world",
        "local",
        "local_top",
        "grip",
        "carrier",
        "side_key",
        "power_key",
        "rail_side",
        "screen_clamp_left",
        "screen_clamp_right",
    }:
        raise ValueError(f"unknown print orientation mode: {mode}")

    box = shape.BoundingBox()
    delta = (
        -float(box.xmin + box.xmax) / 2.0,
        -float(box.ymin + box.ymax) / 2.0,
        -float(box.zmin),
    )
    shape = shape.translate(delta)
    operations.append({"translate_mm": list(delta), "purpose": "XY 居中并将最低点置于 Z=0"})
    return shape, operations


def _close(a: float, b: float, tolerance: float = 1.0e-4) -> bool:
    return abs(a - b) <= tolerance


def _assert_matches_verification(
    source_name: str, shape: cq.Shape, verification: dict[str, object]
) -> None:
    expected = verification.get("parts", {}).get(source_name)
    if not isinstance(expected, dict):
        raise RuntimeError(f"{source_name}: missing part record in verification.json")
    actual = geometry(shape)
    if expected.get("valid") is not True or expected.get("solids") != 1:
        raise RuntimeError(f"{source_name}: verification record is not a valid single solid")
    if not _close(float(expected["volume_mm3"]), float(actual["volume_mm3"]), 1.0e-3):
        raise RuntimeError(f"{source_name}: STEP volume differs from verification.json")
    expected_box = expected.get("bbox", {})
    for key, actual_value in (
        ("xmin", actual["bounds_mm"][0]),
        ("xmax", actual["bounds_mm"][1]),
        ("ymin", actual["bounds_mm"][2]),
        ("ymax", actual["bounds_mm"][3]),
        ("zmin", actual["bounds_mm"][4]),
        ("zmax", actual["bounds_mm"][5]),
    ):
        if not _close(float(expected_box[key]), float(actual_value)):
            raise RuntimeError(f"{source_name}: STEP bbox differs from verification.json at {key}")


def _load_gates(
    source_file: Path = SOURCE_FILE,
    model_root: Path = MODEL_ROOT,
) -> tuple[dict[str, object], str, str]:
    if not source_file.is_file():
        raise FileNotFoundError(source_file)
    verification_file = model_root / "verification.json"
    assembly_file = model_root / "GL30_FULL_R7_ASSEMBLY.step"
    if not verification_file.is_file():
        raise FileNotFoundError(verification_file)
    if not assembly_file.is_file():
        raise FileNotFoundError(assembly_file)
    verification = json.loads(verification_file.read_text(encoding="utf-8"))
    source_hash = digest(source_file)
    expected_hash = verification.get("model_source_sha256")
    if verification.get("all_checks_pass") is not True:
        raise RuntimeError("R7 verification.json all_checks_pass is false; export is blocked")
    if source_hash != expected_hash:
        raise RuntimeError(
            "R7 source hash mismatch: regenerate verification and parts before export"
        )
    return verification, source_hash, digest(assembly_file)


def export_pack(
    *,
    source_file: Path = SOURCE_FILE,
    model_root: Path = MODEL_ROOT,
    output_dir: Path = DEFAULT_OUT,
) -> dict[str, object]:
    """Export the 19-model pack after all hard gates pass."""
    verification, source_hash, assembly_hash = _load_gates(source_file, model_root)
    parts_root = model_root / "parts"
    if len(MODELS) != 19 or len({row[0] for row in MODELS}) != 19:
        raise RuntimeError("R7 whitelist must contain exactly 19 unique model keys")
    if len({row[1] for row in MODELS}) != 19:
        raise RuntimeError("R7 whitelist must contain exactly 19 unique source parts")

    specs: list[dict[str, object]] = []
    output_dir = output_dir.resolve()
    for key, source_name, label, quantity, mode, note in MODELS:
        source_path = parts_root / f"{source_name}.step"
        if not source_path.is_file():
            raise FileNotFoundError(source_path)
        original = cq.importers.importStep(str(source_path)).val()
        source_checks = geometry(original)
        _assert_geometry(source_name, source_checks, print_ready=False)
        _assert_matches_verification(source_name, original, verification)
        printable, operations = orient_for_print(original, mode)
        print_checks = geometry(printable)
        _assert_geometry(key, print_checks, print_ready=True)
        if abs(float(printable.Volume()) - float(original.Volume())) > max(
            1.0e-4, float(original.Volume()) * VOLUME_TOLERANCE
        ):
            raise RuntimeError(f"{key}: orientation changed volume")
        specs.append(
            {
                "key": key,
                "source_name": source_name,
                "source_path": source_path,
                "source_shape": original,
                "print_shape": printable,
                "source_checks": source_checks,
                "print_checks": print_checks,
                "quantity": quantity,
                "label": label,
                "mode": mode,
                "operations": operations,
                "note": note,
            }
        )

    # No output directory is created until every source-side gate above passes.
    for extension in ("stl", "3mf", "step"):
        (output_dir / extension).mkdir(parents=True, exist_ok=True)
    manifest: dict[str, object] = {
        "status": "R7_COST_DOWN_UNPOWERED_FIT_SAMPLES",
        "units": "mm",
        "model_source_sha256": source_hash,
        "exporter_sha256": digest(Path(__file__)),
        "source_assembly_sha256": assembly_hash,
        "expected_model_count": 19,
        "physical_item_count": sum(int(row[3]) for row in MODELS),
        "gcode_delivered": False,
        "sliced": False,
        "printer_contacted": False,
        "source_geometry_modified": False,
        "models": {},
    }
    rows: list[str] = []
    for spec in specs:
        key = str(spec["key"])
        source_name = str(spec["source_name"])
        source_path = spec["source_path"]
        printable = spec["print_shape"]
        files: dict[str, object] = {}
        for extension in ("stl", "3mf", "step"):
            target = output_dir / extension / f"{key}.{extension}"
            cq.exporters.export(printable, str(target), tolerance=0.025, angularTolerance=0.08)
            files[extension] = {
                "file": target.relative_to(output_dir).as_posix(),
                "sha256": digest(target),
                "bytes": target.stat().st_size,
            }
        roundtrip = cq.importers.importStep(
            str(output_dir / "step" / f"{key}.step")
        ).val()
        roundtrip_checks = geometry(roundtrip)
        _assert_geometry(f"{key}.step", roundtrip_checks, print_ready=True)
        if abs(float(roundtrip.Volume()) - float(printable.Volume())) > max(
            1.0e-3, float(printable.Volume()) * 1.0e-7
        ):
            raise RuntimeError(f"{key}.step: round-trip volume changed")
        manifest["models"][key] = {
            "source_part": source_name,
            "source_step_sha256": digest(source_path),
            "purpose": spec["label"],
            "print_quantity": spec["quantity"],
            "source_cad": spec["source_checks"],
            "cad": spec["print_checks"],
            "step_roundtrip": roundtrip_checks,
            "transform_sequence": spec["operations"],
            "orientation_note": spec["note"],
            "files": files,
        }
        dims = " × ".join(f"{float(value):.2f}" for value in spec["print_checks"]["envelope_mm"])
        rows.append(
            f"| {key} | {spec['label']} | {spec['quantity']} | {dims} | {spec['note']} |"
        )

    if digest(source_file) != source_hash:
        raise RuntimeError("CAD source changed during R7 export")
    manifest["all_checks_pass"] = True
    (output_dir / "geometry_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    readme = "\n".join(
        [
            "# GL30 R7 降加工成本尺寸试装包",
            "",
            "文件单位为 **mm**，按 **100%** 导入。每个模型同时提供 STL、3MF、STEP；三种格式是同一零件，不要重复打印。包内只含几何，不含切片配置或 G-code。",
            "",
            "本包共 19 种模型、23 个物件数量：侧键帽 4 只，PCB 25 mm 直隔柱 2 只，其余各 1 只。8×6×57.1 屏柱直管和 25 mm PCB 隔柱优先购买或截取标准金属件；对应网格只作尺寸样。",
            "",
            "承力架、轴承筒、间隔环、顶部压环和电机滑座只做无电尺寸/装配试装。电机定子固定接口、轴承配合、螺纹、材料收缩、强度、热和手感仍需实物验证。",
            "",
            "| 文件名 | 零件 | 数量 | 打印包络 mm | 方向与边界 |",
            "| --- | --- | ---: | --- | --- |",
            *rows,
            "",
            "导轨背脊已补至底脚平面，并保留侧键让位口；按世界 Y+90 侧卧，使 +X 面朝平台。屏柱左右夹座分别按世界 Y-90/Y+90，使外侧平面朝平台。局部让位口和孔面仍检查支撑，不能把本包解释为免支撑证明。",
            "",
            "几何检查包括 R7 verification.json 的 all_checks_pass、源文件 SHA-256、逐件 STEP 重开后的单实体/有效/正体积、摆放后的 Z=0 和单喷嘴包络。STL/3MF 还需要独立网格导入检查；本脚本不切片、不打印。",
            "",
        ]
    )
    (output_dir / "README_CN.md").write_text(readme, encoding="utf-8")
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Export the explicit GL30 R7 cost-down fit-sample pack")
    parser.add_argument("--model-root", type=Path, default=MODEL_ROOT)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args(argv)
    try:
        manifest = export_pack(model_root=args.model_root, output_dir=args.out)
    except Exception as exc:
        print(f"R7 export blocked: {exc}", file=sys.stderr)
        return 1
    print(
        json.dumps(
            {
                "out": str(args.out.resolve()),
                "expected_model_count": manifest["expected_model_count"],
                "physical_item_count": manifest["physical_item_count"],
                "all_checks_pass": manifest["all_checks_pass"],
                "gcode_delivered": manifest["gcode_delivered"],
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    result = main()
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(result)
