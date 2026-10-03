"""Independent readback of fixture STL, 3MF, individual STEP and assembly."""
import hashlib
import json
import os
import sys
from pathlib import Path

import cadquery as cq
from check_print_meshes import check_stl_file, load_and_check_3mf

ROOT=Path(__file__).resolve().parents[2]
KIT=ROOT/"output/models/GL30_MOTOR_FIXTURE"


def main():
    report=json.loads((KIT/"geometry_report.json").read_text(encoding="utf-8"))
    assert report["all_checks_pass"]
    expected={"01_base","02_stator_plate","03_rotor_grip"}
    assert set(report["models"])==expected
    results=[]
    for name,data in report["models"].items():
        for ext,info in data["files"].items():
            path=KIT/info["file"]
            assert hashlib.sha256(path.read_bytes()).hexdigest()==info["sha256"]
            if ext=="step":
                shape=cq.importers.importStep(str(path)).val()
                assert len(shape.Solids())==1 and shape.isValid(),name
                b=shape.BoundingBox()
                metrics={"volume_mm3":shape.Volume(),"envelope_mm":[b.xlen,b.ylen,b.zlen]}
            else:
                metrics,okay,issues=(check_stl_file(path) if ext=="stl" else load_and_check_3mf(path))
                assert okay,(path,issues)
            delta=abs(metrics["volume_mm3"]/data["volume_mm3"]-1)
            bbox_error=max(abs(metrics["envelope_mm"][i]-data["bbox"][key]) for i,key in enumerate(("xlen","ylen","zlen")))
            assert delta<.005 and bbox_error<.06,(name,ext,delta,bbox_error)
            results.append({"file":info["file"],"sha256":info["sha256"],"volume_relative_error":delta,"envelope_max_error_mm":bbox_error,"metrics":metrics})
    path=KIT/"step/GL30_MOTOR_FIXTURE_ASSEMBLY.step"
    assert hashlib.sha256(path.read_bytes()).hexdigest()==report["assembly_step_sha256"]
    assembly=cq.Assembly.load(str(path))
    parts={n.rsplit("/",1)[-1]:s.moved(loc) for s,n,loc,_ in assembly}
    assert set(parts)=={"base","stator_plate","rotor_grip","motor_official"}
    assert {n:len(s.Solids()) for n,s in parts.items()}=={"base":1,"stator_plate":1,"rotor_grip":1,"motor_official":7}
    assert all(s.isValid() for s in parts.values())
    collisions={}
    for a,b in (("base","stator_plate"),("base","rotor_grip"),("stator_plate","rotor_grip"),("stator_plate","motor_official"),("rotor_grip","motor_official")):
        volume=abs(parts[a].intersect(parts[b]).Volume())
        assert volume<1e-5,(a,b,volume)
        collisions[a+"/"+b]=volume
    # Moving geometry is entirely at/above the highest motor plane, and has
    # axial clearance to both fixed prints. This holds for every Z-axis angle.
    rotor_min=parts["rotor_grip"].BoundingBox().zmin
    motor_max=parts["motor_official"].BoundingBox().zmax
    assert abs(rotor_min-motor_max)<1e-5
    assert rotor_min-max(parts[n].BoundingBox().zmax for n in ("base","stator_plate"))>28
    # Nominal screw-axis mapping from the official STEP, independent of builder.
    for name,holes in (("stator_plate",((-6.427876,-7.660444),(-3.420201,9.396926),(9.848078,-1.736482))),
                       ("rotor_grip",((0,-10),(0,10),(-10,0),(10,0)))):
        z=parts[name].BoundingBox().zmin
        for x,y in holes:
            probe=cq.Workplane("XY").center(x,y).circle(1.5).extrude(3.5).translate((0,0,z)).val()
            assert abs(probe.intersect(parts[name]).Volume())<1e-6,(name,x,y)
    for source,expected_sha in report["source_hashes"].items():
        assert hashlib.sha256((ROOT/source).read_bytes()).hexdigest()==expected_sha
    output={"pass":True,"model_files_checked":9,"assembly_components":4,"assembly_solids":10,
            "assembly_step_sha256":report["assembly_step_sha256"],"collisions_mm3":collisions,
            "full_rotation_geometric_clearance":True,"nominal_screw_hole_probes":7,"results":results,
            "limitations":["No self-intersection sweep, physical fit, thread measurement, strength or thermal validation",
                "Fasteners and cables are not included as solids; their actual lengths/routing must be checked",
                "No slicing and no printer connection"]}
    (KIT/"readback_validation.json").write_text(json.dumps(output,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print("PASS: 9 part files + 4-component/10-solid assembly, holes and geometric rotation clearance",flush=True)
    return 0


if __name__=="__main__":
    code=main();sys.stdout.flush();sys.stderr.flush()
    if os.name=="nt":os._exit(code)
    raise SystemExit(code)
