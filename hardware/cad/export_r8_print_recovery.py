"""Local R8 print recovery from the user's failed physical samples.

Uses the frozen single-part STEP files.  Mounting geometry remains in its
original assembly frame; print placement is applied only after the changes.
No slicer configuration, G-code or printer connection is produced.
"""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import sys

import cadquery as cq
import numpy as np
from OCP.BRepBndLib import BRepBndLib
from OCP.Bnd import Bnd_Box

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'output/print/GL30_R8_REVIEW_SAMPLES'
WORK = ROOT / 'outputs/r8-print-recovery-20260913'
STAGE = WORK / 'staged_models'
PARTS = {
    '07_motor_slider': 'motor_tension_slide_plate',
    '08_guide_support': 'fixed_rail_spine_and_floor_foot',
    '31_carriage_adapter': 'carriage_to_plate_L_adapter',
    '14_grip': 'rotating_ring',
}
AXES = {'X': (1, 0, 0), 'Y': (0, 1, 0), 'Z': (0, 0, 1)}


def move_from_print_to_world(shape, operations):
    for operation in reversed(operations):
        if 'translate_mm' in operation:
            shape = shape.translate(tuple(-v for v in operation['translate_mm']))
        else:
            shape = shape.rotate((0, 0, 0), AXES[operation['axis']], -operation['rotate_deg'])
    return shape


def placed(shape):
    x0, y0, z0, x1, y1, z1 = exact_bounds(shape)
    return shape.translate((-(x0 + x1) / 2, -(y0 + y1) / 2, -z0))


def exact_bounds(shape):
    # Cached STL tessellation can inflate CadQuery's default bounding box.
    bounds = Bnd_Box()
    BRepBndLib.AddOptimal_s(shape.wrapped, bounds, False, False)
    return bounds.Get()


def flat_bed_area(shape):
    return sum(face.Area() for face in shape.Faces()
               if face.geomType() == 'PLANE'
               and abs(face.BoundingBox().zmin) < 1e-5
               and abs(face.BoundingBox().zmax) < 1e-5
               and face.normalAt().z < -.99)


def description(shape):
    x0, y0, z0, x1, y1, z1 = exact_bounds(shape)
    return {'valid': bool(shape.isValid()), 'solids': len(shape.Solids()),
            'volume_mm3': shape.Volume(), 'zmin_mm': z0,
            'extents_mm': [x1-x0, y1-y0, z1-z0],
            'flat_bed_area_mm2': flat_bed_area(shape)}


def fill_guide_back(original):
    # The long back is 0.5 mm above the foot's outer X face in the delivered
    # print orientation. Extend existing material only, retaining every wire
    # of the face so holes and the side-key relief are not filled.
    additions = []
    for face in original.Faces():
        b = face.BoundingBox()
        if (face.geomType() == 'PLANE' and b.zlen < 1e-6
                and abs(b.zmin - .5) < 1e-6 and face.normalAt().z < -.99):
            additions.append(cq.Solid.extrudeLinear(face.outerWire(), face.innerWires(),
                                                   cq.Vector(0, 0, -.5)))
    assert len(additions) == 4, 'R8 guide back faces changed; re-inspect before exporting'
    # The outer housing corner is close to this edge. Leave the final
    # 0.6 mm strip at the old thickness; the rest forms the common bed face.
    ymax = exact_bounds(original)[4]
    edge_relief = cq.Workplane('XY').box(200, 2, 2, centered=(False, False, False))
    edge_relief = edge_relief.translate((-100, ymax - .6, -1)).val()
    additions = [addition.cut(edge_relief) for addition in additions]
    result = original.fuse(*additions).clean()
    return result, {'back_fill_mm': .5, 'back_face_count': len(additions),
                    'unfilled_housing_corner_edge_strip_mm': .6}


def strengthen_adapter_back(original):
    # In the frozen printed frame, original local X=15..17 maps to -4..-2.
    # Existing side arms reach X=-6. Fill to that existing envelope, then
    # retain the original screw-head seating plane at X=-4 with counterbores.
    # Stop short of the original foot's contact plane. Only the new pad is
    # relieved; the frozen screw seats, feet and actuator are left intact.
    pad = cq.Workplane('XY').box(2, 17, 25.6, centered=(False, False, False))
    pad = pad.translate((-6, -5.3, .4)).val()
    holes = [(y, z) for y in (-2.8, 9.2) for z in (8.75, 16.75)]
    for y, z in holes:
        void = cq.Solid.makeCylinder(2.1, 2.2, cq.Vector(-6.1, y, z), cq.Vector(1, 0, 0))
        pad = pad.cut(void)
    # Fixed R8 connector envelope and rear-switch wire route, in world mm.
    # The adapter moves -0.35 mm along its local Z while pressing, so retain
    # both endpoint keepouts. Expand each wire segment's box by 0.85 mm
    # for the 0.4 mm radius wire. Planar reliefs avoid tiny spherical patches
    # at the intersecting screw recesses and are straightforward to print.
    angle = math.radians(26)
    center_z = 30 + 40 * math.tan(angle)

    def world_to_print(shape):
        return (shape.translate((-30, 10, -center_z))
                .rotate((0, 0, 0), AXES['X'], -26)
                .translate((0, -30.8, 39.5)))

    connector = cq.Workplane('XY').box(8.8, 6.8, 6.8, centered=(False, False, False))
    connector = world_to_print(connector.translate((16.6, 24.8, 44.8)).val())
    route = [(20, 42.15, 53), (20, 39.5, 53), (24.1, 39.5, 50),
             (24.1, 36, 50), (24.1, 36, 35), (24.1, 36, 32.8),
             (22.4, 35.3, 32.4)]
    wire_voids = []
    for p0, p1 in zip(route, route[1:]):
        lower = tuple(min(a, b) - .85 for a, b in zip(p0, p1))
        size = tuple(abs(a - b) + 1.7 for a, b in zip(p0, p1))
        wire_voids.append(cq.Workplane('XY').box(*size, centered=(False, False, False))
                          .translate(lower).val())
    for stroke in (0, .35):
        pad = pad.cut(connector.translate((0, 0, stroke)))
        for void in wire_voids:
            pad = pad.cut(world_to_print(void).translate((0, 0, stroke)))
    result = original.fuse(pad).clean()
    added = result.cut(original)
    probes = []
    for y, z in holes:
        shaft = cq.Solid.makeCylinder(1.0, 4, cq.Vector(-4, y, z), cq.Vector(1, 0, 0))
        head = cq.Solid.makeCylinder(1.8, 1.2, cq.Vector(-5.2, y, z), cq.Vector(1, 0, 0))
        access = cq.Solid.makeCylinder(1.95, 2, cq.Vector(-6, y, z), cq.Vector(1, 0, 0))
        overlap = result.intersect(shaft.fuse(head)).Volume()
        added_at_access = added.intersect(access).Volume()
        seat = cq.Solid.makeCylinder(1.9, .05, cq.Vector(-4, y, z), cq.Vector(1, 0, 0))
        unchanged_seat = original.intersect(seat).cut(result.intersect(seat)).Volume()
        assert overlap < 1e-6 and added_at_access < 1e-6 and unchanged_seat < 1e-6
        probes.append({'y_mm': y, 'z_mm': z, 'screw_body_overlap_mm3': overlap,
                       'added_material_at_head_access_mm3': added_at_access,
                       'removed_seating_material_mm3': unchanged_seat})
    return result, {'back_fill_mm': 2, 'counterbore_diameter_mm': 4.2,
                    'counterbore_depth_mm': 2, 'original_through_hole_mm': 2.4,
                    'M2x4_under_head_length_mm': 4, 'original_plate_thickness_mm': 2,
                    'nominal_block_engagement_mm': 2,
                    'backing_offset_from_original_foot_mm': .4,
                    'NTC_connector_keepout_margin_mm': .4,
                    'rear_wire_segment_box_padding_mm': .85,
                    'keepout_press_endpoints_mm': [0, .35],
                    'screw_head_seat_x_mm_in_original_print_frame': -4,
                    'screw_and_access_probes': probes}


def mesh_readback(path):
    raw = path.read_bytes()
    count = int.from_bytes(raw[80:84], 'little')
    dtype = np.dtype([('normal', '<f4', (3,)), ('tri', '<f4', (3, 3)), ('attr', '<u2')])
    assert len(raw) == 84 + count * 50
    triangles = np.frombuffer(raw, dtype=dtype, offset=84)['tri'].astype(float)
    assert np.isfinite(triangles).all()
    vertices, index = np.unique(np.round(triangles.reshape(-1, 3), 5), axis=0, return_inverse=True)
    faces = index.reshape(-1, 3)
    edges = np.concatenate((faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]))
    undirected = np.sort(edges, axis=1)
    _, counts = np.unique(undirected, axis=0, return_counts=True)
    volume = np.einsum('ij,ij->i', triangles[:, 0],
                       np.cross(triangles[:, 1], triangles[:, 2])).sum() / 6
    assert np.all(counts == 2), 'Open or non-manifold mesh edge'
    assert volume > 0
    return {'faces': count, 'vertices': len(vertices),
            'closed_edges': bool(np.all(counts == 2)), 'signed_volume_mm3': float(volume),
            'zmin_mm': float(vertices[:, 2].min()),
            'sha256': hashlib.sha256(raw).hexdigest()}


def build():
    WORK.mkdir(parents=True, exist_ok=True)
    STAGE.mkdir(exist_ok=True)
    manifest = json.loads((SOURCE / 'geometry_manifest.json').read_text(encoding='utf-8'))
    report = {'scope': 'Unpowered assembly print samples after photo feedback',
              'sliced': False, 'gcode_generated': False, 'physical_success_verified': False,
              'parts': {}, 'world_step_replacements': {}}
    for key, name in PARTS.items():
        original_path = SOURCE / 'A/step' / (key + '.step')
        original = cq.importers.importStep(str(original_path)).val()
        changes = {}
        if key == '08_guide_support':
            modified, changes = fill_guide_back(original)
        elif key == '31_carriage_adapter':
            modified, changes = strengthen_adapter_back(original)
        else:
            modified = original
        removed_volume = original.cut(modified).Volume()
        added_volume = modified.cut(original).Volume()
        assert removed_volume < 1e-5
        assert modified.isValid() and len(modified.Solids()) == 1
        transform = manifest['models'][key]['transform_sequence']
        world = move_from_print_to_world(modified, transform)
        world_path = STAGE / (key + '_assembly_position.step')
        cq.exporters.export(world, str(world_path))
        report['world_step_replacements'][name] = str(world_path)
        printing = modified
        if key == '07_motor_slider':
            printing = printing.rotate((0, 0, 0), AXES['X'], 180)
            changes['print_rotation_about_X_deg'] = 180
        elif key == '31_carriage_adapter':
            printing = printing.rotate((0, 0, 0), AXES['Y'], -90)
            changes['print_rotation_about_Y_deg'] = -90
        printing = placed(printing)
        assert printing.isValid() and len(printing.Solids()) == 1
        step = STAGE / (key + '.step')
        stl = STAGE / (key + '.stl')
        cq.exporters.export(printing, str(step))
        cq.exporters.export(printing, str(stl), tolerance=.025, angularTolerance=.08)
        reloaded = cq.importers.importStep(str(step)).val()
        assert reloaded.isValid() and len(reloaded.Solids()) == 1
        assert abs(reloaded.Volume() - printing.Volume()) < 1e-3
        mesh = mesh_readback(stl)
        assert abs(mesh['zmin_mm']) < 1e-4
        assert abs(mesh['signed_volume_mm3'] - printing.Volume()) / printing.Volume() < .005
        entry = {'source_step_sha256': hashlib.sha256(original_path.read_bytes()).hexdigest(),
                 'old_material_removed_mm3': removed_volume, 'added_volume_mm3': added_volume,
                 'original': description(original), 'modified_print': description(printing),
                 'changes': changes, 'mesh': mesh}
        report['parts'][key] = entry
        print(key, json.dumps(entry, ensure_ascii=False), flush=True)
    (WORK / 'geometry-verification.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    try:
        build()
        code = 0
    except Exception:
        import traceback
        traceback.print_exc()
        code = 1
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(code)
