"""Independently reopen and check the exported PCB STEP and DXF files.

This checker deliberately does not import or execute the PCB generator.  It
reads each exported STEP and DXF, rebuilds a temporary solid from the DXF,
and compares the two independent readbacks against the frozen mechanical
contract.  The result is a mechanical exchange-format check only; it is not
an electrical, fabrication, or hardware qualification.
"""

from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from math import cos, hypot, radians, sin
import os
from pathlib import Path
import sys
from typing import Any

import cadquery as cq
import ezdxf


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BOARDS_DIR = ROOT / "output" / "models" / "GL30_PCB_1P2" / "boards"
DEFAULT_REPORT = ROOT / "output" / "models" / "GL30_PCB_1P2" / "independent-board-check.json"
TOL = 1.0e-5
GEOMETRY_TOL = 2.0e-4


def _hole_points(radius: float) -> list[list[float]]:
    return [
        [radius * cos(radians(angle)), radius * sin(radians(angle))]
        for angle in (7.5, 127.5, 247.5)
    ]


EXPECTED: dict[str, dict[str, Any]] = {
    "A": {
        "step": "PCB_A_1p2.step",
        "dxf": "PCB_A_outline.dxf",
        "bbox": [-49.0, 49.0, -17.0, 17.0, 0.0, 1.2],
        "hole_radius": 1.35,
        "holes": [[-44.0, -13.0], [44.0, -13.0], [-44.0, 13.0], [44.0, 13.0]],
        "corner_radius": 3.0,
        "corner_centers": [[-46.0, -14.0], [46.0, -14.0], [-46.0, 14.0], [46.0, 14.0]],
        "dxf_counts": {"ARC": 4, "CIRCLE": 4, "LINE": 4},
        "dxf_wires": 5,
        "line_segments": [
            [(-49.0, -14.0), (-49.0, 14.0)],
            [(-46.0, 17.0), (46.0, 17.0)],
            [(49.0, -14.0), (49.0, 14.0)],
            [(-46.0, -17.0), (46.0, -17.0)],
        ],
    },
    "B": {
        "step": "PCB_B_1p2.step",
        "dxf": "PCB_B_outline.dxf",
        "bbox": [-49.0, 49.0, -17.0, 17.0, 0.0, 1.2],
        "hole_radius": 1.35,
        "holes": [[-44.0, -13.0], [44.0, -13.0], [-44.0, 13.0], [44.0, 13.0]],
        "corner_radius": 3.0,
        "corner_centers": [[-46.0, -14.0], [46.0, -14.0], [-46.0, 14.0], [46.0, 14.0]],
        "dxf_counts": {"ARC": 4, "CIRCLE": 4, "LINE": 4},
        "dxf_wires": 5,
        "line_segments": [
            [(-49.0, -14.0), (-49.0, 14.0)],
            [(-46.0, 17.0), (46.0, 17.0)],
            [(49.0, -14.0), (49.0, 14.0)],
            [(-46.0, -17.0), (46.0, -17.0)],
        ],
    },
    "C": {
        "step": "PCB_C_1p2.step",
        "dxf": "PCB_C_outline.dxf",
        "bbox": [-37.0, 37.0, -37.0, 47.0, 0.0, 1.2],
        "hole_radius": 1.10,
        "holes": _hole_points(34.0),
        "outer_radius": 37.0,
        "inner_radius": 28.0,
        "dxf_counts": {"ARC": 1, "CIRCLE": 4, "LINE": 3},
        "dxf_wires": 5,
        "line_segments": [
            [(-12.0, 35.0), (-12.0, 47.0)],
            [(-12.0, 47.0), (12.0, 47.0)],
            [(12.0, 35.0), (12.0, 47.0)],
        ],
    },
}


def _relative(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _bbox(shape: cq.Shape) -> list[float]:
    box = shape.BoundingBox()
    return [
        float(box.xmin), float(box.xmax), float(box.ymin), float(box.ymax),
        float(box.zmin), float(box.zmax),
    ]


def _bbox_dict(values: list[float]) -> dict[str, float]:
    return {
        "xmin": values[0], "xmax": values[1], "ymin": values[2],
        "ymax": values[3], "zmin": values[4], "zmax": values[5],
        "xlen": values[1] - values[0], "ylen": values[3] - values[2],
        "zlen": values[5] - values[4],
    }


def _close(a: float, b: float, tolerance: float = TOL) -> bool:
    return abs(float(a) - float(b)) <= tolerance


def _vector_close(actual: list[float], expected: list[float], tolerance: float = TOL) -> bool:
    return len(actual) == len(expected) and all(
        _close(a, b, tolerance) for a, b in zip(actual, expected)
    )


def _circle_records(shape: cq.Shape) -> list[dict[str, float]]:
    records: list[dict[str, float]] = []
    for edge in shape.Edges():
        if edge.geomType() != "CIRCLE":
            continue
        circle = edge._geomAdaptor().Circle()
        location = circle.Location()
        records.append({
            "x": float(location.X()),
            "y": float(location.Y()),
            "z": float(location.Z()),
            "radius": float(circle.Radius()),
            "length": float(edge.Length()),
        })
    return records


def _group_circles(records: list[dict[str, float]], radius: float) -> list[dict[str, Any]]:
    groups: list[dict[str, Any]] = []
    for record in records:
        if not _close(record["radius"], radius, GEOMETRY_TOL):
            continue
        found = None
        for group in groups:
            if hypot(record["x"] - group["x"], record["y"] - group["y"]) <= GEOMETRY_TOL:
                found = group
                break
        if found is None:
            found = {"x": record["x"], "y": record["y"], "radius": record["radius"], "records": []}
            groups.append(found)
        found["records"].append(record)
    result = []
    for group in groups:
        result.append({
            "center": [float(group["x"]), float(group["y"])],
            "radius": float(group["radius"]),
            "z_values": sorted(float(item["z"]) for item in group["records"]),
            "edge_count": len(group["records"]),
        })
    return sorted(result, key=lambda item: (item["center"][0], item["center"][1]))


def _matches_holes(
    groups: list[dict[str, Any]],
    spec: dict[str, Any],
    zmin: float | None = None,
    zmax: float | None = None,
) -> tuple[bool, bool, list[dict[str, Any]]]:
    expected = spec["holes"]
    if len(groups) != len(expected):
        return False, False, groups
    unused = list(groups)
    centers_ok = True
    through_ok = True
    for point in expected:
        if not unused:
            centers_ok = False
            through_ok = False
            break
        match = min(unused, key=lambda item: hypot(item["center"][0] - point[0], item["center"][1] - point[1]))
        distance = hypot(match["center"][0] - point[0], match["center"][1] - point[1])
        if distance > GEOMETRY_TOL or not _close(match["radius"], spec["hole_radius"], GEOMETRY_TOL):
            centers_ok = False
        if zmin is not None and zmax is not None:
            z_values = match["z_values"]
            through_ok = through_ok and any(_close(z, zmin, GEOMETRY_TOL) for z in z_values)
            through_ok = through_ok and any(_close(z, zmax, GEOMETRY_TOL) for z in z_values)
        unused.remove(match)
    return centers_ok, through_ok, groups


def _feature_groups_ok(shape: cq.Shape, spec: dict[str, Any]) -> dict[str, bool]:
    circles = _circle_records(shape)
    checks: dict[str, bool] = {}
    holes = _group_circles(circles, spec["hole_radius"])
    checks["hole_circle_edge_count"] = len(holes) == len(spec["holes"])
    if spec.get("corner_radius") is not None:
        corners = _group_circles(circles, spec["corner_radius"])
        centers = [item["center"] for item in corners]
        checks["rounded_corner_radius_and_centers"] = (
            len(corners) == len(spec["corner_centers"])
            and all(any(_vector_close(center, expected, GEOMETRY_TOL) for center in centers)
                     for expected in spec["corner_centers"])
        )
    else:
        outer = _group_circles(circles, spec["outer_radius"])
        inner = _group_circles(circles, spec["inner_radius"])
        checks["outer_ring_radius_and_center"] = (
            len(outer) == 1 and _vector_close(outer[0]["center"], [0.0, 0.0], GEOMETRY_TOL)
            and len(outer[0]["z_values"]) >= 2
        )
        checks["inner_ring_radius_and_center"] = (
            len(inner) == 1 and _vector_close(inner[0]["center"], [0.0, 0.0], GEOMETRY_TOL)
            and len(inner[0]["z_values"]) >= 2
        )
    return checks


def _step_readback(path: Path, spec: dict[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {"path": _relative(path), "exists": path.is_file(), "checks": {}}
    if not path.is_file():
        result["error"] = "missing file"
        result["all_checks_pass"] = False
        return result
    result.update({"bytes": path.stat().st_size, "sha256": _sha256(path)})
    try:
        imported = cq.importers.importStep(str(path))
        solids = imported.solids().vals()
        one_solid = len(solids) == 1
        shape = solids[0] if one_solid else None
        checks = result["checks"]
        checks["one_continuous_solid"] = one_solid
        checks["solid_valid_positive_volume"] = bool(shape) and shape.isValid() and shape.Volume() > 0.0
        if shape is None:
            result["error"] = f"expected one solid, got {len(solids)}"
            result["all_checks_pass"] = False
            return result
        actual_bbox = _bbox(shape)
        result["actual"] = {
            "bbox": _bbox_dict(actual_bbox),
            "volume_mm3": float(shape.Volume()),
            "faces": len(shape.Faces()),
            "edges": len(shape.Edges()),
        }
        checks["bbox_and_thickness"] = _vector_close(actual_bbox, spec["bbox"], GEOMETRY_TOL)
        circles = _circle_records(shape)
        hole_groups = _group_circles(circles, spec["hole_radius"])
        centers_ok, through_ok, _ = _matches_holes(
            hole_groups, spec, spec["bbox"][4], spec["bbox"][5]
        )
        checks["hole_centers_and_diameters"] = centers_ok
        checks["holes_through_board"] = through_ok
        checks.update(_feature_groups_ok(shape, spec))
        result["hole_groups"] = hole_groups
        result["circle_edge_count"] = len(circles)
        result["all_checks_pass"] = all(checks.values())
    except Exception as exc:  # Keep a failed readback report usable.
        result["error"] = f"{type(exc).__name__}: {exc}"
        result["all_checks_pass"] = False
    return result


def _xy_point(value: Any) -> tuple[float, float]:
    return float(value.x), float(value.y)


def _segment_signature(start: tuple[float, float], end: tuple[float, float]) -> tuple[tuple[float, float], tuple[float, float]]:
    return tuple(sorted((
        (round(start[0], 6), round(start[1], 6)),
        (round(end[0], 6), round(end[1], 6)),
    )))  # type: ignore[return-value]


def _expected_segments(spec: dict[str, Any]) -> set[tuple[tuple[float, float], tuple[float, float]]]:
    return {
        _segment_signature(tuple(start), tuple(end))
        for start, end in spec["line_segments"]
    }


def _arc_record(entity: Any) -> dict[str, Any]:
    center = _xy_point(entity.dxf.center)
    start = float(entity.dxf.start_angle)
    end = float(entity.dxf.end_angle)
    span = (end - start) % 360.0
    if _close(span, 0.0, 1.0e-6):
        span = 360.0
    return {
        "center": [center[0], center[1]],
        "radius": float(entity.dxf.radius),
        "start_angle_deg": start,
        "end_angle_deg": end,
        "span_deg": span,
        "endpoints": [
            [center[0] + float(entity.dxf.radius) * cos(radians(start)),
             center[1] + float(entity.dxf.radius) * sin(radians(start))],
            [center[0] + float(entity.dxf.radius) * cos(radians(end)),
             center[1] + float(entity.dxf.radius) * sin(radians(end))],
        ],
    }


def _dxf_readback(path: Path, spec: dict[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {"path": _relative(path), "exists": path.is_file(), "checks": {}}
    if not path.is_file():
        result["error"] = "missing file"
        result["all_checks_pass"] = False
        return result
    result.update({"bytes": path.stat().st_size, "sha256": _sha256(path)})
    try:
        document = ezdxf.readfile(path)
        entities = list(document.modelspace())
        counts = Counter(entity.dxftype() for entity in entities)
        result["entity_counts"] = dict(sorted(counts.items()))
        result["units"] = document.header.get("$INSUNITS")
        lines = []
        arcs = []
        circles = []
        for entity in entities:
            if entity.dxftype() == "LINE":
                lines.append({"start": list(_xy_point(entity.dxf.start)), "end": list(_xy_point(entity.dxf.end))})
            elif entity.dxftype() == "ARC":
                arcs.append(_arc_record(entity))
            elif entity.dxftype() == "CIRCLE":
                center = _xy_point(entity.dxf.center)
                circles.append({
                    "center": [center[0], center[1]],
                    "radius": float(entity.dxf.radius),
                    "z": float(entity.dxf.center.z),
                })
        result["primitives"] = {"lines": lines, "arcs": arcs, "circles": circles}
        checks = result["checks"]
        checks["units_are_millimetres"] = result["units"] == 4
        checks["primitive_counts_expected"] = dict(counts) == spec["dxf_counts"]
        checks["line_boundary_expected"] = {
            _segment_signature(tuple(item["start"]), tuple(item["end"])) for item in lines
        } == _expected_segments(spec)
        if spec.get("corner_radius") is not None:
            checks["corner_arc_radius_and_centers"] = (
                len(arcs) == 4
                and all(_close(arc["radius"], spec["corner_radius"], GEOMETRY_TOL) for arc in arcs)
                and all(_close(arc["span_deg"], 90.0, GEOMETRY_TOL) for arc in arcs)
                and all(any(_vector_close(arc["center"], expected, GEOMETRY_TOL)
                            for arc in arcs) for expected in spec["corner_centers"])
            )
        else:
            outer_arc = [arc for arc in arcs if _close(arc["radius"], spec["outer_radius"], GEOMETRY_TOL)]
            inner_circles = [
                circle for circle in circles
                if _close(circle["radius"], spec["inner_radius"], GEOMETRY_TOL)
            ]
            checks["outer_arc_radius_center_and_join"] = (
                len(outer_arc) == 1
                and _vector_close(outer_arc[0]["center"], [0.0, 0.0], GEOMETRY_TOL)
                and _close(outer_arc[0]["span_deg"], 322.150711, 1.0e-3)
                and {
                    (round(point[0], 4), round(point[1], 4))
                    for point in outer_arc[0]["endpoints"]
                } == {(-12.0, 35.0), (12.0, 35.0)}
            )
            checks["inner_arc_circle_diameter_and_center"] = (
                len(inner_circles) == 1
                and _vector_close(inner_circles[0]["center"], [0.0, 0.0], GEOMETRY_TOL)
            )
        hole_circles = [
            {"center": circle["center"], "radius": circle["radius"], "z_values": [circle["z"]]}
            for circle in circles
            if _close(circle["radius"], spec["hole_radius"], GEOMETRY_TOL)
        ]
        centers_ok, _, _ = _matches_holes(hole_circles, spec)
        checks["hole_centers_and_diameters"] = centers_ok
        result["hole_circles"] = hole_circles

        imported = cq.importers.importDXF(str(path))
        wires = imported.wires().vals()
        extruded = imported.wires().toPending().extrude(spec["bbox"][5] - spec["bbox"][4])
        solids = extruded.solids().vals()
        one_solid = len(solids) == 1
        shape = solids[0] if one_solid else None
        checks["expected_wire_count"] = len(wires) == spec["dxf_wires"]
        checks["reopened_as_one_solid"] = one_solid
        checks["reopened_solid_valid_positive_volume"] = bool(shape) and shape.isValid() and shape.Volume() > 0.0
        if shape is not None:
            actual_bbox = _bbox(shape)
            result["actual"] = {
                "bbox": _bbox_dict(actual_bbox),
                "volume_mm3": float(shape.Volume()),
                "faces": len(shape.Faces()),
                "edges": len(shape.Edges()),
            }
            checks["bbox_and_thickness"] = _vector_close(actual_bbox, spec["bbox"], GEOMETRY_TOL)
        else:
            result["error"] = f"DXF extrusion expected one solid, got {len(solids)}"
            checks["bbox_and_thickness"] = False
        result["all_checks_pass"] = all(checks.values())
    except Exception as exc:  # Keep a failed readback report usable.
        result["error"] = f"{type(exc).__name__}: {exc}"
        result["all_checks_pass"] = False
    return result


def _hole_signature(groups: list[dict[str, Any]]) -> list[list[float]]:
    return sorted([
        [float(item["center"][0]), float(item["center"][1]), float(item["radius"])]
        for item in groups
    ])


def _cross_format(step: dict[str, Any], dxf: dict[str, Any]) -> dict[str, Any]:
    checks: dict[str, bool] = {}
    step_actual = step.get("actual", {})
    dxf_actual = dxf.get("actual", {})
    step_bbox = step_actual.get("bbox", {})
    dxf_bbox = dxf_actual.get("bbox", {})
    bbox_keys = ("xmin", "xmax", "ymin", "ymax", "zmin", "zmax", "xlen", "ylen", "zlen")
    checks["bbox_and_thickness_match"] = bool(step_bbox and dxf_bbox) and all(
        _close(step_bbox.get(key), dxf_bbox.get(key), GEOMETRY_TOL) for key in bbox_keys
    )
    checks["volume_match"] = (
        "volume_mm3" in step_actual and "volume_mm3" in dxf_actual
        and _close(step_actual["volume_mm3"], dxf_actual["volume_mm3"], 1.0e-5)
    )
    checks["hole_centers_and_diameters_match"] = (
        bool(step.get("hole_groups")) and bool(dxf.get("hole_circles"))
        and _vector_close(
            [item for group in _hole_signature(step["hole_groups"]) for item in group],
            [item for group in _hole_signature(dxf["hole_circles"]) for item in group],
            GEOMETRY_TOL,
        )
    )
    checks["both_formats_have_one_valid_solid"] = (
        step.get("checks", {}).get("one_continuous_solid", False)
        and dxf.get("checks", {}).get("reopened_as_one_solid", False)
        and step.get("checks", {}).get("solid_valid_positive_volume", False)
        and dxf.get("checks", {}).get("reopened_solid_valid_positive_volume", False)
    )
    return {"checks": checks, "all_checks_pass": all(checks.values())}


def _validate_board(board: str, spec: dict[str, Any], boards_dir: Path) -> dict[str, Any]:
    step_path = boards_dir / spec["step"]
    dxf_path = boards_dir / spec["dxf"]
    step = _step_readback(step_path, spec)
    dxf = _dxf_readback(dxf_path, spec)
    cross = _cross_format(step, dxf)
    return {
        "board": board,
        "contract": {
            "step": spec["step"],
            "dxf": spec["dxf"],
            "expected_bbox_mm": spec["bbox"],
            "expected_hole_diameter_mm": 2.0 * spec["hole_radius"],
            "expected_hole_centers_xy_mm": spec["holes"],
        },
        "step": step,
        "dxf": dxf,
        "cross_format": cross,
        "all_checks_pass": bool(step.get("all_checks_pass"))
        and bool(dxf.get("all_checks_pass"))
        and bool(cross.get("all_checks_pass")),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boards-dir", type=Path, default=DEFAULT_BOARDS_DIR)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    args = parser.parse_args()
    boards_dir = args.boards_dir.resolve()
    report_path = args.report.resolve()
    results = {board: _validate_board(board, spec, boards_dir) for board, spec in EXPECTED.items()}
    report = {
        "schema": "independent-board-check.v1",
        "scope": "Independent STEP and DXF reopen; not PCB fabrication, electrical, hardware, or SolidWorks qualification",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "boards_dir": _relative(boards_dir),
        "expected": {
            "A_B": "98 x 34 x 1.2 mm, R3, four 2.7 mm holes at (+/-44,+/-13) mm",
            "C": "outer diameter 74 mm, inner diameter 56 mm, 24 mm ear at Y=32..47, 1.2 mm thick, three 2.2 mm holes on R34 at 7.5/127.5/247.5 degrees",
        },
        "files": results,
        "all_checks_pass": all(item["all_checks_pass"] for item in results.values()),
        "limitations": [
            "This proves the current exported exchange files reopen as valid single solids with the stated geometry; it does not prove routed copper, drill manufacturing tolerances, board stack-up, electrical function, or physical assembly fit.",
            "STEP and DXF are checked as local board coordinates. Mounted transforms and the wider assembly are outside this report.",
        ],
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    for board, result in results.items():
        status = "PASS" if result["all_checks_pass"] else "FAIL"
        print(f"{status} {board}: STEP/DXF independent reopen and geometry contract", flush=True)
        for label in ("step", "dxf", "cross_format"):
            section = result[label]
            for name, passed in section.get("checks", {}).items():
                if not passed:
                    print(f"  FAILED {label}/{name}", flush=True)
    print(f"Report: {_relative(report_path)}", flush=True)
    return 0 if report["all_checks_pass"] else 1


if __name__ == "__main__":
    code = main()
    sys.stdout.flush()
    sys.stderr.flush()
    if os.name == "nt":
        os._exit(code)
    raise SystemExit(code)
