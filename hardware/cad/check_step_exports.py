"""Integration check of current STEP exports using CadQuery's STEP reader.

Reads exported geometry without rebuilding the model or calling its checks.
Not a SolidWorks desktop test or manufacturing/optical qualification.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from math import cos, pi, radians, sin, tan
from pathlib import Path

import cadquery as cq
from OCP.BRepClass3d import BRepClass3d_SolidClassifier
from OCP.TopAbs import TopAbs_IN
from OCP.gp import gp_Pnt

from v7_params import DEFAULTS
from v7_product_model import COLORS

OUTPUT_DIR = Path(__file__).resolve().parent / "out" / "CONCEPT_FIT_DEFAULTS"
PREFIX = "V7_CONCEPT_FIT_DEFAULTS"
TOL = 1.0e-5  # mm; distinct from the volume tolerance below.
VOLUME_TOL = 1.0e-6  # mm^3
MAX_BBOX_DIAMETER_MM = 54.0 + 1.0e-4
EXTERIOR = {
    name: 1 for name in (
        "housing", "bottom_cover", "ring_sleeve", "ring_cap", "ring_index_mark", "fixed_bezel",
        "display_glass", "button_left_1", "button_left_2", "button_right_1",
        "button_right_2", "power_button", "service_pinhole", "speaker_grille",
        "microphone", "status_led", "front_light_strip", "usb_c_charge_data",
        "foot_1", "foot_2", "foot_3", "foot_4",
    )
}
ASSEMBLY = EXTERIOR | {
    "bearing_envelope": 1, "torque_carrier": 1, "fixed_spider": 1,
    "support_tube": 1, "battery_keepout": 1, "electronics_keepout": 1,
    "motor_official": 7, "display_official": 403,
}


def expected_color(name: str) -> tuple:
    for prefix, key in (("button_", "button"), ("foot_", "foot"), ("usb_c_", "usb_c")):
        if name.startswith(prefix):
            return COLORS[key]
    return COLORS[name]


def ring_local(shape: cq.Shape) -> cq.Shape:
    p = DEFAULTS
    # Inverse of rotate-then-translate placement: undo translation FIRST.
    return shape.translate((0.0, -p.knob_center_y_mm, -p.knob_center_z_mm)).rotate(
        (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), -p.deck_angle_deg
    )


def classify(classifier, radius: float, angle: float, z: float) -> bool:
    classifier.Perform(gp_Pnt(radius * cos(angle), radius * sin(angle), z), TOL)
    return classifier.State() == TopAbs_IN


def sample_radial_profile(
    solid: cq.Shape,
    center_radius_mm: float,
    z_mm: float,
    angle_count: int,
    radius_delta_mm: float,
) -> tuple[bool, list[dict[str, bool]]]:
    classifier = BRepClass3d_SolidClassifier(solid.Solids()[0].wrapped)
    checks = []
    ok = True
    for idx in range(angle_count):
        angle = idx * 2.0 * pi / angle_count
        inside = classify(classifier, center_radius_mm - radius_delta_mm, angle, z_mm)
        outside = not classify(classifier, center_radius_mm + radius_delta_mm, angle, z_mm)
        checks.append({"angle": idx, "inside_inner": inside, "outside_outer": outside})
        if not (inside and outside):
            ok = False
    return ok, checks


def read_and_check(path: Path, expected: dict[str, int]) -> dict:
    print(f"Reading {path.name}...", flush=True)
    assembly = cq.Assembly.load(str(path))
    entries = list(assembly)
    names = [name.rsplit("/", 1)[-1] for _, name, _, _ in entries]
    shapes = {name: shape.moved(loc) for name, (shape, _, loc, _) in zip(names, entries)}
    colors = {
        name: None if color is None else color.toTuple()
        for name, (_, _, _, color) in zip(names, entries)
    }
    nodes = dict(assembly.traverse())
    color_checks = {}
    color_assignments = {}
    for name in expected:
        if name not in nodes:
            color_checks[name] = False
            continue
        node = nodes[name]
        subcolors = node._subshape_colors
        node_solids = [solid for shape in node.shapes for solid in shape.Solids()]
        # STEP may attach RGBA to each constituent solid, not its compound.
        # Require actual coverage plus matching subshape overrides, not just
        # the first color found somewhere in the assembly.
        coverage = colors.get(name) is not None or (
            bool(node_solids) and all(
                any(solid.isSame(colored) for colored in subcolors)
                for solid in node_solids
            )
        )
        values = ([colors[name]] if colors.get(name) is not None else []) + [
            color.toTuple() for color in subcolors.values()
        ]
        matches = bool(values) and all(
            all(abs(actual - target) <= 1.0e-5
                for actual, target in zip(value, expected_color(name)))
            for value in values
        )
        color_checks[name] = coverage and matches
        color_assignments[name] = {
            "component_rgba": colors.get(name), "colored_subshapes": len(subcolors),
            "all_solids_covered": coverage, "all_rgba_match": matches,
        }
        if color_checks[name]:
            colors[name] = values[0]
    compound = assembly.toCompound()
    solids = compound.Solids()
    checks = {
        "component_names_exact": len(names) == len(set(names)) and set(names) == set(expected),
        "total_solid_count": len(solids) == sum(expected.values()),
        "compound_topology_valid": compound.isValid(),
        "all_solids_valid_positive_volume": bool(solids) and all(
            solid.isValid() and solid.Volume() > VOLUME_TOL for solid in solids
        ),
        "per_component_solid_counts": all(
            name in shapes and len(shapes[name].Solids()) == count
            for name, count in expected.items()
        ),
        "all_component_rgba_preserved": all(color_checks.values()),
    }
    if not checks["component_names_exact"] or not checks["per_component_solid_counts"]:
        raise ValueError(f"{path.name}: unexpected component structure: {names}")

    p = DEFAULTS
    local = {name: ring_local(shapes[name]) for name in (
        "ring_sleeve", "ring_cap", "display_glass", "ring_index_mark"
    )}
    cover_box = local["display_glass"].BoundingBox()
    cap_box = local["ring_cap"].BoundingBox()
    sleeve_box = local["ring_sleeve"].BoundingBox()
    marker_box = local["ring_index_mark"].BoundingBox()
    r = p.knob_outer_diameter_mm / 2.0
    bore = p.knob_inner_diameter_mm / 2.0
    bearing_front = p.ring_bearing_start_normal_mm + p.bearing_width_mm
    knurl_start = p.ring_bearing_start_normal_mm + p.ring_knurl_axial_land_mm
    knurl_end = bearing_front - p.ring_knurl_axial_land_mm
    spacing = pi * r / p.ring_knurl_count / tan(radians(p.ring_knurl_angle_deg))
    measured = {"core_missing_mm3": {}, "knurl_sample_pairs": {}}
    key_measurements = {}
    housing = shapes["housing"]
    housing_classifier = BRepClass3d_SolidClassifier(housing.Solids()[0].wrapped)
    bottom = shapes["bottom_cover"]
    checks["bottom_cover_rest_and_removal_clearance"] = all(
        abs(bottom.translate((0,0,dz)).intersect(housing).Volume()) <= VOLUME_TOL
        for dz in (0,-1,-4,-12)
    )
    bottom_box = bottom.BoundingBox()
    checks["bottom_cover_dimensions"] = all(abs(a-b) <= TOL for a,b in (
        (bottom_box.xlen,89.4),(bottom_box.ylen,89.4),
        (bottom_box.zmin,0),(bottom_box.zmax,3)))
    hole_checks, head_checks, nut_checks = [], [], []
    for x,y in ((-40,-29),(40,-29),(-40,14),(40,14)):
        hole_probe = cq.Solid.makeCylinder(1.69,12,cq.Vector(x,y,0))
        hole_checks.append(abs(hole_probe.intersect(bottom).Volume()) <= VOLUME_TOL
                           and abs(hole_probe.intersect(housing).Volume()) <= VOLUME_TOL)
        head = cq.Solid.makeCone(3.30,1.70,1.60,cq.Vector(x,y,0))
        head_checks.append(abs(head.intersect(bottom).Volume()) <= VOLUME_TOL)
        nut = cq.Workplane("XY").center(x,y).polygon(6,5.5/cos(pi/6)).extrude(2.4).translate((0,0,6)).val()
        nut_checks.append(abs(nut.intersect(housing).Volume()) <= VOLUME_TOL)
    checks["four_bottom_fastener_bores"] = all(hole_checks)
    checks["four_bottom_countersunk_seats"] = all(head_checks)
    checks["four_bottom_nut_pockets"] = all(nut_checks)
    opening_checks = []
    for name in ("button_left_1", "button_left_2", "button_right_1", "button_right_2"):
        key = shapes[name]
        side = -1 if "left" in name else 1
        number = int(name[-1])
        y = p.side_button_front_y_mm if number == 1 else p.side_button_rear_y_mm
        box = key.BoundingBox()
        face = box.xmin if side < 0 else box.xmax
        shifted = key.translate((-side * p.side_button_clearance_check_travel_mm, 0, 0))
        at_rest = abs(key.intersect(housing).Volume())
        inward = abs(shifted.intersect(housing).Volume())
        key_measurements[name] = {"face_recess_mm": p.width_mm / 2 - abs(face),
                                  "rest_intersection_mm3": at_rest,
                                  "clearance_test_intersection_mm3": inward}
        housing_classifier.Perform(gp_Pnt(side * 46, y, p.side_button_z_mm), TOL)
        opening_checks.append(housing_classifier.State() != TopAbs_IN)
        checks[f"{name}_dimensions_and_recess"] = (
            abs(box.ylen - (p.side_button_length_mm + 2 * p.side_button_flange_margin_mm)) <= TOL
            and abs(box.zlen - (p.side_button_height_mm + 2 * p.side_button_flange_margin_mm)) <= TOL
            and abs(box.xlen - (p.side_button_body_depth_mm + p.side_button_flange_thickness_mm)) <= TOL
            and abs((box.ymax + box.ymin) / 2 - y) <= TOL
            and abs((box.zmax + box.zmin) / 2 - p.side_button_z_mm) <= TOL
            and abs(p.width_mm / 2 - abs(face) - p.side_button_face_recess_mm) <= TOL
        )
        checks[f"{name}_rest_and_inward_clearance_only"] = at_rest <= VOLUME_TOL and inward <= VOLUME_TOL
    # Actual wall probes above/below the recess and its rounded ends; a pocket
    # that breaks through the top would fail. Do not assume a full-depth ramp:
    # the real housing has a 26-degree deck ending at a horizontal rear platform.
    border_ok = []
    pocket_empty = []
    for side in (-1, 1):
        for y in (14.3, 20, 27.5, 35, 40.7):
            for z in (39.5, 46.5):
                housing_classifier.Perform(gp_Pnt(side * 47.7, y, z), TOL)
                border_ok.append(housing_classifier.State() == TopAbs_IN)
        for y in (10.8, 44.2):
            housing_classifier.Perform(gp_Pnt(side * 47.7, y, 43), TOL)
            border_ok.append(housing_classifier.State() == TopAbs_IN)
        for y in (12, 27.5, 43):
            housing_classifier.Perform(gp_Pnt(side * 47.7, y, 43), TOL)
            pocket_empty.append(housing_classifier.State() != TopAbs_IN)
    checks["four_through_wall_key_guides_exist"] = all(opening_checks)
    checks["recesses_surrounded_by_real_side_wall"] = all(border_ok)
    checks["both_shallow_common_pockets_exist"] = all(pocket_empty)
    measured["recessed_keys"] = key_measurements
    sleeve = local["ring_sleeve"]
    sleeve_outer_radius_reference = p.knob_outer_diameter_mm / 2.0
    sleeve_core = cq.Workplane("XY").circle(sleeve_outer_radius_reference - p.ring_knurl_depth_mm - TOL).circle(
        p.bearing_outer_diameter_mm / 2.0 + p.ring_shell_radial_clearance_mm + TOL
    ).extrude(knurl_end - knurl_start - 2.0 * TOL).translate((0.0, 0.0, knurl_start + TOL)).val()
    sleeve_missing = sleeve_core.cut(sleeve).Volume()
    measured["core_missing_mm3"]["ring_sleeve"] = sleeve_missing
    checks["ring_sleeve_protected_core_intact"] = abs(sleeve_missing) <= VOLUME_TOL
    sleeve_classifier = BRepClass3d_SolidClassifier(sleeve.Solids()[0].wrapped)
    knurl_samples = []
    for row in range(1, int((knurl_end - knurl_start) / spacing) + 1):
        z = knurl_start + row * spacing
        if not knurl_start + TOL < z < knurl_end - TOL:
            continue
        for index in range(0, p.ring_knurl_count, 8):
            angle = (row + 2 * index) * pi / p.ring_knurl_count
            knurl_samples.append(
                not classify(sleeve_classifier, r - p.ring_knurl_depth_mm / 2, angle, z)
                and classify(sleeve_classifier, r - p.ring_knurl_depth_mm / 2,
                             angle + pi / p.ring_knurl_count, z)
            )
    measured["knurl_sample_pairs"]["ring_sleeve"] = {"passed": sum(knurl_samples), "total": len(knurl_samples)}
    checks["ring_sleeve_exported_knurl_present"] = bool(knurl_samples) and all(knurl_samples)

    cap = local["ring_cap"]
    waist_ok, waist_samples = sample_radial_profile(
        cap,
        p.ring_waist_radius_mm,
        p.ring_waist_normal_mm,
        16,
        0.03,
    )
    crown_ok, crown_samples = sample_radial_profile(
        cap,
        p.ring_crown_radius_mm,
        p.ring_crown_normal_mm,
        16,
        0.03,
    )
    top_ok, top_samples = sample_radial_profile(
        cap,
        p.ring_top_radius_mm,
        p.ring_front_normal_mm - 0.001,
        16,
        0.05,
    )
    measured["cap_radial_profile_samples"] = {
        "waist": waist_samples,
        "crown": crown_samples,
        "top": top_samples,
    }
    checks["ring_cap_profile_waist_16_sections"] = waist_ok
    checks["ring_cap_profile_crown_16_sections"] = crown_ok
    checks["ring_cap_top_radius_16_sections"] = top_ok

    classifier = BRepClass3d_SolidClassifier(cap.Solids()[0].wrapped)
    shoulder_z = p.ring_front_normal_mm - p.ring_transition_recess_mm
    shoulder_radius = bore + p.ring_transition_width_mm / 2
    checks["recessed_black_transition_shoulder_present"] = all(
        classify(classifier, shoulder_radius, index * pi / 8, shoulder_z - 0.05)
        and not classify(classifier, shoulder_radius, index * pi / 8, shoulder_z + 0.05)
        for index in range(16)
    )
    free_bore = cq.Workplane("XY").circle(bore - TOL).extrude(
        p.ring_front_normal_mm + 1
    ).val()
    intrusion = cap.intersect(free_bore).Volume()
    measured["cap_bore_intrusion_mm3"] = intrusion
    checks["transition_does_not_bridge_fixed_rotating_gap"] = abs(intrusion) <= VOLUME_TOL
    checks["cover_diameter_and_flush_height"] = (
        abs(cover_box.xlen - p.fixed_bezel_outer_diameter_mm) <= TOL
        and abs(cover_box.ylen - p.fixed_bezel_outer_diameter_mm) <= TOL
        and abs(cover_box.zmax - p.ring_front_normal_mm) <= TOL
        and abs(cover_box.zlen - p.display_cover_thickness_mm) <= TOL
    )
    checks["local_cap_xy_bbox_not_greater_than_54"] = (
        cap_box.xlen <= MAX_BBOX_DIAMETER_MM
        and cap_box.ylen <= MAX_BBOX_DIAMETER_MM
    )
    checks["local_sleeve_xy_bbox_not_greater_than_54"] = (
        sleeve_box.xlen <= MAX_BBOX_DIAMETER_MM
        and sleeve_box.ylen <= MAX_BBOX_DIAMETER_MM
    )
    inner_faces = [face for face in cap.Faces() if face.geomType() == "CYLINDER"
                   and abs(face._geomAdaptor().Cylinder().Radius() - bore) <= TOL]
    if not inner_faces:
        raise ValueError(f"{path.name}: missing ring ID40 cylindrical face")
    gap = min(local["display_glass"].distance(face) for face in inner_faces)
    measured["cover_to_inner_cylinder_distance_mm"] = gap
    checks["cover_to_cap_radial_gap_at_least_0p5"] = gap >= 0.5 - TOL
    checks["blue_marker_dimensions_and_flush"] = (
        abs(marker_box.xlen - p.ring_marker_width_mm) <= TOL
        and abs(marker_box.ylen - p.ring_marker_length_mm) <= TOL
        and abs(marker_box.zlen - p.ring_marker_depth_mm) <= TOL
        and abs(marker_box.zmax - p.ring_front_normal_mm) <= TOL
    )
    marker_overlap = cap.intersect(local["ring_index_mark"]).Volume()
    measured["marker_cap_intersection_mm3"] = marker_overlap
    checks["marker_not_overlapping_cap"] = abs(marker_overlap) <= VOLUME_TOL
    bounds = compound.BoundingBox()
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    result = {
        "file": path.name, "sha256": digest, "bytes": path.stat().st_size,
        "components": len(names), "solids": len(solids),
        "component_solids": {name: len(shape.Solids()) for name, shape in shapes.items()},
        "colors_rgba_srgb": colors,
        "color_assignments": color_assignments,
        "envelope_mm": [bounds.xlen, bounds.ylen, bounds.zlen],
        "measured": measured, "checks": checks, "all_checks_pass": all(checks.values()),
    }
    print(f"{path.name}: {len(names)} components, {len(solids)} solids; "
          f"{sum(checks.values())}/{len(checks)} checks", flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=OUTPUT_DIR)
    output = parser.parse_args().output_dir.resolve()
    results = {
        kind: read_and_check(output / f"{PREFIX}_{kind}.step", expected)
        for kind, expected in (("exterior", EXTERIOR), ("assembly", ASSEMBLY))
    }
    report = {
        "scope": "STEP file integration readback; not SolidWorks or physical validation",
        "files": results, "all_checks_pass": all(r["all_checks_pass"] for r in results.values()),
    }
    (output / f"{PREFIX}_step_readback.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    for kind, result in results.items():
        for name, passed in result["checks"].items():
            if not passed:
                print(f"FAILED: {kind}/{name}")
    return 0 if report["all_checks_pass"] else 1


if __name__ == "__main__":
    code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    if os.name == "nt":
        os._exit(code)  # Avoid the known OCP Windows interpreter-teardown crash.
    raise SystemExit(code)
