"""Derive a non-powered PETG appearance/clearance kit from current STEP.

Print-only fixtures are deliberately separate from production CAD. No bearing,
motor, electronic dummy mesh or disconnected structural concept is shipped.
"""
from __future__ import annotations

import hashlib
import json
import os
import sys
from pathlib import Path

import cadquery as cq

from v7_params import DEFAULTS as P
from v7_product_model import _render

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "out" / "CONCEPT_FIT_DEFAULTS" / "V7_CONCEPT_FIT_DEFAULTS_exterior.step"
OUT = HERE.parents[1] / "output" / "print" / "GL30_H2D_PETG_VALIDATION"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def local_ring(shape):
    return shape.translate((0, -P.knob_center_y_mm, -P.knob_center_z_mm)).rotate(
        (0,0,0), (1,0,0), -P.deck_angle_deg)


def world_ring(shape):
    return shape.rotate((0,0,0), (1,0,0), P.deck_angle_deg).translate(
        (0,P.knob_center_y_mm,P.knob_center_z_mm))


def ring(outer, inner, bottom, top):
    return cq.Workplane("XY").circle(outer/2).circle(inner/2).extrude(top-bottom).translate((0,0,bottom)).val()


def on_bed(shape):
    box = shape.BoundingBox()
    return shape.translate((-(box.xmin+box.xmax)/2, -(box.ymin+box.ymax)/2, -box.zmin))


def capsule_xz(x, z, length, height, start_y, end_y):
    # XZ normal is -Y; negative extrusion creates a +Y blind/through pocket.
    return cq.Workplane("XZ").center(x,z).slot2D(length,height).extrude(
        -(end_y-start_y)).translate((0,start_y,0)).val()


def button_coupon():
    wall = cq.Workplane("XY").box(100,3,16,centered=(True,False,False)).val()
    base = cq.Workplane("XY").box(100,12,2,centered=(True,False,False)).translate((0,-4.5,0)).val()
    result = wall.fuse(base)
    result = result.cut(capsule_xz(0,10,88,6.6,-.1,.6))
    records = []
    for x,g in zip((-36,-18,0,18,36),(.15,.20,.25,.30,.40)):
        tool = capsule_xz(x,10,11+2*g,4+2*g,-.1,3.1)
        result = result.cut(tool)
        label = cq.Workplane("XZ").center(x,4.4).text(
            f"{g:.2f}",2.6,-.35,combine=False,font="Arial").val()
        result = result.cut(label)
        records.append({"x_mm":x,"single_side_nominal_gap_mm":g,"opening_mm":[11+2*g,4+2*g]})
    return result.clean(),records


def shape_checks(shape):
    solids = shape.Solids()
    box = shape.BoundingBox()
    return {"one_solid":len(solids)==1,"valid":shape.isValid(),
            "positive_volume":shape.Volume()>1.0e-6,
            "on_bed":abs(box.zmin)<2.0e-5,
            "fits_h2d_single_nozzle_envelope":box.xlen<325 and box.ylen<320 and box.zlen<325,
            "envelope_mm":[box.xlen,box.ylen,box.zlen],"volume_mm3":shape.Volume()}


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    for folder in ("3mf","stl","preview"):
        (OUT/folder).mkdir(exist_ok=True)
    source_hash = digest(SOURCE)
    assembly = cq.Assembly.load(str(SOURCE))
    parts = {name.rsplit("/",1)[-1]:shape.moved(loc) for shape,name,loc,_ in assembly}
    print("Building five-gap upright button coupon...",flush=True)
    coupon,coupon_records = button_coupon()
    key = parts["button_right_1"].rotate((0,0,0),(0,1,0),-90).rotate((0,0,0),(0,0,1),90)
    key = on_bed(key)

    print("Fusing print-only grip, preserving its outer profile...",flush=True)
    stock_sleeve,stock_cap = [local_ring(parts[name]) for name in ("ring_sleeve","ring_cap")]
    stock_grip = stock_sleeve.fuse(stock_cap).clean()
    # Reinforce inward only: original bearing placeholder is NOT a print fit.
    reinforcement = ring(52.20,50.00,.80,7.80)
    grip = stock_grip.fuse(reinforcement).clean()
    grip_removed = abs(stock_grip.cut(grip).Volume())
    added_outside = abs(grip.cut(stock_grip).cut(
        cq.Workplane("XY").circle(26.1+1e-5).extrude(14).val()).Volume())

    print("Building open-bottom STATIC full-size form...",flush=True)
    housing = parts["housing"]
    bottom_opening = cq.Workplane("XY").box(90,90,4.2,centered=(True,True,False)).translate((0,0,-1)).val()
    open_housing = housing.cut(bottom_opening).clean()
    # Short print-only neck fixes the grip at its original deck-normal datum.
    # It never acts as a bearing, torque joint or detachable interface.
    neck = ring(53.40,50.00,-1.20,1.00)
    screen = cq.Workplane("XY").circle(19.50).extrude(3.00).translate((0,0,10.20)).val()
    static_top = grip.fuse(neck).fuse(screen)
    for angle in (0,120,240):
        spoke = cq.Workplane("XY").box(9,3,1.2).translate((19.5,0,10.8)).val().rotate((0,0,0),(0,0,1),angle)
        static_top = static_top.fuse(spoke)
    static_model = open_housing.fuse(world_ring(static_top)).clean()
    # Inside rear wall only: the appearance model is visibly not a working part.
    label = cq.Workplane("XZ").center(0,36).text(
        "STATIC",3.4,.5,combine=False,font="Arial").val().translate((0,45.2,0))
    static_model = static_model.fuse(label).clean()

    models = {
        "01_button_gap_coupon": (on_bed(coupon), "立式五档键孔试片，0.15/0.20/0.25/0.30/0.40为单边名义间隙",1),
        "02_button_cap_print_4": (key,"键帽：内挡边朝下、键面朝上；复制4个，无回弹结构",4),
        "03_grip_ring_shape_only": (on_bed(grip.rotate((0,0,0),(1,0,0),180)),"顶面朝下；单独握持旋环，袖套内径改为50仅供PETG外形样，不测轴承配合",1),
        "04_fullsize_STATIC_form": (on_bed(static_model),"1:1静态外形：底部敞开、旋环/假屏固定，不能转动/通电",1),
    }
    report={"status":"PRINT_GEOMETRY_VALIDATED_NOT_PHYSICALLY_PRINTED",
            "source_step_sha256":source_hash,"printer":"Bambu H2D",
            "material":"user grey PETG, brand unknown", "nozzle_assumption_mm":.4,
            "source_geometry_modified":False,"coupon":coupon_records,
            "grip_source_material_removed_mm3":grip_removed,
            "grip_added_material_outside_original_bore_mm3":added_outside,
            "source_vendor_parts_in_print_files":False,"gcode_delivered":False,
            "print_only_modifications":["Open bottom 90x90 for support removal and key access",
                "Grip sleeve inner diameter 50 instead of 52.2; never a bearing fit",
                "Fixed print-only neck joins ring to housing",
                "3mm opaque fake screen with three internal spokes; no rotation/electronics",
                "STATIC embossed inside rear wall; standalone grip printed top-face down",
                "No disconnected spiders, actual vendor motors/display boards or battery placeholders"],
            "models":{}}
    for name,(shape,purpose,quantity) in models.items():
        checks = shape_checks(shape)
        assert all(checks[k] for k in ("one_solid","valid","positive_volume","on_bed","fits_h2d_single_nozzle_envelope")),(name,checks)
        paths={}
        for extension in ("stl","3mf"):
            path=OUT/extension/f"{name}.{extension}"
            cq.exporters.export(shape,str(path),tolerance=.025,angularTolerance=.08)
            paths[extension]={"file":str(path.relative_to(OUT)),"sha256":digest(path),"bytes":path.stat().st_size}
        report["models"][name]={"purpose":purpose,"print_quantity":quantity,"cad":checks,"files":paths}
        print(name,checks,flush=True)
    assert grip_removed<1e-6 and added_outside<1e-6
    assert digest(SOURCE)==source_hash
    (OUT/"geometry_manifest.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")

    grey=(.51,.53,.56,1)
    _render(OUT/"preview"/"print_kit.png",[
        ("coupon",models["01_button_gap_coupon"][0],grey,(-63,-65,0)),
        ("key",key,grey,(-65,-39,0)),
        ("grip",models["03_grip_ring_shape_only"][0],grey,(65,-45,0)),
        ("static",models["04_fullsize_STATIC_form"][0],grey,(0,42,0)),
    ],title="H2D / GREY PETG VALIDATION KIT - STATIC FORM, NO MOTOR",camera=(225,-270,240),focal=(0,3,22))
    # Appearance illustration: caps lightly retained from inside after printing.
    _render(OUT/"preview"/"static_form.png",[
        ("static",static_model,grey,(0,0,0)),
        *[(name,parts[name],grey,(0,0,0)) for name in ("button_left_1","button_left_2","button_right_1","button_right_2")]
    ],title="1:1 STATIC FIT FORM - RING AND FAKE SCREEN DO NOT ROTATE",camera=(165,-195,145))
    print(f"Generated print-only kit: {OUT}",flush=True)
    return 0


if __name__=="__main__":
    code=main()
    sys.stdout.flush()
    sys.stderr.flush()
    if os.name=="nt": os._exit(code)
    raise SystemExit(code)
