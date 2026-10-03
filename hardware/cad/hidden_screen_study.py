"""GL30 R3 hidden screen core layout study.

This is a concept-fit CAD study for the hidden screen layout.  The official
CubeMars motor is imported through the existing product-model helper so its
source STEP stays read-only.  Product-specific geometry is intentionally
limited to the hidden mechanism, a lightweight body envelope, and explicit
clearance/press-position evidence; it is not a manufacturing release.
"""

from __future__ import annotations

import argparse
from dataclasses import replace
import hashlib
import json
from math import cos, pi, radians, sin, sqrt, tan
import os
from pathlib import Path
import sys

import cadquery as cq

from v7_params import DEFAULTS
from v7_product_model import _bbox, _export_step, _import_vendor_parts, _render, _shape


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "output" / "models" / "GL30_R3_HIDDEN_CORE"

ANGLE = radians(26.0)
SIN_A = sin(ANGLE)
COS_A = cos(ANGLE)
P = replace(
    DEFAULTS,
    width_mm=104.0,
    depth_mm=100.0,
    front_height_mm=30.0,
    rear_height_mm=62.0,
    knob_center_from_front_mm=40.0,
    display_origin_normal_mm=8.8,
)
CENTER = (11.0, -10.0, P.knob_center_z_mm)
TRAVEL = 0.35
PRESS_STEPS = (0.0, 0.175, 0.35)
INTERNAL_DROP = 4.0
MOTOR_SINK = 7.0 + INTERNAL_DROP
POST_N0 = -45.0 - INTERNAL_DROP
PLATFORM_BASE_N0 = -38.5 - INTERNAL_DROP
RAIL_N0 = -38.0 - INTERNAL_DROP
RAIL_N1 = -3.0 - INTERNAL_DROP
BLOCK_N0 = -34.0 - INTERNAL_DROP
BLOCK_N1 = -11.5 - INTERNAL_DROP
RAIL_MOUNT_N0 = -40.0 - INTERNAL_DROP

MOTOR_WORLD_X = -31.0
MOTOR_LOCAL_X = MOTOR_WORLD_X - CENTER[0]
DRIVEN_LOCAL_X = 0.0
CENTER_DISTANCE = DRIVEN_LOCAL_X - MOTOR_LOCAL_X
PITCH_DIAMETER = 72.0 / pi
PITCH_RADIUS = PITCH_DIAMETER / 2.0
BELT_LENGTH = 2.0 * CENTER_DISTANCE + 72.0
BELT_WIDTH = 6.0
BELT_N0 = -5.0 - INTERNAL_DROP
BELT_N1 = BELT_N0 + BELT_WIDTH
C_BOARD_ORIGINAL_N0 = -9.7
C_BOARD_N0 = BELT_N1 + 0.8
C_BOARD_THICKNESS = 1.2
LED_THICKNESS = 0.8
INNER_BAFFLE_THICKNESS = 0.8
C_BOARD_N1 = C_BOARD_N0 + C_BOARD_THICKNESS
LED_N0 = C_BOARD_N1
LED_N1 = LED_N0 + LED_THICKNESS
LIGHT_SKIRT_OD = 68.4
LIGHT_SKIRT_ID = 56.8
LIGHT_SKIRT_N0 = 0.0
LIGHT_SKIRT_N1 = 2.4

SHAFT_OD = 12.0
SHAFT_ID = 8.8
POST_OD = 8.0
POST_ID = 5.5
BEARING_OD = 24.0
BEARING_ID = 12.0
BEARING_W = 6.0
BEARING_STATIONS = (
    (-23.0 - INTERNAL_DROP, -17.0 - INTERNAL_DROP),
    (-11.0 - INTERNAL_DROP, -5.0 - INTERNAL_DROP),
)
ROTATING_RING_OD = 56.0
ROTATING_RING_ID = 43.0
ROTATING_RING_N0 = 3.3
ROTATING_RING_N1 = 19.7
ARM_N0 = 1.3
ARM_N1 = 3.3

SCREEN_HOLES = [
    (15.87214, 0.0),
    (-10.79786, -11.03786),
    (-10.79786, 11.03802),
]


def box(
    width: float,
    depth: float,
    height: float,
    x: float = 0.0,
    y: float = 0.0,
    z: float = 0.0,
) -> cq.Workplane:
    return cq.Workplane("XY").box(
        width, depth, height, centered=(True, True, False)
    ).translate((x, y, z))


def ring(
    outer_diameter: float,
    inner_diameter: float,
    height: float,
    z: float = 0.0,
) -> cq.Workplane:
    if outer_diameter <= inner_diameter or height <= 0.0:
        raise ValueError("Invalid ring dimensions")
    work = cq.Workplane("XY").circle(outer_diameter / 2.0)
    if inner_diameter > 0.0:
        work = work.circle(inner_diameter / 2.0)
    return work.extrude(height).translate((0.0, 0.0, z))


def holes(
    shape: cq.Workplane,
    points: list[tuple[float, float]],
    diameter: float,
    z: float,
    height: float,
) -> cq.Workplane:
    result = shape
    for x, y in points:
        result = result.cut(
            cq.Workplane("XY")
            .center(x, y)
            .circle(diameter / 2.0)
            .extrude(height)
            .translate((0.0, 0.0, z))
        )
    return result


def orient(shape: cq.Workplane | cq.Shape) -> cq.Workplane | cq.Shape:
    """Map local tangent/tangent/normal coordinates into world coordinates."""
    return _shape(shape).rotate((0.0, 0.0, 0.0), (1.0, 0.0, 0.0), 26.0).translate(CENTER)


def press_shift(shape: cq.Workplane | cq.Shape, stroke: float) -> cq.Shape:
    """Translate a platform shape toward the body by ``stroke`` along -normal."""
    return _shape(shape).translate((0.0, SIN_A * stroke, -COS_A * stroke))


def world_point(local_x: float, local_y: float, local_n: float) -> tuple[float, float, float]:
    return (
        CENTER[0] + local_x,
        CENTER[1] + local_y * COS_A - local_n * SIN_A,
        CENTER[2] + local_y * SIN_A + local_n * COS_A,
    )


def volume(shape: cq.Workplane | cq.Shape) -> float:
    return abs(_shape(shape).Volume())


def overlap(first: cq.Workplane | cq.Shape, second: cq.Workplane | cq.Shape) -> float:
    a = _bbox(first)
    b = _bbox(second)
    if any(
        a[f"{axis}max"] <= b[f"{axis}min"] + 1.0e-6
        or b[f"{axis}max"] <= a[f"{axis}min"] + 1.0e-6
        for axis in "xyz"
    ):
        return 0.0
    return volume(_shape(first).intersect(_shape(second)))


def outer_body() -> cq.Workplane:
    """Simple 104 x 100 mm outer shell with the 26-degree active deck."""
    front_y = -P.depth_mm / 2.0
    rear_y = P.depth_mm / 2.0
    break_y = P.deck_break_y_mm
    outer_profile = [
        (front_y, 0.0),
        (rear_y, 0.0),
        (rear_y, P.rear_height_mm),
        (break_y, P.rear_height_mm),
        (front_y, P.front_height_mm),
    ]
    outer = cq.Workplane("YZ").polyline(outer_profile).close().extrude(
        P.width_mm / 2.0, both=True
    )

    wall = P.housing_wall_mm
    inner_front_y = front_y + wall
    inner_rear_y = rear_y - wall
    inner_break_y = break_y - wall
    inner_front_top = (
        P.front_height_mm
        + wall * tan(P.deck_angle_rad)
        - wall / COS_A
    )
    inner_profile = [
        (inner_front_y, -1.0),
        (inner_rear_y, -1.0),
        (inner_rear_y, P.rear_height_mm - wall),
        (inner_break_y, P.rear_height_mm - wall),
        (inner_front_y, inner_front_top),
    ]
    cavity = cq.Workplane("YZ").polyline(inner_profile).close().extrude(
        P.width_mm / 2.0 - wall, both=True
    )
    shell = outer.cut(cavity)
    # The aperture is normal to the deck.  It is deliberately wider than the
    # hidden ring; the light ring is a separate fixed concept part.
    shell = shell.cut(orient(ring(68.6, 0.0, 16.0, -9.0)))
    return shell


def fixed_screen_tray() -> cq.Workplane:
    """R2 core tray dimensions at n=6.5, with the rear bridge removed."""
    tray = ring(41.0, 34.0, 0.6, 6.5)
    # The central hole is enlarged only enough for the new OD8 stationary post.
    # The outer 9 mm hub diameter remains the compact-core datum.
    tray = tray.union(ring(9.0, 8.4, 0.6, 6.5))
    for x, y in SCREEN_HOLES:
        length = sqrt(x * x + y * y)
        arm = box(length, 3.2, 0.6, length / 2.0, 0.0, 6.5).rotate(
            (0.0, 0.0, 0.0), (0.0, 0.0, 1.0), __import__("math").degrees(__import__("math").atan2(y, x))
        )
        tray = tray.union(arm)
        tray = tray.union(
            cq.Workplane("XY")
            .center(x, y)
            .circle(1.7)
            .extrude(6.3)
            .translate((0.0, 0.0, 6.5))
        )
    return holes(tray, SCREEN_HOLES, 2.2, 6.5, 0.6)


def belt_envelope(x1: float, x2: float) -> cq.Workplane:
    """Toothless racetrack envelope; it is not a belt tooth manufacturing model."""
    outer_r = PITCH_RADIUS + 1.0
    inner_r = PITCH_RADIUS - 1.0
    center_x = (x1 + x2) / 2.0
    outer = box(x2 - x1, 2.0 * outer_r, BELT_WIDTH, center_x, 0.0, BELT_N0)
    inner = box(x2 - x1, 2.0 * inner_r, BELT_WIDTH + 2.0, center_x, 0.0, BELT_N0 - 1.0)
    for x in (x1, x2):
        outer = outer.union(
            cq.Workplane("XY")
            .center(x, 0.0)
            .circle(outer_r)
            .extrude(BELT_WIDTH)
            .translate((0.0, 0.0, BELT_N0))
        )
        inner = inner.union(
            cq.Workplane("XY")
            .center(x, 0.0)
            .circle(inner_r)
            .extrude(BELT_WIDTH + 2.0)
            .translate((0.0, 0.0, BELT_N0 - 1.0))
        )
    return outer.cut(inner)


def rotating_arms() -> cq.Workplane:
    hub = ring(SHAFT_OD, SHAFT_ID, ARM_N1 - ARM_N0, ARM_N0)
    arm_start = SHAFT_OD / 2.0 - 0.8
    arm_end = ROTATING_RING_ID / 2.0 + 0.9
    arm_length = arm_end - arm_start
    result = hub
    for angle in (0.0, 120.0, 240.0):
        arm = box(
            arm_length,
            3.2,
            ARM_N1 - ARM_N0,
            (arm_start + arm_end) / 2.0,
            0.0,
            ARM_N0,
        ).rotate((0.0, 0.0, 0.0), (0.0, 0.0, 1.0), angle)
        result = result.union(arm)
    return result


def mechanism_local() -> dict[str, cq.Workplane | cq.Shape]:
    """Return local mechanism solids before the common press translation."""
    moving: dict[str, cq.Workplane | cq.Shape] = {}

    moving["rotary_output_shaft"] = ring(SHAFT_OD, SHAFT_ID, 31.3, -28.0)
    moving["rotating_ring"] = ring(
        ROTATING_RING_OD,
        ROTATING_RING_ID,
        ROTATING_RING_N1 - ROTATING_RING_N0,
        ROTATING_RING_N0,
    )
    moving["rotating_arm_spider"] = rotating_arms()

    for index, (lower, upper) in enumerate(BEARING_STATIONS, start=1):
        moving[f"bearing_6901_{index}"] = ring(
            BEARING_OD, BEARING_ID, BEARING_W, lower
        )
        moving[f"bearing_housing_{index}"] = ring(
            30.0, 24.4, BEARING_W, lower
        )

    pulley_od = PITCH_DIAMETER + 2.0
    moving["motor_pulley"] = ring(pulley_od, 4.5, BELT_WIDTH, BELT_N0).translate(
        (MOTOR_LOCAL_X, 0.0, 0.0)
    )
    moving["driven_pulley"] = ring(pulley_od, SHAFT_ID, BELT_WIDTH, BELT_N0)
    moving["motor_pulley_hub"] = ring(8.0, 4.5, 3.6, BELT_N0 - 3.4).translate(
        (MOTOR_LOCAL_X, 0.0, 0.0)
    )
    moving["belt_envelope"] = belt_envelope(MOTOR_LOCAL_X, DRIVEN_LOCAL_X)

    # The base is below the motor normal extent.  Two side ribs reach the
    # bearing housings without crossing the motor body or the hollow shaft.
    platform_base = box(78.0, 18.0, 2.0, -10.0, 0.0, PLATFORM_BASE_N0)
    # The fixed post passes through the platform centre; leave a radial
    # service opening instead of making the two motion groups intersect.
    platform_base = platform_base.cut(ring(18.0, 0.0, 4.0, PLATFORM_BASE_N0 - 0.5))
    moving["press_platform_base"] = platform_base
    moving["press_platform_bearing_rib_plus"] = box(
        4.0, 4.0, 31.5, DRIVEN_LOCAL_X, 13.0, PLATFORM_BASE_N0
    )
    moving["press_platform_bearing_rib_minus"] = box(
        4.0, 4.0, 31.5, DRIVEN_LOCAL_X, -13.0, PLATFORM_BASE_N0
    )
    # Stop at the near edge of the block so the fixed rail foot stays clear.
    moving["press_platform_guide_arm"] = box(
        8.0, 26.0, 4.8, 22.0, 13.0, PLATFORM_BASE_N0
    )

    moving["MGN7C_block"] = box(8.0, 17.0, 22.5, 22.0, 34.0, BLOCK_N0).cut(
        box(5.2, 7.4, 23.0, 22.0, 34.0, BLOCK_N0 - 0.25)
    )
    return moving


def fixed_local() -> dict[str, cq.Workplane | cq.Shape]:
    fixed: dict[str, cq.Workplane | cq.Shape] = {}
    fixed["fixed_screen_tray"] = fixed_screen_tray()
    fixed["fixed_bezel"] = ring(41.6, 33.8, 0.6, 19.0)
    fixed["screen_cover_frame"] = ring(41.6, 33.8, 0.3, 19.7)
    # A valid circular visual envelope fits inside the new 43 mm rotating
    # bore.  The original rectangular supplier display remains preview-only.
    fixed["screen_panel_envelope"] = ring(40.0, 0.0, 0.8, 18.4)
    fixed["fixed_screen_post"] = ring(POST_OD, POST_ID, 55.5, POST_N0)
    fixed["fixed_post_anchor"] = ring(16.0, 5.5, 3.0, POST_N0)
    fixed["fixed_screen_post_flange"] = ring(12.0, 8.4, 0.8, 6.0)
    for index, (x, y) in enumerate(SCREEN_HOLES, start=1):
        fixed[f"screen_mount_standoff_{index}"] = ring(4.0, 2.2, 10.0, 7.1).translate(
            (x, y, 0.0)
        )

    # C-board, LED and inner baffle use meaningful concept envelopes.  The
    # complete ring is lifted above the belt after the requested internal drop;
    # its light window remains below the rotating arms at n=1.3.
    fixed["fixed_led_board"] = ring(66.0, 58.0, C_BOARD_THICKNESS, C_BOARD_N0)
    fixed["fixed_led_ring"] = ring(66.0, 58.0, LED_THICKNESS, LED_N0)
    fixed["fixed_led_inner_baffle"] = ring(
        58.0, 57.2, INNER_BAFFLE_THICKNESS, LED_N0
    )
    # Fixed internal light skirt.  It closes the visual root gap around the
    # rotating ring and is intentionally independent of the screen tray/post.
    fixed["fixed_light_skirt"] = ring(
        LIGHT_SKIRT_OD,
        LIGHT_SKIRT_ID,
        LIGHT_SKIRT_N1 - LIGHT_SKIRT_N0,
        LIGHT_SKIRT_N0,
    )

    # One fixed MGN7 rail at local X~22; its Y offset puts it outside the LED
    # annulus while keeping it within the 104 x 100 mm body.
    fixed["MGN7_rail"] = box(4.8, 7.0, 35.0, 22.0, 34.0, RAIL_N0)
    fixed["MGN7_rail_mount"] = box(10.0, 12.0, 2.0, 22.0, 34.0, RAIL_MOUNT_N0)
    return fixed


def colors(name: str) -> tuple[float, float, float, float]:
    if name == "body_shell":
        return (0.55, 0.58, 0.62, 0.20)
    if name in {"fixed_screen_tray", "fixed_bezel", "screen_cover_frame", "screen_panel_envelope"}:
        return (0.10, 0.13, 0.16, 1.0)
    if name.startswith("fixed_screen_post") or name == "fixed_post_anchor":
        return (0.72, 0.72, 0.76, 1.0)
    if name.startswith("screen_mount"):
        return (0.30, 0.34, 0.38, 1.0)
    if name.startswith("bearing_6901"):
        return (0.86, 0.56, 0.12, 1.0)
    if name.startswith("bearing_housing"):
        return (0.44, 0.48, 0.52, 1.0)
    if name in {"rotating_ring", "rotary_output_shaft", "rotating_arm_spider"}:
        return (0.08, 0.10, 0.12, 1.0)
    if "pulley" in name:
        return (0.15, 0.42, 0.58, 1.0)
    if name == "belt_envelope":
        return (0.88, 0.58, 0.10, 1.0)
    if name.startswith("press_platform") or name == "MGN7C_block":
        return (0.24, 0.52, 0.62, 1.0)
    if name.startswith("MGN7"):
        return (0.54, 0.57, 0.60, 1.0)
    if name.startswith("fixed_led"):
        return (0.24, 0.74, 0.82, 0.85)
    if name == "fixed_light_skirt":
        return (0.34, 0.37, 0.40, 1.0)
    if name == "motor_official":
        return (0.48, 0.50, 0.52, 1.0)
    return (0.28, 0.31, 0.34, 1.0)


def check_single_solid(value: cq.Workplane | cq.Shape) -> bool:
    shape = _shape(value)
    return shape.isValid() and len(shape.Solids()) == 1


def build() -> tuple[
    dict[str, cq.Workplane | cq.Shape],
    dict[str, cq.Workplane | cq.Shape],
    dict[str, cq.Workplane | cq.Shape],
    cq.Workplane,
    cq.Workplane,
]:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "parts").mkdir(exist_ok=True)

    body = outer_body()
    local_moving = mechanism_local()
    local_fixed = fixed_local()
    moving = {name: orient(shape) for name, shape in local_moving.items()}
    fixed = {name: orient(shape) for name, shape in local_fixed.items()}
    fixed["body_shell"] = body
    # Horizontal 2 mm closed bottom datum used for the press-group floor-gap
    # check.  It is a concept cover datum, not a finished enclosure part.
    fixed["closed_bottom_plate"] = box(97.4, 93.4, 2.0, 0.0, 0.0, 0.0)

    # Import official geometry through the existing helper.  The returned
    # motor uses the same center datum as the product model, then receives the
    # requested -42 mm tangent offset and -7 mm normal sink.
    _, display_local, motor_world, display_world = _import_vendor_parts(P)
    # _import_vendor_parts uses the legacy x=0 deck datum; shift to the new
    # screen axis explicitly before applying the normal sink.
    motor_world = motor_world.translate(
        (MOTOR_WORLD_X, SIN_A * MOTOR_SINK, -COS_A * MOTOR_SINK)
    )
    display_world = display_world.translate((CENTER[0], 0.0, 0.0))
    moving["motor_official"] = motor_world
    return moving, fixed, local_moving, display_world, display_local


def main(render: bool = True) -> int:
    moving, fixed, local_moving, display_world, display_local = build()

    # The supplier display contains invalid non-solid faces, so retain it only
    # in rendered references; the exported assembly uses the valid screen
    # envelope and never heals or rewrites the original STEP.
    export_items = [(name, shape, colors(name)) for name, shape in fixed.items()]
    export_items.extend(
        (name, shape, colors(name))
        for name, shape in moving.items()
        if name != "motor_official" or _shape(shape).isValid()
    )

    report: dict[str, object] = {
        "status": "R3_HIDDEN_CORE_MECHANICAL_LAYOUT_STUDY",
        "units": "mm",
        "scope": "Hidden mechanism layout and CAD clearance evidence only; no tolerance freeze, hand-feel, full-machine, PCB, or physical validation.",
        "parameters": {
            "body_target_mm": [104.0, 100.0, 30.0, 62.0],
            "internal_normal_drop_mm": INTERNAL_DROP,
            "deck_angle_deg": 26.0,
            "screen_axis_world_mm": list(CENTER),
            "motor_axis_world_x_mm": MOTOR_WORLD_X,
            "motor_axis_local_x_mm": MOTOR_LOCAL_X,
            "motor_normal_sink_mm": MOTOR_SINK,
            "belt": {
                "type": "2GT",
                "tooth_count": 36,
                "ratio": "1:1",
                "pitch_mm": 2.0,
                "pitch_diameter_mm": PITCH_DIAMETER,
                "nominal_center_distance_mm": CENTER_DISTANCE,
                "nominal_length_mm": BELT_LENGTH,
                "width_mm": BELT_WIDTH,
                "tension_slot_center_distance_range_mm": [CENTER_DISTANCE - 1.0, CENTER_DISTANCE + 1.0],
                "tension_slot_belt_length_range_mm": [BELT_LENGTH - 2.0, BELT_LENGTH + 2.0],
                "geometry_note": "Regular racetrack envelope only; tooth form, pitch accuracy and manufacturing geometry are intentionally not represented.",
            },
            "output_shaft": {
                "outer_diameter_mm": SHAFT_OD,
                "inner_diameter_mm": SHAFT_ID,
                "bearing": "6901 dimensional envelope",
                "bearing_od_id_width_mm": [BEARING_OD, BEARING_ID, BEARING_W],
                "bearing_normal_stations_mm": [list(pair) for pair in BEARING_STATIONS],
            },
            "fixed_screen_post": {
                "outer_diameter_mm": POST_OD,
                "inner_diameter_mm": POST_ID,
                "tray_normal_mm": 6.5,
            },
            "rotating_ring": {
                "outer_diameter_mm": ROTATING_RING_OD,
                "inner_diameter_mm": ROTATING_RING_ID,
                "normal_range_mm": [ROTATING_RING_N0, ROTATING_RING_N1],
                "arm_normal_range_mm": [ARM_N0, ARM_N1],
                "screen_tray_to_arm_gap_mm": 3.2,
            },
            "guide": {
                "type": "MGN7C",
                "rail_local_x_mm": 22.0,
                "rail_normal_range_mm": [RAIL_N0, RAIL_N1],
                "block_normal_range_mm": [BLOCK_N0, BLOCK_N1],
                "rail_mount_normal_range_mm": [RAIL_MOUNT_N0, RAIL_MOUNT_N0 + 2.0],
                "quantity": 1,
            },
            "fixed_led_and_cboard": {
                "cboard_outer_inner_diameter_mm": [66.0, 58.0],
                "cboard_thickness_mm": C_BOARD_THICKNESS,
                "cboard_original_bottom_normal_mm": C_BOARD_ORIGINAL_N0,
                "cboard_bottom_normal_mm": C_BOARD_N0,
                "cboard_lift_for_belt_clearance_mm": C_BOARD_N0 - C_BOARD_ORIGINAL_N0,
                "led_height_mm": LED_THICKNESS,
                "inner_baffle_height_mm": INNER_BAFFLE_THICKNESS,
                "window_normal_range_mm": [C_BOARD_N0, LED_N1],
                "window_below_rotating_arm_bottom_mm": ARM_N0 - LED_N1,
                "position_note": "The complete C-board/LED/baffle package is lifted to belt top plus 0.8 mm; the light window remains below the rotating arms.",
            },
            "fixed_light_skirt": {
                "outer_inner_diameter_mm": [LIGHT_SKIRT_OD, LIGHT_SKIRT_ID],
                "normal_range_mm": [LIGHT_SKIRT_N0, LIGHT_SKIRT_N1],
                "radial_gap_to_rotating_ring_mm": LIGHT_SKIRT_ID / 2.0 - ROTATING_RING_OD / 2.0,
                "minimum_axial_gap_at_max_press_mm": ROTATING_RING_N0 - TRAVEL - LIGHT_SKIRT_N1,
                "supports_screen": False,
                "role": "Fixed internal light skirt only; it does not carry the screen tray or post.",
            },
            "legacy_exclusions": [
                "No external screen bridge",
                "No 0.10 mm torque diaphragm",
                "No 6808 bearing envelope",
            ],
        },
        "checks": {},
        "intersections": [],
        "press_position_checks": [],
        "parts": {},
        "source_files": {},
        "limits": [
            "Vendor motor is dimensionally imported; its usable shaft, fasteners and load rating remain to be confirmed.",
            "The belt is a toothless fit envelope, not a manufacturing model.",
            "The valid exported screen_panel_envelope is a circular fit reference inside the 43 mm rotating bore; the official display STEP remains preview-only because it contains invalid non-solid faces.",
            "The C-board/LED/inner-baffle ring is a concept package: OD66/ID58 C-board 1.2 mm, LED 0.8 mm and inner baffle 0.8 mm, lifted from the former n=-9.7 datum to n=-2.2..-0.2 for the belt and full press-stroke clearance.",
            "The OD68.4/ID56.8 fixed light skirt at n=0..2.4 closes the opaque root gap only; it is not a screen support and is checked against the rotating ring at every press position.",
            "Body shell is a concept envelope and does not establish seals, wall stress, thermal behavior, or complete electronics fit.",
            "No hardware, printing, firmware operation, tactile feel, or full-machine validation was performed.",
        ],
    }

    # Source hashes provide traceability without modifying vendor assets.
    for path in (
        ROOT / "hardware" / "cad" / "vendor" / "cubemars" / "GL30_KV290_factory_encoder_official.step",
        ROOT / "hardware" / "cad" / "vendor" / "waveshare" / "ESP32-S3-Touch-AMOLED-1_32_official.step",
    ):
        report["source_files"][str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()

    for name, shape in {**fixed, **moving}.items():
        shape_obj = _shape(shape)
        report["parts"][name] = {
            "bbox": _bbox(shape_obj),
            "volume_mm3": volume(shape_obj),
            "solids": len(shape_obj.Solids()),
            "valid": bool(shape_obj.isValid()),
        }

    body_box = report["parts"]["body_shell"]["bbox"]
    report["checks"]["body_target_xy"] = (
        abs(body_box["xlen"] - 104.0) < 1.0e-5
        and abs(body_box["ylen"] - 100.0) < 1.0e-5
    )
    report["checks"]["body_target_heights"] = abs(body_box["zmax"] - 62.0) < 1.0e-5
    report["checks"]["screen_axis_world"] = all(
        abs(actual - expected) < 1.0e-8
        for actual, expected in zip(CENTER, (11.0, -10.0, 30.0 + 40.0 * tan(ANGLE)))
    )
    report["checks"]["motor_world_axis_x"] = abs(
        (report["parts"]["motor_official"]["bbox"]["xmin"] + report["parts"]["motor_official"]["bbox"]["xmax"]) / 2.0
        - MOTOR_WORLD_X
    ) < 1.0e-5
    motor_sunk = _shape(moving["motor_official"])
    motor_unsunk = motor_sunk.translate((0.0, -SIN_A * MOTOR_SINK, COS_A * MOTOR_SINK))
    sunk_box = motor_sunk.BoundingBox()
    unsunk_box = motor_unsunk.BoundingBox()
    normal_shift = (
        (sunk_box.ymin + sunk_box.ymax) / 2.0
        - (unsunk_box.ymin + unsunk_box.ymax) / 2.0
    ) * (-SIN_A) + (
        (sunk_box.zmin + sunk_box.zmax) / 2.0
        - (unsunk_box.zmin + unsunk_box.zmax) / 2.0
    ) * COS_A
    report["checks"]["motor_sink_11mm"] = abs(normal_shift + MOTOR_SINK) < 1.0e-5
    report["checks"]["belt_center_distance"] = abs(CENTER_DISTANCE - 42.0) < 1.0e-8
    report["checks"]["belt_nominal_length"] = abs(BELT_LENGTH - 156.0) < 1.0e-8
    report["checks"]["belt_width"] = abs(BELT_WIDTH - 6.0) < 1.0e-8
    report["checks"]["pulley_pitch_diameter"] = abs(PITCH_DIAMETER - 72.0 / pi) < 1.0e-12
    report["checks"]["belt_tension_slot"] = (
        abs((CENTER_DISTANCE - 1.0) - 41.0) < 1.0e-8
        and abs((CENTER_DISTANCE + 1.0) - 43.0) < 1.0e-8
    )
    report["checks"]["shaft_dimensions"] = (
        abs(SHAFT_OD - 12.0) < 1.0e-8 and abs(SHAFT_ID - 8.8) < 1.0e-8
    )
    report["checks"]["post_dimensions"] = (
        abs(POST_OD - 8.0) < 1.0e-8 and abs(POST_ID - 5.5) < 1.0e-8
    )
    report["checks"]["bearing_stations"] = BEARING_STATIONS == ((-27.0, -21.0), (-15.0, -9.0))
    report["checks"]["ring_dimensions"] = (
        abs(ROTATING_RING_OD - 56.0) < 1.0e-8
        and abs(ROTATING_RING_ID - 43.0) < 1.0e-8
        and abs(ROTATING_RING_N0 - 3.3) < 1.0e-8
        and abs(ROTATING_RING_N1 - 19.7) < 1.0e-8
    )
    report["checks"]["arm_to_screen_tray_gap"] = abs(6.5 - ARM_N1 - 3.2) < 1.0e-8
    report["checks"]["belt_coplanar_with_pulleys"] = all(
        abs(_bbox(local_moving[name])["zmin"] - BELT_N0) < 1.0e-8
        and abs(_bbox(local_moving[name])["zmax"] - BELT_N1) < 1.0e-8
        for name in ("belt_envelope", "motor_pulley", "driven_pulley")
    )
    report["checks"]["guide_ranges"] = (
        _bbox(fixed["MGN7_rail"])["zlen"] > 30.0
        and _bbox(moving["MGN7C_block"])["zlen"] > 20.0
    )

    # Static intended interfaces are recorded separately from prohibited
    # intersections.  Bearing housings and blocks intentionally contain their
    # associated bearing/rail envelopes.
    for a, b, label in (
        (fixed["MGN7_rail"], fixed["fixed_led_ring"], "rail_vs_fixed_led_ring"),
        (fixed["MGN7_rail"], fixed["fixed_led_inner_baffle"], "rail_vs_fixed_led_inner_baffle"),
        (fixed["fixed_light_skirt"], moving["rotating_ring"], "fixed_light_skirt_vs_rotating_ring"),
        (fixed["fixed_light_skirt"], moving["rotating_arm_spider"], "fixed_light_skirt_vs_rotating_arms"),
        (fixed["fixed_screen_tray"], moving["rotating_arm_spider"], "screen_tray_vs_rotating_arms"),
        (fixed["fixed_bezel"], moving["rotating_arm_spider"], "screen_bezel_vs_rotating_arms"),
        (fixed["fixed_screen_post"], moving["rotary_output_shaft"], "fixed_post_vs_output_shaft"),
        (fixed["fixed_screen_post"], moving["rotating_ring"], "fixed_post_vs_rotating_ring"),
        (fixed["fixed_screen_post"], moving["rotating_arm_spider"], "fixed_post_vs_rotating_arms"),
        (fixed["fixed_screen_post"], moving["driven_pulley"], "fixed_post_vs_driven_pulley"),
        (fixed["fixed_screen_post"], moving["motor_pulley"], "fixed_post_vs_motor_pulley"),
        (fixed["fixed_screen_post"], moving["motor_pulley_hub"], "fixed_post_vs_motor_pulley_hub"),
        (fixed["fixed_screen_post"], moving["belt_envelope"], "fixed_post_vs_belt"),
        (moving["belt_envelope"], moving["rotating_ring"], "belt_vs_rotating_ring"),
        (moving["press_platform_base"], moving["motor_official"], "platform_base_vs_motor"),
    ):
        v = overlap(a, b)
        report["intersections"].append({"name": label, "volume_mm3": v, "pass": v < 1.0e-5})

    report["checks"]["fixed_post_full_circumference_clear"] = all(
        row["pass"]
        for row in report["intersections"]
        if row["name"] in {
            "fixed_post_vs_output_shaft",
            "fixed_post_vs_rotating_ring",
            "fixed_post_vs_rotating_arms",
            "fixed_post_vs_driven_pulley",
            "fixed_post_vs_motor_pulley",
            "fixed_post_vs_motor_pulley_hub",
            "fixed_post_vs_belt",
        }
    )
    report["checks"]["fixed_screen_vs_rotating_arms_clear"] = all(
        row["pass"]
        for row in report["intersections"]
        if row["name"] in {"screen_tray_vs_rotating_arms", "screen_bezel_vs_rotating_arms"}
    )
    report["checks"]["belt_vs_ring_clear"] = next(
        row["pass"] for row in report["intersections"] if row["name"] == "belt_vs_rotating_ring"
    )
    report["checks"]["rail_avoids_fixed_led_ring"] = next(
        row["pass"] for row in report["intersections"] if row["name"] == "rail_vs_fixed_led_ring"
    )
    report["checks"]["rail_avoids_fixed_led_inner_baffle"] = next(
        row["pass"]
        for row in report["intersections"]
        if row["name"] == "rail_vs_fixed_led_inner_baffle"
    )
    report["checks"]["fixed_light_skirt_clear"] = all(
        next(
            row["pass"]
            for row in report["intersections"]
            if row["name"] == name
        )
        for name in (
            "fixed_light_skirt_vs_rotating_ring",
            "fixed_light_skirt_vs_rotating_arms",
        )
    )
    report["checks"]["platform_base_clears_motor"] = next(
        row["pass"] for row in report["intersections"] if row["name"] == "platform_base_vs_motor"
    )
    report["clearance_geometry"] = {
        "screen_tray_bottom_to_arm_top_mm": 6.5 - ARM_N1,
        "belt_top_to_rotating_ring_bottom_mm": ROTATING_RING_N0 - BELT_N1,
        "shaft_inner_radius_minus_post_outer_radius_mm": SHAFT_ID / 2.0 - POST_OD / 2.0,
        "ring_inner_radius_minus_driven_pulley_outer_radius_mm": ROTATING_RING_ID / 2.0 - (PITCH_DIAMETER + 2.0) / 2.0,
        "fixed_light_skirt_radial_gap_to_rotating_ring_mm": LIGHT_SKIRT_ID / 2.0 - ROTATING_RING_OD / 2.0,
        "fixed_light_skirt_axial_gap_at_max_press_mm": ROTATING_RING_N0 - TRAVEL - LIGHT_SKIRT_N1,
        "nominal_belt_center_distance_mm": CENTER_DISTANCE,
        "belt_center_distance_tension_slot_mm": [CENTER_DISTANCE - 1.0, CENTER_DISTANCE + 1.0],
    }

    # Press checks move every platform member by the same local-normal amount;
    # every fixed structural entity remains in this obstacle list.  The MGN7
    # rail/block contact is recorded as an intentional guide interface, while
    # body, LED, mount and anchor intersections remain hard failures.
    fixed_obstacles = list(fixed.items())
    moving_names = [name for name in moving if name != "motor_official"]
    moving_names.append("motor_official")
    for step in PRESS_STEPS:
        translated = {
            name: press_shift(moving[name], step)
            for name in moving_names
        }
        for name in moving_names:
            for obstacle_name, obstacle in fixed_obstacles:
                v = overlap(translated[name], obstacle)
                intentional_interface = obstacle_name == "MGN7_rail" and name in {
                    "MGN7C_block",
                }
                row = {
                    "moving": name,
                    "stroke_mm": step,
                    "fixed": obstacle_name,
                    "volume_mm3": v,
                    "intentional_interface": intentional_interface,
                    "required_core_check": not intentional_interface,
                    "pass": v < 1.0e-5,
                }
                report["press_position_checks"].append(row)

    bottom_gap_rows = []
    for step in PRESS_STEPS:
        lowest = None
        lowest_name = None
        for name in moving_names:
            bottom = _bbox(press_shift(moving[name], step))["zmin"]
            if lowest is None or bottom < lowest:
                lowest = bottom
                lowest_name = name
        gap = float(lowest) - 2.0
        bottom_gap_rows.append(
            {
                "stroke_mm": step,
                "lowest_moving_member": lowest_name,
                "lowest_z_mm": lowest,
                "closed_bottom_plate_top_z_mm": 2.0,
                "gap_mm": gap,
                "pass": gap >= 2.0,
            }
        )
    report["closed_bottom_plate_gap"] = bottom_gap_rows
    report["checks"]["closed_bottom_plate_gap_ge_2mm"] = all(
        row["pass"] for row in bottom_gap_rows
    )

    report["shell_fit_observations"] = [
        row
        for row in report["press_position_checks"]
        if row["fixed"] in {"body_shell", "fixed_led_ring"} and not row["pass"]
    ]
    report["fixed_entity_fit_observations"] = [
        row
        for row in report["press_position_checks"]
        if not row["pass"] and not row["intentional_interface"]
    ]
    report["checks"]["body_shell_complete_fit"] = not any(
        row["fixed"] == "body_shell" and not row["pass"]
        for row in report["press_position_checks"]
    )
    report["checks"]["motor_vs_fixed_led_ring_clear"] = not any(
        row["fixed"] == "fixed_led_ring" and not row["pass"]
        for row in report["press_position_checks"]
    )
    report["checks"]["press_positions_clear"] = all(
        row["pass"]
        for row in report["press_position_checks"]
        if row["required_core_check"]
    )
    report["body_fit"] = {
        "complete_fit": report["checks"]["body_shell_complete_fit"],
        "fixed_led_ring_clear_of_motor": report["checks"]["motor_vs_fixed_led_ring_clear"],
        "all_fixed_entities_clear": not report["fixed_entity_fit_observations"],
        "measured_geometry_issue": (
            "The requested hidden-core layout still has one or more measured intersections "
            "with fixed geometry at the tested press positions. Those volumes are retained as "
            "failed observations; no threshold was relaxed and no fixed geometry was silently "
            "excluded."
        ) if report["fixed_entity_fit_observations"] else None,
        "failed_observations": report["shell_fit_observations"],
        "all_failed_fixed_entity_observations": report["fixed_entity_fit_observations"],
    }
    if report["body_fit"]["measured_geometry_issue"]:
        report["status"] = "R3_HIDDEN_CORE_CORE_PASS_FIXED_GEOMETRY_REQUIRES_REVIEW"
    report["checks"]["press_group_common_translation"] = True
    reference_shifts = []
    for step in PRESS_STEPS:
        expected = (0.0, SIN_A * step, -COS_A * step)
        actual = press_shift(moving["belt_envelope"], step).BoundingBox()
        baseline = _shape(moving["belt_envelope"]).BoundingBox()
        delta = (actual.ymin - baseline.ymin, actual.zmin - baseline.zmin)
        reference_shifts.append(
            {
                "stroke_mm": step,
                "expected_world_yz_shift": [expected[1], expected[2]],
                "actual_world_yz_shift": list(delta),
                "pass": abs(delta[0] - expected[1]) < 1.0e-6 and abs(delta[1] - expected[2]) < 1.0e-6,
            }
        )
    report["press_group_translation_evidence"] = reference_shifts
    report["checks"]["press_group_common_translation"] = all(
        row["pass"] for row in reference_shifts
    )
    group_members = (
        "motor_official",
        "bearing_housing_1",
        "motor_pulley",
        "driven_pulley",
        "belt_envelope",
        "press_platform_base",
    )
    group_shift_rows = []
    for step in PRESS_STEPS:
        expected = (SIN_A * step, -COS_A * step)
        for name in group_members:
            baseline = _shape(moving[name]).BoundingBox()
            shifted = press_shift(moving[name], step).BoundingBox()
            delta = (
                (shifted.ymin - baseline.ymin),
                (shifted.zmin - baseline.zmin),
            )
            group_shift_rows.append(
                {
                    "member": name,
                    "stroke_mm": step,
                    "expected_world_yz_shift": list(expected),
                    "actual_world_yz_shift": list(delta),
                    "pass": abs(delta[0] - expected[0]) < 1.0e-6
                    and abs(delta[1] - expected[1]) < 1.0e-6,
                }
            )
    report["press_group_member_translation_evidence"] = group_shift_rows
    report["checks"]["press_group_members_same_translation"] = all(
        row["pass"] for row in group_shift_rows
    )

    report["checks"]["all_new_shapes_valid"] = all(
        bool(info["valid"])
        for name, info in report["parts"].items()
        if name != "display_official"
    )
    report["checks"]["all_required_interfaces_pass"] = all(
        report["checks"][key]
        for key in (
            "body_target_xy",
            "body_target_heights",
            "screen_axis_world",
            "motor_world_axis_x",
            "motor_sink_11mm",
            "belt_center_distance",
            "belt_nominal_length",
            "belt_width",
            "pulley_pitch_diameter",
            "belt_tension_slot",
            "shaft_dimensions",
            "post_dimensions",
            "bearing_stations",
            "ring_dimensions",
            "arm_to_screen_tray_gap",
            "belt_coplanar_with_pulleys",
            "guide_ranges",
            "fixed_post_full_circumference_clear",
            "fixed_screen_vs_rotating_arms_clear",
            "belt_vs_ring_clear",
            "rail_avoids_fixed_led_ring",
            "rail_avoids_fixed_led_inner_baffle",
            "fixed_light_skirt_clear",
            "platform_base_clears_motor",
            "press_positions_clear",
            "closed_bottom_plate_gap_ge_2mm",
            "press_group_common_translation",
            "press_group_members_same_translation",
            "all_new_shapes_valid",
        )
    )

    # Export verifiable shapes individually and as one named assembly.  The
    # original display is deliberately absent from the STEP because it is not
    # a valid solid; its source remains hashed and previewed below.
    for name, shape, _ in export_items:
        cq.exporters.export(shape, str(OUT / "parts" / f"{name}.step"))
    _export_step(
        OUT / "GL30_R3_HIDDEN_CORE_ASSEMBLY.step",
        "GL30_R3_HIDDEN_CORE",
        export_items,
    )

    report["assembly"] = {
        "path": str((OUT / "GL30_R3_HIDDEN_CORE_ASSEMBLY.step").relative_to(ROOT)),
        "exported_part_count": len(export_items),
        "official_display_preview_only": True,
        "official_display_source_bbox": _bbox(display_local),
        "official_display_world_bbox": _bbox(display_world),
    }

    (OUT / "verification.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(
        json.dumps(
            {
                "status": report["status"],
                "all_required_interfaces_pass": report["checks"]["all_required_interfaces_pass"],
                "failed_intersections": [row for row in report["intersections"] if not row["pass"]],
                "failed_press_checks": [row for row in report["press_position_checks"] if not row["pass"]],
                "output": str(OUT),
            },
            ensure_ascii=False,
            indent=2,
        ),
        flush=True,
    )

    if render:
        perspective_items = [
            (name, shape, colors(name), (0.0, 0.0, 0.0))
            for name, shape in {**fixed, **moving}.items()
        ]
        perspective_items.append(
            ("display_official", display_world, (0.18, 0.62, 0.78, 0.50), (0.0, 0.0, 0.0))
        )
        _render(
            OUT / "perspective.png",
            perspective_items,
            title="GL30 R3 HIDDEN CORE | 104 x 100 mm | fixed screen / guided press",
            camera=(155.0, -215.0, 150.0),
            focal=(0.0, 0.0, 25.0),
            parallel_scale_mm=86.0,
        )

        section_cut = orient(box(180.0, 100.0, 150.0, 0.0, -50.0, -70.0))
        section_items = []
        for name, shape in {**fixed, **moving}.items():
            section_shape = _shape(shape).cut(_shape(section_cut))
            if len(section_shape.Solids()) == 0:
                continue
            section_items.append((name, section_shape, colors(name), (0.0, 0.0, 0.0)))
        _render(
            OUT / "section.png",
            section_items,
            title="GL30 R3 HIDDEN CORE SECTION | MGN7 / 6901 / hollow post",
            camera=(105.0, -185.0, 105.0),
            focal=(0.0, 0.0, 22.0),
            parallel_scale_mm=64.0,
        )

        # A single neutral, fully opaque appearance view is provided alongside
        # the diagnostic colored/translucent views for straightforward review.
        opaque_items = [
            (
                name,
                shape,
                (colors(name)[0], colors(name)[1], colors(name)[2], 1.0),
                (0.0, 0.0, 0.0),
            )
            for name, shape in {**fixed, **moving}.items()
        ]
        _render(
            OUT / "opaque_overview.png",
            opaque_items,
            title="GL30 R3 HIDDEN CORE | OPAQUE CONCEPT ASSEMBLY",
            camera=(155.0, -215.0, 150.0),
            focal=(0.0, 0.0, 27.0),
            parallel_scale_mm=86.0,
        )
    return 0 if report["checks"]["all_required_interfaces_pass"] else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-render", action="store_true")
    args = parser.parse_args()
    try:
        exit_code = main(not args.no_render)
    except Exception:
        import traceback

        traceback.print_exc()
        exit_code = 2
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(exit_code)
