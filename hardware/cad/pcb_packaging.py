"""Current 1.2 mm PCB mechanical design. No routed PCB or load approval implied.

All board coordinates are local XY, with the PCB bottom at Z=0. A/B are
mounted horizontally; C follows the inclined deck. The old V7 code is used
for its vendor transforms and existing sculpted knob, not its old enclosure.
"""
from __future__ import annotations

import argparse
from dataclasses import replace
import hashlib
import json
from math import cos, sin, radians, sqrt, tan
import os
from pathlib import Path
import sys

import cadquery as cq
from v7_params import DEFAULTS
from v7_product_model import (
    _annulus, _bbox, _deck_center, _export_step, _spider,
    _import_vendor_parts, _intersection_volume, _normal, _orient_at_knob,
    _render, _shape, _side_capsule, _side_button, _rear_power_button,
)

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = ROOT / 'output/models/GL30_PCB_1P2'
P = replace(DEFAULTS, width_mm=118., depth_mm=114., rear_height_mm=58.,
            knob_center_from_front_mm=42., rear_usb_c_center_z_mm=34.8,
            power_button_x_mm=30., power_button_z_mm=49.)
T = 1.2
PARAMETER_KEYS = ('width_mm','depth_mm','front_height_mm','rear_height_mm',
                  'deck_angle_deg','housing_wall_mm','knob_center_from_front_mm',
                  'knob_outer_diameter_mm','knob_inner_diameter_mm',
                  'motor_output_face_normal_mm','display_origin_normal_mm',
                  'rear_usb_c_center_z_mm','power_button_x_mm','power_button_z_mm')
BATTERY = {
    'model': 'LPHD5919096 3S 1100mAh', 'nominal_v': 11.1, 'capacity_mah': 1100,
    'max_pack_mm': [101., 20., 18.], 'pack_center_xy_mm': [0., 32.],
    'pack_bottom_z_mm': 5., 'cavity_mm': [107., 26., 24.],
    'cavity_bottom_z_mm': 4.5,
    'pcm': True, 'ntc_in_pack': False, 'cell_balancing': 'not documented',
    'max_charge_a': .5, 'max_discharge_a': 3., 'charge_temp_c': [10, 45],
    'source': 'https://www.lipobattery.us/wp-content/uploads/2022/06/LPHD5919096-3S-11.1V-1100mAh.pdf',
    'status': 'mechanical design reference; confirm sample, individual-cell protection, balancing and charging interface before procurement release',
}
HOLES_AB = [[x, y, 2.7] for x in (-44.,44.) for y in (-13.,13.)]
HOLES_C = [[34*cos(radians(a)),34*sin(radians(a)),2.2] for a in (7.5,127.5,247.5)]
BOARDS = {
    'A': {'name': 'Motor and control', 'kind': 'rounded_rectangle',
          'width_mm':98., 'height_mm':34., 'corner_r_mm':3., 'thickness_mm':T,
          'layers':4, 'holes':HOLES_AB, 'world_origin_mm':[0.,36.,45.2]},
    'B': {'name': 'Power and interface', 'kind':'rounded_rectangle',
          'width_mm':98., 'height_mm':34., 'corner_r_mm':3., 'thickness_mm':T,
          'layers':4, 'holes':HOLES_AB, 'world_origin_mm':[0.,36.,32.]},
    'C': {'name':'Fixed RGB and press', 'kind':'annulus_with_ear',
          'outer_d_mm':74., 'inner_d_mm':56., 'ear_width_mm':24.,
          'ear_top_y_mm':47., 'ear_overlap_y_mm':32.,
          'width_mm':74., 'height_mm':84., 'thickness_mm':T, 'layers':2,
          'holes':HOLES_C, 'deck_normal_bottom_mm':-5.2, 'deck_rotation_deg':-90.},
}

# Bounding boxes are intentional layout reservations, not land patterns.
# name, local center X/Y, width/depth/height above PCB; all mm.
COMPONENTS = {
 'A': [
  ('STM32G474_region',-15,-1,10,10,2), ('DRV8316R_region',15,-6,10,8,2),
  ('TLV1704_1_region',-30,-8,8,5,2), ('TLV1704_2_region',-30,6,8,5,2),
  ('HSE_region',-14,-11,4,3,2), ('Phase_connector_reserve',32,-6,15,8,6),
  ('Motor_bulk_caps_reserve',15,7,10,9,6.5), ('Power_connector_reserve',35,8,12,6,6),
  ('SWD_header_reserve',-9,11,14,5,4), ('Display_link_reserve',3,0,12,5,4),
  ('RGB_press_link_reserve',-37,0,12,5,4),
 ],
 'B': [
  ('USB_C_connector_reserve',0,17.5,9,9,3.2), ('HUSB238_region',0,7,6,6,1.8),
  ('USB_ESD_region',10,13,5,5,1.5), ('BQ25798_region',-13,5,8,8,1.5),
  ('Charger_inductor_reserve',-22,6,6,6,3.2), ('TPS2663_region',16,6,8,8,1.5),
  ('Logic_buck_zone',-32,-6,12,15,6.5), ('LED_buck_zone',32,-6,12,15,6.5),
  ('LM74800_region',12,-6,7,7,1.5),
  ('MOS_1_region',4,-13,6,5,1.2), ('MOS_2_region',14,-13,6,5,1.2),
  ('MOS_3_region',23,-13,6,5,1.2), ('Shunt_region',-5,-12,8,5,1.5),
  ('INA228_region',-15,-11,6,6,1.5), ('Fuse_region',-32,12,7,4,3),
  ('TVS_region',-22,14,6,4,2.5), ('Brake_control_zone',26,11,11,7,2),
  ('Logic_link_reserve',0,-1,12,5,4.5), ('Battery_link_reserve',44,1,10,8,6.5),
  ('Brake_test_link_reserve',-43,1,8,8,6),
 ],
 'C': [
  ('RGB_buffer_region',-6,37,5,3,1.5), ('Press_switch_reserve',0,38,4.5,3.4,1.5),
  ('Cable_connector_reserve',7,42,8,6,4.5), ('Ambient_sensor_reserve',-6,44,6,4,1.4),
 ]
}
COLORS = {'housing':(.74,.76,.77,1), 'PCB_A':(.05,.40,.23,1),
          'PCB_B':(.05,.26,.46,1), 'PCB_C':(.13,.14,.15,1),
          'battery':(.25,.47,.76,1), 'diffuser':(.45,.83,.9,1),
          'tray':(.25,.27,.29,1), 'bridge':(.34,.36,.39,1)}

def box(w,d,h,x=0,y=0,z=0):
    return cq.Workplane('XY').box(w,d,h,centered=(True,True,False)).translate((x,y,z))

def board_outline(k, thickness=T, holes=True):
    b=BOARDS[k]
    if k in 'AB':
        part=box(b['width_mm'],b['height_mm'],thickness).edges('|Z').fillet(b['corner_r_mm'])
    else:
        part=_annulus(b['outer_d_mm'],b['inner_d_mm'],thickness)
        part=part.union(box(b['ear_width_mm'],15,thickness,0,39.5))
    if holes:
        for x,y,d in b['holes']:
            part=part.cut(cq.Workplane('XY').center(x,y).circle(d/2).extrude(thickness))
    return part

def mounted(k,shape):
    if k=='C':
        return c_orient(shape.translate((0,0,BOARDS[k]['deck_normal_bottom_mm'])))
    return shape.translate(tuple(BOARDS[k]['world_origin_mm']))

def c_orient(shape):
    # Put the connector/sensor ear to the right, avoiding a raised rear hump.
    return _orient_at_knob(shape.rotate((0,0,0),(0,0,1),-90),P)

def cavity():
    w=P.housing_wall_mm
    front=-P.depth_mm/2+w
    inner_z=P.front_height_mm+w*tan(P.deck_angle_rad)-w/cos(P.deck_angle_rad)
    return cq.Workplane('YZ').polyline([
        (front,-1),(P.depth_mm/2-w,-1),(P.depth_mm/2-w,P.rear_height_mm-w),
        (P.deck_break_y_mm-w,P.rear_height_mm-w),(front,inner_z),
    ]).close().extrude(P.width_mm/2-w,both=True)

def housing():
    outer=cq.Workplane('YZ').polyline([
        (-57,0),(57,0),(57,P.rear_height_mm),(P.deck_break_y_mm,P.rear_height_mm),(-57,20)
    ]).close().extrude(59,both=True).edges('|X').fillet(2.)
    h=outer.cut(cavity())
    # Entire new ring recess, including the service ear, opens into the body.
    opening=board_outline('C',20,False).union(_annulus(75.,0.,20)).translate((0,0,-10))
    h=h.cut(c_orient(opening))
    # Recessed PCB support shelf and uninterrupted outer light-mixing wall.
    shelf=board_outline('C',1.8,False).translate((0,0,-7.))
    wall=_annulus(78.,74.8,8.2).translate((0,0,-7.))
    shelf=shelf.union(wall)
    # Ear support and light-tight cap are attached to the stationary base.
    ear=box(26.4,17.4,8.2,0,39.5,-7).cut(box(24.4,15.4,10,0,39.5,-5.2))
    shelf=shelf.union(ear)
    # Merge the light cavity and service ear so neither wall clips the PCB.
    light_cavity=cq.Workplane('XY').circle(37.4).extrude(10).translate((0,0,-5.2))
    light_cavity=light_cavity.union(box(24.4,15.4,10,0,39.5,-5.2))
    shelf=shelf.cut(light_cavity)
    for x,y,d in HOLES_C:
        shelf=shelf.cut(cq.Workplane('XY').center(x,y).circle(.8).extrude(5).translate((0,0,-8)))
    h=h.union(c_orient(shelf))
    h=h.cut(box(10.4,8,4.6,0,56,32.5))
    # Existing recessed side button language, repositioned with the wider body.
    for side in (-1,1):
        for y in (P.side_button_front_y_mm,P.side_button_rear_y_mm):
            h=h.cut(_side_capsule(P,side=side,y_mm=y,
                length=P.side_button_length_mm+2*P.side_button_clearance_mm,
                height=P.side_button_height_mm+2*P.side_button_clearance_mm,
                depth=3.2,x_base=55.9))
    power=cq.Workplane('XY').circle(P.power_button_recess_diameter_mm/2).extrude(4).rotate((0,0,0),(1,0,0),-90).translate((30,54,49))
    h=h.cut(power)
    # Cover bosses are outside the motor, battery and PCB mounting pattern.
    for x,y in ((-47,-39),(47,-39),(-49,10),(49,10)):
        boss=cq.Workplane('XY').center(x,y).circle(4).extrude(8).translate((0,0,3))
        link=box(59-abs(x),6,8,(x+(59 if x>0 else -59))/2,y,3)
        h=h.union(boss).union(link)
        h=h.cut(cq.Workplane('XY').center(x,y).circle(1.3).extrude(13))
    # Four integral side-wall ledges fasten the removable electronics bridge.
    # Front ledges are ahead of the battery, rear ledges behind it.
    for x in (-53.5,53.5):
        for y in (15.,50.):
            h=h.union(box(5.4,8,3,x,y,26))
            h=h.cut(cq.Workplane('XY').center(x,y).circle(.8).extrude(5).translate((0,0,26)))
    return h

def mounting_parts():
    # PCB shelf supported from side walls, never by posts through the battery.
    bridge=box(111.6,34,1,0,36,29).cut(box(82,22,3,0,36,28))
    for x in (-53.5,53.5):
        bridge=bridge.union(box(4.6,12,1,x,17,29))
        for y in (15.,50.):
            bridge=bridge.cut(cq.Workplane('XY').center(x,y).circle(1.1).extrude(3).translate((0,0,28)))
    for x,y,d in HOLES_AB:
        bridge=bridge.union(cq.Workplane('XY').center(x,y+36).circle(3.1).extrude(2).translate((0,0,30)))
        bridge=bridge.cut(cq.Workplane('XY').center(x,y+36).circle(1.35).extrude(5).translate((0,0,28)))
    posts=[]
    for x,y,d in HOLES_AB:
        posts.append(_annulus(5.6,2.7,12.).translate((x,y+36,33.2)))
    # Open tray: bottom cushioning plus side clearance, no pressure on cell faces.
    tray=box(110,29,23.5,0,32,3).cut(box(107,26,25,0,32,4.5))
    # Wires leave the documented end of the pack, then bend into the rear channel.
    tray=tray.cut(box(8,24,20,53,37,6))
    cover=box(111.4,107.4,3)
    for x,y in ((-47,-39),(47,-39),(-49,10),(49,10)):
        cover=cover.cut(cq.Workplane('XY').center(x,y).circle(1.7).extrude(4))
    return bridge,posts,tray,cover

def layout_core():
    """Smooth grip envelope with the original waist; texture omitted for fit CAD."""
    parts={
        'bearing_envelope':_annulus(52,40,7).translate((0,0,.8)),
        'ring_sleeve':_annulus(54,52.2,7).translate((0,0,.8)),
        'fixed_bezel':_annulus(39,33.8,.5).translate((0,0,12.2)),
        'display_glass':cq.Workplane('XY').circle(19.5).extrude(.5).translate((0,0,12.7)),
        'support_tube':_annulus(5,3.2,30.2).translate((0,0,-30)),
        'torque_carrier':_spider(hub_outer=22,hub_inner=6.4,rim_outer=52,rim_inner=48,beam_width=4,thickness=1.3).translate((0,0,-1.3)),
        'fixed_spider':_spider(hub_outer=8,hub_inner=5.2,rim_outer=44,rim_inner=40,beam_width=3,thickness=.6).translate((0,0,.2)),
    }
    parts['ring_cap']=(cq.Workplane('XZ').moveTo(20,7.8).lineTo(27,7.8)
        .bezier([(27,8.9),(25,9.1),(25,10.4)],includeCurrent=True)
        .bezier([(25,11.2),(25.5,11.2),(25.5,12)],includeCurrent=True)
        .bezier([(25.5,12.7),(25.2,13.2),(24.9,13.2)],includeCurrent=True)
        .lineTo(21.5,13.2).lineTo(21.2,13).lineTo(20.2,13).lineTo(20,12.8)
        .close().revolve(360,(0,0),(0,1)))
    parts={n:_orient_at_knob(s,P) for n,s in parts.items()}
    for side_name,side in (('left',-1),('right',1)):
        for i,y in enumerate((P.side_button_front_y_mm,P.side_button_rear_y_mm),1):
            parts[f'button_{side_name}_{i}']=_side_button(P,side=side,y_mm=y)
    parts['power_button']=_rear_power_button(P)
    for i,(x,y) in enumerate(((-51,-47),(51,-47),(-51,47),(51,47)),1):
        parts[f'foot_{i}']=cq.Workplane('XY').circle(4).extrude(1.2).translate((x,y,-1.2))
    return parts

def export_board(k,solid):
    target=OUT/'boards'
    target.mkdir(exist_ok=True)
    cq.exporters.export(solid,str(target/f'PCB_{k}_1p2.step'))
    cq.exporters.export(solid.faces('<Z'),str(target/f'PCB_{k}_outline.dxf'))
    # Export only mechanical geometry, no footprints/nets that could be mistaken
    # for a ready-to-fabricate PCB. Component reservations have their own JSON.
    local=_shape(solid)
    back=cq.importers.importStep(str(target/f'PCB_{k}_1p2.step'))
    b=_bbox(back)
    assert abs(b['zlen']-T)<1e-5, (k,b)
    assert _shape(back).isValid() and len(_shape(back).Solids())==1
    assert abs(local.Volume()-_shape(back).Volume())<1e-3
    dxf_back=cq.importers.importDXF(str(target/f'PCB_{k}_outline.dxf')).wires().toPending().extrude(T)
    assert abs(_shape(dxf_back).Volume()-local.Volume())<1e-3
    return {'step_bbox_mm':b,'step_volume_mm3':_shape(back).Volume(),
            'dxf_volume_mm3':_shape(dxf_back).Volume(),'step_dxf_roundtrip_pass':True}

def main(render=True):
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'parts').mkdir(exist_ok=True)
    local={k:board_outline(k) for k in BOARDS}
    report={'revision':'PCB_1P2_20260910','units':'mm','scope':'PCB mechanical outline and assembly envelope',
            'parameters':{key:getattr(P,key) for key in PARAMETER_KEYS},'battery':BATTERY,'boards':BOARDS,'components':COMPONENTS,
            'checks':{},'interference_checks':[], 'limits':[
                'No schematic, routed PCB, ERC/DRC or component land pattern verification in this release.',
                'Component boxes are layout reservations; exact footprints and passive routing still require PCB design.',
                'Press mechanism force path, springs, coupling and travel stops remain a separate mechanical design.',
                'Battery PCM per-cell thresholds/balancing, connector, external NTC and charging compatibility require vendor confirmation.',
                'No thermal, load, charging, optical uniformity or hardware test claimed.',
                '25 W aluminum brake resistor remains an external bench component, not fitted inside this housing.',
            ]}
    items={f'PCB_{k}':mounted(k,v) for k,v in local.items()}
    for k,v in local.items():
        report['boards'][k]['export_verification']=export_board(k,v)
    print('Board STEP / DXF round-trip passed.',flush=True)
    _,display_local,motor,display=_import_vendor_parts(P)
    items.update(motor_official=motor,display_official=display,housing=housing())
    bridge,posts,tray,cover=mounting_parts()
    items.update(bridge=bridge,tray=tray,bottom_cover=cover,
                 battery=box(101,20,18,0,32,5))
    items.update({f'spacer_{i+1}':s for i,s in enumerate(posts)})
    # The existing core is retained as an assembly reference. No old strip or
    # permanent blue paint marker is present in this current exported model.
    core=layout_core()
    report['limits'].append('Grip texture is omitted from this packaging model; the waist and maximum diameter are preserved.')
    for n in ('bearing_envelope','ring_sleeve','ring_cap','torque_carrier','fixed_spider',
              'support_tube','fixed_bezel','display_glass','button_left_1','button_left_2',
              'button_right_1','button_right_2','power_button','foot_1','foot_2','foot_3','foot_4'):
        items[n]=core[n]
    items['diffuser']=_orient_at_knob(_annulus(74,56,1.2).translate((0,0,-.2)),P)
    ear_cap=box(26.4,13.5,1.2,0,42.65,1.2)
    # Isolated light window over the ambient sensor, outside the lit annulus.
    ear_cap=ear_cap.cut(box(6,4,2,-6,44,1))
    items['ear_cap']=c_orient(ear_cap)
    report['battery']['wire_channel_mm']=[101.,7.,18.]
    report['battery']['wire_channel_center_xy_mm']=[0.,50.5]
    keepouts={'battery_expansion':box(107,26,24,0,32,4.5),
              'rear_wire_channel':box(101,7,18,0,50.5,5),
              'battery_end_wire_bend':box(5,22,13,53,36,7)}
    db=_bbox(display_local)
    display_r=sqrt(max(abs(db['xmin']),abs(db['xmax']))**2+max(abs(db['zmin']),abs(db['zmax']))**2)
    display_low=P.display_origin_normal_mm-db['ymax']
    display_high=P.display_origin_normal_mm-db['ymin']
    keepouts['display_conservative_envelope']=_orient_at_knob(cq.Workplane('XY').circle(display_r).extrude(display_high-display_low).translate((0,0,display_low)),P)
    report['display_envelope_from_official_bbox']={'radius_mm':display_r,'normal_range_mm':[display_low,display_high],
        'reason':'Official display reference is not fully OCC-valid; new-part clearance uses a larger valid cylinder containing its complete local bounding box.'}
    report['battery']['end_wire_bend_mm']=[5,22,13]
    report['battery']['end_wire_bend_center_xy_mm']=[53,36]
    report['component_box_semantics']='Conservative placement zones above PCB, not exact MPN dimensions or PCB land patterns.'
    for k,rows in COMPONENTS.items():
        for name,x,y,w,d,h in rows:
            items[f'{k}_{name}']=mounted(k,box(w,d,h,x,y,T))
    for i in range(24):
        a=i*15.
        led=box(2,2,.9,32,0,T).rotate((0,0,0),(0,0,1),a)
        cap=box(1.6,.8,.8,29.5,0,T).rotate((0,0,0),(0,0,1),a)
        items[f'C_LED_{i+1:02}']=mounted('C',led)
        items[f'C_CAP_{i+1:02}']=mounted('C',cap)
    def overlap(av,bv):
        aa=_bbox(av);bb=_bbox(bv)
        if any(aa[axis+'max']<=bb[axis+'min']+1e-6 or bb[axis+'max']<=aa[axis+'min']+1e-6 for axis in 'xyz'):
            return 0.
        return _intersection_volume(av,bv)
    print('Checking new hardware and clearance reservations...',flush=True)
    def check_pair(a,b,required=True):
        av=items.get(a,keepouts.get(a)); bv=items.get(b,keepouts.get(b))
        vol=overlap(av,bv)
        entry={'a':a,'b':b,'overlap_mm3':round(vol,7),'pass':vol<1e-5,'required':required}
        report['interference_checks'].append(entry)
        if len(report['interference_checks'])%100==0:
            print(f"Checked {len(report['interference_checks'])} assembly pairs",flush=True)
        return entry['pass']
    # PCB/component fit checks target newly designed parts, all new hardware,
    # official core, shell and reserved expansion/wiring space.
    obstacles=['housing','motor_official','display_conservative_envelope','support_tube',
               'battery','bridge','tray','bottom_cover','diffuser','ear_cap']
    newparts=[n for n in items if n.startswith(('PCB_','A_','B_','C_','spacer_'))]
    for a in newparts:
        for b in obstacles:
            check_pair(a,b)
    for a,b in [('PCB_A','PCB_B'),('PCB_A','PCB_C'),('PCB_B','PCB_C'),
                ('battery','motor_official'),('battery','support_tube'),
                ('battery_expansion','bridge'),('battery_expansion','motor_official'),
                ('rear_wire_channel','housing'),('battery','housing'),
                ('tray','motor_official'),('bridge','motor_official')]:
        check_pair(a,b)
    for obstacle in ('housing','battery','tray','bridge','motor_official'):
        check_pair('battery_end_wire_bend',obstacle)
    for obstacle in ('housing','tray'):
        check_pair('rear_wire_channel',obstacle)
    check_pair('bridge','housing')
    for k in 'ABC':
        names=[n for n in items if n.startswith(k+'_')]
        for i,a in enumerate(names):
            for b in names[i+1:]: check_pair(a,b)
        for a in names:
            for i in range(4):
                if k in 'AB':check_pair(a,f'spacer_{i+1}')
    # One continuous, conservative solid covers all 0..0.4 mm travel positions
    # of the ring, bearing, screen and bezel; do not re-Boolean the knurl mesh.
    travel_rows=[]
    swept=_orient_at_knob(cq.Workplane('XY').circle(27).extrude(12.8).translate((0,0,.4)),P)
    for target in newparts+['diffuser','ear_cap','housing']:
        vol=overlap(swept,items[target])
        travel_rows.append({'target':target,'continuous_travel_mm':[0,.4],
                            'overlap_mm3':vol,'pass':vol<1e-5})
    report['press_clearance_samples']=travel_rows
    report['checks']['press_envelope_pass']=all(v['pass'] for v in travel_rows)
    report['checks']['new_parts_interference_pass']=all(v['pass'] for v in report['interference_checks'] if v['required'])
    report['parts']={n:{'bbox_mm':_bbox(s),'valid':_shape(s).isValid(),'solids':len(_shape(s).Solids()),'volume_mm3':_shape(s).Volume()} for n,s in items.items()}
    report['checks']['all_new_shapes_valid']=all(v['valid'] for n,v in report['parts'].items() if not n.endswith('_official'))
    report['vendor_shape_validity']={n:v['valid'] for n,v in report['parts'].items() if n.endswith('_official')}
    if not report['vendor_shape_validity']['display_official']:
        report['limits'].append('Original official screen STEP contains OCC-invalid geometry and is retained as reference without modifying the vendor file; clearance checks use a conservative valid enclosing cylinder.')
    report['checks']['all_boards_1p2_and_roundtrip']=True
    report['hmi_center_world_mm']=list(_deck_center(P))
    report['motor_floor_clearance_mm']=_bbox(motor)['zmin']-3.
    report['board_A_roof_clearance_mm']=P.rear_height_mm-3-(45.2+T+6.5)
    report['board_B_to_A_max_height_clearance_mm']=45.2-(32+T+6.5)
    report['battery_to_B_bottom_clearance_mm']=32-23
    report['bridge_mounting']={'case_ledges':[[x,y] for x in (-53.5,53.5) for y in (15.,50.)],
                              'case_pilot_d_mm':1.6,'bridge_clearance_hole_d_mm':2.2,
                              'ledge_z_mm':[26.,29.],'bridge_z_mm':[29.,30.],
                              'method':'Four M2 fastenings on integral sidewall ledges; ledges stay outside battery expansion volume.'}
    report['source_files']={str(path.relative_to(ROOT)):{'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for path in (
        HERE/'vendor/cubemars/GL30_KV290_factory_encoder_official.step',
        HERE/'vendor/waveshare/ESP32-S3-Touch-AMOLED-1_32_official.step')}
    report['valid_for']='mechanical review; not fabrication or battery/press operation approval'
    def color(n):
        if n in COLORS:return COLORS[n]
        if n.startswith('C_LED_'):return (.25,.75,1.,1)
        if n.startswith(('A_','B_','C_')):return (.13,.15,.18,1)
        if n.startswith('spacer'):return (.66,.67,.69,1)
        if n in ('motor_official','bearing_envelope'):return (.5,.52,.55,1)
        if n.startswith('foot'):return (.10,.11,.12,1)
        return (.065,.07,.075,1)
    export_items=[(n,s,color(n)) for n,s in items.items()]
    _export_step(OUT/'GL30_PCB_1P2_ASSEMBLY.step','GL30_PCB_1P2',export_items)
    for n in ('housing','bridge','tray','bottom_cover','diffuser','ear_cap'):
        cq.exporters.export(items[n],str(OUT/'parts'/f'{n}.step'))
        cq.exporters.export(items[n],str(OUT/'parts'/f'{n}.stl'))
    combined=cover.union(tray)
    assert _shape(combined).isValid() and len(_shape(combined).Solids())==1
    cq.exporters.export(combined,str(OUT/'parts'/'bottom_cover_with_battery_tray.step'))
    cq.exporters.export(combined,str(OUT/'parts'/'bottom_cover_with_battery_tray.stl'))
    (OUT/'layout.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    failed=[v for v in report['interference_checks'] if not v['pass']]
    print(json.dumps({'checks':report['checks'],'failed_intersections':failed},indent=2),flush=True)
    if render:
        _render(OUT/'assembly.png',[(n,s,c,(0,0,0)) for n,s,c in export_items],
                title='GL30 | 118 x 114 mm | 3 PCBs, 1.2 mm',camera=(155,-215,180),parallel_scale_mm=82)
        _render(OUT/'internal_layout.png',[(n,s,c,(0,0,0)) for n,s,c in export_items if n not in ('housing','ear_cap','diffuser','ring_cap','ring_sleeve','display_glass','fixed_bezel')],
                title='PCB LAYOUT | Green A / Blue B / Fixed ring C / Rear 3S battery',camera=(175,165,155),parallel_scale_mm=80)
        # True solid section, not a hand-drawn approximation.
        half=box(150,150,150,75,0,-5)
        section=[(n,_shape(s).cut(_shape(half)),c,(0,0,0)) for n,s,c in export_items]
        section=[r for r in section if len(_shape(r[1]).Solids())]
        _render(OUT/'section.png',section,title='CENTER SECTION | battery and 1.2 mm PCB stack',camera=(195,40,100),parallel_scale_mm=75)
    failures=not all(report['checks'].values())
    print('FAILED: see layout.json' if failures else 'PASS: mechanical checks and board export round-trip',flush=True)
    return 1 if failures else 0

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--no-render',action='store_true')
    args=parser.parse_args()
    try:
        code=main(not args.no_render)
    except Exception:
        import traceback
        traceback.print_exc()
        code=2
    # Known Windows OCP destructor failure; preserve the explicitly computed
    # status, after closing files and flushing output. Never masks a failed check.
    sys.stdout.flush(); sys.stderr.flush(); os._exit(code)
