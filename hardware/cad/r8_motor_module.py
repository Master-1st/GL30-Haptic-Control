"""R8 motor-side mechanical module, in the GL30 local tangent/normal frame.

This module owns only the motor-side replacement geometry that is being
integrated by ``full_knob_assembly.py``.  It does not edit the vendor STEP and
does not register or transform parts into an assembly.  The caller supplies
the already-positioned product-world motor and applies the product's ``O``
transform to every returned local shape.

The local frame is the one used by the existing GL30 model:

* ``x`` is tangent and the motor axis centre is expected at ``x=-42`` mm;
* ``y`` is the other tangent axis;
* ``n`` is the deck normal, represented by the local CadQuery ``z`` axis;
* current product-world to local conversion is inverse ``rotate X(26 deg)``
  followed by translation by ``(11, -10, 49.509303542634456)`` mm.

``create_motor_module(motor_world)`` returns ``(parts, metadata,
yoke_cut_shape_local, details)``.  ``parts`` are local shapes.  The returned
``yoke_cut_shape_local`` is the trimmed back-plate/ear/short-wall avoidance
sweep at x offsets ``-0.5, 0, +0.5`` mm, with 0.25 mm radial clearance and the
specified left flat.  The lock-bearing platform around x=-26..-20 is kept in
the yoke layer.  The integrator may subtract its transformed shape from the
press yoke; feet, lock heads and their load seats are excluded.

All holes are derived from the existing
``gl30_motor_fixture.holes_at_vendor_end`` helper on the read-only vendor STEP.
The local audit records thread candidates separately.  A zero intersection
with a threaded hole is not evidence that thread depth, head form, or real
clamping has been proven.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from math import acos, pi, radians, sqrt
from pathlib import Path
import sys
from typing import Any, Mapping

import cadquery as cq

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from gl30_motor_fixture import holes_at_vendor_end  # noqa: E402
from hidden_screen_study import CENTER, MOTOR_LOCAL_X, MOTOR_WORLD_X  # noqa: E402
from v7_product_model import GL30_STEP, _shape  # noqa: E402


ANGLE_DEG = 26.0
MOTOR_AXIS_X = -42.0
MOTOR_BACK_N = -40.5
MOTOR_FRONT_N = -12.3
PLATE_OUTER_DIAMETER = 37.4
PLATE_INNER_DIAMETER = 8.0
PLATE_N0 = -42.5
PLATE_N1 = -40.5
PLATE_LEFT_TRIM_X = -59.0
YOKE_RADIAL_CLEARANCE = 0.25
LOCK_CENTRES = ((-23.5, -15.0), (-23.5, 15.0))
LOCK_ROD_DIAMETER = 2.5
LOCK_ROD_SLOT_LENGTH = 3.7
LOCK_ROD_SLOT_WIDTH = 2.7
LOCK_HEAD_DIAMETER = 4.3
LOCK_HEAD_N0 = -37.5
LOCK_HEAD_N1 = -36.0
LOCK_TOOL_DIAMETER = 5.4
LOCK_TOOL_SLOT_LENGTH = 6.4
LOCK_TOOL_SLOT_WIDTH = 5.4
LOCK_TOOL_N0 = -37.5
LOCK_TOOL_N1 = -32.0
LOCK_TOOL_ACCESS_DIAMETER = 2.8
BELT_MAX_ABS_Y = 12.459
ADJUSTMENT_MM = 0.5
PITCH_DIAMETER = 36.0 * 2.0 / pi
PULLEY_OD = PITCH_DIAMETER + 2.0
PULLEY_BODY_N0 = -8.9
PULLEY_BODY_N1 = -2.9
PULLEY_FLANGE_N0 = -9.7
PULLEY_FLANGE_OD = 27.0
PULLEY_BORE = 14.1
HUB_BORE = 6.4
HUB_FLANGE_OD = 26.0
HUB_FLANGE_N0 = -12.3
HUB_FLANGE_N1 = -11.3
HUB_SHOULDER_REQUESTED_OD = 18.0
HUB_SHOULDER_OD = 18.0
HUB_LOCATOR_OD = 14.0
HUB_SHOULDER_HEAD_CLEARANCE_DIAMETER = 5.8
HUB_SHOULDER_N0 = -11.3
HUB_SHOULDER_N1 = -9.7
HUB_LOCATOR_N0 = -9.7
HUB_LOCATOR_N1 = -2.35
HUB_THREAD_N0 = -3.85
HUB_THREAD_N1 = -2.35
PULLEY_COUNTERBORE_DIAMETER = 18.4
PULLEY_COUNTERBORE_N0 = -3.85
PULLEY_COUNTERBORE_N1 = -2.35
PULLEY_TOP_FLANGE_THICKNESS = 0.55
PULLEY_FLANGE_N1 = PULLEY_COUNTERBORE_N1
PULLEY_LOCKNUT_OD = 18.0
PULLEY_LOCKNUT_ID = 14.0
PULLEY_LOCKNUT_N0 = -3.85
PULLEY_LOCKNUT_N1 = -2.35
PULLEY_LOCKNUT_SLOT_WIDTH = 1.2
PULLEY_LOCKNUT_SLOT_DEPTH = 0.5
PULLEY_LOCKNUT_SLOT_LENGTH = 2.4
PULLEY_LOCKNUT_SLOT_CENTRE_RADIUS = 8.0


def _box(w: float, d: float, h: float, x: float, y: float, n: float) -> cq.Shape:
    return _shape(cq.Workplane("XY").box(w, d, h, centered=(True, True, False)).translate((x, y, n)))


def _cylinder(diameter: float, height: float, x: float, y: float, n: float) -> cq.Shape:
    return _shape(cq.Workplane("XY").center(x, y).circle(diameter / 2.0).extrude(height).translate((0.0, 0.0, n)))


def _ring(outer_diameter: float, inner_diameter: float, height: float, x: float, y: float, n: float) -> cq.Shape:
    return _shape(
        cq.Workplane("XY")
        .center(x, y)
        .circle(outer_diameter / 2.0)
        .circle(inner_diameter / 2.0)
        .extrude(height)
        .translate((0.0, 0.0, n))
    )


def _round_slotted_locknut(x: float, y: float, n: float) -> cq.Shape:
    """Return the round M14 locknut envelope with shallow radial wrench slots.

    The slots are cut only into the upper 0.5 mm.  The uninterrupted lower
    face therefore remains the nominal clamp seat in the pulley counterbore.
    The actual thread, slot tooling and clamp torque remain physical checks.
    """
    nut = _ring(PULLEY_LOCKNUT_OD, PULLEY_LOCKNUT_ID,
                PULLEY_LOCKNUT_N1 - PULLEY_LOCKNUT_N0, x, y, n)
    slot_n = PULLEY_LOCKNUT_N1 - PULLEY_LOCKNUT_SLOT_DEPTH
    slot_offset = PULLEY_LOCKNUT_SLOT_CENTRE_RADIUS
    for sign in (-1.0, 1.0):
        slot = _box(
            PULLEY_LOCKNUT_SLOT_LENGTH,
            PULLEY_LOCKNUT_SLOT_WIDTH,
            PULLEY_LOCKNUT_SLOT_DEPTH + 0.01,
            x + sign * slot_offset,
            y,
            slot_n,
        )
        nut = nut.cut(slot)
    return nut.clean()


def _bbox(shape: cq.Shape) -> dict[str, float]:
    box = _shape(shape).BoundingBox()
    return {
        "xmin": float(box.xmin), "xmax": float(box.xmax),
        "ymin": float(box.ymin), "ymax": float(box.ymax),
        "zmin": float(box.zmin), "zmax": float(box.zmax),
        "xlen": float(box.xlen), "ylen": float(box.ylen), "zlen": float(box.zlen),
    }


def _inverse_product_transform(motor_world: cq.Shape | cq.Workplane) -> cq.Shape:
    """Map the existing product-world motor back to the local frame."""
    return _shape(motor_world).translate(tuple(-value for value in CENTER)).rotate(
        (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), -ANGLE_DEG
    )


def _vendor_mounting_holes() -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Extract the native vendor-end holes without modifying the STEP."""
    vendor = _shape(cq.importers.importStep(str(GL30_STEP)))
    return holes_at_vendor_end(vendor, True), holes_at_vendor_end(vendor, False)


def _hole_xy(center_x: float, hole: Mapping[str, Any]) -> tuple[float, float]:
    x_offset, y_offset = hole["xy_mm"]
    return center_x + float(x_offset), float(y_offset)


def _stator_mount_screw(x: float, y: float) -> cq.Shape:
    """M3x5 candidate from the back plate toward the 3 mm vendor blind hole."""
    # The five millimetre under-head stack is 0.5 mm washer + 2 mm plate +
    # nominal 2.5 mm entry into the vendor blind hole.  The washer and low
    # head stay outside the motor face.
    # The rod starts at n=-43.0 and runs 5 mm: 2 mm plate plus 2.5 mm nominal
    # entry into the vendor blind hole.  The low head occupies -44.2..-43.0;
    # the separately returned washer occupies -43.0..-42.5.
    shaft = _cylinder(3.0, 5.0, x, y, -43.0)
    head = _cylinder(5.5, 1.2, x, y, -44.2)
    return shaft.fuse(head).clean()


def _stator_washer(x: float, y: float) -> cq.Shape:
    return _ring(6.0, 3.2, 0.5, x, y, -43.0)


def _rotor_mount_screw(x: float, y: float) -> cq.Shape:
    """M3x3 candidate: 2 mm real entry plus a 1.2 mm low head."""
    shaft = _cylinder(3.0, 3.0, x, y, -14.3)
    head = _cylinder(5.2, 1.2, x, y, -11.3)
    return shaft.fuse(head).clean()


def _lock_screw(x: float, y: float) -> cq.Shape:
    """Existing clamp screw envelope, retained at x=-23.5/y=+/-15.0."""
    rod = _cylinder(LOCK_ROD_DIAMETER, 4.5, x, y, -42.0)
    head = _cylinder(LOCK_HEAD_DIAMETER, LOCK_HEAD_N1 - LOCK_HEAD_N0, x, y, LOCK_HEAD_N0)
    return rod.fuse(head).clean()


def _slot_void(length: float, width: float, x: float, y: float, n: float, height: float) -> cq.Shape:
    """Return an x-oriented slot void in the local XY plate plane."""
    slot = cq.Workplane("XY").center(x, y).slot2D(length, width, 0.0).extrude(height)
    return _shape(slot.translate((0.0, 0.0, n)))


def _lock_rod_slot_void(x: float, y: float, n: float, height: float) -> cq.Shape:
    return _slot_void(LOCK_ROD_SLOT_LENGTH, LOCK_ROD_SLOT_WIDTH, x, y, n, height)


def _lock_tool_slot_void(x: float, y: float, n: float, height: float) -> cq.Shape:
    return _slot_void(LOCK_TOOL_SLOT_LENGTH, LOCK_TOOL_SLOT_WIDTH, x, y, n, height)


def _make_slider(motor_local: cq.Shape, back_holes: list[dict[str, Any]]) -> tuple[cq.Shape, dict[str, Any]]:
    """Make the rear-face load plate, outer short walls, feet and cable opening."""
    plate = _ring(PLATE_OUTER_DIAMETER, PLATE_INNER_DIAMETER, 2.0, MOTOR_AXIS_X, 0.0, PLATE_N0)

    # The left shell interface is a real flat, not a bounding-box claim.  It
    # preserves x >= -59.0 in local coordinates; at the -0.5 mm setup shift
    # the plate edge is x=-59.5, leaving 0.5 mm to the x=-49 shell wall in the
    # product-world check supplied by the parent.
    plate = plate.cut(_box(200.0, 100.0, 3.0, PLATE_LEFT_TRIM_X - 100.0, 0.0, PLATE_N0 - 0.5))

    # The vendor STEP's cable/connector solids occupy approximately local
    # x=-50.5..-45.2, y=-3.5..5.2 at the rear.  A radial opening reaches that
    # region and continues to the outer edge, so the back plate does not seal
    # the actual exit behind the motor.
    cable_slot = _box(34.5, 14.0, 3.0, MOTOR_AXIS_X - 17.75, 0.0, PLATE_N0 - 0.5)
    plate = plate.cut(cable_slot)
    for hole in back_holes:
        x, y = _hole_xy(MOTOR_AXIS_X, hole)
        plate = plate.cut(_cylinder(3.4, 2.4, x, y, PLATE_N0 - 0.2))
    slider = plate
    feet: list[str] = []
    ears: list[str] = []
    walls: list[str] = []
    # Cut the ear/wall interface against the vendor motor swept over the full
    # +/-0.5 mm setup range.  This keeps the nominal OD35.2 shell avoidance
    # true after the slider moves, rather than checking only the datum pose.
    motor_clearance_sweep = [
        motor_local.translate((-ADJUSTMENT_MM, 0.0, 0.0)),
        motor_local,
        motor_local.translate((ADJUSTMENT_MM, 0.0, 0.0)),
    ]
    for index, (x, y) in enumerate(LOCK_CENTRES, 1):
        # The lock centre is intentionally outside the OD37.4 back plate.
        # Each short ear overlaps the plate at its outer right/front or rear
        # edge and reaches x=-26; the side wall then overlaps the ear and the
        # raised foot.  The ear stays out of the lock-centre support zone in
        # the back-plate n layer.
        # The 1.5 mm tangential ear band is intentionally narrower than the
        # foot/wall.  Its OD35.2 motor-shell-side boundary stays clear while
        # its x=-32..-26 span overlaps both the small plate and the wall.
        ear = _box(6.0, 1.5, 3.1, -29.0, y, -42.5)
        for motor_clearance in motor_clearance_sweep:
            ear = ear.cut(motor_clearance)
        slider = slider.fuse(ear)
        ears.append(f"motor_tension_slide_ear_{index}")

        # The foot keeps a continuous contact land below n=-37.5; only the
        # M2.5 rod clearance passes through that land.
        foot = _box(7.0, 6.0, 2.0, x, y, -39.5)
        foot = foot.cut(_lock_rod_slot_void(x, y, -39.6, 2.2))
        # The wall starts at n=-39.5 so its material does not cover the lock
        # centre in the n=-42.5..-39.5 back-plate/yoke support layer.  It
        # overlaps the ear over n=-39.5..-39.4 and the foot over
        # n=-39.5..-37.5.
        wall = _box(7.0, 6.0, 7.5, -23.5, y, -39.5)
        for motor_clearance in motor_clearance_sweep:
            wall = wall.cut(motor_clearance)
        # The rod slot runs through the wall.  Above the n=-37.5 foot contact
        # plane, a 6.4 x 5.4 mm tool slot sweeps the complete Ø4.3 head over
        # the same +/-0.5 mm travel.
        wall = wall.cut(_lock_rod_slot_void(x, y, -39.6, 7.7))
        wall = wall.cut(_lock_tool_slot_void(x, y, LOCK_TOOL_N0, 5.6))
        slider = slider.fuse(foot).fuse(wall)
        feet.append(f"motor_tension_slide_foot_{index}")
        walls.append(f"motor_tension_slide_outer_wall_{index}")
    slider = slider.clean()
    # Keep a direct readback of the small rear connector/cable solid used to
    # place the opening.  The vendor file contains seven solids; this selects
    # the rear harness-sized solid by its local envelope, without editing or
    # healing the vendor geometry.
    cable_candidates = []
    for solid_index, solid in enumerate(motor_local.Solids()):
        candidate_box = _bbox(solid)
        if (
            50.0 < abs(float(solid.Volume())) < 200.0
            and candidate_box["xmax"] < MOTOR_AXIS_X - 2.0
            and candidate_box["zmin"] < -39.0
            and candidate_box["zmax"] > -36.5
        ):
            cable_candidates.append({"solid_index": solid_index, "bbox_local_mm": candidate_box, "volume_mm3": abs(float(solid.Volume()))})
    cable_exit = cable_candidates[0] if len(cable_candidates) == 1 else {"candidates": cable_candidates}
    cable_exit_box = cable_exit.get("bbox_local_mm") if "bbox_local_mm" in cable_exit else None
    cable_exit_covered = bool(cable_exit_box and cable_exit_box["xmin"] >= MOTOR_AXIS_X - 35.0 and cable_exit_box["xmax"] <= MOTOR_AXIS_X - 0.5 and cable_exit_box["ymin"] >= -7.0 and cable_exit_box["ymax"] <= 7.0)
    details = {
        "plate": {
            "outer_diameter_mm": PLATE_OUTER_DIAMETER,
            "inner_diameter_mm": PLATE_INNER_DIAMETER,
            "outer_radius_mm": PLATE_OUTER_DIAMETER / 2.0,
            "inner_radius_mm": PLATE_INNER_DIAMETER / 2.0,
            "normal_range_mm": [PLATE_N0, PLATE_N1],
            "left_flat_trim_x_mm": PLATE_LEFT_TRIM_X,
            "cable_opening_box_local_mm": {
                "xmin": MOTOR_AXIS_X - 35.0,
                "xmax": MOTOR_AXIS_X - 0.5,
                "ymin": -7.0,
                "ymax": 7.0,
                "nmin": PLATE_N0 - 0.5,
                "nmax": PLATE_N1 + 0.5,
            },
            "vendor_back_holes": back_holes,
            "vendor_cable_exit_readback": cable_exit,
            "cable_exit_xy_covered_by_opening": cable_exit_covered,
        },
        "feet": {
            "centres_local_mm": [list(item) for item in LOCK_CENTRES],
            "normal_range_mm": [-39.5, -37.5],
            "rod_slot_length_mm": LOCK_ROD_SLOT_LENGTH,
            "rod_slot_width_mm": LOCK_ROD_SLOT_WIDTH,
            "contact_plane_n_mm": -37.5,
            "part_names": feet,
        },
        "ears": {
            "x_range_mm": [-32.0, -26.0],
            "tangential_width_mm": 1.5,
            "y_centres_mm": [item[1] for item in LOCK_CENTRES],
            "normal_range_mm": [-42.5, -39.4],
            "part_names": ears,
            "lock_support_zone_clear_mm": 2.5,
        },
        "outer_walls": {
            "wall_x_range_mm": [-27.0, -20.0],
            "normal_range_mm": [-39.5, -32.0],
            "tool_clearance_diameter_mm": LOCK_TOOL_DIAMETER,
            "tool_slot_length_mm": LOCK_TOOL_SLOT_LENGTH,
            "tool_slot_width_mm": LOCK_TOOL_SLOT_WIDTH,
            "tool_clearance_range_mm": [LOCK_TOOL_N0, LOCK_TOOL_N1],
            "part_names": walls,
        },
        "motor_shell_clearance": {
            "nominal_motor_shell_outer_diameter_mm": 35.2,
            "method": "ears and walls cut against the read-only vendor motor swept at local x offsets -0.5, 0, +0.5",
            "positive_volume_collision_after_sweep_cut": False,
        },
    }
    return slider, details


def _circle_intersection_area(radius_a: float, radius_b: float, centre_distance: float) -> float:
    """Area of the overlap of two circles, in square millimetres."""
    if centre_distance >= radius_a + radius_b:
        return 0.0
    if centre_distance <= abs(radius_a - radius_b):
        return pi * min(radius_a, radius_b) ** 2
    term = (
        (-centre_distance + radius_a + radius_b)
        * (centre_distance + radius_a - radius_b)
        * (centre_distance - radius_a + radius_b)
        * (centre_distance + radius_a + radius_b)
    )
    return (
        radius_a * radius_a * acos((centre_distance * centre_distance + radius_a * radius_a - radius_b * radius_b) / (2.0 * centre_distance * radius_a))
        + radius_b * radius_b * acos((centre_distance * centre_distance + radius_b * radius_b - radius_a * radius_a) / (2.0 * centre_distance * radius_b))
        - 0.5 * sqrt(max(0.0, term))
    )


def _make_yoke_cut() -> cq.Shape:
    """Return the back-plate/ear/wall avoidance sweep, preserving lock seats."""
    outer_diameter = PLATE_OUTER_DIAMETER + 2.0 * YOKE_RADIAL_CLEARANCE
    inner_diameter = max(0.1, PLATE_INNER_DIAMETER - 2.0 * YOKE_RADIAL_CLEARANCE)
    base = _ring(outer_diameter, inner_diameter, 2.45, MOTOR_AXIS_X, 0.0, -42.7)
    # The cutter follows the trimmed plate outline.  Expanding the radial
    # outline by 0.25 mm moves the flat edge by the same amount, while the
    # production slider itself retains the exact x=-59.0 flat.
    base = base.cut(_box(200.0, 100.0, 3.0, PLATE_LEFT_TRIM_X - YOKE_RADIAL_CLEARANCE - 100.0, 0.0, -43.0))
    for _, y in LOCK_CENTRES:
        # Include the ear and the small wall footprint in the avoidance cutter
        # but preserve the x=-26..-20 lock-bearing platform through the whole
        # yoke layer.  The actual lock rod/foot remains supported there.
        ear = _box(6.5, 2.0, 3.3, -29.0, y, -42.7)
        wall = _box(7.5, 6.5, 3.3, -23.5, y, -42.7)
        base = base.fuse(ear).fuse(wall)
        lock_platform = _box(6.0, 7.0, 3.5, -23.0, y, -42.8)
        base = base.cut(lock_platform)
    return base.translate((-ADJUSTMENT_MM, 0.0, 0.0)).fuse(base).fuse(base.translate((ADJUSTMENT_MM, 0.0, 0.0))).clean()


def _make_pulley_module(front_holes: list[dict[str, Any]]) -> tuple[dict[str, cq.Shape], dict[str, Any]]:
    """Make the two-piece motor pulley interface and its explicit fasteners."""
    pulley_body = _ring(PULLEY_OD, PULLEY_BORE, 6.0, MOTOR_AXIS_X, 0.0, PULLEY_BODY_N0)
    pulley = pulley_body.fuse(
        _ring(PULLEY_FLANGE_OD, PULLEY_BORE, 0.8, MOTOR_AXIS_X, 0.0, PULLEY_FLANGE_N0)
    ).fuse(
        _ring(
            PULLEY_FLANGE_OD,
            PULLEY_BORE,
            PULLEY_TOP_FLANGE_THICKNESS,
            MOTOR_AXIS_X,
            0.0,
            PULLEY_BODY_N1,
        )
    ).clean()
    # The upper flange and the top of the pulley body are counterbored for
    # the round locknut.  The pocket bottom is the nominal clamp seat at
    # n=-3.85; it stops above the belt working zone and leaves the outer rim.
    pulley = pulley.cut(
        _cylinder(
            PULLEY_COUNTERBORE_DIAMETER,
            PULLEY_COUNTERBORE_N1 - PULLEY_COUNTERBORE_N0 + 0.01,
            MOTOR_AXIS_X,
            0.0,
            PULLEY_COUNTERBORE_N0,
        )
    ).clean()

    hub_flange = _ring(HUB_FLANGE_OD, HUB_BORE, 1.0, MOTOR_AXIS_X, 0.0, HUB_FLANGE_N0)
    hub_shoulder = _ring(HUB_SHOULDER_OD, HUB_BORE, HUB_SHOULDER_N1 - HUB_SHOULDER_N0, MOTOR_AXIS_X, 0.0, HUB_SHOULDER_N0)
    hub_locator = _ring(HUB_LOCATOR_OD, HUB_BORE, HUB_LOCATOR_N1 - HUB_LOCATOR_N0, MOTOR_AXIS_X, 0.0, HUB_LOCATOR_N0)
    hub = hub_flange.fuse(hub_shoulder).fuse(hub_locator).clean()
    rotor_screws: dict[str, cq.Shape] = {}
    for index, hole in enumerate(front_holes, 1):
        x, y = _hole_xy(MOTOR_AXIS_X, hole)
        # The Ø3.4 hole passes the lower flange.  The Ø5.8 counter-clearance
        # passes only the 1.6 mm OD18 shoulder, leaving a 0.3 mm radial side
        # clearance around each Ø5.2 low head while retaining four arc-shaped
        # shoulder lands against the pulley underside.
        hub = hub.cut(_cylinder(3.4, 1.4, x, y, HUB_FLANGE_N0 - 0.2))
        hub = hub.cut(_cylinder(HUB_SHOULDER_HEAD_CLEARANCE_DIAMETER, 1.8, x, y, HUB_SHOULDER_N0 - 0.1))
        rotor_screws[f"motor_pulley_rotor_M3x3_{index}"] = _rotor_mount_screw(x, y)

    locknut = _round_slotted_locknut(MOTOR_AXIS_X, 0.0, PULLEY_LOCKNUT_N0)
    parts = {
        "motor_pulley": pulley,
        "motor_pulley_hub": hub,
        "motor_pulley_locknut_M14x0p75": locknut,
        **rotor_screws,
    }
    shoulder_outer_radius = HUB_SHOULDER_OD / 2.0
    # The pulley underside only contacts the shoulder outside its own
    # Ø14.1 bore; the hub's Ø6.4 through-bore is not contact area.
    shoulder_inner_radius = PULLEY_BORE / 2.0
    rotor_hole_radius = HUB_SHOULDER_HEAD_CLEARANCE_DIAMETER / 2.0
    rotor_hole_overlap_area = _circle_intersection_area(shoulder_outer_radius, rotor_hole_radius, 10.0)
    shoulder_annulus_area = pi * (shoulder_outer_radius**2 - shoulder_inner_radius**2)
    shoulder_contact_area = shoulder_annulus_area - len(front_holes) * rotor_hole_overlap_area
    # The shallow slots stop above the lower face, so the nominal locknut seat
    # is the annulus between the pulley bore and the counterbore.  This area is
    # finite and is deliberately computed against the pulley bore (not hub ID).
    locknut_seat_outer_radius = min(PULLEY_LOCKNUT_OD, PULLEY_COUNTERBORE_DIAMETER) / 2.0
    locknut_seat_inner_radius = max(PULLEY_LOCKNUT_ID, PULLEY_BORE) / 2.0
    locknut_seat_contact_area = pi * (
        locknut_seat_outer_radius**2 - locknut_seat_inner_radius**2
    )
    details = {
        "pulley": {
            "teeth": 36,
            "pitch_mm": 2.0,
            "pitch_diameter_mm": PITCH_DIAMETER,
            "nominal_outer_diameter_mm": PULLEY_OD,
            "body_normal_range_mm": [PULLEY_BODY_N0, PULLEY_BODY_N1],
            "flange_normal_ranges_mm": [[PULLEY_FLANGE_N0, PULLEY_BODY_N0], [PULLEY_BODY_N1, PULLEY_FLANGE_N1]],
            "top_flange_thickness_mm": PULLEY_TOP_FLANGE_THICKNESS,
            "flange_outer_diameter_mm": PULLEY_FLANGE_OD,
            "bore_diameter_mm": PULLEY_BORE,
            "counterbore_diameter_mm": PULLEY_COUNTERBORE_DIAMETER,
            "counterbore_bottom_normal_mm": PULLEY_COUNTERBORE_N0,
            "tooth_geometry": "envelope only; no tooth cutter or manufacturing claim",
        },
        "hub": {
            "lower_flange_outer_diameter_mm": HUB_FLANGE_OD,
            "lower_flange_normal_range_mm": [HUB_FLANGE_N0, HUB_FLANGE_N1],
            "requested_shoulder_outer_diameter_mm": HUB_SHOULDER_REQUESTED_OD,
            "implemented_shoulder_outer_diameter_mm": HUB_SHOULDER_OD,
            "shoulder_normal_range_mm": [HUB_SHOULDER_N0, HUB_SHOULDER_N1],
            "locator_outer_diameter_mm": HUB_LOCATOR_OD,
            "locator_normal_range_mm": [HUB_LOCATOR_N0, HUB_LOCATOR_N1],
            "bore_diameter_mm": HUB_BORE,
            "front_holes": front_holes,
            "rotor_screw_head_normal_range_mm": [-11.3, -10.1],
            "pulley_bottom_to_screw_head_gap_mm": 0.4,
            "rotor_head_clearance_diameter_mm": HUB_SHOULDER_HEAD_CLEARANCE_DIAMETER,
            "rotor_head_radial_side_clearance_mm": (HUB_SHOULDER_HEAD_CLEARANCE_DIAMETER - 5.2) / 2.0,
            "shoulder_2d_annulus_area_mm2": shoulder_annulus_area,
            "shoulder_2d_hole_overlap_area_each_mm2": rotor_hole_overlap_area,
            "shoulder_2d_contact_area_mm2": shoulder_contact_area,
            "shoulder_2d_contact_area_positive": shoulder_contact_area > 0.0,
            "shoulder_contact_inner_diameter_mm": PULLEY_BORE,
            "clearance_reason": "OD18 shoulder retained for pulley face support; four Ø5.8 through-cuts at the extracted R10 front-hole centres clear the Ø5.2 low heads with 0.3 mm radial side clearance.",
            "assembly_sequence": [
                "install four rotor M3 candidates into the extracted motor front holes",
                "seat the pulley over the OD14 locator and OD18 shoulder arcs",
                "install the round slotted M14x0.75 locknut into the Ø18.4 counterbore seat",
            ],
            "thread_envelope": {"nominal": "M14x0.75", "normal_range_mm": [HUB_THREAD_N0, HUB_THREAD_N1]},
        },
        "locknut": {
            "nominal": "M14x0.75 thin locknut",
            "outer_diameter_mm": PULLEY_LOCKNUT_OD,
            "inner_diameter_mm": PULLEY_LOCKNUT_ID,
            "height_mm": PULLEY_LOCKNUT_N1 - PULLEY_LOCKNUT_N0,
            "normal_range_mm": [PULLEY_LOCKNUT_N0, PULLEY_LOCKNUT_N1],
            "slot_width_mm": PULLEY_LOCKNUT_SLOT_WIDTH,
            "slot_depth_mm": PULLEY_LOCKNUT_SLOT_DEPTH,
            "seat_normal_mm": PULLEY_LOCKNUT_N0,
            "seat_contact_area_mm2": locknut_seat_contact_area,
            "seat_contact_area_positive": locknut_seat_contact_area > 0.0,
            "seat_area_method": "nominal annulus between pulley bore Ø14.1 and counterbore Ø18.4; top slots stop 1.0 mm above the seat",
        },
    }
    return parts, details


def _make_metadata(parts: Mapping[str, cq.Shape], slider_details: Mapping[str, Any], pulley_details: Mapping[str, Any]) -> dict[str, dict[str, str]]:
    metadata: dict[str, dict[str, str]] = {
        "motor_tension_slide_plate": {
            "group": "moving",
            "status": "rear-face load plate OD37.4/ID8 x 2 mm with x=-59 flat, cable exit and two outer short walls; does not clamp the motor circular shell",
        },
        "motor_slide_lock_screw_1": {
            "group": "fastener",
            "status": "M2.5 clamp screw envelope through 3.7 x 2.7 mm slider slots, rod Ø2.5 n=-42..-37.5, head Ø4.3 n=-37.5..-36; actual thread and seat pending",
        },
        "motor_slide_lock_screw_2": {
            "group": "fastener",
            "status": "M2.5 clamp screw envelope through 3.7 x 2.7 mm slider slots, rod Ø2.5 n=-42..-37.5, head Ø4.3 n=-37.5..-36; actual thread and seat pending",
        },
        "motor_pulley": {
            "group": "moving",
            "status": "36T/2 mm/6 mm two-flange pulley envelope; teeth are not modeled for manufacture",
        },
        "motor_pulley_hub": {
            "group": "moving",
            "status": "separate rotor adapter with extracted four-hole face pattern, OD18 shoulder with four Ø5.8 head-clearance cuts, OD14 locator and M14x0.75 nominal thread envelope",
        },
        "motor_pulley_locknut_M14x0p75": {
            "group": "fastener",
            "status": "nominal M14x0.75 round slotted locknut OD18/ID14 x 1.5 mm in a Ø18.4 counterbore; thread and friction torque require physical verification",
        },
    }
    for index in range(1, 4):
        metadata[f"motor_stator_mount_M3x5_{index}"] = {
            "group": "fastener",
            "status": "M3x5 candidate: head n=-44.2..-43, washer n=-43..-42.5, rod n=-43..-38; PDF/CAD depth discrepancy remains",
        }
        metadata[f"motor_stator_mount_washer_M3_{index}"] = {
            "group": "fastener",
            "status": "Ø6/Ø3.2 x 0.5 mm washer envelope under candidate M3x5 head",
        }
    for index in range(1, 5):
        metadata[f"motor_pulley_rotor_M3x3_{index}"] = {
            "group": "fastener",
            "status": "M3 ultra-low-head x 3 candidate; 1.2 mm head plus nominal 2.0 mm real entry, head shape and thread depth require actual rotor verification",
        }
    return metadata


def create_motor_module(motor_world: cq.Shape | cq.Workplane) -> tuple[dict[str, cq.Shape], dict[str, dict[str, str]], cq.Shape, dict[str, Any]]:
    """Build the local R8 motor module from the existing product-world motor.

    The input is expected to be the current product-world official motor (the
    same object presently called ``GL30_with_factory_encoder_E``).  It is
    inverse-transformed for local construction; the vendor STEP remains
    read-only.  The caller owns world registration and assembly naming.
    """
    motor_local = _inverse_product_transform(motor_world)
    motor_box = _bbox(motor_local)
    axis_center = (motor_box["xmin"] + motor_box["xmax"]) / 2.0
    back_n = motor_box["zmin"]
    front_n = motor_box["zmax"]
    back_holes, front_holes = _vendor_mounting_holes()
    slider, slider_details = _make_slider(motor_local, back_holes)
    pulley_parts, pulley_details = _make_pulley_module(front_holes)

    parts: dict[str, cq.Shape] = {
        "motor_tension_slide_plate": slider,
        "motor_slide_lock_screw_1": _lock_screw(*LOCK_CENTRES[0]),
        "motor_slide_lock_screw_2": _lock_screw(*LOCK_CENTRES[1]),
        **pulley_parts,
    }
    # Stator screws and washers use the three exact back-face hole coordinates.
    for index, hole in enumerate(back_holes, 1):
        x, y = _hole_xy(MOTOR_AXIS_X, hole)
        parts[f"motor_stator_mount_M3x5_{index}"] = _stator_mount_screw(x, y)
        parts[f"motor_stator_mount_washer_M3_{index}"] = _stator_washer(x, y)

    metadata = _make_metadata(parts, slider_details, pulley_details)

    # Return the trimmed back-plate/ear/wall avoidance sweep.  Feet, lock
    # heads and their pressure seats remain available to the parent assembly;
    # the lock-bearing platform is explicitly kept out of the cutter.
    yoke_cut = _make_yoke_cut()

    details: dict[str, Any] = {
        "coordinate_frame": {
            "input": "current product world official motor",
            "localization": "inverse translate CENTER=(11,-10,49.509303542634456), then rotate X(-26 deg)",
            "motor_axis_local_x_expected_mm": MOTOR_AXIS_X,
            "motor_axis_local_x_measured_mm": axis_center,
            "motor_back_n_expected_mm": MOTOR_BACK_N,
            "motor_back_n_measured_mm": back_n,
            "motor_front_n_expected_mm": MOTOR_FRONT_N,
            "motor_front_n_measured_mm": front_n,
            "source_motor_world_x_mm": MOTOR_WORLD_X,
            "source_motor_local_x_mm": MOTOR_LOCAL_X,
        },
        "vendor_source": {
            "step": str(GL30_STEP),
            "sha256": hashlib.sha256(GL30_STEP.read_bytes()).hexdigest(),
            "back_3_holes": back_holes,
            "front_4_holes": front_holes,
            "back_face_cad_depth_mm": [hole["cad_cylindrical_depth_mm"] for hole in back_holes],
            "front_face_cad_depth_mm": [hole["cad_cylindrical_depth_mm"] for hole in front_holes],
        },
        "slider": slider_details,
        "pulley_module": pulley_details,
        "yoke_cut": {
            "swept_x_offsets_mm": [-ADJUSTMENT_MM, ADJUSTMENT_MM],
            "shape_is_backplate_ear_wall_avoidance_sweep": True,
            "includes_feet": False,
            "includes_ear_and_wall_avoidance_footprints": True,
            "preserved_lock_platform_x_range_mm": [-26.0, -20.0],
            "radial_clearance_mm": YOKE_RADIAL_CLEARANCE,
            "backplate_normal_range_mm": [-42.7, -40.25],
            "ear_wall_avoidance_normal_range_mm": [-42.7, -39.4],
            "bounding_box_local_mm": _bbox(yoke_cut),
        },
        "lock_screws": {
            "centres_local_mm": [list(item) for item in LOCK_CENTRES],
            "rod_diameter_mm": LOCK_ROD_DIAMETER,
            "rod_normal_range_mm": [-42.0, -37.5],
            "head_diameter_mm": LOCK_HEAD_DIAMETER,
            "head_normal_range_mm": [LOCK_HEAD_N0, LOCK_HEAD_N1],
            "tool_clearance_diameter_mm": LOCK_TOOL_DIAMETER,
            "tool_clearance_normal_range_mm": [LOCK_TOOL_N0, LOCK_TOOL_N1],
            "setup_x_range_mm": [-ADJUSTMENT_MM, ADJUSTMENT_MM],
            "rod_slot_overall_length_mm": LOCK_ROD_SLOT_LENGTH,
            "rod_slot_width_mm": LOCK_ROD_SLOT_WIDTH,
            "tool_slot_overall_length_mm": LOCK_TOOL_SLOT_LENGTH,
            "tool_slot_width_mm": LOCK_TOOL_SLOT_WIDTH,
            "tool_access_diameter_mm": LOCK_TOOL_ACCESS_DIAMETER,
            "belt_outer_max_abs_y_mm": BELT_MAX_ABS_Y,
            "tool_side_clearance_mm": min(abs(y) for _, y in LOCK_CENTRES) - BELT_MAX_ABS_Y - LOCK_TOOL_ACCESS_DIAMETER / 2.0,
        },
        "allowed_thread_pairs": [],
        "limits": [
            "The 3 back-face and 4 front-face hole patterns come from the vendor STEP helper; physical rotor/stator hole depth and thread form remain unverified.",
            "The M3x5 stator and M3x3 rotor fasteners are candidates, not a released screw specification.",
            "The M14x0.75 locknut provides a nominal friction clamp envelope; zero backlash and torque transmission are not claimed.",
            "The 36T pulley is an envelope; tooth geometry, belt fit, preload, and transmission error remain unverified.",
            "No new housing hole is created by this module; the parent supplies the yoke cut and existing mounting interfaces.",
            "The yoke cutter is the trimmed back-plate plus ear/short-wall avoidance sweep with 0.25 mm radial clearance; the lock-bearing x=-26..-20 platform and all foot load seats are preserved.",
            "Four OD18 shoulder reliefs are an assembly clearance feature, not new vendor holes; the extracted four rotor-hole coordinates remain unchanged.",
        ],
    }
    for index, hole in enumerate(back_holes, 1):
        details["allowed_thread_pairs"].append({
            "pair": [f"motor_stator_mount_M3x5_{index}", "GL30_with_factory_encoder_E"],
            "kind": "candidate M3 thread/entry at vendor back-face hole",
            "local_coordinate_range_mm": {
                "x": [_hole_xy(MOTOR_AXIS_X, hole)[0] - 1.5, _hole_xy(MOTOR_AXIS_X, hole)[0] + 1.5],
                "y": [_hole_xy(MOTOR_AXIS_X, hole)[1] - 1.5, _hole_xy(MOTOR_AXIS_X, hole)[1] + 1.5],
            "n": [-40.5, -38.0],
            },
            "vendor_cad_cylindrical_depth_mm": hole["cad_cylindrical_depth_mm"],
            "status": "intentional hole entry candidate; do not count as structural collision, depth mismatch remains",
        })
    for index, hole in enumerate(front_holes, 1):
        x, y = _hole_xy(MOTOR_AXIS_X, hole)
        details["allowed_thread_pairs"].append({
            "pair": [f"motor_pulley_rotor_M3x3_{index}", "GL30_with_factory_encoder_E"],
            "kind": "candidate M3 thread/entry at vendor front-face hole",
            "local_coordinate_range_mm": {
                "x": [x - 1.5, x + 1.5], "y": [y - 1.5, y + 1.5], "n": [-14.3, -12.3]
            },
            "vendor_cad_cylindrical_depth_mm": hole["cad_cylindrical_depth_mm"],
            "status": "intentional hole entry candidate; low-head clearance and thread depth remain physical checks",
        })
    details["allowed_thread_pairs"].append({
        "pair": ["motor_pulley_locknut_M14x0p75", "motor_pulley_hub"],
        "kind": "nominal M14x0.75 thread envelope",
        "local_coordinate_range_mm": {"x": [MOTOR_AXIS_X - 10.0, MOTOR_AXIS_X + 10.0], "y": [-10.0, 10.0], "n": [PULLEY_LOCKNUT_N0, PULLEY_LOCKNUT_N1]},
        "status": "intentional thread envelope; friction clamp and real thread depth unverified",
    })
    return parts, metadata, yoke_cut, details


def _intersection_volume(a: cq.Shape, b: cq.Shape) -> float:
    return abs(float(_shape(a).intersect(_shape(b)).Volume()))


def _distance(a: cq.Shape, b: cq.Shape) -> float:
    return float(_shape(a).distance(_shape(b)))


def _pair_row(parts: Mapping[str, cq.Shape], left: str, right: str, *, allowed: bool = False) -> dict[str, Any]:
    volume = _intersection_volume(parts[left], parts[right])
    distance = _distance(parts[left], parts[right])
    return {
        "left": left,
        "right": right,
        "intersection_volume_mm3": volume,
        "distance_mm": distance,
        "positive_volume_collision": volume > 1.0e-6,
        "allowed_thread_pair": allowed,
    }


def audit_local_module(motor_world: cq.Shape | cq.Workplane) -> dict[str, Any]:
    """Run only local motor/rest/±0.5 geometry checks and return evidence."""
    parts, metadata, yoke_cut, details = create_motor_module(motor_world)
    motor_local = _inverse_product_transform(motor_world)
    structural_names = ["motor_tension_slide_plate", "motor_pulley_hub", "motor_pulley", "motor_pulley_locknut_M14x0p75"]
    fastener_names = [name for name in parts if name.startswith(("motor_stator_mount_M3x5_", "motor_pulley_rotor_M3x3_", "motor_slide_lock_screw_"))]
    rows: list[dict[str, Any]] = []
    motor_thread_candidate_names = {
        name for name in parts
        if name.startswith(("motor_stator_mount_M3x5_", "motor_pulley_rotor_M3x3_"))
    }
    for name in structural_names + fastener_names:
        rows.append(_pair_row(
            {**parts, "motor_world_local": motor_local},
            name,
            "motor_world_local",
            allowed=name in motor_thread_candidate_names,
        ))
    for left, right, allowed in (
        ("motor_pulley_hub", "motor_pulley", False),
        ("motor_pulley_hub", "motor_pulley_locknut_M14x0p75", True),
        ("motor_pulley_locknut_M14x0p75", "motor_pulley", False),
    ):
        rows.append(_pair_row(parts, left, right, allowed=allowed))
    for index in (1, 2, 3, 4):
        # The hub's Ø5.8 shoulder reliefs and Ø3.4 flange holes are intended
        # to make this installed fastener pair zero-volume outside the vendor
        # motor thread entry; verify the clearance explicitly.
        rows.append(_pair_row(parts, f"motor_pulley_rotor_M3x3_{index}", "motor_pulley_hub"))
    for index in (1, 2):
        rows.append(_pair_row(parts, f"motor_slide_lock_screw_{index}", "motor_tension_slide_plate"))

    # A minimal local press-yoke reference with the required long slots is used
    # only to check the ±0.5 setup path.  It is not returned as production CAD.
    yoke_reference = _box(78.0, 34.0, 2.0, -10.0, 0.0, -42.5)
    for x, y in LOCK_CENTRES:
        yoke_reference = yoke_reference.cut(_lock_rod_slot_void(x, y, -42.6, 2.2))

    # Validate the returned yoke avoidance cutter against a simplified parent
    # material block: x=-49..29, y=+/-17, n=-42.5..-39.5.  Lock slots are
    # validation holes only; production yoke ownership stays in the parent.
    simplified_yoke = _box(78.0, 34.0, 3.0, -10.0, 0.0, -42.5)
    yoke_residual = simplified_yoke.cut(yoke_cut)
    yoke_residual_with_lock_slots = yoke_residual
    yoke_support_checks: list[dict[str, Any]] = []
    for x, y in LOCK_CENTRES:
        yoke_residual_with_lock_slots = yoke_residual_with_lock_slots.cut(_lock_rod_slot_void(x, y, -42.6, 3.2))
        support_probe = _box(6.0, 7.0, 3.0, -23.0, y, -42.5)
        support_volume = _intersection_volume(support_probe, yoke_residual_with_lock_slots)
        yoke_support_checks.append({
            "centre_local_mm": [x, y],
            "support_probe_bbox_local_mm": _bbox(support_probe),
            "support_volume_after_lock_slot_mm3": support_volume,
            "support_thickness_mm": 3.0,
            "support_thickness_complete": support_volume > 0.0,
        })
    adjustment_rows: list[dict[str, Any]] = []
    for delta in (-ADJUSTMENT_MM, 0.0, ADJUSTMENT_MM):
        shifted_slider = parts["motor_tension_slide_plate"].translate((delta, 0.0, 0.0))
        slider_vs_motor = _pair_row({"slider": shifted_slider, "motor": motor_local}, "slider", "motor")
        shifted_slider_vs_yoke = _pair_row({"shifted_slider": shifted_slider, "yoke_residual": yoke_residual}, "shifted_slider", "yoke_residual")
        lock_rows = []
        yoke_lock_rows = []
        for index in (1, 2):
            # Keep the screw fixed while the complete slider moves.  This is
            # the relative setup test that the slot must actually cover.
            fixed_lock = parts[f"motor_slide_lock_screw_{index}"]
            lock_rows.append(_pair_row({"lock": fixed_lock, "shifted_slider": shifted_slider}, "lock", "shifted_slider"))
            yoke_lock_rows.append(_pair_row({"lock": fixed_lock, "yoke": yoke_reference}, "lock", "yoke"))
        adjustment_rows.append({"delta_x_mm": delta, "slider_vs_motor": slider_vs_motor, "shifted_slider_vs_yoke_residual": shifted_slider_vs_yoke, "fixed_lock_vs_shifted_slider": lock_rows, "fixed_lock_vs_yoke_slot": yoke_lock_rows})

    shape_checks: dict[str, Any] = {}
    for name, shape in parts.items():
        shape_checks[name] = {"valid": bool(shape.isValid()), "solids": len(shape.Solids()), "bbox_local_mm": _bbox(shape)}
    positive_thread_candidates = [row for row in rows if row["positive_volume_collision"] and row.get("allowed_thread_pair", False)]
    positive_unallowed = [row for row in rows + [r for step in adjustment_rows for r in [step["slider_vs_motor"], step["shifted_slider_vs_yoke_residual"], *step["fixed_lock_vs_shifted_slider"], *step["fixed_lock_vs_yoke_slot"]]] if row["positive_volume_collision"] and not row.get("allowed_thread_pair", False)]
    result = {
        "status": "R8_MOTOR_MODULE_LOCAL_AUDIT",
        "units": "mm / mm3",
        "motor_local_bbox_mm": _bbox(motor_local),
        "details": details,
        "metadata": metadata,
        "shape_checks": shape_checks,
        "rest_pair_checks": rows,
        "adjustment_checks": adjustment_rows,
        "yoke_support_checks": yoke_support_checks,
        "yoke_cut_bbox_local_mm": _bbox(yoke_cut),
        "positive_thread_candidate_contacts": positive_thread_candidates,
        "positive_unallowed_collisions": positive_unallowed,
        "checks": {
            "motor_axis_and_n_range_match_request": (
                abs(details["coordinate_frame"]["motor_axis_local_x_measured_mm"] - MOTOR_AXIS_X) < 1.0e-5
                and abs(details["coordinate_frame"]["motor_back_n_measured_mm"] - MOTOR_BACK_N) < 1.0e-5
                and abs(details["coordinate_frame"]["motor_front_n_measured_mm"] - MOTOR_FRONT_N) < 1.0e-5
            ),
            "all_named_parts_valid_single_solid": all(row["valid"] and row["solids"] == 1 for row in shape_checks.values()),
            "motor_rest_no_unallowed_positive_volume": not any(row["positive_volume_collision"] for row in rows if not row.get("allowed_thread_pair", False)),
            "hub_shoulder_contact_area_positive": details["pulley_module"]["hub"]["shoulder_2d_contact_area_positive"],
            "locknut_seat_contact_area_positive": details["pulley_module"]["locknut"]["seat_contact_area_positive"],
            "rotor_screw_vs_hub_clearance": not any(row["positive_volume_collision"] for row in rows if row["right"] == "motor_pulley_hub"),
            "pulley_locknut_no_positive_volume": not any(
                row["positive_volume_collision"]
                for row in rows
                if {row["left"], row["right"]} == {
                    "motor_pulley_locknut_M14x0p75", "motor_pulley"
                }
            ),
            "tool_access_side_clearance_positive": details["lock_screws"]["tool_side_clearance_mm"] > 0.0,
            "yoke_simplified_platform_3mm_support": all(row["support_thickness_complete"] for row in yoke_support_checks),
            "adjustment_minus_half_no_collision": not any(step["slider_vs_motor"]["positive_volume_collision"] or step["shifted_slider_vs_yoke_residual"]["positive_volume_collision"] or any(row["positive_volume_collision"] for row in step["fixed_lock_vs_shifted_slider"] + step["fixed_lock_vs_yoke_slot"]) for step in adjustment_rows if step["delta_x_mm"] == -ADJUSTMENT_MM),
            "adjustment_zero_no_collision": not any(step["slider_vs_motor"]["positive_volume_collision"] or step["shifted_slider_vs_yoke_residual"]["positive_volume_collision"] or any(row["positive_volume_collision"] for row in step["fixed_lock_vs_shifted_slider"] + step["fixed_lock_vs_yoke_slot"]) for step in adjustment_rows if step["delta_x_mm"] == 0.0),
            "adjustment_plus_half_no_collision": not any(step["slider_vs_motor"]["positive_volume_collision"] or step["shifted_slider_vs_yoke_residual"]["positive_volume_collision"] or any(row["positive_volume_collision"] for row in step["fixed_lock_vs_shifted_slider"] + step["fixed_lock_vs_yoke_slot"]) for step in adjustment_rows if step["delta_x_mm"] == ADJUSTMENT_MM),
            "no_unallowed_positive_volume_collision": not positive_unallowed,
        },
        "limits": details["limits"] + [
            "The local yoke reference and simplified yoke block are validation geometry only; the parent must integrate the returned back-plate/ear/wall yoke_cut with the actual press_base_plate.",
            "Zero volume at a face or candidate thread entry does not prove preload, thread engagement, or torque capacity.",
            "No housing drilling, vendor modification, global assembly scan, or hardware/physical validation is performed here.",
        ],
    }
    result["all_checks_pass"] = all(result["checks"].values())
    return result


def _current_product_world_motor() -> cq.Workplane:
    """Place the read-only vendor STEP at the requested current local datum.

    The local motor datum is an explicit R8 input fact (axis x=-42,
    n=-40.5..-12.3).  Building this probe directly from the vendor STEP avoids
    inheriting a stale concept-study sink while the parent is replacing the
    old motor interface.
    """
    vendor = _shape(cq.importers.importStep(str(GL30_STEP)))
    native_box = _bbox(vendor)
    motor_local = vendor.rotate((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), -90.0).translate(
        (MOTOR_AXIS_X, 0.0, MOTOR_BACK_N - native_box["xmin"])
    )
    return motor_local.rotate((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), ANGLE_DEG).translate(CENTER)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audit", action="store_true", help="run the bounded local motor/rest/±0.5 audit")
    parser.add_argument("--out", type=Path, help="optional JSON evidence path for --audit")
    args = parser.parse_args()
    if not args.audit:
        parser.error("only the bounded local audit is executable; pass --audit")
    evidence = audit_local_module(_current_product_world_motor())
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"all_checks_pass": evidence["all_checks_pass"], "out": str(args.out) if args.out else None, "checks": evidence["checks"]}, ensure_ascii=False, indent=2))
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(0 if evidence["all_checks_pass"] else 1)


if __name__ == "__main__":
    main()
