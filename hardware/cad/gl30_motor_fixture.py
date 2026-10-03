"""Three-part, real-hole GL30 bench fixture derived from the current product.

No electronics or bearing are fabricated. Only the factory motor is included
as reference in the assembly STEP; exported meshes contain the printed parts.
"""
from __future__ import annotations

import hashlib
import json
import os
import sys
from math import cos, pi, sin
from pathlib import Path

import cadquery as cq
from OCP.BRepAdaptor import BRepAdaptor_Surface
from OCP.GeomAbs import GeomAbs_Cylinder

from v7_params import DEFAULTS as P
from v7_product_model import GL30_STEP, _export_step, _render

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "output/models/GL30_MOTOR_FIXTURE"
PRODUCT = ROOT / "hardware/cad/out/CONCEPT_FIT_DEFAULTS/V7_CONCEPT_FIT_DEFAULTS_exterior.step"
POSTS = ((-29,-26),(29,-26),(-29,26),(29,26))
PLATE_Z = 17.0
PLATE_THICKNESS = 3.5
MOTOR_BACK_Z = PLATE_Z + PLATE_THICKNESS


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def box(shape):
    b=shape.BoundingBox()
    return {k:getattr(b,k) for k in ("xmin","xmax","ymin","ymax","zmin","zmax","xlen","ylen","zlen")}


def annulus(outer,inner,z,height):
    return cq.Workplane("XY").circle(outer/2).circle(inner/2).extrude(height).translate((0,0,z)).val()


def holes_at_vendor_end(motor, back):
    """Extract axes opening at the actual end plane; exclude internal holes."""
    b=motor.BoundingBox()
    end=b.xmin if back else b.xmax
    found={}
    for face in motor.Faces():
        adaptor=BRepAdaptor_Surface(face.wrapped)
        if adaptor.GetType()!=GeomAbs_Cylinder:
            continue
        cyl=adaptor.Cylinder()
        if abs(cyl.Axis().Direction().X())<.999 or not 1.2<cyl.Radius()<1.6:
            continue
        loc=cyl.Location()
        if abs((loc.Y()**2+loc.Z()**2)**.5-10)>.001:
            continue
        fb=face.BoundingBox()
        plane=fb.xmin if back else fb.xmax
        if abs(plane-end)>.0001:
            continue
        key=(round(-loc.Z(),6),round(loc.Y(),6))  # native X -> assembly Z
        found[key]={"xy_mm":list(key),"cad_cylindrical_depth_mm":fb.xlen}
    values=sorted(found.values(),key=lambda x:x["xy_mm"])
    assert len(values)==(3 if back else 4), values
    return values


def make_base():
    # Flat underside avoids spanning the entire base above four little feet.
    part=cq.Workplane("XY").box(100,90,7,centered=(True,True,False)).edges("|Z").fillet(3).val()
    for x,y in POSTS:
        part=part.fuse(cq.Workplane("XY").center(x,y).circle(6).extrude(10).translate((0,0,7)).val())
        part=part.cut(cq.Workplane("XY").center(x,y).circle(1.7).extrude(20).val())
        nut=cq.Workplane("XY").center(x,y).polygon(6,5.8/cos(pi/6)).extrude(5.0).val()
        part=part.cut(nut)
    for x in (-43,43):
        for y in (-23,23):
            part=part.cut(cq.Workplane("XY").center(x,y).slot2D(14,5,90).extrude(8).val())
    # Access/vent hole and stationary cable tie slots; no through-motor tube.
    part=part.cut(cq.Workplane("XY").circle(8).extrude(8).val())
    for y in (-9,9):
        part=part.cut(cq.Workplane("XY").center(-38,y).slot2D(9,3,90).extrude(8).val())
    label=cq.Workplane("XY").center(0,-36).text("GL30 BENCH",4,-.4,combine=False,font="Arial").translate((0,0,7)).val()
    return part.cut(label).clean()


def make_plate(back_holes):
    part=cq.Workplane("XY").box(68,62,PLATE_THICKNESS,centered=(True,True,False)).edges("|Z").fillet(2).val()
    for x,y in POSTS:
        part=part.cut(cq.Workplane("XY").center(x,y).circle(1.7).extrude(4).val())
    for hole in back_holes:
        x,y=hole["xy_mm"]
        part=part.cut(cq.Workplane("XY").center(x,y).circle(1.7).extrude(4).val())
    part=part.cut(cq.Workplane("XY").circle(4).extrude(4).val())
    notch=cq.Workplane("XY").box(21,12,5,centered=(False,True,False)).translate((-34.5,0,-.5)).val()
    part=part.cut(notch)
    label=cq.Workplane("XY").center(0,21).text("3 HOLES / FIXED",2.6,-.3,combine=False,font="Arial").translate((0,0,PLATE_THICKNESS)).val()
    return part.cut(label).clean()


def make_grip(product_parts,front_holes):
    original=[]
    for name in ("ring_sleeve","ring_cap"):
        local=product_parts[name].translate((0,-P.knob_center_y_mm,-P.knob_center_z_mm)).rotate((0,0,0),(1,0,0),-P.deck_angle_deg)
        original.append(local)
    exterior=original[0].fuse(original[1])
    part=exterior.fuse(annulus(52.2,48,.8,7)).fuse(annulus(54,48,0,.8))
    # Inward ramp joins the cap without a horizontal underside ledge.
    ramp=(cq.Workplane("XZ").polyline([(24,3.5),(26.1,3.5),(26.1,7.8),(20,7.8)])
          .close().revolve(360,(0,0),(0,1)).val())
    part=part.fuse(ramp)
    # Integral four-arm drive flange, screws accessible through the open top.
    part=part.fuse(annulus(30,8,0,3.5))
    for angle in (45,135,225,315):
        arm=cq.Workplane("XY").box(13,6,3.5,centered=(False,True,False)).translate((13,0,0)).val().rotate((0,0,0),(0,0,1),angle)
        part=part.fuse(arm)
    for hole in front_holes:
        x,y=hole["xy_mm"]
        part=part.cut(cq.Workplane("XY").center(x,y).circle(1.7).extrude(4).val())
    return part.clean(),exterior


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    for folder in ("stl","3mf","step","preview"):
        (OUT/folder).mkdir(exist_ok=True)
    source_hashes={str(p.relative_to(ROOT)):sha(p) for p in (GL30_STEP,PRODUCT)}
    motor=cq.importers.importStep(str(GL30_STEP)).val()
    back=holes_at_vendor_end(motor,True)
    front=holes_at_vendor_end(motor,False)
    mb=box(motor)
    motor_height=mb["xmax"]-mb["xmin"]
    motor_placed=motor.rotate((0,0,0),(0,1,0),-90).translate((0,0,MOTOR_BACK_Z-mb["xmin"]))
    rotor_z=MOTOR_BACK_Z+motor_height
    source=cq.Assembly.load(str(PRODUCT))
    product_parts={name.rsplit("/",1)[-1]:shape.moved(loc) for shape,name,loc,_ in source}
    print("Building base, stator plate, integral rotor grip...",flush=True)
    base=make_base(); plate=make_plate(back); grip,ring_reference=make_grip(product_parts,front)
    models={"01_base":base,"02_stator_plate":plate,"03_rotor_grip":grip}
    placed={"base":base,"stator_plate":plate.translate((0,0,PLATE_Z)),"rotor_grip":grip.translate((0,0,rotor_z))}
    print("Checking nominal interfaces, insertion and 360-degree clearance...",flush=True)
    checks={}
    for name,shape in models.items():
        checks[name+"_single_valid_solid"]=len(shape.Solids())==1 and shape.isValid()
        checks[name+"_on_bed"]=abs(shape.BoundingBox().zmin)<1e-5
        checks[name+"_h2d_fit"]=all(box(shape)[key]<limit for key,limit in (("xlen",325),("ylen",320),("zlen",325)))
    for a,b in (("base","stator_plate"),("base","rotor_grip"),("stator_plate","rotor_grip")):
        checks[a+"_"+b+"_no_collision"]=abs(placed[a].intersect(placed[b]).Volume())<1e-5
    for name,shape in placed.items():
        checks[name+"_motor_no_collision"]=abs(shape.intersect(motor_placed).Volume())<1e-5
    # Full swept lower grip envelope clears the motor below the rotor face.
    checks["rotating_bottom_at_output_plane"]=abs(placed["rotor_grip"].BoundingBox().zmin-rotor_z)<1e-5
    checks["stator_plate_to_rotating_grip_axial_gap_gt_28mm"]=rotor_z-MOTOR_BACK_Z>28
    # All screw axes stay inside printed bores along their grip length.
    for prefix,part,holes in (("stator",plate,back),("rotor",grip,front)):
        for i,h in enumerate(holes):
            x,y=h["xy_mm"]
            probe=cq.Workplane("XY").center(x,y).circle(1.5).extrude(3.5).val()
            checks[f"{prefix}_M3_clearance_{i}"]=abs(probe.intersect(part).Volume())<1e-6
    # Screen-sized opening is deliberately empty: no fake fixed display.
    checks["grip_does_not_remove_reference_ring_material"]=abs(ring_reference.cut(grip).Volume())<1e-6
    report={"status":"CAD_ASSEMBLY_CHECKED_PHYSICAL_FIT_PENDING", "units":"mm",
            "source_hashes":source_hashes,"printer":"H2D","material":"grey PETG, user sliced",
            "source_mounting_holes":{"back_3_m3":back,"front_4_m3":front},
            "motor_back_z_mm":MOTOR_BACK_Z,"motor_output_z_mm":rotor_z,
            "nominal_motor_screw_stack":{"screw_under_head_mm":6,"printed_plate_mm":3.5,"washer_mm":.5,"engagement_mm":2},
            "nominal_frame_screw_stack":{"screw_under_head_mm":20,"head_seat_z_with_washer_mm":21,"tip_z_mm":1,"nut_top_seat_z_mm":5,"nut_thickness_mm":2.4,"nut_bottom_z_mm":2.6},
            "models":{},"checks":checks,"all_checks_pass":all(checks.values()),
            "assembly_limits":["Back 3-hole face must remain stationary during manual rotor turn before assembly",
                "Measure actual M3 thread depth and printed stack; PDF depths differ from CAD",
                "No external bearing, fixed screen, centre tube, battery or electronics in fixture",
                "Only current NUCLEO/TI low-current commissioning; no axial push, heavy side load or high-speed test",
                "No strength, bearing-life, thermal, slicing or physical-print result implied"]}
    if not report["all_checks_pass"]:
        (OUT/"geometry_report.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
        raise RuntimeError([k for k,v in checks.items() if not v])
    for name,shape in models.items():
        files={}
        for ext in ("stl","3mf","step"):
            target=OUT/ext/(name+"."+ext)
            cq.exporters.export(shape,str(target),tolerance=.025,angularTolerance=.08)
            files[ext]={"file":str(target.relative_to(OUT)),"sha256":sha(target)}
        report["models"][name]={"quantity":1,"bbox":box(shape),"volume_mm3":shape.Volume(),"files":files}
    grey=(.57,.60,.64,1); black=(.07,.085,.10,1); orange=(.85,.47,.15,1)
    items=[("base",base,grey),("stator_plate",placed["stator_plate"],grey),
           ("rotor_grip",placed["rotor_grip"],black),("motor_official",motor_placed,orange)]
    assembly_path=OUT/"step/GL30_MOTOR_FIXTURE_ASSEMBLY.step"
    _export_step(assembly_path,"GL30_MOTOR_FIXTURE",items)
    report["assembly_step_sha256"]=sha(assembly_path)
    for path,expected in source_hashes.items():
        assert sha(ROOT/path)==expected
    (OUT/"geometry_report.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    _render(OUT/"preview/assembly.png",[(n,s,c,(0,0,0)) for n,s,c in items],
            title="GL30 MOTOR FIXTURE / FACTORY MOTOR + THREE PRINTED PARTS",camera=(140,-180,125),focal=(0,0,26))
    offsets={"base":(0,0,0),"stator_plate":(0,0,14),"motor_official":(0,0,30),"rotor_grip":(0,0,45)}
    _render(OUT/"preview/exploded.png",[(n,s,c,offsets[n]) for n,s,c in items],
            title="3-HOLE FIXED FACE / 4-HOLE ROTOR GRIP / FASTENERS OMITTED",camera=(160,-190,160),focal=(0,0,49),parallel_scale_mm=86)
    _render(OUT/"preview/print_parts.png",[("base",base,grey,(0,40,0)),("plate",plate,grey,(-49,-42,0)),("grip",grip,grey,(41,-42,0))],
            title="THREE PETG PARTS / MODELS ONLY / USER SLICING",camera=(180,-230,220),focal=(0,3,12))
    print(json.dumps({"all_checks_pass":True,"checks":len(checks),"out":str(OUT),"back_holes":back,"front_holes":front},ensure_ascii=False),flush=True)
    return 0


if __name__=="__main__":
    result=main();sys.stdout.flush();sys.stderr.flush()
    if os.name=="nt":os._exit(result)
    raise SystemExit(result)
