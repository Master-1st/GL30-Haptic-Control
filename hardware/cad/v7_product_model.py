"""Build the GL30 AMOLED V7 CONCEPT_FIT_DEFAULTS assembly.

The script imports the official CubeMars and Waveshare STEP files, builds only
the product-specific concept geometry, exports STEP/STL, writes a machine-
readable geometry report, and renders three PNG review views.  It is not a
manufacturing release and deliberately leaves vendor-unknown interfaces out.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import asdict
from math import cos, radians, sin, sqrt, tan
from pathlib import Path
from typing import Iterable

import cadquery as cq
import vtk

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
    beam_length = rim_inner / 2.0 - hub_outer / 2.0
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
        (inner_front_y, wall),
        (inner_rear_y, wall),
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

    aperture = (
        cq.Workplane("XY")
        .circle(p.deck_aperture_diameter_mm / 2.0)
        .extrude(60.0, both=True)
    )
    aperture = _orient_at_knob(aperture, p)
    housing = housing.cut(aperture)

    for x in (-18.0, 18.0):
        usb_slot = cq.Workplane("XY").box(10.0, 7.0, 4.2).translate((x, 50.0, 18.0))
        housing = housing.cut(usb_slot)
    return housing


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
    ring_sleeve = _annulus(
        p.knob_outer_diameter_mm,
        sleeve_inner,
        p.bearing_width_mm,
    ).translate((0.0, 0.0, p.ring_bearing_start_normal_mm))

    bearing_front = p.ring_bearing_start_normal_mm + p.bearing_width_mm
    ring_cap = _annulus(
        p.knob_outer_diameter_mm,
        p.knob_inner_diameter_mm,
        p.ring_front_normal_mm - bearing_front,
    ).translate((0.0, 0.0, bearing_front))

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
        .circle(p.fixed_bezel_aperture_diameter_mm / 2.0 - 0.2)
        .extrude(0.35)
        .translate((0.0, 0.0, p.bezel_start_normal_mm + 0.36))
    )

    parts = {
        "housing": housing,
        "bearing_envelope": _orient_at_knob(bearing, p),
        "ring_sleeve": _orient_at_knob(ring_sleeve, p),
        "ring_cap": _orient_at_knob(ring_cap, p),
        "torque_carrier": _orient_at_knob(torque_carrier, p),
        "fixed_spider": _orient_at_knob(fixed_spider, p),
        "support_tube": _orient_at_knob(support_tube, p),
        "fixed_bezel": _orient_at_knob(bezel, p),
        "display_glass": _orient_at_knob(glass, p),
    }

    for index, x in enumerate((-30.0, -10.0, 10.0, 30.0), start=1):
        button = (
            cq.Workplane("XY")
            .circle(p.button_diameter_mm / 2.0)
            .extrude(p.button_height_mm)
        )
        parts[f"button_{index}"] = _place_on_active_deck(
            button,
            p,
            x_mm=x,
            from_front_mm=p.button_center_from_front_mm,
        )

    parts["speaker_grille"] = (
        cq.Workplane("XY").circle(10.0).extrude(0.7).translate((-37.0, 34.0, 52.0))
    )
    parts["microphone"] = (
        cq.Workplane("XY").circle(1.0).extrude(0.8).translate((37.0, 34.0, 52.0))
    )
    parts["status_strip"] = (
        cq.Workplane("XY").box(72.0, 1.2, 2.2).translate((0.0, -50.4, 8.0))
    )
    for index, x in enumerate((-18.0, 18.0), start=1):
        parts[f"usb_c_{index}"] = (
            cq.Workplane("XY").box(9.0, 2.0, 3.4).translate((x, 49.2, 18.0))
        )
    for index, (x, y) in enumerate(
        ((-52.0, -38.0), (52.0, -38.0), (-52.0, 38.0), (52.0, 38.0)),
        start=1,
    ):
        parts[f"foot_{index}"] = (
            cq.Workplane("XY").circle(4.0).extrude(1.2).translate((x, y, -1.2))
        )
    return parts


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
    checks = {
        "official_gl30_axis_length_28p2": abs(motor_local_box["xlen"] - 28.2) <= 0.2,
        "official_gl30_radial_diameter_34p5": abs(2.0 * motor_radius - 34.5) <= 0.25,
        "official_display_thickness_11p3": abs(display_local_box["ylen"] - 11.3) <= 0.25,
        "active_deck_fits_body": 0.0 < p.active_deck_run_mm < p.depth_mm,
        "rear_platform_exists": p.rear_platform_run_mm > 0.0,
        "ring_rear_margin_at_least_2mm": p.ring_rear_margin_mm >= 2.0,
        "moving_gap_at_least_0p5mm": p.moving_radial_gap_mm >= 0.5,
        "screen_to_ring_clearance_at_least_0p5mm": screen_to_ring >= 0.5,
        "motor_to_inner_floor_clearance_at_least_0p5mm": floor_clearance >= 0.5,
    }
    return {
        "release_label": "CONCEPT_FIT_DEFAULTS",
        "evidence_boundary": "CAD geometry only; no physical, load, thermal, or electrical validation",
        "parameters_mm": asdict(p),
        "parameter_provenance": PARAMETER_PROVENANCE,
        "derived": {
            "active_deck_run_mm": p.active_deck_run_mm,
            "rear_platform_run_mm": p.rear_platform_run_mm,
            "deck_break_y_mm": p.deck_break_y_mm,
            "knob_center_y_mm": p.knob_center_y_mm,
            "knob_center_z_mm": p.knob_center_z_mm,
            "moving_radial_gap_mm": p.moving_radial_gap_mm,
            "ring_rear_margin_mm": p.ring_rear_margin_mm,
            "official_display_max_radius_mm": display_radius,
            "screen_to_ring_radial_clearance_mm": screen_to_ring,
            "official_gl30_max_radius_mm": motor_radius,
            "motor_to_inner_floor_clearance_mm": floor_clearance,
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
        },
        "checks": checks,
        "all_checks_pass": all(checks.values()),
        "vendor_holds": [
            "encoder electrical interface, voltage, pinout, connector and timing",
            "rotor/stator face roles and cable bend envelope",
            "6 mm bore support/wiring permission",
            "allowed radial/axial load and bearing selection",
        ],
    }


COLORS: dict[str, tuple[float, float, float, float]] = {
    "housing": (0.16, 0.18, 0.21, 1.0),
    "bearing_envelope": (0.68, 0.72, 0.76, 1.0),
    "ring_sleeve": (0.12, 0.13, 0.15, 1.0),
    "ring_cap": (0.08, 0.09, 0.11, 1.0),
    "torque_carrier": (0.90, 0.48, 0.10, 1.0),
    "fixed_spider": (0.18, 0.52, 0.72, 1.0),
    "support_tube": (0.60, 0.64, 0.68, 1.0),
    "fixed_bezel": (0.07, 0.08, 0.10, 1.0),
    "display_glass": (0.02, 0.05, 0.07, 1.0),
    "speaker_grille": (0.05, 0.05, 0.06, 1.0),
    "microphone": (0.03, 0.03, 0.03, 1.0),
    "status_strip": (0.10, 0.75, 0.90, 1.0),
    "usb_c": (0.35, 0.37, 0.40, 1.0),
    "button": (0.22, 0.24, 0.27, 1.0),
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


def _vtk_actor(
    value: cq.Workplane | cq.Shape,
    rgba: tuple[float, float, float, float],
    *,
    tolerance: float = 0.22,
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
) -> None:
    renderer = vtk.vtkRenderer()
    renderer.SetBackground(0.96, 0.97, 0.98)
    renderer.SetBackground2(0.82, 0.86, 0.90)
    renderer.GradientBackgroundOn()
    for _, value, rgba, offset in items:
        actor = _vtk_actor(value, rgba)
        actor.SetPosition(*offset)
        renderer.AddActor(actor)

    text = vtk.vtkTextActor()
    text.SetInput(title)
    text.GetTextProperty().SetFontSize(22)
    text.GetTextProperty().SetColor(0.08, 0.10, 0.12)
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


def build(output_dir: Path, *, skip_render: bool = False) -> dict[str, object]:
    p = DEFAULTS
    p.validate()
    output_dir.mkdir(parents=True, exist_ok=True)
    parts_dir = output_dir / "parts"
    parts_dir.mkdir(parents=True, exist_ok=True)

    custom = _build_custom_parts(p)
    motor_local, display_local, motor_world, display_world = _import_vendor_parts(p)
    report = _geometry_report(
        p, custom, motor_local, display_local, motor_world, display_world
    )
    (output_dir / "V7_CONCEPT_FIT_DEFAULTS_geometry_report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    if not report["all_checks_pass"]:
        failed = [name for name, ok in report["checks"].items() if not ok]
        raise RuntimeError(f"Geometry checks failed: {', '.join(failed)}")

    assembly = cq.Assembly(name="GL30_AMOLED_V7_CONCEPT_FIT_DEFAULTS")
    for name, part, rgba in _assembly_items(custom, motor_world, display_world):
        assembly.add(part, name=name, color=cq.Color(*rgba))
    assembly.save(
        str(output_dir / "V7_CONCEPT_FIT_DEFAULTS_assembly.step"),
        exportType="STEP",
        mode="default",
    )

    export_names = (
        "housing",
        "ring_sleeve",
        "ring_cap",
        "torque_carrier",
        "fixed_spider",
        "support_tube",
        "fixed_bezel",
    )
    for name in export_names:
        cq.exporters.export(
            custom[name],
            str(parts_dir / f"{name}_CONCEPT_ONLY.stl"),
            tolerance=0.05,
            angularTolerance=0.08,
        )

    if not skip_render:
        external_names = {
            "housing",
            "ring_sleeve",
            "ring_cap",
            "fixed_bezel",
            "display_glass",
            "speaker_grille",
            "microphone",
            "status_strip",
            "display_official",
        }
        external_items = []
        for name, part, rgba in _assembly_items(custom, motor_world, display_world):
            if name in external_names or name.startswith(("button_", "usb_c_", "foot_")):
                external_items.append((name, part, rgba, (0.0, 0.0, 0.0)))
        _render(
            output_dir / "V7_CONCEPT_FIT_DEFAULTS_isometric.png",
            external_items,
            title="GL30 AMOLED V7 - CONCEPT_FIT_DEFAULTS",
            camera=(165.0, -195.0, 145.0),
        )
        _render(
            output_dir / "V7_CONCEPT_FIT_DEFAULTS_side.png",
            external_items,
            title="V7 SIDE / PACKAGE REVIEW",
            camera=(210.0, 0.0, 55.0),
        )

        n = _normal(p)
        explode_normal = {
            "motor_official": -18.0,
            "torque_carrier": -7.0,
            "support_tube": -2.0,
            "fixed_spider": 4.0,
            "bearing_envelope": 10.0,
            "ring_sleeve": 16.0,
            "ring_cap": 20.0,
            "display_official": 28.0,
            "fixed_bezel": 35.0,
            "display_glass": 37.0,
        }
        exploded_items = []
        for name, part, rgba in _assembly_items(custom, motor_world, display_world):
            if name.startswith(("button_", "usb_c_", "foot_")) or name in {
                "speaker_grille",
                "microphone",
                "status_strip",
            }:
                continue
            distance = explode_normal.get(name, 0.0)
            offset = (n[0] * distance, n[1] * distance, n[2] * distance)
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
