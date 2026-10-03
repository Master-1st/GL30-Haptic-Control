"""Build the GL30 AMOLED V7 wireless CONCEPT_FIT_DEFAULTS assembly.

The script imports the official CubeMars and Waveshare STEP files, builds only
the product-specific concept geometry, exports STEP/STL, writes a machine-
readable geometry report, and renders CAD review views.  It is not a
manufacturing release and deliberately leaves vendor-unknown interfaces out.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import asdict
from math import cos, degrees, pi, radians, sin, sqrt, tan
from pathlib import Path
from typing import Iterable

import cadquery as cq
import vtk
from OCP.BRepClass3d import BRepClass3d_SolidClassifier
from OCP.gp import gp_Pnt
from OCP.TopAbs import TopAbs_IN
from cadquery.occ_impl.exporters.assembly import exportStepMeta

from v7_params import DEFAULTS, PARAMETER_PROVENANCE, V7Defaults


HERE = Path(__file__).resolve().parent
DEFAULT_OUTPUT = HERE / "out" / "CONCEPT_FIT_DEFAULTS"
GL30_STEP = (
    HERE / "vendor" / "cubemars" / "GL30_KV290_factory_encoder_official.step"
)
DISPLAY_STEP = (
    HERE
    / "vendor"
    / "waveshare"
    / "ESP32-S3-Touch-AMOLED-1_32_official.step"
)


def _shape(value: cq.Workplane | cq.Shape) -> cq.Shape:
    return value.val() if isinstance(value, cq.Workplane) else value


def _bbox(value: cq.Workplane | cq.Shape) -> dict[str, float]:
    box = _shape(value).BoundingBox()
    return {
        "xmin": box.xmin,
        "xmax": box.xmax,
        "ymin": box.ymin,
        "ymax": box.ymax,
        "zmin": box.zmin,
        "zmax": box.zmax,
        "xlen": box.xlen,
        "ylen": box.ylen,
        "zlen": box.zlen,
    }


def _annulus(outer_diameter: float, inner_diameter: float, height: float) -> cq.Workplane:
    if outer_diameter <= inner_diameter or inner_diameter < 0.0 or height <= 0.0:
        raise ValueError("Invalid annulus dimensions")
    return (
        cq.Workplane("XY")
        .circle(outer_diameter / 2.0)
        .circle(inner_diameter / 2.0)
        .extrude(height)
    )


def _spider(
    *,
    hub_outer: float,
    hub_inner: float,
    rim_outer: float,
    rim_inner: float,
    beam_width: float,
    thickness: float,
) -> cq.Workplane:
    part = _annulus(hub_outer, hub_inner, thickness).union(
        _annulus(rim_outer, rim_inner, thickness)
    )
    # Positive overlap prevents a merely tangent hub/beam contact.
    overlap = 0.6
    beam_length = rim_inner / 2.0 - hub_outer / 2.0 + 2.0 * overlap
    beam_center = (rim_inner / 2.0 + hub_outer / 2.0) / 2.0
    for angle in (0.0, 120.0, 240.0):
        beam = (
            cq.Workplane("XY")
            .box(beam_length, beam_width, thickness)
            .translate((beam_center, 0.0, thickness / 2.0))
            .rotate((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), angle)
        )
        part = part.union(beam)
    return part


def _normal(p: V7Defaults) -> tuple[float, float, float]:
    angle = radians(p.deck_angle_deg)
    return (0.0, -sin(angle), cos(angle))


def _deck_center(p: V7Defaults) -> tuple[float, float, float]:
    return (0.0, p.knob_center_y_mm, p.knob_center_z_mm)


def _orient_at_knob(part: cq.Workplane, p: V7Defaults) -> cq.Workplane:
    return part.rotate(
        (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), p.deck_angle_deg
    ).translate(_deck_center(p))


def _place_on_active_deck(
    part: cq.Workplane,
    p: V7Defaults,
    *,
    x_mm: float,
    from_front_mm: float,
) -> cq.Workplane:
    y = -p.depth_mm / 2.0 + from_front_mm
    z = p.front_height_mm + from_front_mm * tan(p.deck_angle_rad)
    return part.rotate(
        (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), p.deck_angle_deg
    ).translate((x_mm, y, z))


def _side_capsule(
    p: V7Defaults, *, side: int, y_mm: float, length: float,
    height: float, depth: float, x_base: float,
) -> cq.Workplane:
    if side not in (-1, 1):
        raise ValueError("Side must be -1 or +1")
    return (
        cq.Workplane("YZ").center(y_mm, p.side_button_z_mm)
        .slot2D(length, height).extrude(side * depth)
        .translate((side * x_base, 0.0, 0.0))
    )


def _side_button(p: V7Defaults, *, side: int, y_mm: float) -> cq.Workplane:
    inner_wall = p.width_mm / 2.0 - p.housing_wall_mm
    cap = _side_capsule(
        p, side=side, y_mm=y_mm, length=p.side_button_length_mm,
        height=p.side_button_height_mm, depth=p.side_button_body_depth_mm,
        x_base=inner_wall,
    )
    flange = _side_capsule(
        p, side=side, y_mm=y_mm,
        length=p.side_button_length_mm + 2.0 * p.side_button_flange_margin_mm,
        height=p.side_button_height_mm + 2.0 * p.side_button_flange_margin_mm,
        depth=-p.side_button_flange_thickness_mm, x_base=inner_wall,
    )
    # Outward retention only. Switch, return spring, inward stop and PCB datum
    # must be designed after an actual tactile switch has been selected.
    return cap.union(flange)


def _rear_power_button(p: V7Defaults) -> cq.Workplane:
    return (
        cq.Workplane("XY")
        .circle(p.power_button_diameter_mm / 2.0)
        .extrude(p.power_button_protrusion_mm)
        .rotate((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), -90.0)
        .translate(
            (
                p.power_button_x_mm,
                p.depth_mm / 2.0
                - p.power_button_face_recess_mm
                - p.power_button_protrusion_mm,
                p.power_button_z_mm,
            )
        )
    )


def _build_housing(p: V7Defaults) -> cq.Workplane:
    front_y = -p.depth_mm / 2.0
    rear_y = p.depth_mm / 2.0
    outer_profile = [
        (front_y, 0.0),
        (rear_y, 0.0),
        (rear_y, p.rear_height_mm),
        (p.deck_break_y_mm, p.rear_height_mm),
        (front_y, p.front_height_mm),
    ]
    outer = (
        cq.Workplane("YZ")
        .polyline(outer_profile)
        .close()
        .extrude(p.width_mm / 2.0, both=True)
    )
    try:
        outer = outer.edges("|X").fillet(p.housing_edge_radius_mm)
    except Exception:
        # The exact outer profile remains valid if OCC declines a cosmetic fillet.
        pass

    wall = p.housing_wall_mm
    inner_front_y = front_y + wall
    inner_rear_y = rear_y - wall
    inner_break_y = p.deck_break_y_mm - wall
    inner_front_top = (
        p.front_height_mm
        + wall * tan(p.deck_angle_rad)
        - wall / cos(p.deck_angle_rad)
    )
    inner_profile = [
        (inner_front_y, -1.0),
        (inner_rear_y, -1.0),
        (inner_rear_y, p.rear_height_mm - wall),
        (inner_break_y, p.rear_height_mm - wall),
        (inner_front_y, inner_front_top),
    ]
    cavity = (
        cq.Workplane("YZ")
        .polyline(inner_profile)
        .close()
        .extrude(p.width_mm / 2.0 - wall, both=True)
    )
    housing = outer.cut(cavity)
    # Nut pockets open toward the interior. The rear bosses stay ahead of the
    # battery bay. These fasten the cover, not the motor or central display.
    for x, y in ((-40,-29),(40,-29),(-40,14),(40,14)):
        boss = cq.Workplane("XY").center(x,y).circle(6).extrude(8).translate((0,0,3))
        screw = cq.Workplane("XY").center(x,y).circle(1.7).extrude(12)
        nut_access = cq.Workplane("XY").center(x,y).polygon(6,5.8/cos(pi/6)).extrude(6).translate((0,0,6))
        housing = housing.union(boss).cut(screw).cut(nut_access)

    aperture = (
        cq.Workplane("XY")
        .circle(p.deck_aperture_diameter_mm / 2.0)
        .extrude(60.0, both=True)
    )
    aperture = _orient_at_knob(aperture, p)
    housing = housing.cut(aperture)

    usb_slot = (
        cq.Workplane("XY")
        .box(10.0, 7.0, 4.2)
        .translate((0.0, p.depth_mm / 2.0, p.rear_usb_c_center_z_mm))
    )
    housing = housing.cut(usb_slot)
    power_pocket_depth = (
        p.power_button_face_recess_mm + p.power_button_protrusion_mm + 0.2
    )
    power_pocket = (
        cq.Workplane("XY")
        .circle(p.power_button_recess_diameter_mm / 2.0)
        .extrude(power_pocket_depth)
        .rotate((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), -90.0)
        .translate(
            (
                p.power_button_x_mm,
                p.depth_mm / 2.0 - power_pocket_depth + 0.1,
                p.power_button_z_mm,
            )
        )
    )
    housing = housing.cut(power_pocket)
    front_light_pocket = (
        cq.Workplane("XY")
        .box(
            p.front_light_strip_pocket_width_mm,
            p.front_light_strip_pocket_depth_mm,
            p.front_light_strip_pocket_height_mm,
        )
        .translate(
            (
                0.0,
                -p.depth_mm / 2.0
                + p.front_light_strip_pocket_depth_mm / 2.0
                - 0.1,
                p.front_light_strip_center_z_mm,
            )
        )
    )
    housing = housing.cut(front_light_pocket)
    for side in (-1, 1):
        pocket = _side_capsule(
            p, side=side,
            y_mm=(p.side_button_front_y_mm + p.side_button_rear_y_mm) / 2.0,
            length=p.side_button_pocket_length_mm, height=p.side_button_pocket_height_mm,
            depth=p.side_button_pocket_depth_mm + 0.1,
            x_base=p.width_mm / 2.0 - p.side_button_pocket_depth_mm,
        )
        housing = housing.cut(pocket)
        for y_mm in (p.side_button_front_y_mm, p.side_button_rear_y_mm):
            guide = _side_capsule(
                p, side=side, y_mm=y_mm,
                length=p.side_button_length_mm + 2.0 * p.side_button_clearance_mm,
                height=p.side_button_height_mm + 2.0 * p.side_button_clearance_mm,
                depth=p.housing_wall_mm + 0.2,
                x_base=p.width_mm / 2.0 - p.housing_wall_mm - 0.1,
            )
            housing = housing.cut(guide)
    return housing


def _knurl_tools(p: V7Defaults) -> list[cq.Workplane]:
    """Two crossed shallow cuts; a cylindrical stop bounds their TOTAL depth.

    Untouched cylindrical lands form flat-topped diamonds, not sharp pyramids.
    These are concept solids, not a prescription for rolling a thin bearing seat.
    """
    radius = p.knob_outer_diameter_mm / 2.0
    root = radius - p.ring_knurl_depth_mm
    half_width = p.ring_knurl_groove_width_mm / (2.0 * radius)
    start = p.ring_bearing_start_normal_mm + p.ring_knurl_axial_land_mm
    height = p.bearing_width_mm - 2.0 * p.ring_knurl_axial_land_mm
    twist = degrees(height * tan(radians(p.ring_knurl_angle_deg)) / radius)
    profile = []
    for index in range(p.ring_knurl_count):
        angle = index * 2.0 * pi / p.ring_knurl_count
        for offset, r in ((-2, radius + 0.6), (-1, radius), (0, root),
                          (1, radius), (2, radius + 0.6)):
            theta = angle + offset * half_width
            profile.append((r * cos(theta), r * sin(theta)))
    stop = _annulus(2.0 * (radius + 1.0), 2.0 * root, height).translate((0, 0, start))
    tools = []
    for direction in (-1, 1):
        cutter = (
            cq.Workplane("XY").circle(radius + 1.0)
            .polyline(profile).close().twistExtrude(height, direction * twist, combine=False)
            .translate((0.0, 0.0, start)).intersect(stop)
        )
        if not cutter.val().isValid():
            raise RuntimeError("Invalid crossed-knurl cutter")
        tools.append(cutter)
    return tools


def _knurl_ring(part: cq.Workplane, tools: list[cq.Workplane]) -> cq.Workplane:
    for cutter in tools:
        part = part.cut(cutter)
    return part


def _build_custom_parts(p: V7Defaults) -> dict[str, cq.Workplane]:
    housing = _build_housing(p)

    bearing = _annulus(
        p.bearing_outer_diameter_mm,
        p.bearing_inner_diameter_mm,
        p.bearing_width_mm,
    ).translate((0.0, 0.0, p.ring_bearing_start_normal_mm))

    sleeve_inner = (
        p.bearing_outer_diameter_mm + 2.0 * p.ring_shell_radial_clearance_mm
    )
    outer = p.knob_outer_diameter_mm / 2.0
    rear = p.ring_bearing_start_normal_mm
    bearing_front = rear + p.bearing_width_mm
    inset = p.ring_lower_edge_inset_mm
    # A small rounded lower edge, entirely outside the protected bearing wall.
    ring_sleeve = (
        cq.Workplane("XZ").moveTo(sleeve_inner / 2.0, rear)
        .lineTo(outer - inset, rear)
        .bezier([(outer - 0.4 * inset, rear), (outer, rear + 0.4 * inset),
                 (outer, rear + inset)], includeCurrent=True)
        .lineTo(outer, bearing_front).lineTo(sleeve_inner / 2.0, bearing_front)
        .close().revolve(360.0, (0, 0), (0, 1))
    )
    print("Building shallow diamond knurl...", flush=True)
    knurl_tools = _knurl_tools(p)
    ring_sleeve = _knurl_ring(ring_sleeve, knurl_tools)

    outer, inner = p.knob_outer_diameter_mm / 2.0, p.knob_inner_diameter_mm / 2.0
    front = p.ring_front_normal_mm
    shoulder = front - p.ring_transition_recess_mm
    waist_r, waist_z = p.ring_waist_radius_mm, p.ring_waist_normal_mm
    crown_r, crown_z = p.ring_crown_radius_mm, p.ring_crown_normal_mm
    # Three tangent-continuous Bezier segments: tucked waist, slight crown,
    # then a rolled shoulder into the smaller flat top. Control-point hulls
    # bound every radius by the unchanged 27 mm maximum. All sculpting is
    # above the bearing; the bearing bore and fixed display datum stay put.
    ring_cap = (
        cq.Workplane("XZ").moveTo(inner, bearing_front).lineTo(outer, bearing_front)
        .bezier([(outer, bearing_front + (waist_z - bearing_front) * 11.0 / 26.0),
                 (waist_r, (bearing_front + waist_z) / 2.0),
                 (waist_r, waist_z)], includeCurrent=True)
        .bezier([(waist_r, (waist_z + crown_z) / 2.0),
                 (crown_r, (waist_z + crown_z) / 2.0),
                 (crown_r, crown_z)], includeCurrent=True)
        .bezier([(crown_r, crown_z + (front - crown_z) * 7.0 / 12.0),
                 ((crown_r + p.ring_top_radius_mm) / 2.0, front),
                 (p.ring_top_radius_mm, front)], includeCurrent=True)
    )
    # The visible transition ring is a shoulder machined INTO the existing cap,
    # radially OUTWARD from its bore. Nothing bridges the 0.50 mm moving gap.
    ring_cap = (
        ring_cap.lineTo(inner + p.ring_transition_width_mm + p.ring_transition_blend_mm, front)
        .lineTo(inner + p.ring_transition_width_mm, shoulder)
        .lineTo(inner + p.ring_transition_recess_mm, shoulder)
        .lineTo(inner, shoulder - p.ring_transition_recess_mm)
        .close().revolve(360.0, (0, 0), (0, 1))
    )
    # One blue paint-filled index mark moves with the ring. It is not a lamp,
    # an extra mechanical insert, or a promise of calibrated absolute zero.
    ring_marker = (
        cq.Workplane("XY")
        .center(0.0, p.ring_marker_radius_mm)
        .slot2D(p.ring_marker_length_mm, p.ring_marker_width_mm, 90.0)
        .extrude(p.ring_marker_depth_mm)
        .translate((0.0, 0.0, p.ring_front_normal_mm - p.ring_marker_depth_mm))
    )
    ring_cap = ring_cap.cut(ring_marker)

    torque_carrier = _spider(
        hub_outer=22.0,
        hub_inner=6.4,
        rim_outer=52.0,
        rim_inner=48.0,
        beam_width=4.0,
        thickness=p.torque_carrier_thickness_mm,
    ).translate(
        (0.0, 0.0, p.motor_output_face_normal_mm)
    )

    fixed_spider = _spider(
        hub_outer=8.0,
        hub_inner=p.support_tube_outer_diameter_mm + 0.2,
        rim_outer=44.0,
        rim_inner=40.0,
        beam_width=3.0,
        thickness=p.fixed_spider_thickness_mm,
    ).translate((0.0, 0.0, p.fixed_spider_start_normal_mm))

    support_tube_length = (
        p.fixed_spider_start_normal_mm - p.support_tube_back_normal_mm
    )
    support_tube = _annulus(
        p.support_tube_outer_diameter_mm,
        p.support_tube_inner_diameter_mm,
        support_tube_length,
    ).translate((0.0, 0.0, p.support_tube_back_normal_mm))

    bezel = _annulus(
        p.fixed_bezel_outer_diameter_mm,
        p.fixed_bezel_aperture_diameter_mm,
        p.bezel_thickness_mm,
    ).translate((0.0, 0.0, p.bezel_start_normal_mm))
    glass = (
        cq.Workplane("XY")
        .circle(p.fixed_bezel_outer_diameter_mm / 2.0)
        .extrude(p.display_cover_thickness_mm)
        .translate((0.0, 0.0, p.bezel_start_normal_mm + p.bezel_thickness_mm))
    )

    parts = {
        "housing": housing,
        "bottom_cover": _bottom_cover(p),
        "bearing_envelope": _orient_at_knob(bearing, p),
        "ring_sleeve": _orient_at_knob(ring_sleeve, p),
        "ring_cap": _orient_at_knob(ring_cap, p),
        "ring_index_mark": _orient_at_knob(ring_marker, p),
        "torque_carrier": _orient_at_knob(torque_carrier, p),
        "fixed_spider": _orient_at_knob(fixed_spider, p),
        "support_tube": _orient_at_knob(support_tube, p),
        "fixed_bezel": _orient_at_knob(bezel, p),
        "display_glass": _orient_at_knob(glass, p),
    }

    for side_name, side in (("left", -1), ("right", 1)):
        for index, y_mm in enumerate(
            (p.side_button_front_y_mm, p.side_button_rear_y_mm), start=1
        ):
            parts[f"button_{side_name}_{index}"] = _side_button(
                p, side=side, y_mm=y_mm
            )

    parts["power_button"] = _rear_power_button(p)
    parts["service_pinhole"] = (
        cq.Workplane("XY")
        .circle(p.service_pinhole_diameter_mm / 2.0)
        .extrude(-0.4)
        .translate((30.0, -30.0, 0.0))
    )
    parts["battery_keepout"] = (
        cq.Workplane("XY")
        .box(
            p.battery_keepout_width_mm,
            p.battery_keepout_depth_mm,
            p.battery_keepout_height_mm,
        )
        .translate(
            (
                0.0,
                p.battery_keepout_center_y_mm,
                p.battery_keepout_center_z_mm,
            )
        )
    )
    parts["electronics_keepout"] = (
        cq.Workplane("XY")
        .box(
            p.electronics_keepout_width_mm,
            p.electronics_keepout_depth_mm,
            p.electronics_keepout_height_mm,
        )
        .translate(
            (
                0.0,
                p.electronics_keepout_center_y_mm,
                p.electronics_keepout_center_z_mm,
            )
        )
    )

    parts["speaker_grille"] = (
        cq.Workplane("XY")
        .circle(10.0)
        .extrude(-0.7)
        .translate((-30.0, 24.0, 0.0))
    )
    parts["microphone"] = (
        cq.Workplane("XY")
        .circle(1.0)
        .extrude(p.power_button_protrusion_mm)
        .rotate((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), -90.0)
        .translate((-30.0, p.depth_mm / 2.0, 29.0))
    )
    parts["status_led"] = (
        cq.Workplane("XY")
        .circle(1.0)
        .extrude(p.power_button_protrusion_mm)
        .rotate((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), -90.0)
        .translate((0.0, p.depth_mm / 2.0, 12.0))
    )
    parts["front_light_strip"] = (
        cq.Workplane("XY")
        .box(
            p.front_light_strip_width_mm,
            p.front_light_strip_depth_mm,
            p.front_light_strip_height_mm,
        )
        .translate(
            (
                0.0,
                -p.depth_mm / 2.0
                + p.front_light_strip_face_recess_mm
                + p.front_light_strip_depth_mm / 2.0,
                p.front_light_strip_center_z_mm,
            )
        )
    )
    parts["usb_c_charge_data"] = (
        cq.Workplane("XY")
        .box(9.0, 2.0, 3.4)
        .translate(
            (0.0, p.depth_mm / 2.0 + 0.2, p.rear_usb_c_center_z_mm)
        )
    )
    foot_x = p.width_mm / 2.0 - 8.0
    foot_y = p.depth_mm / 2.0 - 10.0
    for index, (x, y) in enumerate(
        (
            (-foot_x, -foot_y),
            (foot_x, -foot_y),
            (-foot_x, foot_y),
            (foot_x, foot_y),
        ),
        start=1,
    ):
        parts[f"foot_{index}"] = (
            cq.Workplane("XY").circle(4.0).extrude(1.2).translate((x, y, -1.2))
        )
    return parts


def _bottom_cover(p: V7Defaults) -> cq.Workplane:
    """Inside-fitting removable cover; 0.30 mm nominal side clearance."""
    cover = cq.Workplane("XY").box(
        p.width_mm - 2*p.housing_wall_mm - .6,
        p.depth_mm - 2*p.housing_wall_mm - .6,
        3, centered=(True,True,False))
    for x,y in ((-40,-29),(40,-29),(-40,14),(40,14)):
        cover = cover.cut(cq.Workplane("XY").center(x,y).circle(1.7).extrude(4))
        # 90-degree M3 countersunk seat: head stays above the foot plane.
        # Actual screw head dimensions and printed seating remain fit checks.
        seat = cq.Solid.makeCone(3.35,1.7,1.65,cq.Vector(x,y,0))
        cover = cover.cut(seat)
    cover = cover.cut(cq.Workplane("XY").center(30,-30).circle(1).extrude(4))
    return cover


def _import_vendor_parts(
    p: V7Defaults,
) -> tuple[cq.Workplane, cq.Workplane, cq.Workplane, cq.Workplane]:
    if not GL30_STEP.is_file() or not DISPLAY_STEP.is_file():
        raise FileNotFoundError("Official vendor STEP file is missing")

    motor_local = cq.importers.importStep(str(GL30_STEP))
    display_local = cq.importers.importStep(str(DISPLAY_STEP))
    n = _normal(p)
    center = _deck_center(p)

    # Official GL30 local +X is the motor axis and its encoder body extends -X.
    motor_world = (
        motor_local.rotate((0.0, 0.0, 0.0), (0.0, 1.0, 0.0), -90.0)
        .rotate((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), p.deck_angle_deg)
        .translate(
            (
                center[0] + n[0] * p.motor_origin_normal_mm,
                center[1] + n[1] * p.motor_origin_normal_mm,
                center[2] + n[2] * p.motor_origin_normal_mm,
            )
        )
    )

    # Official Waveshare model thickness is local Y; local -Y faces the user.
    display_world = display_local.rotate(
        (0.0, 0.0, 0.0),
        (1.0, 0.0, 0.0),
        p.deck_angle_deg - 90.0,
    ).translate(
        (
            center[0] + n[0] * p.display_origin_normal_mm,
            center[1] + n[1] * p.display_origin_normal_mm,
            center[2] + n[2] * p.display_origin_normal_mm,
        )
    )
    return motor_local, display_local, motor_world, display_world


def _max_vertex_radius(
    value: cq.Workplane | cq.Shape, axis_a: str, axis_b: str
) -> float:
    maximum = 0.0
    for vertex in _shape(value).Vertices():
        point = vertex.Center()
        a = getattr(point, axis_a)
        b = getattr(point, axis_b)
        maximum = max(maximum, sqrt(a * a + b * b))
    return maximum


def _intersection_volume(
    first: cq.Workplane | cq.Shape, second: cq.Workplane | cq.Shape
) -> float:
    return _shape(first).intersect(_shape(second)).Volume()


def _classified_inside(classifier: BRepClass3d_SolidClassifier, point: cq.Vector) -> bool:
    # Reuse the solid classifier for hundreds of knurl samples. CadQuery's
    # isInside() reconstructs it per point, which is costly for crossed grooves.
    classifier.Perform(gp_Pnt(*point.toTuple()), 1.0e-6)
    return classifier.State() == TopAbs_IN or classifier.IsOnAFace()


def _geometry_report(
    p: V7Defaults,
    custom: dict[str, cq.Workplane],
    motor_local: cq.Workplane,
    display_local: cq.Workplane,
    motor_world: cq.Workplane,
    display_world: cq.Workplane,
) -> dict[str, object]:
    motor_local_box = _bbox(motor_local)
    display_local_box = _bbox(display_local)
    motor_world_box = _bbox(motor_world)
    display_radius = _max_vertex_radius(display_local, "x", "z")
    motor_radius = _max_vertex_radius(motor_local, "y", "z")

    floor_clearance = motor_world_box["zmin"] - p.housing_wall_mm
    screen_to_ring = p.knob_inner_diameter_mm / 2.0 - display_radius
    battery_box = _bbox(custom["battery_keepout"])
    electronics_box = _bbox(custom["electronics_keepout"])
    power_button_box = _bbox(custom["power_button"])
    usb_c_box = _bbox(custom["usb_c_charge_data"])
    front_light_strip_box = _bbox(custom["front_light_strip"])
    inner_x = p.width_mm / 2.0 - p.housing_wall_mm
    inner_rear_y = p.depth_mm / 2.0 - p.housing_wall_mm
    inner_roof_z = p.rear_height_mm - p.housing_wall_mm
    battery_motor_intersection = _intersection_volume(
        custom["battery_keepout"], motor_world
    )
    battery_support_intersection = _intersection_volume(
        custom["battery_keepout"], custom["support_tube"]
    )
    side_button_names = [name for name in custom if name.startswith("button_")]
    button_measurements = {}
    for name in side_button_names:
        side = -1 if "left" in name else 1
        part = _shape(custom[name])
        box = _bbox(part)
        face_x = box["xmin"] if side < 0 else box["xmax"]
        moved = part.translate((-side * p.side_button_clearance_check_travel_mm, 0, 0))
        button_measurements[name] = {
            "single_valid_solid": len(part.Solids()) == 1 and part.isValid(),
            "face_recess_mm": p.width_mm / 2.0 - abs(face_x),
            "housing_intersection_mm3": _intersection_volume(part, custom["housing"]),
            "inward_clearance_only_mm": p.side_button_clearance_check_travel_mm,
            "inward_clearance_housing_intersection_mm3": _intersection_volume(moved, custom["housing"]),
            "internal_keepout_intersection_mm3": sum(
                _intersection_volume(moved, custom[keepout])
                for keepout in ("battery_keepout", "electronics_keepout")
            ),
        }
    center = _deck_center(p)
    face_local = {
        name: _shape(custom[name]).translate(tuple(-value for value in center)).rotate(
            (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), -p.deck_angle_deg
        )
        for name in ("ring_sleeve", "ring_cap", "ring_index_mark", "display_glass")
    }
    cover_box = _bbox(face_local["display_glass"])
    marker_box = _bbox(face_local["ring_index_mark"])
    cover_to_display = cover_box["zmin"] - (
        p.display_origin_normal_mm - display_local_box["ymin"]
    )
    cover_to_ring = min(
        _shape(custom["display_glass"]).distance(_shape(custom[name]))
        for name in ("ring_cap", "ring_sleeve")
    )
    radius = p.knob_outer_diameter_mm / 2.0
    sample_radius = radius - p.ring_knurl_depth_mm / 2.0
    knurl_start = p.ring_bearing_start_normal_mm + p.ring_knurl_axial_land_mm
    bearing_front = p.ring_bearing_start_normal_mm + p.bearing_width_mm
    knurl_end = bearing_front - p.ring_knurl_axial_land_mm
    crossing_step = pi * radius / p.ring_knurl_count / tan(radians(p.ring_knurl_angle_deg))
    knurl_samples = {}
    core_missing_volumes = {}
    for name, lower, upper, bore in (
        ("ring_sleeve", knurl_start, knurl_end,
         p.bearing_outer_diameter_mm + 2.0 * p.ring_shell_radial_clearance_mm),
    ):
        solid = face_local[name].Solids()[0]
        classifier = BRepClass3d_SolidClassifier(solid.wrapped)
        knurl_samples[name] = []
        # At these axial planes both helix families cross. Check every valley
        # and every intervening flat diamond land, not just one decorative face.
        for row in range(1, int((knurl_end - knurl_start) / crossing_step) + 1):
            z = knurl_start + row * crossing_step
            if not lower + 1.0e-4 < z < upper - 1.0e-4:
                continue
            phase = row * pi / p.ring_knurl_count
            for index in range(p.ring_knurl_count):
                angle = phase + index * 2.0 * pi / p.ring_knurl_count
                valley = cq.Vector(sample_radius * cos(angle), sample_radius * sin(angle), z)
                angle += pi / p.ring_knurl_count
                land = cq.Vector(sample_radius * cos(angle), sample_radius * sin(angle), z)
                knurl_samples[name].append(
                    not _classified_inside(classifier, valley) and _classified_inside(classifier, land)
                )
        protected_core = _annulus(
            2.0 * (radius - p.ring_knurl_depth_mm - 1.0e-5), bore, p.bearing_width_mm
        ).translate((0.0, 0.0, p.ring_bearing_start_normal_mm))
        core_missing_volumes[name] = protected_core.cut(cq.Workplane(obj=solid)).val().Volume()
        print(f"Checked {name}: {sum(knurl_samples[name])}/{len(knurl_samples[name])} knurl samples", flush=True)
    cap = face_local["ring_cap"].Solids()[0]
    cap_classifier = BRepClass3d_SolidClassifier(cap.wrapped)
    profile_samples = {}
    for name, r, z, epsilon in (
        ("waist", p.ring_waist_radius_mm, p.ring_waist_normal_mm, 0.03),
        ("crown", p.ring_crown_radius_mm, p.ring_crown_normal_mm, 0.03),
        ("top", p.ring_top_radius_mm, p.ring_front_normal_mm - 0.001, 0.05),
    ):
        profile_samples[name] = []
        for index in range(16):
            angle = index * 2.0 * pi / 16.0
            profile_samples[name].append(
                _classified_inside(cap_classifier, cq.Vector((r - epsilon) * cos(angle),
                                                           (r - epsilon) * sin(angle), z))
                and not _classified_inside(cap_classifier, cq.Vector((r + epsilon) * cos(angle),
                                                                   (r + epsilon) * sin(angle), z))
            )
    transition_radius = p.knob_inner_diameter_mm / 2.0 + p.ring_transition_width_mm / 2.0
    shoulder_z = p.ring_front_normal_mm - p.ring_transition_recess_mm
    transition_samples = []
    for index in range(16):
        angle = index * 2.0 * pi / 16.0
        x, y = transition_radius * cos(angle), transition_radius * sin(angle)
        transition_samples.append(
            _classified_inside(cap_classifier, cq.Vector(x, y, shoulder_z - 0.05))
            and not _classified_inside(cap_classifier, cq.Vector(x, y, shoulder_z + 0.05))
        )
    # Trimmed curves can give conservative axial boxes. Verify actual material outside the two design
    # half-spaces instead of treating that box inflation as solid overlap.
    axial_overruns = {}
    for name, z0, z1 in (
        ("ring_sleeve", bearing_front + 1.0e-6, p.ring_front_normal_mm + 1.0),
        ("ring_cap", p.ring_bearing_start_normal_mm - 1.0, bearing_front - 1.0e-6),
    ):
        slab = cq.Workplane("XY").box(
            p.knob_outer_diameter_mm + 2.0, p.knob_outer_diameter_mm + 2.0, z1 - z0
        ).translate((0.0, 0.0, (z0 + z1) / 2.0))
        axial_overruns[name] = _intersection_volume(face_local[name], slab)
    checks = {
        "torque_carrier_single_connected_solid": len(_shape(custom["torque_carrier"]).Solids()) == 1,
        "fixed_spider_single_connected_solid": len(_shape(custom["fixed_spider"]).Solids()) == 1,
        "bottom_cover_single_valid_solid": len(_shape(custom["bottom_cover"]).Solids()) == 1 and _shape(custom["bottom_cover"]).isValid(),
        "bottom_cover_does_not_intersect_housing": _intersection_volume(custom["bottom_cover"],custom["housing"]) <= 1e-6,
        "bottom_cover_does_not_intersect_battery": _intersection_volume(custom["bottom_cover"],custom["battery_keepout"]) <= 1e-6,
        "housing_bosses_clear_battery": _intersection_volume(custom["housing"],custom["battery_keepout"]) <= 1e-6,
        "official_gl30_axis_length_28p2": abs(motor_local_box["xlen"] - 28.2) <= 0.2,
        "official_gl30_radial_diameter_34p5": abs(2.0 * motor_radius - 34.5) <= 0.25,
        "official_display_thickness_11p3": abs(display_local_box["ylen"] - 11.3) <= 0.25,
        "active_deck_fits_body": 0.0 < p.active_deck_run_mm < p.depth_mm,
        "rear_platform_exists": p.rear_platform_run_mm > 0.0,
        "ring_rear_margin_at_least_2mm": p.ring_rear_margin_mm >= 2.0,
        "moving_gap_at_least_0p5mm": p.moving_radial_gap_mm >= 0.5,
        "modified_appearance_parts_are_single_valid_solids": all(
            _shape(custom[name]).isValid() and len(_shape(custom[name]).Solids()) == 1
            for name in (
                "housing", "ring_sleeve", "ring_cap", "ring_index_mark",
                "fixed_bezel", "display_glass",
            )
        ),
        "crossed_knurl_valleys_and_flat_lands_present_in_sleeve_grip_band": all(
            len(samples) >= p.ring_knurl_count and all(samples)
            for samples in knurl_samples.values()
        ),
        "knurl_root_wall_at_least_0p65mm": p.ring_knurl_root_wall_mm >= 0.65,
        "crossed_cuts_total_depth_does_not_exceed_0p20mm": all(
            volume <= 1.0e-5 for volume in core_missing_volumes.values()
        ),
        "sculpted_waist_crown_and_top_present_around_full_circumference": all(
            all(samples) for samples in profile_samples.values()
        ),
        "sculpted_ring_stays_within_original_maximum_diameter": all(
            _bbox(face_local[name])[axis] <= p.knob_outer_diameter_mm + 1.0e-4
            for name in ("ring_sleeve", "ring_cap") for axis in ("xlen", "ylen")
        ),
        "integral_transition_shoulder_present_around_full_circumference": all(transition_samples),
        "transition_ring_does_not_intrude_into_original_bore": (
            _intersection_volume(
                cq.Workplane(obj=cap),
                cq.Workplane("XY").circle(p.knob_inner_diameter_mm / 2.0 - 1.0e-5)
                .extrude(p.ring_front_normal_mm + 1.0),
            ) <= 1.0e-6
        ),
        "ring_does_not_intersect_bearing_envelope": all(
            _intersection_volume(custom[name], custom["bearing_envelope"]) <= 1.0e-6
            for name in ("ring_sleeve", "ring_cap")
        ),
        "ring_parts_remain_in_disjoint_design_axial_envelopes": (
            all(volume <= 1.0e-6 for volume in axial_overruns.values())
        ),
        "black_inner_transition_two_slopes_present": sum(
            face.geomType() == "CONE" for face in _shape(custom["ring_cap"]).Faces()
        ) == 2,
        "ring_and_housing_are_different_colors": (
            sum((a - b) ** 2 for a, b in zip(COLORS["ring_cap"][:3], COLORS["housing"][:3]))
            > 0.5
        ),
        "rotating_ring_and_fixed_cover_share_black_color": (
            COLORS["ring_cap"] == COLORS["ring_sleeve"] == COLORS["display_glass"]
            and max(COLORS["ring_cap"][:3]) < 0.10
        ),
        "index_mark_is_blue": (
            COLORS["ring_index_mark"][2] > 0.8
            and COLORS["ring_index_mark"][0] < 0.1
        ),
        "index_mark_dimensions_and_top_are_correct": all((
            abs(marker_box["xlen"] - p.ring_marker_width_mm) <= 1.0e-5,
            abs(marker_box["ylen"] - p.ring_marker_length_mm) <= 1.0e-5,
            abs(marker_box["zlen"] - p.ring_marker_depth_mm) <= 1.0e-5,
            abs(marker_box["zmax"] - _bbox(face_local["ring_cap"])["zmax"]) <= 1.0e-5,
        )),
        "index_mark_contacts_ring_without_volume_overlap": (
            _intersection_volume(custom["ring_index_mark"], custom["ring_cap"]) <= 1.0e-6
            and _shape(custom["ring_index_mark"]).distance(_shape(custom["ring_cap"])) <= 1.0e-6
        ),
        "fixed_cover_spans_entire_fixed_face": all((
            abs(cover_box["xlen"] - p.fixed_bezel_outer_diameter_mm) <= 1.0e-5,
            abs(cover_box["ylen"] - p.fixed_bezel_outer_diameter_mm) <= 1.0e-5,
            abs(cover_box["zlen"] - p.display_cover_thickness_mm) <= 1.0e-5,
        )),
        "fixed_cover_and_ring_top_are_flush": (
            abs(cover_box["zmax"] - _bbox(face_local["ring_cap"])["zmax"]) <= 1.0e-5
        ),
        "fixed_cover_to_rotating_ring_gap_at_least_0p5mm": cover_to_ring >= 0.5 - 1.0e-5,
        "fixed_cover_to_official_display_axial_gap_at_least_0p3mm": cover_to_display >= 0.3,
        "fixed_cover_contacts_backing_without_volume_overlap": (
            _intersection_volume(custom["display_glass"], custom["fixed_bezel"]) <= 1.0e-6
            and _shape(custom["display_glass"]).distance(_shape(custom["fixed_bezel"])) <= 1.0e-6
        ),
        "screen_to_ring_clearance_at_least_0p5mm": screen_to_ring >= 0.5,
        "motor_to_inner_floor_clearance_at_least_0p5mm": floor_clearance >= 0.5,
        "battery_keepout_inside_rear_bay": (
            battery_box["xmin"] >= -inner_x
            and battery_box["xmax"] <= inner_x
            and battery_box["ymin"] >= p.deck_break_y_mm
            and battery_box["ymax"] <= inner_rear_y
            and battery_box["zmin"] >= p.housing_wall_mm
            and battery_box["zmax"] <= inner_roof_z
        ),
        "electronics_keepout_inside_rear_bay": (
            electronics_box["xmin"] >= -inner_x
            and electronics_box["xmax"] <= inner_x
            and electronics_box["ymin"] >= p.deck_break_y_mm
            and electronics_box["ymax"] <= inner_rear_y
            and electronics_box["zmin"] >= p.housing_wall_mm
            and electronics_box["zmax"] <= inner_roof_z
        ),
        "battery_to_motor_no_intersection": battery_motor_intersection <= 1.0e-6,
        "battery_to_center_support_no_intersection": (
            battery_support_intersection <= 1.0e-6
        ),
        "four_user_keys_are_side_mounted": len(side_button_names) == 4,
        "side_keys_are_four_single_valid_solids": len(button_measurements) == 4 and all(
            v["single_valid_solid"] for v in button_measurements.values()
        ),
        "side_keys_are_recessed_and_match_housing_color": COLORS["button"] == COLORS["housing"] and all(
            abs(v["face_recess_mm"] - p.side_button_face_recess_mm) <= 1.0e-6
            for v in button_measurements.values()
        ),
        "side_keys_clear_housing_at_rest": all(
            abs(v["housing_intersection_mm3"]) <= 1.0e-6 for v in button_measurements.values()
        ),
        "side_keys_clear_inward_test_envelope_not_working_stroke": all(
            abs(v["inward_clearance_housing_intersection_mm3"]) <= 1.0e-6
            and abs(v["internal_keepout_intersection_mm3"]) <= 1.0e-6
            for v in button_measurements.values()
        ),
        "single_rear_power_key_present": "power_button" in custom,
        "rear_power_key_is_recessed": (
            power_button_box["ymax"]
            <= p.depth_mm / 2.0 - p.power_button_face_recess_mm + 1.0e-6
        ),
        "single_rear_usb_c_present": sum(
            name.startswith("usb_c_") for name in custom
        )
        == 1,
        "rear_usb_c_is_above_battery_keepout": (
            usb_c_box["zmin"] - battery_box["zmax"] >= 3.0
        ),
        "front_bottom_light_strip_present": "front_light_strip" in custom,
        "front_light_strip_is_recessed": (
            front_light_strip_box["ymin"]
            >= -p.depth_mm / 2.0 + p.front_light_strip_face_recess_mm - 1.0e-6
        ),
        "front_light_strip_clear_of_floor_and_deck": (
            front_light_strip_box["zmin"] > p.housing_wall_mm
            and front_light_strip_box["zmax"]
            < p.front_height_mm - p.housing_wall_mm
        ),
    }
    return {
        "release_label": "CONCEPT_FIT_DEFAULTS",
        "evidence_boundary": "CAD geometry only; no physical, load, thermal, or electrical validation",
        "parameters_mm": asdict(p),
        "parameter_provenance": PARAMETER_PROVENANCE,
        "side_key_geometry": {
            "measurements": button_measurements,
            "electrical_switch_return_and_working_travel_verified": False,
            "markings": "render-only diameter 0.60 mm single/double grey dots; not extra STEP solids",
            "construction": "same-color capsule key with internal retaining flange in a real recessed pocket and through-wall guide; outward retention only",
        },
        "derived": {
            "active_deck_run_mm": p.active_deck_run_mm,
            "rear_platform_run_mm": p.rear_platform_run_mm,
            "deck_break_y_mm": p.deck_break_y_mm,
            "knob_center_y_mm": p.knob_center_y_mm,
            "knob_center_z_mm": p.knob_center_z_mm,
            "moving_radial_gap_mm": p.moving_radial_gap_mm,
            "ring_knurl_root_wall_mm": p.ring_knurl_root_wall_mm,
            "ring_knurl_circumferential_pitch_mm": 2.0 * pi * radius / p.ring_knurl_count,
            "knurl_protected_core_missing_volume_mm3": core_missing_volumes,
            "grip_band_normal_extent_mm": [knurl_start, knurl_end],
            "sculpted_side_profile_sample_passes": {
                name: sum(samples) for name, samples in profile_samples.items()
            },
            "ring_axial_envelope_overrun_volume_mm3": axial_overruns,
            "ring_axial_check_plane_normal_mm": bearing_front,
            "ring_axial_check_tolerance_mm": 1.0e-6,
            "ring_cap_bottom_conservative_bbox_normal_mm": _bbox(face_local["ring_cap"])["zmin"],
            "ring_sleeve_top_conservative_bbox_normal_mm": _bbox(face_local["ring_sleeve"])["zmax"],
            "transition_shoulder_inner_radius_mm": p.knob_inner_diameter_mm / 2.0,
            "transition_shoulder_outer_radius_mm": p.knob_inner_diameter_mm / 2.0 + p.ring_transition_width_mm,
            "transition_shoulder_front_normal_mm": shoulder_z,
            "fixed_cover_to_ring_distance_mm": cover_to_ring,
            "fixed_cover_to_official_display_axial_gap_mm": cover_to_display,
            "fixed_cover_front_normal_mm": cover_box["zmax"],
            "cover_to_ring_top_step_mm": cover_box["zmax"] - _bbox(face_local["ring_cap"])["zmax"],
            "ring_rear_margin_mm": p.ring_rear_margin_mm,
            "official_display_max_radius_mm": display_radius,
            "official_display_front_normal_mm": (
                p.display_origin_normal_mm - display_local_box["ymin"]
            ),
            "screen_to_ring_radial_clearance_mm": screen_to_ring,
            "official_gl30_max_radius_mm": motor_radius,
            "motor_to_inner_floor_clearance_mm": floor_clearance,
            "battery_to_motor_bbox_y_clearance_mm": (
                battery_box["ymin"] - motor_world_box["ymax"]
            ),
            "battery_to_motor_intersection_mm3": battery_motor_intersection,
            "battery_to_center_support_intersection_mm3": (
                battery_support_intersection
            ),
            "rear_power_button_face_recess_mm": (
                p.depth_mm / 2.0 - power_button_box["ymax"]
            ),
            "rear_usb_c_to_battery_vertical_clearance_mm": (
                usb_c_box["zmin"] - battery_box["zmax"]
            ),
            "front_light_strip_face_recess_mm": (
                front_light_strip_box["ymin"] + p.depth_mm / 2.0
            ),
            "footprint_reduction_from_128x100_percent": (
                100.0 - p.width_mm * p.depth_mm / (128.0 * 100.0) * 100.0
            ),
        },
        "official_local_bounding_boxes": {
            "gl30_factory_encoder": motor_local_box,
            "waveshare_display": display_local_box,
        },
        "placed_bounding_boxes": {
            "housing": _bbox(custom["housing"]),
            "gl30_factory_encoder": motor_world_box,
            "waveshare_display": _bbox(display_world),
            "ring_cap": _bbox(custom["ring_cap"]),
            "battery_keepout": battery_box,
            "electronics_keepout": electronics_box,
        },
        "appearance": {
            "basis": "user selected Bentley-inspired black dial; sculpted waist and rolled crown replace straight-cylinder side profile; lower diamond grip band",
            "reference_url": "https://www.bentleymotors.com/en/models/technology.html",
            "selected_preview": "black ring / blue index mark / matte light silver housing",
            "selection_status": "user specified black ring and blue marker; physical finishes pending samples",
            "ring_side_profile": "three tangent-continuous Bezier-revolved shoulders above bearing: waist R25, crown R25.5, top R24.9, maximum R27; smooth black finish without silver accent",
            "transition_ring": "1.20 mm radial shoulder cut OUTWARD from existing bore into rotating cap; 0.20 mm recess and 0.30 mm outer slope; not a separate insert and not a bridge across the moving gap",
            "index_mark": "one flush blue paint-filled slot on rotating ring at model +Y; no LED; not calibrated zero",
            "fixed_display_cover": {
                "geometry": "one fixed full-face circular cover, separate from rotating ring",
                "diameter_mm": p.fixed_bezel_outer_diameter_mm,
                "thickness_mm": p.display_cover_thickness_mm,
                "preview_state": "screen off; opaque black CAD shading is not an optical simulation",
                "optical_concept": "dark transmissive window with matched black border masking below the cover",
                "pending_samples": [
                    "off-state black level, reflectance, gloss and oblique-view boundary",
                    "on-state brightness, color shift, viewing angle and touch sensitivity",
                    "cover material, coating, transmittance, adhesive and support strength",
                ],
                "physical_dead_front_verified": False,
            },
            "additional_top_led_hardware": False,
            "colors_rgba": COLORS,
            "crossed_knurl_verified_by_solid_point_samples": {
                name: sum(samples) for name, samples in knurl_samples.items()
            },
            "wall_note": (
                f"{p.ring_knurl_root_wall_mm:.2f} mm nominal groove-root sleeve wall "
                "is geometry only, not strength or machining approval"
            ),
        },
        "existing_multi_solid_concept_parts": {
            name: len(_shape(part).Solids()) for name, part in custom.items()
            if len(_shape(part).Solids()) != 1
        },
        "battery_candidate": {
            "architecture": (
                "3S pack -> BQ25798 SYS -> external bidirectional isolation -> "
                "MOTOR_BUS; independent brake required"
            ),
            "mechanical_reference_only": "published 3S 800 mAh pack 80 x 20 x 16 mm",
            "modeled_keepout_mm": {
                "width": p.battery_keepout_width_mm,
                "depth": p.battery_keepout_depth_mm,
                "height": p.battery_keepout_height_mm,
            },
            "not_frozen": [
                "cell and pack manufacturer",
                "capacity and runtime",
                "BMS/protector and balancing implementation",
                "continuous/pulse current and charge rate",
                "swelling allowance, holder and thermal barrier",
            ],
        },
        "checks": checks,
        "all_checks_pass": all(checks.values()),
        "vendor_holds": [
            "encoder electrical interface, voltage, pinout, connector and timing",
            "rotor/stator face roles and cable bend envelope",
            "6 mm bore support/wiring permission",
            "allowed radial/axial load and bearing selection",
            "battery pack, protector/BMS, NTC and production certifications",
            "power-PCB, logic-PCB, speaker and fastener production geometry",
        ],
    }


COLORS: dict[str, tuple[float, float, float, float]] = {
    "bottom_cover": (0.76, 0.79, 0.83, 1.0),
    "housing": (0.76, 0.79, 0.83, 1.0),
    "bearing_envelope": (0.68, 0.72, 0.76, 1.0),
    "ring_sleeve": (0.065, 0.067, 0.070, 1.0),
    "ring_cap": (0.065, 0.067, 0.070, 1.0),
    "ring_index_mark": (0.035, 0.50, 0.95, 1.0),
    "torque_carrier": (0.90, 0.48, 0.10, 1.0),
    "fixed_spider": (0.18, 0.52, 0.72, 1.0),
    "support_tube": (0.60, 0.64, 0.68, 1.0),
    "fixed_bezel": (0.065, 0.067, 0.070, 1.0),
    "display_glass": (0.065, 0.067, 0.070, 1.0),
    "speaker_grille": (0.05, 0.05, 0.06, 1.0),
    "microphone": (0.03, 0.03, 0.03, 1.0),
    "status_led": (0.10, 0.75, 0.90, 1.0),
    "front_light_strip": (0.08, 0.78, 0.96, 0.92),
    "usb_c": (0.35, 0.37, 0.40, 1.0),
    "button": (0.76, 0.79, 0.83, 1.0),
    "power_button": (0.15, 0.16, 0.18, 1.0),
    "service_pinhole": (0.03, 0.03, 0.03, 1.0),
    "battery_keepout": (0.12, 0.44, 0.22, 0.62),
    "electronics_keepout": (0.15, 0.34, 0.68, 0.58),
    "foot": (0.04, 0.04, 0.04, 1.0),
    "motor_official": (0.82, 0.48, 0.12, 1.0),
    "display_official": (0.10, 0.34, 0.22, 1.0),
}


def _color_for(name: str) -> tuple[float, float, float, float]:
    if name.startswith("button_"):
        return COLORS["button"]
    if name.startswith("usb_c_"):
        return COLORS["usb_c"]
    if name.startswith("foot_"):
        return COLORS["foot"]
    return COLORS[name]


def _button_mark_preview_items(p: V7Defaults) -> list:
    """Render-only low-contrast dot artwork; no extra mechanical STEP solids."""
    items = []
    face = p.width_mm / 2.0 - p.side_button_face_recess_mm
    for side in (-1, 1):
        for number, y_mm in enumerate((p.side_button_front_y_mm, p.side_button_rear_y_mm), 1):
            for dot, delta in enumerate((0.0,) if number == 1 else (-0.65, 0.65)):
                ink = (cq.Workplane("YZ").center(y_mm + delta, p.side_button_z_mm)
                       .circle(0.30).extrude(side * 0.015).translate((side * face, 0, 0)))
                items.append((f"key_ink_{side}_{number}_{dot}", ink,
                              (0.43, 0.46, 0.50, 1.0), (0, 0, 0)))
    return items


def _vtk_actor(
    value: cq.Workplane | cq.Shape,
    rgba: tuple[float, float, float, float],
    *,
    tolerance: float = 0.22,
    name: str = "",
) -> vtk.vtkActor:
    vertices, triangles = _shape(value).tessellate(tolerance, 0.18)
    points = vtk.vtkPoints()
    for vertex in vertices:
        points.InsertNextPoint(vertex.x, vertex.y, vertex.z)
    cells = vtk.vtkCellArray()
    for triangle in triangles:
        cells.InsertNextCell(3)
        cells.InsertCellPoint(int(triangle[0]))
        cells.InsertCellPoint(int(triangle[1]))
        cells.InsertCellPoint(int(triangle[2]))
    mesh = vtk.vtkPolyData()
    mesh.SetPoints(points)
    mesh.SetPolys(cells)
    normals = vtk.vtkPolyDataNormals()
    normals.SetInputData(mesh)
    normals.ConsistencyOn()
    normals.AutoOrientNormalsOn()
    normals.SplittingOff()
    mapper = vtk.vtkPolyDataMapper()
    mapper.SetInputConnection(normals.GetOutputPort())
    actor = vtk.vtkActor()
    actor.SetMapper(mapper)
    actor.GetProperty().SetColor(rgba[0], rgba[1], rgba[2])
    actor.GetProperty().SetOpacity(rgba[3])
    actor.GetProperty().SetInterpolationToPhong()
    actor.GetProperty().SetAmbient(0.22)
    actor.GetProperty().SetDiffuse(0.78)
    if name in {"ring_cap", "ring_sleeve", "display_glass"}:
        # Matched-black appearance target, not measured optical materials.
        actor.GetProperty().SetSpecular(0.85)
        actor.GetProperty().SetSpecularPower(28.0)
    else:
        actor.GetProperty().SetSpecular(0.08)
        actor.GetProperty().SetSpecularPower(18.0)
    return actor


def _render(
    output_path: Path,
    items: Iterable[
        tuple[
            str,
            cq.Workplane | cq.Shape,
            tuple[float, float, float, float],
            tuple[float, float, float],
        ]
    ],
    *,
    title: str,
    camera: tuple[float, float, float],
    focal: tuple[float, float, float] = (0.0, 0.0, 28.0),
    parallel_scale_mm: float | None = None,
) -> None:
    renderer = vtk.vtkRenderer()
    renderer.SetBackground(0.96, 0.97, 0.98)
    renderer.SetBackground2(0.82, 0.86, 0.90)
    renderer.GradientBackgroundOn()
    for name, value, rgba, offset in items:
        actor = _vtk_actor(value, rgba, name=name)
        actor.SetPosition(*offset)
        renderer.AddActor(actor)
    lights = vtk.vtkLightKit()
    lights.SetKeyLightIntensity(0.8)
    lights.SetKeyLightWarmth(0.5)
    lights.SetFillLightWarmth(0.5)
    lights.SetBackLightWarmth(0.5)
    lights.AddLightsToRenderer(renderer)
    # Neutral highlights reveal the actual shallow CAD texture on black parts.
    # This is studio illustration lighting, not measured anodized-aluminum BRDF.
    highlight = vtk.vtkLight()
    highlight.SetLightTypeToSceneLight()
    highlight.SetPosition(camera[0] - 100.0, camera[1] + 35.0, camera[2] + 100.0)
    highlight.SetFocalPoint(*focal)
    highlight.SetIntensity(0.9)
    highlight.SetPositional(False)
    renderer.AddLight(highlight)

    text = vtk.vtkTextActor()
    text.SetInput(title)
    text.GetTextProperty().SetFontSize(22)
    text.GetTextProperty().SetColor(0.08, 0.10, 0.12)
    text.GetTextProperty().SetBackgroundColor(0.95, 0.97, 0.99)
    text.GetTextProperty().SetBackgroundOpacity(0.88)
    text.SetDisplayPosition(26, 934)
    renderer.AddActor2D(text)

    note = vtk.vtkTextActor()
    note.SetInput("CAD GEOMETRY ONLY - VENDOR DATA AND PHYSICAL VALIDATION PENDING")
    note.GetTextProperty().SetFontSize(15)
    note.GetTextProperty().SetColor(0.55, 0.12, 0.08)
    note.SetDisplayPosition(26, 24)
    renderer.AddActor2D(note)

    vtk_camera = renderer.GetActiveCamera()
    vtk_camera.SetPosition(*camera)
    vtk_camera.SetFocalPoint(*focal)
    vtk_camera.SetViewUp(0.0, 0.0, 1.0)
    vtk_camera.SetViewAngle(30.0)
    if parallel_scale_mm is not None:
        vtk_camera.ParallelProjectionOn()
        vtk_camera.SetParallelScale(parallel_scale_mm)
    renderer.ResetCameraClippingRange()

    window = vtk.vtkRenderWindow()
    window.SetOffScreenRendering(1)
    window.SetMultiSamples(8)
    window.SetSize(1400, 1000)
    window.AddRenderer(renderer)
    window.Render()

    capture = vtk.vtkWindowToImageFilter()
    capture.SetInput(window)
    capture.SetScale(1)
    capture.ReadFrontBufferOff()
    capture.Update()
    writer = vtk.vtkPNGWriter()
    writer.SetFileName(str(output_path))
    writer.SetInputConnection(capture.GetOutputPort())
    writer.Write()
    window.Finalize()


def _assembly_items(
    custom: dict[str, cq.Workplane],
    motor_world: cq.Workplane,
    display_world: cq.Workplane,
) -> list[tuple[str, cq.Workplane, tuple[float, float, float, float]]]:
    items = [(name, part, _color_for(name)) for name, part in custom.items()]
    items.extend(
        [
            ("motor_official", motor_world, COLORS["motor_official"]),
            ("display_official", display_world, COLORS["display_official"]),
        ]
    )
    return items


def _export_step(
    path: Path,
    name: str,
    items: Iterable[tuple[str, cq.Workplane, tuple[float, float, float, float]]],
) -> None:
    assembly = cq.Assembly(name=name)
    for part_name, part, rgba in items:
        assembly.add(part, name=part_name, color=cq.Color(*rgba))
    if not exportStepMeta(assembly, str(path)):
        raise RuntimeError(f"STEP export failed: {path.name}")


def _screen_preview_items(p: V7Defaults) -> list[tuple]:
    """Render-only display graphics; never exported as hardware or firmware."""
    items = []
    for name, text, size, y in (("screen_preview_value", "48", 8.0, 1.0),
                                ("screen_preview_label", "VOLUME", 2.0, -6.0)):
        graphic = cq.Workplane("XY").text(text, size, 0.01, font="Arial", combine=True)
        graphic = graphic.translate((0.0, y, p.display_cover_front_normal_mm + 0.01))
        items.append((name, _orient_at_knob(graphic, p), (0.80, 0.85, 0.88, 1.0), (0, 0, 0)))
    return items


def build(output_dir: Path, *, skip_render: bool = False) -> dict[str, object]:
    p = DEFAULTS
    p.validate()
    output_dir.mkdir(parents=True, exist_ok=True)
    parts_dir = output_dir / "parts"
    parts_dir.mkdir(parents=True, exist_ok=True)

    custom = _build_custom_parts(p)
    print("Importing vendor geometry and checking clearances...", flush=True)
    motor_local, display_local, motor_world, display_world = _import_vendor_parts(p)
    report = _geometry_report(
        p, custom, motor_local, display_local, motor_world, display_world
    )
    # Supplier source files stay intact and drive the conservative fit checks
    # above. Export every supplier solid, but not unattached non-solid faces;
    # the Waveshare source contains such faces and fails compound validity.
    items = []
    vendor_exports = {}
    for name, part, rgba in _assembly_items(custom, motor_world, display_world):
        if name in {"motor_official", "display_official"}:
            original = _shape(part)
            solids = original.Solids()
            exported = cq.Compound.makeCompound(solids)
            original_box, export_box = _bbox(original), _bbox(exported)
            bbox_error = max(abs(original_box[key] - export_box[key]) for key in original_box)
            vendor_exports[name] = {
                "retained_solid_count": len(solids),
                "excluded_non_solid_face_count": len(original.Faces()) - len(exported.Faces()),
                "max_bounding_box_difference_mm": bbox_error,
                "export_shape_valid": exported.isValid(),
                "source_file_unchanged": True,
            }
            report["checks"][f"{name}_solid_export_is_valid"] = (
                len(solids) > 0 and exported.isValid()
            )
            report["checks"][f"{name}_solid_export_preserves_envelope"] = bbox_error <= 1.0e-5
            part = cq.Workplane(obj=exported)
        items.append((name, part, rgba))
    report["vendor_step_exports"] = vendor_exports
    report["appearance"]["powered_on_preview"] = "render-only VOLUME 48 illustration; not hardware solids and not firmware/UI validation"
    report["all_checks_pass"] = all(report["checks"].values())
    (output_dir / "V7_CONCEPT_FIT_DEFAULTS_geometry_report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    if not report["all_checks_pass"]:
        failed = [name for name, ok in report["checks"].items() if not ok]
        raise RuntimeError(f"Geometry checks failed: {', '.join(failed)}")

    external_names = {
        "housing", "bottom_cover", "ring_sleeve", "ring_cap", "ring_index_mark", "fixed_bezel", "display_glass",
        "speaker_grille", "microphone", "status_led", "front_light_strip",
        "power_button", "service_pinhole",
    }
    exterior_items = [
        (name, part, rgba) for name, part, rgba in items
        if name in external_names or name.startswith(("button_", "usb_c_", "foot_"))
    ]
    print("Geometry checks passed; exporting current STEP and STL...", flush=True)
    _export_step(
        output_dir / "V7_CONCEPT_FIT_DEFAULTS_assembly.step",
        "GL30_AMOLED_V7_CONCEPT_FIT_DEFAULTS", items,
    )
    # A second view of the same current model, without heavy supplier internals.
    # This is not a historical revision or a replacement for the full assembly.
    _export_step(
        output_dir / "V7_CONCEPT_FIT_DEFAULTS_exterior.step",
        "GL30_AMOLED_V7_EXTERIOR_ONLY", exterior_items,
    )

    export_names = (
        "housing",
        "bottom_cover",
        "ring_sleeve",
        "ring_cap",
        "torque_carrier",
        "fixed_spider",
        "support_tube",
        "fixed_bezel",
        "button_right_1",
    )
    for name in export_names:
        cq.exporters.export(
            custom[name],
            str(parts_dir / f"{name}_CONCEPT_ONLY.stl"),
            tolerance=0.05,
            angularTolerance=0.08,
        )

    if not skip_render:
        print("Rendering current-model appearance views...", flush=True)
        external_items = [
            (name, part, rgba, (0.0, 0.0, 0.0))
            for name, part, rgba in exterior_items
        ]
        external_items += _button_mark_preview_items(p)
        _render(
            output_dir / "V7_CONCEPT_FIT_DEFAULTS_isometric.png",
            external_items + _screen_preview_items(p),
            title="SCULPTED BLACK DIAL / DIAMOND GRIP BAND - UI ILLUSTRATION",
            camera=(165.0, -195.0, 145.0),
        )
        powered_off_items = [
            (name, part, (0.12, 0.13, 0.14, 1.0) if name in {
                "front_light_strip", "status_led"
            } else rgba, offset)
            for name, part, rgba, offset in external_items
        ]
        _render(
            output_dir / "V7_CONCEPT_FIT_DEFAULTS_powered_off.png",
            powered_off_items,
            title="POWER OFF - BLACK FACE / BLUE INDEX - OPTICAL TARGET ONLY",
            camera=(135.0, -195.0, 180.0),
        )
        _render(
            output_dir / "V7_CONCEPT_FIT_DEFAULTS_side.png",
            external_items,
            title="SCULPTED DIAL / RECESSED SIDE-REAR KEYS - SIDE VIEW",
            camera=(210.0, 0.0, 55.0),
        )
        _render(
            output_dir / "V7_CONCEPT_FIT_DEFAULTS_rear.png",
            external_items,
            title="V7 REAR / POWER AND USB-C REVIEW",
            camera=(145.0, 195.0, 105.0),
        )
        _render(
            output_dir / "V7_CONCEPT_FIT_DEFAULTS_button_detail.png",
            external_items,
            title="SAME-COLOR RECESSED KEYS / SUBTLE DOT MARKS - SWITCH MECHANISM PENDING",
            camera=(170.0, 27.5, 55.0), focal=(45.0, 27.5, p.side_button_z_mm),
            parallel_scale_mm=17.0,
        )
        ring_detail_items = [
            item for item in external_items
            if item[0] in {
                "ring_sleeve", "ring_cap", "ring_index_mark", "fixed_bezel", "display_glass"
            }
        ]
        _render(
            output_dir / "V7_CONCEPT_FIT_DEFAULTS_ring_detail.png",
            ring_detail_items,
            title="ROLLED CROWN / TUCKED WAIST / DIAMOND GRIP - SCREEN OFF",
            camera=(90.0, -128.0, 76.0),
            focal=(0.0, p.knob_center_y_mm, p.knob_center_z_mm + 6.0),
        )
        center = _deck_center(p)
        profile_items = [
            (name, _shape(part).translate(tuple(-value for value in center)).rotate(
                (0, 0, 0), (1, 0, 0), -p.deck_angle_deg
            ), rgba, offset)
            for name, part, rgba, offset in ring_detail_items
        ]
        _render(
            output_dir / "V7_CONCEPT_FIT_DEFAULTS_ring_profile_side.png",
            profile_items,
            title="DIAL SIDE PROFILE / ROLLED CROWN - TUCKED WAIST - GRIP BAND",
            camera=(120.0, 0.0, 7.0), focal=(0.0, 0.0, 7.0),
            parallel_scale_mm=23.0,
        )

        n = _normal(p)
        explode_normal = {
            "bottom_cover": -25.0,
            "motor_official": -18.0,
            "torque_carrier": -7.0,
            "support_tube": -2.0,
            "fixed_spider": 4.0,
            "bearing_envelope": 10.0,
            "ring_sleeve": 16.0,
            "ring_cap": 20.0,
            "ring_index_mark": 20.0,
            "display_official": 28.0,
            "fixed_bezel": 35.0,
            "display_glass": 37.0,
        }
        exploded_items = []
        for name, part, rgba in items:
            if name.startswith(("button_", "usb_c_", "foot_")) or name in {
                "speaker_grille",
                "microphone",
                "status_led",
                "front_light_strip",
                "power_button",
                "service_pinhole",
            }:
                continue
            distance = explode_normal.get(name, 0.0)
            offset = (n[0] * distance, n[1] * distance, n[2] * distance)
            if name == "bottom_cover":
                offset = (0.0, 0.0, distance)
            shown = rgba
            if name == "housing":
                shown = (rgba[0], rgba[1], rgba[2], 0.28)
            exploded_items.append((name, part, shown, offset))
        _render(
            output_dir / "V7_CONCEPT_FIT_DEFAULTS_exploded.png",
            exploded_items,
            title="V7 COAXIAL STACK / LOAD-PATH CONCEPT",
            camera=(175.0, -205.0, 155.0),
            focal=(0.0, -5.0, 35.0),
            parallel_scale_mm=92.0,
        )
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--skip-render", action="store_true")
    args = parser.parse_args()
    report = build(args.output_dir.resolve(), skip_render=args.skip_render)
    print(
        json.dumps(
            {
                "output_dir": str(args.output_dir.resolve()),
                "all_checks_pass": report["all_checks_pass"],
                "release_label": report["release_label"],
            },
            ensure_ascii=False,
        )
    )
    return 0


if __name__ == "__main__":
    exit_code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    if sys.platform == "win32":
        # cadquery-ocp 7.8.1 can access-violate during CPython 3.12 teardown
        # after large STEP compounds have already been written.  Bypass only
        # the faulty destructor phase; every output above is closed and flushed.
        os._exit(exit_code)
    raise SystemExit(exit_code)
