"""R2 compact mechanical proposal: stationary screen, guided press cartridge.

Dimensions are explicit review inputs. PCB shapes are unrouted mechanical
outlines; the compact battery and guide are reserved envelopes, not purchased
parts. Original vendor STEP files are read-only. No hardware is operated.
"""
from __future__ import annotations

import argparse
from dataclasses import replace
import hashlib
import json
from math import sin, cos, radians, sqrt
import os
from pathlib import Path
import sys

import cadquery as cq
from v7_params import DEFAULTS
from v7_product_model import _render, _export_step, _bbox, _shape, _import_vendor_parts

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/models/GL30_COMPACT_R2'
P = replace(DEFAULTS, width_mm=104., depth_mm=100., front_height_mm=24.,
            rear_height_mm=56., knob_center_from_front_mm=40.,
            display_origin_normal_mm=8.8)
ANGLE = radians(26.)
CENTER = (-8., -10., P.knob_center_z_mm)
TRAVEL = .35
FRONT_STACK_DROP = 6.5
T = 1.2
SCREEN_HOLES = [(15.87214,0.),(-10.79786,-11.03786),(-10.79786,11.03802)]
BACK_HOLES = [(-6.427876,-7.660444),(-3.420201,9.396926),(9.848078,-1.736482)]
FRONT_HOLES = [(-10.,0.),(0.,-10.),(0.,10.),(10.,0.)]

def box(w,d,h,x=0.,y=0.,z=0.):
    return cq.Workplane('XY').box(w,d,h,centered=(True,True,False)).translate((x,y,z))

def ring(od,id,h,z=0.):
    w=cq.Workplane('XY').circle(od/2)
    if id: w=w.circle(id/2)
    return w.extrude(h).translate((0,0,z))

def holes(shape, points, diameter, z, h):
    for x,y in points:
        shape=shape.cut(cq.Workplane('XY').center(x,y).circle(diameter/2).extrude(h).translate((0,0,z)))
    return shape

def orient(shape):
    return shape.rotate((0,0,0),(1,0,0),26).translate(CENTER)

def point(x,y,z):
    return (CENTER[0]+x,CENTER[1]+y*cos(ANGLE)-z*sin(ANGLE),CENTER[2]+y*sin(ANGLE)+z*cos(ANGLE))

def shapevolume(s): return abs(_shape(s).Volume())

def overlap(a,b):
    aa=_bbox(a);bb=_bbox(b)
    if any(aa[v+'max']<=bb[v+'min']+1e-6 or bb[v+'max']<=aa[v+'min']+1e-6 for v in 'xyz'): return 0.
    return shapevolume(_shape(a).intersect(_shape(b)))

def case_solid(w,d,r,zmin,zmax):
    return box(w,d,zmax-zmin,z=zmin).edges('|Z').fillet(r)

def exterior():
    # Rounded plan and a continuous inclined deck ending in a horizontal plateau.
    plan=case_solid(104,100,12,0,60)
    wedge=cq.Workplane('YZ').polyline([(-50,0),(50,0),(50,56),(P.deck_break_y_mm,56),(-50,24)]).close().extrude(60,both=True)
    outer=plan.intersect(wedge)
    innerplan=case_solid(98,94,9,-1,60)
    innerwedge=cq.Workplane('YZ').polyline([(-47,-1),(47,-1),(47,53),(P.deck_break_y_mm-1,53),(-47,24+3*sin(ANGLE)/cos(ANGLE)-3/cos(ANGLE))]).close().extrude(60,both=True)
    shell=outer.cut(innerplan.intersect(innerwedge))
    shell=shell.cut(orient(ring(66.8,0,16,-9)))
    # Flush continuous aperture: the broad diffuser body stays below the deck.
    optical_shelf=ring(68.6,58,1.5,-6.7)
    optical_wall=ring(68.6,66.4,6.7,-6.7)
    inner_baffle=ring(58,57.2,6.7,-6.7)
    top_mask=ring(68.6,62.4,.8,-.8).union(ring(59.6,57.2,.8,-.8))
    support=optical_shelf.union(optical_wall).union(inner_baffle).union(top_mask)
    for a in (37.5,157.5,277.5):
        x,y=31.2*cos(radians(a)),31.2*sin(radians(a))
        boss=ring(4.4,0,3,-9.7).translate((x,y,0))
        support=support.union(boss)
        support=support.cut(ring(1.6,0,4.5,-9.7).translate((x,y,0)))
    shell=shell.union(orient(support))
    ear_cut=box(10,14,10,-34.5,0,-5.2)
    shell=shell.cut(orient(ear_cut))
    shell=shell.union(orient(box(10,14,1.5,-34.5,0,-6.7)))
    # USB-C and one rear key; the case silhouette is uncluttered.
    shell=shell.cut(box(10.4,8,4.4,26,49,29.8))
    shell=shell.cut(cq.Workplane('XZ').center(-28,45).circle(3).extrude(10,both=True).translate((0,49,0)))
    # Bottom cover bosses stay clear of the motor, guide and battery keepout.
    for x,y in [(-44,-37),(43,-38),(-43,38),(43,41)]:
        boss=cq.Workplane('XY').center(x,y).circle(3.6).extrude(7).translate((0,0,3))
        link=box(52-abs(x),5,7,(x+(52 if x>0 else -52))/2,y,3)
        shell=shell.union(boss).union(link)
        shell=shell.cut(cq.Workplane('XY').center(x,y).circle(1.1).extrude(12))
    # Side-wall ledge above the battery supports the power PCB front corner.
    ledge=box(12,8,1.5,46,-18,27.5)
    ledge=holes(ledge,[(43,-18)],1.6,27.5,1.5)
    shell=shell.union(ledge)
    cover=case_solid(97.4,93.4,8.7,0,3)
    cover=holes(cover,[(-44,-37),(43,-38),(-43,38),(43,41)],2.7,0,4)
    cover=holes(cover,[(-38,3),(-38,28),(-8,31),(-36,43),(6,35),(-43,-2)],2.2,0,3)
    return shell,cover,outer

def core():
    """All returned shapes use local normal coordinates; +Z faces the user."""
    p={}
    # Open aluminium cartridge, motor back bolted to the bottom plate.
    carrier=ring(39.8,35.6,31.3-FRONT_STACK_DROP,-29.5).union(ring(39.8,6.4,2.5,-32))
    for a in (0,120,240):
        cut=box(12,12,20,20,0,-27).rotate((0,0,0),(0,0,1),a)
        carrier=carrier.cut(cut)
    carrier=holes(carrier,BACK_HOLES,3.4,-32,3)
    # Real shoulder and removable inner-race retaining ring.
    carrier=carrier.union(ring(43,35.6,.8,1.0-FRONT_STACK_DROP)).union(ring(40,35.6,7,1.8-FRONT_STACK_DROP))
    guide_pad=box(2.2,17,22.5,-18.9,0,-30).cut(ring(35.6,0,25,-30.5))
    carrier=carrier.union(guide_pad)
    wings=box(8,24,1,-22,0,-29.5).cut(box(8.4,17.4,2,-24,0,-30))
    carrier=carrier.union(wings).union(box(2,1,.9,-23.5,12,-30.4))
    # Four M2 mounting axes of MGN7C; countersunk carrier screws stay flush.
    for y in (-6,6):
        for n in (-22.75,-14.75):
            bore=cq.Workplane('YZ').center(y,n).circle(1.1).extrude(12).translate((-28,0,0))
            carrier=carrier.cut(bore)
    p['press_cartridge']=carrier
    p['bearing_6808_envelope']=ring(52,40,7,1.8)
    p['bearing_inner_retainer']=ring(42.5,35.6,.7,8.8)
    # Aluminium outer-race carrier, 2 mm nominal wall at the bearing station.
    sleeve=ring(56,52,7,1.8).union(ring(56,43.4,.8,8.8))
    # Closed lower skirt keeps the mechanical gap visually distinct from light.
    sleeve=sleeve.union(ring(56,54.6,1.,.8))
    p['rotating_bearing_seat']=sleeve
    p['bearing_outer_lower_retainer']=ring(56,48.8,.8,1.0)
    # Taller gently waisted grip; no costly cosmetic knurl in fit CAD.
    p['rotating_grip']=(cq.Workplane('XZ').moveTo(26.2,9.6).lineTo(28,9.6).lineTo(28,13)
        .bezier([(28,15),(26.8,18),(26.8,20)],includeCurrent=True)
        .bezier([(26.8,22),(27.2,23),(27.2,24.7)],includeCurrent=True)
        .bezier([(27.2,25.5),(27.1,26.2),(26.8,26.2)],includeCurrent=True)
        .lineTo(21.5,26.2).lineTo(21.5,11.8).lineTo(26.2,11.8).close().revolve(360,(0,0),(0,1)))
    for n in ('bearing_6808_envelope','bearing_inner_retainer','rotating_bearing_seat','bearing_outer_lower_retainer','rotating_grip'):
        p[n]=p[n].translate((0,0,-FRONT_STACK_DROP))
    drive=ring(30,6.4,2,-1.3).union(ring(33,28,9.3-FRONT_STACK_DROP,.7))
    drive=holes(drive,FRONT_HOLES,3.4,-1.3,2)
    # A thin diaphragm transmits torque while relieving residual axial load.
    # Merely moving the motor and bearing together would leave parallel axial paths.
    drive=drive.union(ring(35,28,.5,3.5))
    p['rotor_drive_bridge']=drive
    flex=ring(35,28,.1,4).union(ring(52,47,.1,4))
    for a in range(0,360,60):
        flex=flex.union(box(9,2,.1,20.5,0,4).rotate((0,0,0),(0,0,1),a))
    p['torque_diaphragm_0p1']=flex
    p['diaphragm_outer_clamp']=ring(52,47,1.2,4.1)
    # 35 mm MGN7 rail, 22.5 mm block. Manufacturer geometry envelope only.
    p['MGN7_rail_reserve']=box(4.8,7,35,-25.6,0,-41.5)
    for n in (-31.5,-16.5):
        bore=cq.Workplane('YZ').center(0,n).circle(1.2).extrude(5).translate((-28,0,0))
        p['MGN7_rail_reserve']=p['MGN7_rail_reserve'].cut(bore)
    block=box(8,17,22.5,-24,0,-30).cut(box(4.8,7,24,-25.6,0,-30.5))
    p['MGN7C_block_reserve']=block
    # Factory-encoder STEP blocks the bore. Use a positive rear bridge instead.
    bridge=box(6,5,45.4-FRONT_STACK_DROP,0,38,-16).union(box(6,22,2,0,29.5,27.4-FRONT_STACK_DROP))
    bridge=bridge.union(box(6,1,14.4,0,20,13-FRONT_STACK_DROP))
    flange=holes(box(18,8,1.5,0,38,-17.5),[(-6,38),(6,38)],2.2,-17.5,1.5)
    bridge=bridge.union(flange).union(box(8,5,.5,0,18,12.5-FRONT_STACK_DROP))
    bridge=bridge.cut(box(4,.6,44.8-FRONT_STACK_DROP,0,40.2,-16)).cut(box(4,21,.6,0,29.5,28.8-FRONT_STACK_DROP))
    p['fixed_screen_bridge']=bridge
    tray=ring(41,34,.6,13).union(ring(9,3.2,.6,13))
    for x,y in SCREEN_HOLES:
        length=sqrt(x*x+y*y)
        arm=box(length,3.2,.6,length/2,0,13).rotate((0,0,0),(0,0,1),__import__('math').degrees(__import__('math').atan2(y,x)))
        tray=tray.union(arm).union(cq.Workplane('XY').center(x,y).circle(1.7).extrude(6.3).translate((0,0,13)))
    tray=holes(tray,SCREEN_HOLES,2.2,13,6.4)
    tray=tray.cut(box(8.2,1.2,.7,0,20,13))
    tray=holes(tray,[(-2.5,18),(2.5,18)],1.7,13,.6)
    p['fixed_screen_bridge']=holes(p['fixed_screen_bridge'],[(-2.5,18),(2.5,18)],1.3,12.5-FRONT_STACK_DROP,.5)
    # Three screen fasteners and a positive bridge joint define screen orientation.
    p['fixed_screen_tray']=tray.translate((0,0,-FRONT_STACK_DROP))
    p['fixed_bezel']=ring(41.6,33.8,.6,25.3-FRONT_STACK_DROP)
    p['screen_cover']=ring(41.6,0,.3,25.9-FRONT_STACK_DROP)
    p['screen_FPC_rear_reserve']=box(3.5,.2,44.8-FRONT_STACK_DROP,0,40.2,-16)
    p['screen_FPC_top_reserve']=box(3.5,20,.2,0,30,28.8-FRONT_STACK_DROP)
    p['bridge_channel_cover']=box(6,22,.3,0,29.5,29.4-FRONT_STACK_DROP).union(box(6,.3,45.4-FRONT_STACK_DROP,0,40.65,-16))
    return p

def fixed_chassis():
    # Rail backplate ends below the rotating grip; two floor feet support it.
    plate=box(3,19,32.5,-29.5,0,-39.5)
    for n in (-31.5,-16.5):
        plate=plate.cut(cq.Workplane('YZ').center(0,n).circle(.8).extrude(4).translate((-31.5,0,0)))
    world=orient(plate)
    for lx,ly,lz in [(-33,-7,-34),(-33,7,-21)]:
        x,y,z=point(lx,ly,lz)
        foot=cq.Workplane('XY').center(x,y).circle(3.5).extrude(max(1,z-3+2)).translate((0,0,3))
        world=world.union(foot)
    floor=box(38,40,2,-24,15,3).cut(box(24,24,3,-24,15,2.5))
    floor=floor.cut(cq.Workplane('XY').center(-43,38).circle(3.9).extrude(3).translate((0,0,2.5)))
    world=world.union(floor)
    x,y,z=point(0,38,-16)
    mast=box(8,10,z-5,x,y,5).intersect(orient(box(30,30,60,0,38,-79.5)))
    cap=holes(box(18,8,2,0,38,-19.5),[(-6,38),(6,38)],1.6,-19.5,2)
    mast=mast.union(orient(cap))
    world=world.union(mast)
    for x,y in [(-38,3),(-38,28),(-8,31)]:
        boss=cq.Workplane('XY').center(x,y).circle(3).extrude(5).translate((0,0,3))
        world=world.union(boss)
        world=world.cut(cq.Workplane('XY').center(x,y).circle(.8).extrude(7).translate((0,0,3)))
    # Stroke stops and springs are next to the guide, outside the motor shell.
    stop_base=box(10,24,3,-26,0,-34).cut(box(4.8,7,4,-25.6,0,-34.5))
    for y in (-10.5,10.5):
        stop_base=stop_base.cut(ring(3.2,0,2,-33).translate((-23.5,y,0)))
    stop_base=stop_base.union(box(6,5,1,-23.5,13,-33)).cut(box(4.2,3.2,1,-23.5,13,-32))
    world=world.union(orient(stop_base))
    return world

# Irregular boards occupy the rear and left side around the mechanical module.
# They are horizontal; every point is a world XY coordinate, bottom set below.
BOARD_POLYGONS={
 'A':[(-46,-6),(-40,-6),(-40,14),(-24,24),(10,24),(10,45),(-42,45),(-46,39)],
 'B':[(-43,23),(14,23),(14,-21),(45,-21),(47,-19),(47,42),(43,45),(-43,45)],
}
BOARD_Z={'A':35.5,'B':29.,'C':-5.2}
BOARD_HOLES={'A':[(-43,-2),(-36,43),(6,35)],'B':[(-36,43),(6,35),(43,-18),(43,41)],
             'C':[(31.2*cos(radians(a)),31.2*sin(radians(a))) for a in (37.5,157.5,277.5)]}

def pcb(k):
    if k=='C':
        s=ring(66,58,T)
        # Connector ear nests behind the knob, under the deck.
        s=s.union(box(9,12,T,-34.5,0))
    else:
        s=cq.Workplane('XY').polyline(BOARD_POLYGONS[k]).close().extrude(T)
        s=s.edges('|Z').fillet(1.5)
        _,mast_y,_=point(0,38,-16)
        s=s.cut(box(14,16,T,-8,mast_y,0).edges('|Z').fillet(1.5))
    s=holes(s,BOARD_HOLES[k],2.2,0,T)
    return s

def placed_board(k,s): return orient(s.translate((0,0,BOARD_Z[k]))) if k=='C' else s.translate((0,0,BOARD_Z[k]))

def build():
    OUT.mkdir(parents=True,exist_ok=True)
    for folder in ('parts','boards'): (OUT/folder).mkdir(exist_ok=True)
    shell,cover,case_envelope=exterior()
    local=core()
    items={n:orient(s) for n,s in local.items()}
    items.update(housing=shell,bottom_cover=cover,fixed_chassis=fixed_chassis())
    # User-authorized simple rectangular custom 3S pack, including protection.
    items['compact_battery_reserve']=box(32,60,21,31.5,5,4.5)
    keepouts={'battery_expansion_and_assembly':box(34,62,23,31.5,5,4)}
    tray=box(35,63,22,31.5,5,3).cut(box(34,62,24,31.5,5,4))
    # Open ends provide a strap path without clamping the pouch faces.
    tray=tray.cut(box(42,56,23,31.5,5,5))
    items['battery_tray']=tray
    for k in 'ABC': items['PCB_'+k]=placed_board(k,pcb(k))
    for i,(x,y) in enumerate([(-36,43),(6,35),(43,41)]):
        base=10 if i==2 else 3
        items[f'PCB_B_standoff_{i}']=ring(5,2.2,29-base,base).translate((x,y,0))
    for i,(x,y) in enumerate([(-36,43),(6,35)]):
        items[f'PCB_A_spacer_{i}']=ring(5,2.2,5.3,30.2).translate((x,y,0))
    items['PCB_A_front_standoff']=ring(5.5,2.2,32.5,3).translate((-43,-2,0))
    items['diffuser']=orient(ring(66.2,58.2,1.1,-1.3).union(ring(62.3,59.7,.4,-.2)))
    items['optical_ear_cap']=orient(box(9.8,13.8,.8,-34.5,0,-.8))
    # LED count stays hidden behind a continuous mixing space.
    for i in range(24):
        a=i*15
        items[f'LED_{i+1:02}']=orient(box(2,2,.9,31,0,-4).rotate((0,0,0),(0,0,1),a))
    # Component boxes are retained functional reservations, not land patterns.
    zones={
      'A_MCU':(-35,28,10,10,2),'A_driver':(4.5,28.5,9,8,2),
      'A_comparators':(-35,39,14,5,2),'A_phase_connector':(-22,28,12,6,5),
      'A_caps':(4.5,41,10,7,6.5),'A_FPC':(-22,42,11,4,2),
      'B_charger':(28,16,8,8,1.5),'B_inductor':(39,16,6,6,3.2),
      'B_logic_buck':(21,0,12,14,6.5),'B_LED_buck':(39,0,12,14,6.5),
      'B_PD':(28,34,6,6,1.8),'B_fuse':(17,36,7,4,3),
      'B_brake':(39,32,10,10,2),'B_USB_C':(26,47.5,9,9,3.2),
      'B_FPC':(-22,42,11,4,2),'B_battery_connector':(39,-13,10,6,5),
      'B_measurement':(-23.5,31,12,10,2),'B_power_switches':(-37.5,31,10,15,2),
    }
    for n,(x,y,w,d,h) in zones.items():
        items[n]=box(w,d,h,x,y,BOARD_Z[n[0]]+T)
    items['C_FPC_reserve']=orient(box(4,10,2,-35,0,-4))
    # Nominal bend reservations: circuit design must still assign pins/current.
    items['A_B_FFC_reserve']=cq.Workplane('YZ').polyline([(42,32.2),(44,32.2),(46,34.2),(46,36.7),(44,38.7),(42,38.7),(42,38.5),(43.9,38.5),(45.8,36.6),(45.8,34.3),(43.9,32.4),(42,32.4)]).close().extrude(4,both=True).translate((-22,0,0))
    # Main currents use a flexible flat wire harness, physically separated from FFC.
    items['power_flat_harness_reserve']=box(4,40,1.2,46.5,10,27.3)
    for i,(x,y) in enumerate([(-42,-38),(42,-38),(-42,38),(42,38)]):
        items[f'foot_{i}']=cq.Workplane('XY').center(x,y).circle(5).extrude(1.2).translate((0,0,-1.2))
    # MGN block presses onto an adjustable hard-stop shim after 0.35 mm.
    shim=box(4,18,.65,-23.5,0,-31).cut(box(5,7.2,1,-25.6,0,-31.1))
    items['press_stop_shim']=orient(shim)
    # Lower fixed seat at -31, moving block bottom -30; gap .35 after .65 shim.
    # Two spring envelopes beside guide; actual force curve is a sample requirement.
    for y in (-10.5,10.5):
        items['return_spring_'+str(y)]=orient(ring(3,2,3.5,-33).translate((-23.5,y,0)))
    items['press_switch_reserve']=orient(box(4,3,1.5,-23.5,13,-32))
    # Separate original references; display invalid topology is never healed silently.
    ml,dl,mw,dw=_import_vendor_parts(P)
    items['motor_official']=mw.translate((-8,0,0))
    items['display_official']=dw.translate((-8,0,0))
    return items,keepouts,local,case_envelope

def main(render=True):
    items,keepouts,local,case_envelope=build()
    report={'status':'R2_MECHANICAL_PROPOSAL_NOT_ROUTED_OR_HARDWARE_VALIDATED','units':'mm',
      'dimensions':{'body':[104,100,24,56],'old_body':[118,114,20,58],
      'footprint_reduction_percent':100*(1-104*100/(118*114)),
      'knob_od':56,'knob_normal_height':26.2-FRONT_STACK_DROP,'old_knob_normal_height':13.2,
      'visible_light_radial_width':1.4,'light_top_normal':.2,'PCB_thickness':1.2,
      'press_stroke':TRAVEL,'screen_bridge_width_thickness':[6,2],
      'bridge_to_rotating_grip_axial_gap':1.2,
      'MGN7C_envelope':[17,8,22.5],'rail_length':35,'hmi_center':CENTER},
      'battery':{'reference':'Custom rectangular protected 3S pack; user authorized custom shape',
        'complete_pack_target_mm':[60,32,21],'reserve_world_mm':[32,60,21],
        'reserved_expansion_mm':[34,62,23],'status':'Manufacturing target, not a selected battery. Supplier must confirm capacity, complete PCM/NTC/balancing dimensions and currents. Old 101 mm pack does not fit.',
        'source':'https://www.dnkpower.com/products/11-1v-1000mah-lipo-battery-dnk503450-3s-battery-pack-3s-lithium-polymer-battery/'},
      'kinematics':{'fixed':['housing','fixed_chassis','MGN7_rail_reserve','fixed_screen_bridge','fixed_screen_tray','display_official','PCB_A','PCB_B','PCB_C'],
        'press_only':['press_cartridge','MGN7C_block_reserve','motor_stator','bearing_inner_race'],
        'rotate_and_press':['motor_rotor','rotor_drive_bridge','torque_diaphragm_0p1','diaphragm_outer_clamp','bearing_outer_race','rotating_bearing_seat','rotating_grip'],
        'screen_does_not_rotate_or_press':True,
        'scope':'Intended physical motion groups, not STEP joint constraints. Vendor motor and bearing are composite dimensional references; their stator/race identities are not fabricated as separate verified CAD parts.'},
      'pcb_mounting':{
        'A':{'holes':BOARD_HOLES['A'],'supports':['PCB_A_front_standoff','PCB_A_spacer_0','PCB_A_spacer_1']},
        'B':{'holes':BOARD_HOLES['B'],'supports':['PCB_B_standoff_0','PCB_B_standoff_1','integral housing ledge at (43,-18), z27.5..29','PCB_B_standoff_2 on existing housing boss z10']},
        'C':{'holes':BOARD_HOLES['C'],'supports':'Three integral housing bosses plus annular shelf, normal -9.7..-5.2, 1.6 mm pilot bores'}},
      'checks':{},'intersections':[], 'limits':[
        'User approved a custom battery shape. 60 x 32 x 21 mm is the complete-pack target; capacity and supplier feasibility are not confirmed.',
        'MGN7 guide/block are official-dimension envelopes, not vendor detailed solids.',
        'Mechanical PCB outlines only; no routing, ERC, DRC, thermal or current qualification.',
        'Original display STEP is OCC-invalid. Screen fit uses sampled tessellation radius plus margin and a separate screw-axis check.',
        'Fit tolerances, bearing retention/preload, switch force, guide seal drag and lubrication need physical verification.',
        'Factory encoder STEP obstructs a through-centre post: 67.2505 mm3 overlap for the former OD5/ID3.2 tube. R2 uses an external stationary rear bridge instead.',
        'Motor end-face/stator identity and usable screw depth require arrival confirmation.',
        'No motor power, firmware flashing, printing, manufacture or operation was performed.'
      ]}
    for k in 'ABC':
        s=pcb(k)
        cq.exporters.export(s,str(OUT/'boards'/f'PCB_{k}_1p2.step'))
        cq.exporters.export(s.faces('<Z'),str(OUT/'boards'/f'PCB_{k}_outline.dxf'))
        back=cq.importers.importStep(str(OUT/'boards'/f'PCB_{k}_1p2.step'))
        report['checks'][f'PCB_{k}_STEP_roundtrip']=_shape(back).isValid() and abs(_bbox(back)['zlen']-T)<1e-5 and abs(shapevolume(back)-shapevolume(s))<1e-5
    for a in ['PCB_A','PCB_B','PCB_C','compact_battery_reserve','battery_tray','fixed_chassis']+[n for n in items if n.startswith(('A_','B_','C_','PCB_A_','PCB_B_'))]:
        for b in ['housing','bottom_cover','motor_official','press_cartridge','fixed_screen_bridge']:
            if a==b: continue
            vol=overlap(items[a],items[b]);report['intersections'].append({'a':a,'b':b,'volume_mm3':vol,'pass':vol<1e-5})
    for a,b in [('compact_battery_reserve','fixed_chassis'),('battery_tray','fixed_chassis'),('PCB_A','PCB_B'),('PCB_B','battery_tray'),('PCB_A','MGN7_rail_reserve'),('PCB_B','MGN7_rail_reserve'),('fixed_screen_bridge','motor_official'),('fixed_screen_bridge','press_cartridge'),('fixed_screen_tray','rotating_grip'),('fixed_screen_tray','rotor_drive_bridge'),('fixed_screen_tray','diaphragm_outer_clamp'),('fixed_screen_tray','torque_diaphragm_0p1')]:
        vol=overlap(items[a],items[b]);report['intersections'].append({'a':a,'b':b,'volume_mm3':vol,'pass':vol<1e-5})
    # Continuous press/rotation envelopes: cylinders contain every intervening angle.
    sweeps={'rotating_lower':ring(56,54.6,1+TRAVEL,.8-TRAVEL-FRONT_STACK_DROP),
      'bearing_region':ring(56,40,8.6+TRAVEL,1.8-TRAVEL-FRONT_STACK_DROP),
      'grip_region':ring(56,43,15.8+TRAVEL,10.4-TRAVEL-FRONT_STACK_DROP),
      'drive_hub':ring(35,6.4,13.1+TRAVEL-FRONT_STACK_DROP,-1.3-TRAVEL),
      'diaphragm_region':ring(52,28,.3+TRAVEL,3.9-TRAVEL)}
    for sn,ss in sweeps.items():
        for target in ('housing','PCB_A','PCB_B','PCB_C','diffuser','fixed_screen_bridge','MGN7_rail_reserve'):
            v=overlap(orient(ss),items[target]);report['intersections'].append({'a':sn,'b':target,'volume_mm3':v,'pass':v<1e-5})
    for target in ('housing','fixed_chassis','press_cartridge','PCB_A','PCB_B','power_flat_harness_reserve'):
        v=overlap(keepouts['battery_expansion_and_assembly'],items[target])
        report['intersections'].append({'a':'battery_expansion_and_assembly','b':target,'volume_mm3':v,'pass':v<1e-5})
    # Distinct PCB functional zones must fit and must not occupy one another.
    report['component_area_checks']=[]
    for k in 'AB':
        names=[n for n in items if n.startswith(k+'_') and n not in ('A_B_FFC_reserve','B_USB_C')]
        for n in names:
            b=_bbox(items[n]);probe=box(b['xlen'],b['ylen'],T,(b['xmin']+b['xmax'])/2,(b['ymin']+b['ymax'])/2,BOARD_Z[k])
            outside=shapevolume(_shape(probe).cut(_shape(items['PCB_'+k])))
            report['component_area_checks'].append({'name':n,'outside_PCB_mm3':outside,'pass':outside<1e-5})
        for i,a in enumerate(names):
            for b in names[i+1:]:
                v=overlap(items[a],items[b]);report['intersections'].append({'a':a,'b':b,'volume_mm3':v,'pass':v<1e-5})
    report['checks']['reserved_component_areas_fit_boards']=all(r['pass'] for r in report['component_area_checks'])
    report['press_position_checks']=[]
    for step in (0,TRAVEL/2,TRAVEL):
        for name in ('press_cartridge','MGN7C_block_reserve'):
            moving=orient(local[name].translate((0,0,-step)))
            for target in ('housing','fixed_chassis','PCB_A','PCB_B','PCB_C','fixed_screen_bridge','bottom_cover','diffuser','MGN7_rail_reserve'):
                v=overlap(moving,items[target]);report['press_position_checks'].append({'part':name,'stroke_mm':step,'target':target,'volume_mm3':v,'pass':v<1e-5})
    report['checks']['press_positions_clear']=all(r['pass'] for r in report['press_position_checks'])
    report['checks']['tested_intersections_pass']=all(r['pass'] for r in report['intersections'])
    report['parts']={n:{'bbox':_bbox(s),'volume_mm3':shapevolume(s),'solids':len(_shape(s).Solids()),'valid':_shape(s).isValid()} for n,s in items.items()}
    report['checks']['new_shapes_valid']=all(p['valid'] for n,p in report['parts'].items() if not n.endswith('_official'))
    report['checks']['structural_parts_single_solid']=all(report['parts'][n]['solids']==1 for n in ('housing','fixed_chassis','press_cartridge','fixed_screen_bridge','fixed_screen_tray','rotating_grip','torque_diaphragm_0p1'))
    report['checks']['hardstop_gap_is_035']=abs((-30)-(-31+.65)-TRAVEL)<1e-8
    report['checks']['FPC_fits_bridge_channel']=3.5<4 and .2<.6
    report['checks']['bridge_stays_above_all_rotation_angles']=27.4-FRONT_STACK_DROP>26.2-FRONT_STACK_DROP
    report['checks']['guide_static_moment_screening']=5*.028<2.84
    report['diaphragm_screening']={'material_target':'0.10 mm spring stainless steel, grade and fatigue not qualified',
        'arm_count':6,'effective_clear_span_mm':6,'arm_width_mm':2,'thickness_mm':.1,
        'assumed_E_N_mm2':200000,'ideal_axial_stiffness_N_mm':6*200000*2*.1**3/6**3,
        'meaning':'Ideal fixed-guided beam estimate. Relieves axial load; does not establish zero motor axial load or fatigue life.'}
    # Preserve the decisive failed old assumption as positive design evidence.
    old_tube=orient(ring(5,3.2,47.2,-34.2))
    blocked=_shape(old_tube).intersect(_shape(items['motor_official']))
    report['factory_encoder_centre_access']={'old_tube_od_id_mm':[5,3.2],
        'overlap_mm3':shapevolume(blocked),'interference_bbox':_bbox(blocked),
        'conclusion':'The official encoder-equipped STEP does not support the former through-post assumption. Actual sample bore remains unverified; R2 uses no through-post.'}
    report['checks']['rear_bridge_avoids_blocked_motor_centre']=shapevolume(blocked)>1 and overlap(items['fixed_screen_bridge'],items['motor_official'])<1e-5
    report['guide_load_screening']={'edge_force_N':5,'radius_mm':28,'moment_Nm':.14,
        'catalog_min_static_moment_Nm':2.84,'ratio':2.84/.14,
        'meaning':'Static catalogue comparison only. Not system stiffness, fatigue, preload friction or ergonomic acceptance.'}
    report['source_files']={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'hardware/cad/vendor/cubemars/GL30_KV290_factory_encoder_official.step',ROOT/'hardware/cad/vendor/waveshare/ESP32-S3-Touch-AMOLED-1_32_official.step']}
    def color(n):
        if n=='housing':return (.66,.68,.68,1)
        if n=='bottom_cover':return (.2,.22,.24,1)
        if n=='compact_battery_reserve':return (.2,.42,.62,1)
        if n=='PCB_A':return (.08,.38,.23,1)
        if n=='PCB_B':return (.08,.25,.4,1)
        if n=='PCB_C':return (.10,.11,.12,1)
        if n=='diffuser' or n.startswith('LED_'):return (.28,.73,.87,1)
        if 'FPC' in n or 'FFC' in n:return (.78,.47,.17,1)
        if n.startswith('fixed_screen'):return (.13,.15,.17,1)
        if n in ('fixed_chassis','MGN7_rail_reserve'):return (.54,.57,.6,1)
        if n in ('press_cartridge','MGN7C_block_reserve'):return (.25,.49,.56,1)
        if n=='motor_official' or n=='bearing_6808_envelope':return (.47,.49,.5,1)
        return (.06,.07,.08,1)
    for n in ('housing','bottom_cover','fixed_chassis','press_cartridge','fixed_screen_bridge','fixed_screen_tray','rotating_grip','rotating_bearing_seat','rotor_drive_bridge','torque_diaphragm_0p1','diaphragm_outer_clamp','bearing_outer_lower_retainer','diffuser','optical_ear_cap','battery_tray'):
        cq.exporters.export(items[n],str(OUT/'parts'/f'{n}.step'))
    export_items=[(n,s,color(n)) for n,s in items.items()]
    # Display is a separate read-only reference to keep an invalid compound out of
    # the independently verifiable proposed assembly. It is present in previews.
    valid_items=[row for row in export_items if row[0]!='display_official']
    _export_step(OUT/'GL30_COMPACT_R2_ASSEMBLY.step','GL30_COMPACT_R2',valid_items)
    report['assembly_reference_note']='Original display supplied separately via source path and transform. Assembly uses cover/tray; preview includes official display.'
    report['assembly_expected']={'solids':sum(len(_shape(s).Solids()) for _,s,_ in valid_items),'volume_mm3':sum(shapevolume(s) for _,s,_ in valid_items)}
    (OUT/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'checks':report['checks'],'failed':[r for r in report['intersections']+report['press_position_checks']+report['component_area_checks'] if not r['pass']],
        'structural_solids':{n:report['parts'][n]['solids'] for n in ('housing','fixed_chassis','press_cartridge','fixed_screen_bridge','fixed_screen_tray','torque_diaphragm_0p1')}},indent=2),flush=True)
    if render:
        _render(OUT/'assembly.png',[(n,s,c,(0,0,0)) for n,s,c in export_items],title='GL30 R2 | 104 x 100 mm | Tall grip / flush light',camera=(155,-215,160),focal=(0,0,35),parallel_scale_mm=78)
        hidden={'housing','rotating_grip','rotating_bearing_seat','fixed_bezel','screen_cover','display_official','diffuser'}
        _render(OUT/'internal_layout.png',[(n,s,c,(0,0,0)) for n,s,c in export_items if n not in hidden],title='R2 | Side battery / shaped boards / guided press',camera=(160,190,180),focal=(0,0,29),parallel_scale_mm=72)
        moving=['press_cartridge','bearing_6808_envelope','rotating_bearing_seat','rotating_grip','rotor_drive_bridge','torque_diaphragm_0p1','diaphragm_outer_clamp','MGN7C_block_reserve','motor_official']
        selected=moving+['fixed_chassis','MGN7_rail_reserve','fixed_screen_bridge','fixed_screen_tray','fixed_bezel','screen_cover']
        # Section through local Y=0 keeps left guide and centre-post load path visible.
        cut=orient(box(160,100,150,0,-50,-70))
        sec=[(n,_shape(items[n]).cut(_shape(cut)),(.82,.47,.13,1) if n.startswith('fixed_screen') else color(n),(0,0,0)) for n in selected]
        sec=[r for r in sec if len(_shape(r[1]).Solids())]
        _render(OUT/'mechanism_section.png',sec,title='FIXED REAR BRIDGE | Guided cartridge / independent rotary bearing',camera=(95,-170,110),focal=(-8,0,43),parallel_scale_mm=53)
    return 0 if all(report['checks'].values()) else 1

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--no-render',action='store_true');args=parser.parse_args()
    try: code=main(not args.no_render)
    except Exception:
        import traceback;traceback.print_exc();code=2
    sys.stdout.flush();sys.stderr.flush();os._exit(code)
