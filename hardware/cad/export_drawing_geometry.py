"""Extract real current STEP projections/sections for prototype drawings.

Uses OCCT hidden-line removal; polylines approximate real edges to 0.015 mm
deflection. Not a manufacturing toolpath, nor a substitute for source STEP.
"""
from __future__ import annotations

import hashlib
import json
import os
import sys
from dataclasses import asdict
from pathlib import Path

import cadquery as cq
from OCP.BRepLib import BRepLib
from OCP.GCPnts import GCPnts_QuasiUniformDeflection
from OCP.HLRAlgo import HLRAlgo_Projector
from OCP.HLRBRep import HLRBRep_Algo, HLRBRep_HLRToShape
from OCP.gp import gp_Ax2, gp_Dir, gp_Pnt

from v7_params import DEFAULTS

OUT = Path(__file__).resolve().parent / "out" / "CONCEPT_FIT_DEFAULTS"
PREFIX = "V7_CONCEPT_FIT_DEFAULTS"


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def edge_points(edge: cq.Edge, axes: tuple[int, int]) -> list:
    curve = edge._geomAdaptor()
    sampler = GCPnts_QuasiUniformDeflection(curve, 0.015)
    if not sampler.IsDone() or sampler.NbPoints() < 2:
        raise ValueError("Could not discretize a STEP edge")
    result = []
    for i in range(1, sampler.NbPoints() + 1):
        v = sampler.Value(i)
        xyz = (v.X(), v.Y(), v.Z())
        result.append([round(xyz[axes[0]], 6), round(xyz[axes[1]], 6)])
    return result


def view_data(shapes: list[cq.Shape], axes=(0, 1)) -> dict:
    lines = [edge_points(edge, axes) for shape in shapes for edge in shape.Edges()]
    points = [p for line in lines for p in line]
    if not points:
        raise ValueError("Empty drawing view")
    return {"polylines": lines, "bounds": [min(p[0] for p in points),
            min(p[1] for p in points), max(p[0] for p in points), max(p[1] for p in points)]}


def project(shape: cq.Shape, normal: tuple, horizontal: tuple) -> dict:
    algo = HLRBRep_Algo()
    algo.Add(shape.wrapped)
    algo.Projector(HLRAlgo_Projector(gp_Ax2(gp_Pnt(), gp_Dir(*normal), gp_Dir(*horizontal))))
    algo.Update()
    algo.Hide()
    result = HLRBRep_HLRToShape(algo)
    visible = []
    for raw in (result.VCompound(), result.Rg1LineVCompound(), result.OutLineVCompound()):
        if not raw.IsNull():
            BRepLib.BuildCurves3d_s(raw, 1.0e-7)
            visible.append(cq.Shape.cast(raw))
    return view_data(visible)


def section(shape: cq.Shape) -> dict:
    # Plane y=0 gives true meridional x-z section, no bounding-box surrogate.
    sliced = shape.intersect(cq.Face.makePlane(basePnt=(0, 0, 0), dir=(0, 1, 0)))
    result = view_data([sliced], (0, 2))
    loops = []
    for face in sliced.Faces():
        if face.innerWires():
            raise ValueError("Section hatch needs explicit holes; do not fill a void")
        points, _ = face.outerWire().sample(0.015)
        loops.append([[round(v.x, 6), round(v.z, 6)] for v in points])
    result["section_loops"] = loops
    return result


def main() -> int:
    p = DEFAULTS
    step = OUT / f"{PREFIX}_exterior.step"
    print("Reading current exterior STEP for drawings...", flush=True)
    assembly = cq.Assembly.load(str(step))
    shapes = {name.rsplit("/", 1)[-1]: shape.moved(loc) for shape, name, loc, _ in assembly}
    whole = assembly.toCompound()
    views = {}
    for name, normal, horizontal in (
        ("assembly_front", (0, -1, 0), (1, 0, 0)),
        ("assembly_side", (1, 0, 0), (0, 1, 0)),
        ("assembly_top", (0, 0, 1), (1, 0, 0)),
    ):
        print(f"Projecting {name}...", flush=True)
        views[name] = project(whole, normal, horizontal)
    key = shapes["button_right_1"].translate((
        -(p.width_mm / 2 - p.housing_wall_mm), -p.side_button_front_y_mm, -p.side_button_z_mm))
    views["button_face"] = project(key, (1, 0, 0), (0, 1, 0))
    views["button_section"] = section(key)
    local = {name: shapes[name].translate((0, -p.knob_center_y_mm, -p.knob_center_z_mm)).rotate(
        (0, 0, 0), (1, 0, 0), -p.deck_angle_deg) for name in ("ring_cap", "ring_sleeve")}
    views["ring_cap_section"] = section(local["ring_cap"])
    views["ring_cap_top"] = project(local["ring_cap"], (0, 0, 1), (1, 0, 0))
    views["ring_sleeve_section"] = section(local["ring_sleeve"])
    rows = [
        ("键帽长×高", "11.00 × 4.00", "各 ±0.10", "与开口间隙配合", "试制建议"),
        ("导向开口长×高", "11.40 × 4.40", "各 ±0.10", "居中单边0.10～0.30", "涂装后修配"),
        ("内挡边长×高 / 厚", "12.40 × 5.40 / 0.60", "各 ±0.10", "居中搭接最小0.40", "仅外向限位"),
        ("键面低于壳体侧壁", "0.30", "±0.10", "比槽底高0.30标称", "装配实测"),
        ("槽长×高 / 深", "33.00 × 6.60 / 0.60", "各 ±0.10", "不作为按键行程", "试制建议"),
        ("旋环内孔有效包络", "Ø40.00", "±0.05", "须计入形状误差", "形状样件目标"),
        ("固定盖板外径有效包络", "Ø39.00", "±0.05", "不得接触旋环", "材料/支承HOLD"),
        ("袖套轴承孔", "Ø52.20 REF", "HOLD", "非生产轴承配合", "禁止据此定公差"),
        ("未注尺寸：CNC外形样件", "线性 / 角度", "±0.10 / ±0.5°", "不套用到HOLD项", "自定试制规则"),
        ("打印壳体", "按名义CAD外形", "能力由供应商确认", "键孔单独修配验收", "不保证打印±0.10"),
    ]
    holds = [
        "开关未选定：工作行程、按压力、回弹、内向限位和PCB高度待设计。底盖已可拆，键帽从内侧装入；0.40仅为空间检查位移。",
        "轴承牌号/游隙、过盈量、薄壁变形、预紧和轴向定位未验证。52.20只能作为空间参考，不能标成H7配合。",
        "torque_carrier与fixed_spider各已修成单实体；产品旋环帽/袖套传扭连接、支承和实际安装仍未完成。",
        "底盖89.4×89.4×3已可拆，四处M3沉头座/螺母槽；实际紧固配合、电机/屏幕安装和线束路径仍需验证。底盖详图不在这六页中。",
        "盖板黑度/透光/触控与强度、电池约束、散热及无线天线空间未做实物验证。",
        "键缝计算假设居中：偏心/毛刺/涂层可吃掉间隙。装配后四周逐项检查，无卡擦且可回位；不得只验单个零件尺寸。",
        "旋环/盖板有效包络满足上表时，同心径向缝0.45～0.55；相对轴偏≤0.10时局部最小缝≥0.35。仅条件计算，须装配全周实测。",
    ]
    geometry = json.loads((OUT / f"{PREFIX}_geometry_report.json").read_text(encoding="utf-8"))
    readback = json.loads((OUT / f"{PREFIX}_step_readback.json").read_text(encoding="utf-8"))
    hashes = {kind: digest(OUT / f"{PREFIX}_{kind}.step") for kind in ("assembly", "exterior")}
    for kind in hashes:
        if hashes[kind] != readback["files"][kind]["sha256"]:
            raise ValueError(f"Stale STEP readback report: {kind}")
    if not geometry["all_checks_pass"] or not readback["all_checks_pass"]:
        raise ValueError("CAD checks must pass before drawing extraction")
    data = {
        "units": "mm", "status": "PROTOTYPE_DRAWING_DRAFT", "parameters": asdict(p),
        "views": views,
        "tolerance_rows": [dict(zip(("feature", "nominal", "tolerance", "fit", "status"), r)) for r in rows],
        "holds": holds, "validation": {"geometry_all_pass": True, "step_all_pass": True},
        "source_sha256": hashes,
        "projection_source": "OCCT HLR visible edges and true y=0 solid sections; polyline deflection 0.015 mm",
        "sources": ["https://www.nsk.com/content/dam/nsk/eu/en_gb/documents/P_TI-0001_EN.pdf"],
    }
    target = OUT / "drawing_geometry.json"
    target.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(target), "views": {k: len(v["polylines"]) for k,v in views.items()}}, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    if os.name == "nt":
        os._exit(code)
    raise SystemExit(code)
