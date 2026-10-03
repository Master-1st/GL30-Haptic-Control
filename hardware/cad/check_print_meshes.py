"""Read-only print-mesh validation for GL30 H2D PETG validation kit exports."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import zipfile
import xml.etree.ElementTree as ET
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import List, Optional, Tuple

import numpy as np
import vtk
from vtk.vtkCommonCore import vtkObject
from vtk.util.numpy_support import numpy_to_vtk, vtk_to_numpy


PRINT_DIR_SUFFIX = Path("output/print/GL30_H2D_PETG_VALIDATION")
DEFAULT_MANIFEST = PRINT_DIR_SUFFIX / "geometry_manifest.json"
DEFAULT_OUTPUT = PRINT_DIR_SUFFIX / "mesh_validation.json"

EXPECTED_MODEL_COUNT = 4
EXPECTED_STL_COUNT = 4
EXPECTED_3MF_COUNT = 4

H2D_BED_XY = 325.0
H2D_BED_Y = 320.0
H2D_BED_Z = 325.0
BED_Z = 0.0
BED_TOL = 0.03

CLEAN_TOL = 1e-6
DEGENERATE_AREA_TOL = 1e-11
EDGE_COUNT_TOL = 0
VOLUME_DIFF_REL_TOL = 0.005
ENVELOPE_TOL = 0.06


@dataclass
class FileCheck:
    path: str
    exists: bool = False
    bytes: Optional[int] = None
    sha256: Optional[str] = None
    sha256_match: Optional[bool] = None
    passed: bool = False
    issues: List[str] = field(default_factory=list)
    metrics: Optional[dict] = None


@dataclass
class ModelResult:
    model_key: str
    manifest_found: bool = False
    manifest_file_block: Optional[dict] = None
    stl: FileCheck = field(default_factory=lambda: FileCheck(""))
    mf3: FileCheck = field(default_factory=lambda: FileCheck(""))
    compare: dict = field(default_factory=dict)
    passed: bool = False
    issues: List[str] = field(default_factory=list)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_transform(text: Optional[str]) -> np.ndarray:
    if not text:
        return np.eye(4, dtype=float)
    raise ValueError("non-empty 3MF transform is not supported by this checker")


def parse_xml_tag_ns(root: ET.Element, tag: str) -> str:
    if root.tag and root.tag[0] == "{":
        ns = root.tag.split("}")[0][1:]
        return f"{{{ns}}}{tag}"
    return tag


def parse_3mf(path: Path) -> Tuple[vtk.vtkPolyData, dict, List[str]]:
    issues: List[str] = []
    with zipfile.ZipFile(path, "r") as zf:
        if "3D/3dmodel.model" not in zf.namelist():
            raise ValueError("3mf missing 3D/3dmodel.model")
        xml_data = zf.read("3D/3dmodel.model")

    root = ET.fromstring(xml_data)
    tag = lambda t: parse_xml_tag_ns(root, t)
    model_unit = root.attrib.get("unit", "").lower() or "unknown"
    if model_unit != "millimeter":
        issues.append(f"3mf unit '{model_unit}', expected 'millimeter'")

    resources = root.find(tag("resources"))
    build = root.find(tag("build"))
    if resources is None or build is None:
        raise ValueError("3mf missing <resources> or <build>")

    objects: dict[str, ET.Element] = {}
    for obj in resources.findall(tag("object")):
        oid = obj.attrib.get("id")
        if oid is not None:
            objects[oid] = obj

    if not objects:
        raise ValueError("3mf has no <object> in resources")

    def parse_mesh(mesh_node: ET.Element) -> Tuple[np.ndarray, np.ndarray]:
        vertices_node = mesh_node.find(tag("vertices"))
        triangles_node = mesh_node.find(tag("triangles"))
        if vertices_node is None or triangles_node is None:
            raise ValueError("mesh node missing vertices or triangles")
        v_nodes = vertices_node.findall(tag("vertex"))
        t_nodes = triangles_node.findall(tag("triangle"))
        pts = np.array(
            [[float(v.attrib["x"]), float(v.attrib["y"]), float(v.attrib["z"])] for v in v_nodes],
            dtype=float,
        )
        tri = np.array(
            [[int(t.attrib["v1"]), int(t.attrib["v2"]), int(t.attrib["v3"])] for t in t_nodes],
            dtype=np.int64,
        )
        return pts, tri

    def visit_object(oid: str, transform: np.ndarray, visiting: List[str]) -> Tuple[np.ndarray, np.ndarray]:
        if oid in visiting:
            raise ValueError(f"cycle detected in component references: {oid}")
        obj = objects.get(oid)
        if obj is None:
            raise ValueError(f"unknown objectid {oid}")

        visiting.append(oid)
        mesh_node = obj.find(tag("mesh"))
        if mesh_node is not None:
            pts, tri = parse_mesh(mesh_node)
            if pts.size == 0:
                visiting.pop()
                return np.empty((0, 3), dtype=float), np.empty((0, 3), dtype=np.int64)
            if pts.shape[1] != 3:
                visiting.pop()
                raise ValueError("mesh point dimension is not 3")
            if np.any(tri < 0) or np.any(tri >= len(pts)):
                visiting.pop()
                raise ValueError(f"object {oid} has invalid triangle indices")
            ptsh = np.c_[pts, np.ones(len(pts))]
            pts_t = (ptsh @ transform.T)[:, :3]
            visiting.pop()
            return pts_t, tri

        components_node = obj.find(tag("components"))
        if components_node is None:
            visiting.pop()
            return np.empty((0, 3), dtype=float), np.empty((0, 3), dtype=np.int64)

        accum_pts: List[np.ndarray] = []
        accum_tri: List[np.ndarray] = []
        for comp in components_node.findall(tag("component")):
            cid = comp.attrib.get("objectid")
            if cid is None:
                continue
            comp_t = transform @ parse_transform(comp.attrib.get("transform"))
            c_pts, c_tri = visit_object(cid, comp_t, visiting)
            if c_pts.size == 0:
                continue
            tri_offset = sum(a.shape[0] for a in accum_pts)
            accum_pts.append(c_pts)
            if c_tri.size:
                accum_tri.append(c_tri + tri_offset)

        visiting.pop()
        if not accum_pts:
            return np.empty((0, 3), dtype=float), np.empty((0, 3), dtype=np.int64)
        return np.vstack(accum_pts), np.vstack(accum_tri) if accum_tri else np.empty((0, 3), dtype=np.int64)

    # Build root geometry from every build item.
    model_pts: List[np.ndarray] = []
    model_tri: List[np.ndarray] = []
    build_items = 0
    referenced_ids: List[str] = []
    for item in build.findall(tag("item")):
        build_items += 1
        oid = item.attrib.get("objectid")
        if oid is None:
            issues.append("build item missing objectid")
            continue
        pts_i, tri_i = visit_object(oid, parse_transform(item.attrib.get("transform")), [])
        if pts_i.size == 0:
            continue
        if tri_i.size == 0:
            continue
        referenced_ids.append(oid)
        tri_offset = sum(a.shape[0] for a in model_pts)
        model_pts.append(pts_i)
        if np.any(tri_i < 0) or np.any(tri_i >= len(pts_i)):
            issues.append(f"object {oid} triangle index out of local range")
        model_tri.append(tri_i + tri_offset)

    if build_items == 0:
        issues.append("3mf build has no items")

    if not model_pts or not model_tri:
        raise ValueError("3mf mesh assembly produced no triangles")

    pts = np.vstack(model_pts)
    tri = np.vstack(model_tri)
    if np.any(tri < 0) or np.any(tri >= len(pts)):
        issues.append("3MF global triangle indices out of bounds")

    vtk_points = vtk.vtkPoints()
    vtk_points.SetData(numpy_to_vtk(pts, deep=True, array_type=vtk.VTK_DOUBLE))
    cell_array = vtk.vtkCellArray()
    for a, b, c in tri.astype(np.int64):
        tri_cell = vtk.vtkTriangle()
        tri_cell.GetPointIds().SetId(0, int(a))
        tri_cell.GetPointIds().SetId(1, int(b))
        tri_cell.GetPointIds().SetId(2, int(c))
        cell_array.InsertNextCell(tri_cell)

    poly = vtk.vtkPolyData()
    poly.SetPoints(vtk_points)
    poly.SetPolys(cell_array)

    metadata = {
        "unit": model_unit,
        "build_item_count": build_items,
        "referenced_objectids": sorted(set(referenced_ids)),
        "object_count": len(objects),
        "mesh_merge_tolerance": CLEAN_TOL,
    }
    return poly, metadata, issues


def check_polydata(path: Path, poly: vtk.vtkPolyData) -> Tuple[dict, bool, List[str]]:
    issues: List[str] = []
    if poly is None:
        return {}, False, ["empty polydata"]

    # Merge near-coincident points to avoid exporter noise.
    cleaner = vtk.vtkCleanPolyData()
    cleaner.SetInputData(poly)
    cleaner.SetTolerance(CLEAN_TOL)
    cleaner.SetAbsoluteTolerance(CLEAN_TOL)
    cleaner.ToleranceIsAbsoluteOn()
    cleaner.Update()
    clean_poly = cleaner.GetOutput()

    if clean_poly.GetNumberOfPoints() == 0:
        return {}, False, ["empty polydata after clean"]
    if clean_poly.GetNumberOfCells() == 0:
        return {}, False, ["no cells after clean"]

    points = vtk_to_numpy(clean_poly.GetPoints().GetData())
    if points.ndim != 2 or points.shape[1] != 3:
        issues.append("invalid point array shape")
        return {}, False, issues
    if not np.isfinite(points).all():
        issues.append("non-finite coordinates in mesh")

    # Ensure triangle-only analysis for robust checks.
    tri_filter = vtk.vtkTriangleFilter()
    tri_filter.SetInputData(clean_poly)
    tri_filter.Update()
    tri_poly = tri_filter.GetOutput()
    tri_count = int(tri_poly.GetNumberOfCells())
    if tri_count <= 0:
        issues.append("no triangles")

    tri_ids_list: List[Tuple[int, int, int]] = []
    non_triangle_cells = 0
    cell = vtk.vtkIdList()
    for i in range(tri_count):
        tri_poly.GetCellPoints(i, cell)
        n_ids = cell.GetNumberOfIds()
        if n_ids != 3:
            non_triangle_cells += 1
            continue
        tri_ids_list.append((int(cell.GetId(0)), int(cell.GetId(1)), int(cell.GetId(2))))
    tri_ids = np.asarray(tri_ids_list, dtype=np.int64)
    if non_triangle_cells:
        issues.append(f"non-triangle cells: {non_triangle_cells}")

    if tri_ids.size:
        max_idx = int(tri_ids.max())
        min_idx = int(tri_ids.min())
        if min_idx < 0 or max_idx >= clean_poly.GetNumberOfPoints():
            issues.append("triangle index out of range")

    degenerate = 0
    orientation_ratio = 0.0
    signed_volume = 0.0
    if tri_ids.size:
        v0 = points[tri_ids[:, 0]]
        v1 = points[tri_ids[:, 1]]
        v2 = points[tri_ids[:, 2]]
        areas = 0.5 * np.linalg.norm(np.cross(v1 - v0, v2 - v0), axis=1)
        degenerate = int(np.count_nonzero(areas <= DEGENERATE_AREA_TOL))
        if degenerate:
            issues.append(f"degenerate triangles: {degenerate}")

        tri_signed = np.einsum("ij,ij->i", v0, np.cross(v1, v2))
        signed_volume = float(tri_signed.sum() / 6.0)
        if signed_volume <= DEGENERATE_AREA_TOL:
            issues.append(f"non-positive signed volume: {signed_volume}")

        edge_sign = {}
        bad_edge_pairs = 0
        edge_count = 0
        for (a, b, c) in tri_ids:
            for u, v in ((a, b), (b, c), (c, a)):
                key = (u, v) if u < v else (v, u)
                if key not in edge_sign:
                    edge_sign[key] = [0, 0]
                if u < v:
                    edge_sign[key][0] += 1
                else:
                    edge_sign[key][1] += 1
        edge_count = len(edge_sign)
        for plus_count, minus_count in edge_sign.values():
            if plus_count + minus_count != 2 or plus_count != 1 or minus_count != 1:
                bad_edge_pairs += 1
        if edge_count:
            orientation_ratio = 1.0 - (bad_edge_pairs / edge_count)
        else:
            orientation_ratio = 0.0
        if bad_edge_pairs:
            issues.append(f"edge orientation mismatch: {bad_edge_pairs}")
    else:
        signed_volume = 0.0
        orientation_ratio = 0.0

    # Boundary and non-manifold checks on cleaned mesh.
    def edge_count_by_type(boundary: bool = True, nonmanifold: bool = False) -> int:
        feat = vtk.vtkFeatureEdges()
        feat.SetInputData(clean_poly)
        if boundary:
            feat.BoundaryEdgesOn()
        else:
            feat.BoundaryEdgesOff()
        if nonmanifold:
            feat.NonManifoldEdgesOn()
        else:
            feat.NonManifoldEdgesOff()
        feat.FeatureEdgesOff()
        feat.ManifoldEdgesOff()
        feat.Update()
        return int(feat.GetOutput().GetNumberOfCells())

    boundary_edges = edge_count_by_type(boundary=True, nonmanifold=False)
    if boundary_edges > EDGE_COUNT_TOL:
        issues.append(f"mesh not closed (boundary edges: {boundary_edges})")

    nonmanifold_edges = edge_count_by_type(boundary=False, nonmanifold=True)
    if nonmanifold_edges > EDGE_COUNT_TOL:
        issues.append(f"non-manifold edges: {nonmanifold_edges}")

    conn = vtk.vtkConnectivityFilter()
    conn.SetInputData(clean_poly)
    conn.SetExtractionModeToAllRegions()
    conn.Update()
    regions = int(conn.GetNumberOfExtractedRegions())
    if regions != 1:
        issues.append(f"mesh has {regions} connected components")

    bounds = clean_poly.GetBounds()
    extent = [float(bounds[1] - bounds[0]), float(bounds[3] - bounds[2]), float(bounds[5] - bounds[4])]
    if extent[0] <= 0 or extent[1] <= 0 or extent[2] <= 0:
        issues.append("invalid extent")
    zmin = float(bounds[4])
    if abs(zmin - BED_Z) > BED_TOL:
        issues.append(f"zmin outside bed tolerance ±{BED_TOL}: {zmin}")
    if extent[0] > H2D_BED_XY + 1e-6 or extent[1] > H2D_BED_Y + 1e-6 or extent[2] > H2D_BED_Z + 1e-6:
        issues.append("exceeds H2D envelope")

    volume = abs(float(signed_volume))

    metrics = {
        "file": str(path),
        "points": int(clean_poly.GetNumberOfPoints()),
        "cells": int(clean_poly.GetNumberOfCells()),
        "triangles": tri_count,
        "triangle_validation": {
            "non_triangle_cells": int(non_triangle_cells),
            "degenerate_triangles": int(degenerate),
        },
        "bounds": [float(b) for b in bounds],
        "envelope_mm": extent,
        "zmin": zmin,
        "boundary_edges": boundary_edges,
        "nonmanifold_edges": nonmanifold_edges,
        "connected_components": regions,
        "volume_mm3": volume,
        "merge_point_count_before": int(poly.GetNumberOfPoints()),
        "merge_point_count_after": int(clean_poly.GetNumberOfPoints()),
        "normal_orientation_ratio": orientation_ratio,
        "h2d_fit": [
            float(extent[0] <= H2D_BED_XY + 1e-6),
            float(extent[1] <= H2D_BED_Y + 1e-6),
            float(extent[2] <= H2D_BED_Z + 1e-6),
        ],
    }
    return metrics, len(issues) == 0, issues


def check_stl_file(path: Path) -> Tuple[dict, bool, List[str]]:
    reader = vtk.vtkSTLReader()
    reader.SetFileName(str(path))
    reader.Update()
    poly = reader.GetOutput()
    if poly.GetNumberOfPoints() == 0 or poly.GetNumberOfCells() == 0:
        return {}, False, ["empty STL"]
    return check_polydata(path, poly)


def load_and_check_3mf(path: Path) -> Tuple[dict, bool, List[str]]:
    poly, meta, issues = parse_3mf(path)
    metrics, ok, poly_issues = check_polydata(path, poly)
    issues.extend(poly_issues)
    metrics["metadata"] = meta
    return metrics, ok and not issues, issues

def within_envelope(expected: List[float], observed: List[float]) -> bool:
    if not expected or not observed or len(expected) != 3 or len(observed) != 3:
        return False
    for e, o in zip(expected, observed):
        if abs(o - e) > ENVELOPE_TOL:
            return False
    return True


def within_volume(expected: float, observed: float) -> bool:
    if expected is None or expected <= 0 or observed is None or observed <= 0:
        return False
    return abs(observed - expected) <= max(abs(expected) * VOLUME_DIFF_REL_TOL, 1e-9)


def envelope_diffs(expected: List[float], observed: List[float]) -> Tuple[List[float], float]:
    if not expected or not observed or len(expected) != 3 or len(observed) != 3:
        return [], 0.0
    diffs = [float(o - e) for o, e in zip(observed, expected)]
    max_abs = max((abs(d) for d in diffs), default=0.0)
    return diffs, float(max_abs)


def evaluate_model(model_key: str, manifest_models: dict, base_dir: Path) -> ModelResult:
    stl_path = base_dir / "stl" / f"{model_key}.stl"
    mf3_path = base_dir / "3mf" / f"{model_key}.3mf"
    manifest_block = manifest_models.get(model_key, {})

    res = ModelResult(
        model_key=model_key,
        manifest_found=bool(manifest_block),
        manifest_file_block=manifest_block or None,
    )

    if not manifest_block:
        res.issues.append("manifest entry missing")

    if stl_path.exists():
        expected_sha = manifest_block.get("files", {}).get("stl", {}).get("sha256") if manifest_block else None
        stl = FileCheck(str(stl_path), exists=True, bytes=stl_path.stat().st_size)
        stl.sha256 = sha256_file(stl_path)
        if expected_sha:
            stl.sha256_match = (stl.sha256.lower() == expected_sha.lower())
            if not stl.sha256_match:
                stl.issues.append("sha256 mismatch")
        try:
            m, ok, issues = check_stl_file(stl_path)
            stl.metrics = m
            stl.passed = ok
            stl.issues.extend(issues)
        except Exception as e:
            stl.passed = False
            stl.issues.append(f"stl check failed: {e}")
        res.stl = stl
    else:
        res.stl = FileCheck(str(stl_path), exists=False, passed=False, issues=["missing stl file"])
        res.issues.extend(res.stl.issues)

    if mf3_path.exists():
        expected_sha = manifest_block.get("files", {}).get("3mf", {}).get("sha256") if manifest_block else None
        mf3 = FileCheck(str(mf3_path), exists=True, bytes=mf3_path.stat().st_size)
        mf3.sha256 = sha256_file(mf3_path)
        if expected_sha:
            mf3.sha256_match = (mf3.sha256.lower() == expected_sha.lower())
            if not mf3.sha256_match:
                mf3.issues.append("sha256 mismatch")
        try:
            m, ok, issues = load_and_check_3mf(mf3_path)
            mf3.metrics = m
            mf3.passed = ok
            mf3.issues.extend(issues)
        except Exception as e:
            mf3.passed = False
            mf3.issues.append(f"3mf check failed: {e}")
        res.mf3 = mf3
    else:
        res.mf3 = FileCheck(str(mf3_path), exists=False, passed=False, issues=["missing 3mf file"])
        res.issues.extend(res.mf3.issues)

    compare = {}
    cad = manifest_block.get("cad", {}) if manifest_block else {}
    expected_env = cad.get("envelope_mm")
    expected_vol = cad.get("volume_mm3")
    if expected_env is not None and res.stl.metrics:
        stl_env = res.stl.metrics.get("envelope_mm", [])
        stl_env_diffs, stl_env_abs = envelope_diffs(expected_env, stl_env)
        compare["stl_envelope_match"] = within_envelope(expected_env, stl_env)
        compare["stl_envelope_diffs_mm"] = stl_env_diffs
        compare["stl_envelope_abs_max_diff_mm"] = stl_env_abs
    if expected_env is not None and res.mf3.metrics:
        mf3_env = res.mf3.metrics.get("envelope_mm", [])
        mf3_env_diffs, mf3_env_abs = envelope_diffs(expected_env, mf3_env)
        compare["mf3_envelope_match"] = within_envelope(expected_env, mf3_env)
        compare["mf3_envelope_diffs_mm"] = mf3_env_diffs
        compare["mf3_envelope_abs_max_diff_mm"] = mf3_env_abs
    if expected_vol is not None and res.stl.metrics:
        stl_vol = float(res.stl.metrics.get("volume_mm3", 0.0))
        compare["stl_volume_match"] = within_volume(expected_vol, stl_vol)
        compare["stl_volume_abs_diff_mm3"] = abs(stl_vol - expected_vol)
        compare["stl_volume_rel_diff"] = abs(stl_vol - expected_vol) / float(expected_vol) if expected_vol else 0.0
    if expected_vol is not None and res.mf3.metrics:
        mf3_vol = float(res.mf3.metrics.get("volume_mm3", 0.0))
        compare["mf3_volume_match"] = within_volume(expected_vol, mf3_vol)
        compare["mf3_volume_abs_diff_mm3"] = abs(mf3_vol - expected_vol)
        compare["mf3_volume_rel_diff"] = abs(mf3_vol - expected_vol) / float(expected_vol) if expected_vol else 0.0
    compare["cad_envelope_mm"] = expected_env
    compare["cad_volume_mm3"] = expected_vol
    if res.stl.metrics and res.mf3.metrics:
        s_vol = float(res.stl.metrics.get("volume_mm3", 0.0))
        m_vol = float(res.mf3.metrics.get("volume_mm3", 0.0))
        compare["stl_mf3_volume_abs_diff"] = abs(s_vol - m_vol)
        compare["stl_mf3_volume_rel_diff"] = abs(s_vol - m_vol) / max(1.0, expected_vol) if expected_vol else abs(s_vol - m_vol)
    res.compare = compare

    if expected_env is not None and not compare.get("stl_envelope_match", True):
        res.issues.append("stl envelope mismatch")
    if expected_env is not None and not compare.get("mf3_envelope_match", True):
        res.issues.append("3mf envelope mismatch")
    if expected_vol is not None and not compare.get("stl_volume_match", True):
        res.issues.append("stl volume mismatch")
    if expected_vol is not None and not compare.get("mf3_volume_match", True):
        res.issues.append("3mf volume mismatch")

    file_pass = res.stl.passed and res.mf3.passed and not res.stl.issues and not res.mf3.issues
    res.passed = file_pass and not res.issues
    return res


def find_manifest_models(manifest_path: Path) -> dict:
    with manifest_path.open("r", encoding="utf-8") as f:
        return json.load(f)


def run(input_dir: Path, manifest_path: Path, output_path: Path) -> int:
    manifest = find_manifest_models(manifest_path)
    manifest_models = manifest.get("models", {})
    coupons = manifest.get("coupon", [])
    stl_dir = input_dir / "stl"
    mf3_dir = input_dir / "3mf"
    stl_keys = {p.stem for p in stl_dir.glob("*.stl")}
    mf3_keys = {p.stem for p in mf3_dir.glob("*.3mf")}
    all_keys = sorted(set(stl_keys) | set(mf3_keys) | set(manifest_models))
    all_model_keys = sorted(manifest_models.keys())
    hard_failures: List[str] = []

    if len(stl_keys) != EXPECTED_STL_COUNT or len(mf3_keys) != EXPECTED_3MF_COUNT:
        hard_failures.append(
            f"output file count mismatch, expected STLs={EXPECTED_STL_COUNT}, 3MFs={EXPECTED_3MF_COUNT}, got STLs={len(stl_keys)}, 3MFs={len(mf3_keys)}"
        )
    if len(manifest_models) != EXPECTED_MODEL_COUNT:
        hard_failures.append(f"manifest model count mismatch, expected {EXPECTED_MODEL_COUNT}, got {len(manifest_models)}")
    missing_stl = [k for k in all_model_keys if k not in stl_keys]
    missing_mf3 = [k for k in all_model_keys if k not in mf3_keys]
    unexpected = [k for k in all_keys if k not in manifest_models]
    if missing_stl:
        hard_failures.append(f"manifest model(s) missing STL file: {', '.join(missing_stl)}")
    if missing_mf3:
        hard_failures.append(f"manifest model(s) missing 3MF file: {', '.join(missing_mf3)}")
    if unexpected:
        hard_failures.append(f"output model(s) not in manifest: {', '.join(unexpected)}")
    if not all_model_keys:
        hard_failures.append("manifest has no models section")
    if len(coupons) != 5:
        hard_failures.append(f"manifest coupon count mismatch, expected 5, got {len(coupons)}")

    model_results: List[ModelResult] = []
    model_failures: List[str] = []
    for key in all_keys:
        result = evaluate_model(key, manifest_models, input_dir)
        if not result.passed:
            model_failures.append(key)
        model_results.append(result)

    all_failures = list(dict.fromkeys(model_failures + hard_failures))
    output = {
        "input_dir": str(input_dir),
        "manifest": str(manifest_path),
        "manifest_sha256": hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
        "pass": len(all_failures) == 0,
        "missing_stl": missing_stl,
        "missing_mf3": missing_mf3,
        "unexpected_models": unexpected,
        "hard_failures": hard_failures,
        "models": [asdict(r) for r in model_results],
        "limitations": [
            "No self-intersection sweep was executed; edge-type checks only.",
            "No slicing/simulator pass, no thermal or flow validation.",
            "No physical printer or material process validation.",
        ],
        "run_summary": {
            "total_files": len(stl_keys) + len(mf3_keys),
            "stl_count": len(stl_keys),
            "mf3_count": len(mf3_keys),
            "manifest_model_count": len(all_model_keys),
            "manifest_coupon_count": len(coupons),
            "merge_tolerance": CLEAN_TOL,
            "expected_model_count": EXPECTED_MODEL_COUNT,
            "expected_stl_count": EXPECTED_STL_COUNT,
            "expected_3mf_count": EXPECTED_3MF_COUNT,
        },
        "failures": all_failures,
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as f:
        json.dump(output, f, ensure_ascii=False, indent=2)

    if all_failures:
        print("FAIL: mesh validation detected errors")
        for item in all_failures:
            print(f" - {item}")
        return 1
    print(f"PASS: wrote {output_path}")
    return 0


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check 3MF/STL mesh exports for H2D PETG validation kit.")
    parser.add_argument(
        "--input-dir",
        default=str(PRINT_DIR_SUFFIX),
        help="Directory containing stl/, 3mf/, geometry_manifest.json",
    )
    parser.add_argument(
        "--manifest",
        default=str(DEFAULT_MANIFEST),
        help="Path to geometry_manifest.json",
    )
    parser.add_argument(
        "--output",
        default=str(DEFAULT_OUTPUT),
        help="Output report JSON path",
    )
    return parser.parse_args(argv)


def disable_vtk_warnings() -> None:
    try:
        vtkObject.GlobalWarningDisplayOff()
    except Exception:
        pass


def main(argv=None) -> int:
    disable_vtk_warnings()
    args = parse_args(argv)
    input_dir = Path(args.input_dir)
    manifest_path = Path(args.manifest)
    output_path = Path(args.output)

    if not input_dir.exists():
        print(f"input dir missing: {input_dir}")
        return 1
    if not manifest_path.exists():
        print(f"manifest missing: {manifest_path}")
        return 1
    return run(input_dir, manifest_path, output_path)


if __name__ == "__main__":
    sys.exit(main())
