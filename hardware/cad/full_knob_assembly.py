"""GL30 R8 mechanical-review revision, in mm. Unpowered prototype definition.

R3 supplied only the central mechanism. This model adds the actual installation
spaces, controls, supports and wiring. Purchased parts without supplier CAD are
explicitly named envelopes, rather than pretending to be finished components.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
from math import cos, sin, radians, tan

import cadquery as cq
import hidden_screen_study as core
from v7_product_model import _bbox, _export_step, _render, _shape

ROOT = Path(__file__).resolve().parents[2]
MODEL_SOURCE_SHA256 = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
OUT = ROOT / "output/models/GL30_FULL_R8"
B, R, O = core.box, core.ring, core.orient
T = 1.2
PARTS = {}
META = {}
KEEPOUTS = {}
LOCAL_BOARDS = {}
REFINEMENT = {}
THREAD_PAIRS = set()
WIRE_PATHS = {}
OVERLAP_SHAPES = {}
DECK_RAISE_NORMAL = 2.3
DECK_RAISE_Z = DECK_RAISE_NORMAL / core.COS_A
REAR_DECK_Z = 67.0
DECK_BREAK_Y = (REAR_DECK_Z - 30.0 - DECK_RAISE_Z) / tan(radians(26)) - 50.0
SIDE_BUTTON_Y = (7.0, 23.0)
SIDE_BUTTON_Z = 47.0
# A non-collinear triangle avoids the rear screen clamp and spring pedestal.
CARTRIDGE_BOLTS = ((12.0,0.0),(-12.0,0.0),(0.0,-12.0))
SIDE_KEY_GAP = .2
SIDE_KEY_STOP = .4
BELT_RAISE = .6


def thread_pair(a,b):
    THREAD_PAIRS.add(frozenset((a,b)))


def local_shape(shape):
    return _shape(shape).translate(tuple(-x for x in core.CENTER)).rotate((0,0,0),(1,0,0),-26)


def cylinder_x(d,length,x,y,n):
    return cq.Solid.makeCylinder(d/2,length,cq.Vector(x,y,n),cq.Vector(1,0,0))


def overlap(first,second):
    """Prune supplier compounds before Boolean work; zero means every solid clears.

    Positive values are sums of individual intersections, used as collision
    indicators. They are not a union-volume measurement for overlapping vendor
    solids. Testing the entire 403-solid module at once is prohibitively slow.
    """
    def data(s):
        key=id(s)
        if key not in OVERLAP_SHAPES:
            # Retain the object as well as its bounds, so Python cannot reuse
            # an id for a later transformed shape during this verification.
            OVERLAP_SHAPES[key]=(s,s.BoundingBox(),[(v,v.BoundingBox()) for v in s.Solids()])
        return OVERLAP_SHAPES[key]
    def separated(ba,bb):
        return any(getattr(ba,k+'max')<=getattr(bb,k+'min')+1e-7 or
                   getattr(bb,k+'max')<=getattr(ba,k+'min')+1e-7 for k in 'xyz')
    a,b=_shape(first),_shape(second)
    _,ba,sa=data(a);_,bb,sb=data(b)
    if separated(ba,bb):
        return 0.
    display=PARTS.get("D_Waveshare_complete_module_supplier_valid_solids")
    bound=KEEPOUTS.get("display_supplier_full_radial_envelope")
    if bound is not None and (a is display or b is display):
        other=b if a is display else a
        if core.overlap(bound,other)<1e-7:
            return 0.
    if len(sa)==len(sb)==1:
        return core.volume(a.intersect(b))
    total=0.
    for x,xb in sa:
        for y,yb in sb:
            if not separated(xb,yb):total+=core.volume(x.intersect(y))
    return total


def add(name, shape, group, color=None, status="design geometry"):
    PARTS[name] = _shape(shape)
    META[name] = {"group": group, "status": status,
                  "color": color or COLORS.get(group, (.55, .58, .62, 1))}
    return PARTS[name]


COLORS = {
    "housing": (.22,.235,.25,1), "base": (.16,.175,.19,1),
    "moving": (.20,.47,.57,1), "rotating": (.055,.065,.075,1),
    "fixed_core": (.56,.61,.65,1), "motor": (.40,.43,.48,1),
    "PCB_A": (.04,.46,.24,1), "PCB_B": (.035,.27,.63,1),
    "PCB_C": (.05,.31,.22,1), "display": (.06,.1,.12,1),
    "battery": (.94,.54,.12,1), "button": (.23,.28,.33,1),
    "wire": (.92,.22,.15,1), "FFC": (.84,.72,.42,1),
    "component": (.13,.16,.20,1), "metal": (.70,.73,.76,1),
    "connector": (.91,.89,.79,1), "speaker": (.13,.16,.19,1),
    "marking": (.61,.62,.61,1),
}


def rounded(w,d,h,r,x=0,y=0,z=0):
    return B(w,d,h,x,y,z).edges("|Z").fillet(r)


def cyl_between(a,b,radius):
    a,b=cq.Vector(*a),cq.Vector(*b)
    direction=b-a
    return cq.Solid.makeCylinder(radius,direction.Length,a,direction.normalized())


def cable(points, radius=.8):
    # Round segments reserve cable diameter; corner bend allowances are reported.
    solids=[cyl_between(a,b,radius) for a,b in zip(points,points[1:])]
    solids += [cq.Solid.makeSphere(radius,cq.Vector(*p)) for p in points[1:-1]]
    return cq.Compound.makeCompound(solids)


def ribbon(points, width=5.5, thickness=.20):
    # Ribbon runs are in a YZ plane; rounded slack loops are represented by swept
    # width, with local corner envelopes. End-to-end length is checked below.
    solids=[]
    for a,b in zip(points,points[1:]):
        av,bv=cq.Vector(*a),cq.Vector(*b)
        direction=(bv-av).normalized()
        across=cq.Vector(1,0,0)
        normal=across.cross(direction).normalized()
        wire=cq.Wire.makePolygon([av-across*width/2-normal*thickness/2,
            av+across*width/2-normal*thickness/2,
            av+across*width/2+normal*thickness/2,
            av-across*width/2+normal*thickness/2],close=True)
        solids.append(cq.Solid.extrudeLinear(wire,[],bv-av))
    return cq.Compound.makeCompound(solids)


def bolt_z(x,y,z,length=5,diameter=2):
    return R(diameter,0,length,z).union(R(3.6,0,1.0,z-1.0)).translate((x,y,0))


def enclosure():
    # Preserve the core deck datum; trim one mm at each end and round unused
    # corners. The motor sets the left wall, not a cosmetic arbitrary box.
    outer_plan=rounded(104,98,100,9)
    inner_plan=rounded(98,92,100,6,z=-1)
    front_z=30+tan(radians(26))+DECK_RAISE_Z
    profile=[(-49,0),(49,0),(49,REAR_DECK_Z),(DECK_BREAK_Y,REAR_DECK_Z),(-49,front_z)]
    wedge=cq.Workplane("YZ").polyline(profile).close().extrude(60,both=True)
    # Intersect the two inward-offset roof planes, preserving 3 mm normal
    # thickness on both planes instead of guessing the inner break position.
    inner_break_y=DECK_BREAK_Y+(3/core.COS_A-3)/tan(radians(26))
    inner_profile=[(-46,-1),(46,-1),(46,REAR_DECK_Z-3),
        (inner_break_y,REAR_DECK_Z-3),(-46,30+4*tan(radians(26))-3/core.COS_A+DECK_RAISE_Z)]
    inner_wedge=cq.Workplane("YZ").polyline(inner_profile).close().extrude(55,both=True)
    cavity=inner_plan.intersect(inner_wedge)
    shell=outer_plan.intersect(wedge).cut(cavity)
    shell=shell.cut(O(R(68.8,0,20,-10)))
    # A rear-left service lobe of board C is under an opaque deck pocket.
    shell=shell.cut(O(B(14,14,5,-27,-21,-5.8)))
    # Actual cap shafts and flange pockets; two buttons on each side.
    for side in (-1,1):
        for y in SIDE_BUTTON_Y:
            shell=shell.cut(B(7,11.4,4.4,side*50,y,SIDE_BUTTON_Z-.2))
            shell=shell.cut(B(3.1,13,6,side*48,y,SIDE_BUTTON_Z-1))
        # Removable two-switch carriers install after the caps. Their upper
        # faces seat against these two shelves, before the motor is installed.
        for y in (1.,30.):
            shell=shell.union(B(6,5,3,side*46.5,y,52.4))
            shell=shell.cut(R(1.7,0,3,52.4).translate((side*46.5,y,0)))
    shell=shell.cut(B(12,8,5,0,48,44.7))
    shell=shell.cut(cq.Solid.makeCylinder(3.8,8,cq.Vector(20,44,53),cq.Vector(0,1,0)))
    for x in (-21,-17,-13):
        shell=shell.cut(B(1.2,8,7,x,48,8))
    # Screw bosses join the wall and are outside both the battery allowance and
    # press mechanism; the floor is removable.
    for x,y in ((-45,-39),(43,-39),(-46,32),(44,41)):
        boss=R(7,2.4,7,2).translate((x,y,0))
        side=1 if x>0 else -1
        shell=shell.union(boss).union(B(6,7,7,side*47,y,2))
    for x,y in ((-45,-39),(43,-39),(-46,32),(44,41)):
        shell=shell.cut(R(2.4,0,10,0).translate((x,y,0)))
        shell=shell.cut(R(3.5,0,6,3).translate((x,y,0)))
        insert=f'cover_M2_insert_{x}_{y}'
        screw=f'cover_screw_{x}_{y}'
        add(insert,R(3.5,1.6,4,3).translate((x,y,0)),"metal",status='M2 brass insert OD3.5 x4; fit and pullout require printed boss coupon')
        thread_pair(insert,screw)
    add("housing_104x98_rounded",shell,"housing")
    floor=rounded(97.4,91.4,2,5.7)
    for x,y in ((-45,-39),(43,-39),(-46,32),(44,41)):
        floor=floor.cut(R(2.7,0,4,-1).translate((x,y,0)))
        seat=cq.Solid.makeCone(2.0,1.0,1,cq.Vector(x,y,0))
        floor=floor.cut(seat)
        screw=seat.fuse(_shape(R(2,0,5,1).translate((x,y,0))))
        add(f"cover_screw_{x}_{y}",screw,"metal",status='M2x6 countersunk envelope; flush underside; engages brass insert 3 mm')
    add("bottom_cover",floor,"base")
    for i,(x,y) in enumerate(((-36,-41),(35,-41),(-38,35),(38,39)),1):
        add(f"silicone_foot_{i}",R(8,0,1.2,-1.2).translate((x,y,0)),"base")
    return cavity


def core_parts():
    moving,fixed,local,display_world,display_local=core.build()
    for name in ("rotary_output_shaft","rotating_ring","rotating_arm_spider",
                 "motor_pulley","driven_pulley","motor_pulley_hub","belt_envelope",
                 "bearing_6901_1","bearing_6901_2"):
        group="rotating" if name in ("rotating_ring","rotary_output_shaft","rotating_arm_spider") else "moving"
        shape=moving[name]
        if name=="bearing_6901_2":
            shape=core.press_shift(shape,2.0)
        if name=="bearing_6901_1":
            shape=core.press_shift(shape,3.0)
        if name=="rotary_output_shaft":
            shape=O(R(12,8.8,35.7,-31.2).union(R(16,8.8,1.2,-31.2)))
        if name=="rotating_arm_spider":
            spider=R(20,12.1,3,-.2)
            for angle in (0,120,240):
                arm=B(17,5,2,17.5,0,1.3).rotate((0,0,0),(0,0,1),angle)
                x,y=24*cos(radians(angle)),24*sin(radians(angle))
                spider=spider.union(arm).cut(R(2.4,0,3,1).translate((x,y,0)))
                sn=f'grip_to_spider_M2x5_{angle}'
                screw=R(2,0,5,1.3).union(R(3.8,0,1.2,.1)).translate((x,y,0))
                add(sn,O(screw),'rotating',status='three bottom-access screws into grip; assemble before stationary screen')
                thread_pair(sn,'rotating_ring')
            shape=O(spider)
        if name=="driven_pulley":
            shape=O(R(core.PITCH_DIAMETER+2,12,6,-9))
        add(name,shape,group,status="purchased envelope" if "bearing" in name or "pulley" in name or "belt" in name else "design geometry")
    # A through-bored cartridge replaces two separate seats on tall thin ribs.
    # The lower bearing seats on an integral shoulder. A 7 mm outer-race spacer
    # retains the original 13 mm centre span; one accessible top ring closes it.
    cartridge=R(30,14,28.5,-39.5).union(R(36,18,4,-15))
    cartridge=cartridge.cut(R(18,0,27.5,-38))
    cartridge=cartridge.cut(R(24,0,20,-30))
    cartridge=cartridge.union(R(18,14,1.2,-40.7))
    cartridge=cartridge.union(R(36,24.2,.05,-11))
    for i,(x,y) in enumerate(CARTRIDGE_BOLTS,1):
        cartridge=cartridge.cut(R(2.5,0,6,-39.5).translate((x,y,0)))
        screw=R(3,0,8,-42.5).union(R(5.5,0,2,-44.5)).translate((x,y,0))
        add(f'cartridge_base_M3x8_{i}',O(screw),'moving',status='M3x8 envelope; pilot holes denote intentional threaded engagement')
    # Outer races have a distinct capture chain and 0.05 mm nominal float.
    # The shaft nut clamps the INNER-race stack only. Matched spacer lengths
    # and actual bearing face offsets still require metrology before machining.
    cap=R(36,21,.6,-10.95)
    for i,y in enumerate((-16.,16.),1):
        cap=cap.cut(R(2.4,0,2,-11.5).translate((0,y,0)))
        cartridge=cartridge.cut(R(1.6,0,3.65,-14.6).translate((0,y,0)))
        screw=R(2,0,4,-14.35).union(R(3.6,0,1,-10.35)).translate((0,y,0))
        add(f'cartridge_cap_M2x4_{i}',O(screw),'moving',status='M2x4 envelope; pilot holes denote intentional threaded engagement')
    add('turned_bearing_cartridge',O(cartridge),'moving',status='turned 6061 cartridge; common through-bore for both 6901 bearings, fit specification pending')
    add('bearing_outer_race_spacer_7mm',O(R(23.8,20.5,7,-24)),'moving',status='turned outer-race spacer, axial tolerance and preload require actual bearings')
    add('bearing_top_retainer_0p6mm',O(cap),'moving',status='0.6 mm sheet ring, two M2 fasteners; fit sample is fragile')
    add('bearing_inner_race_spacer_7mm',O(R(16,12.1,7,-24)),'rotating',status='inner-race spacer; pair and measure against 7 mm outer spacer, not an assumed preload')
    add("upper_shaft_inner_race_spacer",O(R(16,12.1,1.3,-11)),"rotating",status='1.3 mm turned/ground inner-race spacer; separate from 0.6 mm outer retainer')
    nut=R(18,11.2,1.7,2.8)
    for a in (0,180):
        nut=nut.cut(B(1.5,2,2,8.5,0,2.7).rotate((0,0,0),(0,0,1),a))
    add('output_M12x0p75_slotted_locknut',O(nut),'rotating',status='thin M12x0.75 locknut nominal thread envelope; shaft male thread n2.8..4.5; clamp torque and retention test pending')
    thread_pair('output_M12x0p75_slotted_locknut','rotary_output_shaft')
    add("GL30_with_factory_encoder_E",moving["motor_official"],"motor",status="official supplier STEP")
    # A shallow plate can be machined from flat stock without the R6 31.5 mm
    # tower. Encoder relief and the guide pad remain shallow face features.
    yoke=B(78,34,2,-10,0,-42.5).cut(R(18,0,4,-42.7))
    # Increase the load-bearing region without moving into the encoder's
    # rear face at n=-40.5. Its installation pocket retains the original depth.
    reinforcement=B(78,34,1,-10,0,-40.5).cut(R(18,0,3,-41))
    reinforcement=reinforcement.cut(R(36.4,0,3,-41).translate((-42,0,0)))
    yoke=yoke.union(reinforcement)
    yoke=yoke.cut(R(18.2,0,5,-43))
    for x,y in CARTRIDGE_BOLTS:
        yoke=yoke.cut(R(3.4,0,5,-43).translate((x,y,0)))
    yoke=yoke.union(B(12,29,3,19,14.5,-42.5))
    from r8_motor_module import create_motor_module
    motor_parts,motor_meta,motor_cut,motor_details=create_motor_module(PARTS['GL30_with_factory_encoder_E'])
    yoke=yoke.cut(cq.Workplane('XY').add(motor_cut))
    for i,y in enumerate((-15.,15.),1):
        yoke=yoke.cut(R(2.1,0,3.1,-42.5).translate((-23.5,y,0)))
    pieces=sorted(_shape(yoke).Solids(),key=lambda s:abs(s.Volume()),reverse=True)
    # The new through-opening leaves five disconnected remnants of the old
    # encoder pocket. They are offcuts, not structural parts or mounting lands.
    assert all(s.BoundingBox().xmax < -27.5 for s in pieces[1:])
    REFINEMENT['removed_motor_pocket_offcuts_mm3']=[abs(s.Volume()) for s in pieces[1:]]
    yoke=cq.Workplane('XY').add(pieces[0])
    for name,part in motor_parts.items():
        info=motor_meta[name]
        add(name,O(part),'moving',status=info['status'])
    motor_details['tension_moving_names']=[n for n in motor_parts if not n.startswith('motor_slide_lock_screw_')]
    REFINEMENT['motor_module']=motor_details
    add('press_base_plate',O(yoke),'moving',status='3 mm load plate with intact tapped motor lock lands and swept backplate/ear opening')
    # MGN7C principal mounting face is x=17, not the block end at n=-38.
    # Reference HIWIN hole map: y28/40, n-30.75/-22.75, M2 depth2.5.
    rail=B(4.8,7,35,22.6,34,-44)
    block=B(8,17,22.5,21,34,-38).cut(B(7,7.4,23,23.5,34,-38.25))
    adapter=B(2,17,26,16,34,-39.5)
    adapter=adapter.union(B(12,6,2,19,22.5,-39.5))
    adapter=adapter.union(B(4,4,5,15,25.5,-39.5))
    adapter=adapter.union(B(4,4,2,17,35,-15.5))
    for i,(yy,nn) in enumerate([(y,n) for y in (28.,40.) for n in (-30.75,-22.75)],1):
        adapter=adapter.cut(cylinder_x(2.4,3,14.5,yy,nn))
        block=block.cut(cylinder_x(1.6,2.5,17,yy,nn))
        screw=cylinder_x(2,4,15,yy,nn).fuse(cylinder_x(3.6,1.2,13.8,yy,nn))
        name=f'guide_carriage_M2x4_{i}'
        add(name,O(screw),'moving',status='reference MGN7C side mounting face; 2 mm engagement, confirm purchased block')
        thread_pair(name,'MGN7C_carriage_envelope')
    yoke=local_shape(PARTS['press_base_plate'])
    for i,x in enumerate((18.,23.5),1):
        adapter=adapter.cut(R(2.4,0,4,-40).translate((x,21.5,0)))
        yoke=yoke.cut(_shape(R(1.6,0,3.1,-42.5).translate((x,21.5,0))))
        screw=R(2,0,5,-42.5).union(R(3.6,0,1.2,-37.5)).translate((x,21.5,0))
        name=f'guide_adapter_to_plate_M2x5_{i}'
        add(name,O(screw),'moving',status='2 mm adapter foot + 3 mm tapped metal plate; print fit only')
        thread_pair(name,'press_base_plate')
    # The small switch-actuator root is part of this same moving bracket.
    adapter=adapter.union(B(8,6.4,2,17,22.3,-28.2))
    for x in (15.,19.):
        adapter=adapter.cut(R(1.6,0,2,-28.2).translate((x,22.5,0)))
    add('carriage_to_plate_L_adapter',O(adapter),'moving',status='one L adapter, four screws on true carriage mounting face, two plate screws; metal strength pending')
    for x,y in CARTRIDGE_BOLTS:
        yoke=yoke.cut(_shape(R(3.4,0,5,-43).translate((x,y,0))))
    add('press_base_plate',O(yoke),'moving',status='flat load plate with two M2 adapter taps; no end-cap fastening')
    # Extend only the back by 2.5 mm to the existing foot's X=44.5 plane.
    # The guide datum is unchanged; the broad coplanar back can lie on the bed.
    support=B(3,6.6,39,26.5,34,-44).union(B(5.5,14,39,30.75,34,-44))
    support=support.union(B(21.5,6,3,22.75,35,-8))
    support=support.cut(cylinder_x(1.6,3.1,25,34,-34)).cut(cylinder_x(1.6,3.1,25,34,-19))
    for i,n in enumerate((-34.,-19.),1):
        rail=rail.cut(cylinder_x(2.4,5,20.1,34,n)).cut(cylinder_x(3.9,1.3,20.1,34,n))
        screw=cylinder_x(2,6,21.4,34,n).fuse(cylinder_x(3.8,1.2,20.2,34,n))
        name=f'guide_rail_M2x6_{i}'
        add(name,O(screw),'fixed_core',status='rail cut target35 mm, holes10/25 from end; confirm SKU and head/counterbore before cutting')
        thread_pair(name,'fixed_rail_spine_and_floor_foot')
    support=support.cut(R(2.5,0,3,-8).translate((18,35,0)))
    add('release_stop_M3x10_grub',O(R(3,0,10,-13.5).translate((18,35,0))),'fixed_core',status='adjustable release datum and pull-up retention; recessed hex access through jam nut, tip seats L adapter')
    add('release_stop_M3_jam_nut',O(R(5.5,2.5,2.4,-5).translate((18,35,0))),'fixed_core',status='lock adjustment only after spring installation; no load rating claim')
    thread_pair('release_stop_M3x10_grub','fixed_rail_spine_and_floor_foot')
    thread_pair('release_stop_M3x10_grub','release_stop_M3_jam_nut')
    add("MGN7_rail_envelope",O(rail),"fixed_core",status="reference dimensional envelope with actual mounting-face direction; purchased rail SKU pending")
    add("MGN7C_carriage_envelope",O(block),"moving",status="reference 12x8 M2 map; rolling internals not represented")
    # Raise the foot top from Z24 to Z28 to close the V-shaped neck gap.
    # Keep the upper side-key relief; that separate pocket cannot be filled.
    foot=B(10,12,24.5,40,39,3.5)
    support=_shape(O(support)).intersect(_shape(B(100,91,100)))
    support=support.fuse(_shape(foot))
    support=support.cut(_shape(R(8.2,0,9.3,0).translate((44,41,0))))
    # One shallow side-open relief clears the retained side-key beam and its
    # housing shelf. It removes added back material only, not the guide datum.
    support=support.cut(_shape(B(3,15.2,11.9,43.5,25.6,44)))
    # Recess only the top inside edge of the floor foot. Its full-width lower
    # M3 thread lands remain, while rail, carriage and yoke clear the transition.
    support=support.cut(_shape(B(1.55,14,7.2,35.575,38.5,21.3)))
    backplate=B(12,14,1.5,40,38,2).cut(R(8.2,0,2,2).translate((44,41,0)))
    # Open the rear outer corner so the cover-boss relief leaves no tiny,
    # disconnected crescent in the laser-cut/printed backplate.
    backplate=backplate.cut(B(2,1,2,45.5,45,1.8))
    floor=PARTS['bottom_cover']
    for i,(x,y) in enumerate(((37.5,35.),(37.5,42.)),1):
        support=support.cut(_shape(R(2.5,0,5,3.5).translate((x,y,0))))
        backplate=backplate.cut(R(3.4,0,2,2).translate((x,y,0)))
        seat=cq.Solid.makeCone(3.1,1.5,1.7,cq.Vector(x,y,0))
        floor=floor.cut(seat).cut(_shape(R(3.4,0,3,0).translate((x,y,0))))
        sn=f'guide_floor_M3x8_{i}'
        add(sn,seat.fuse(_shape(R(3,0,6.3,1.7).translate((x,y,0)))),'metal',status='countersunk through floor and backplate, 4.5 mm pilot engagement in guide foot')
        thread_pair(sn,'fixed_rail_spine_and_floor_foot')
    add('guide_floor_backplate_1p5mm',backplate,'fixed_core',status='one flat metal load-spreading plate; printable fit dummy, not stiffness proof')
    add('bottom_cover',floor,'base')
    add("fixed_rail_spine_and_floor_foot",support,"fixed_core",status='one-piece guide with closed neck and flat outer back; original guide datum and side-key relief retained')
    # Straight 8 x 6 tube replaces machining/printing a long inclined bore with
    # an integral foot. Two short, open clamp jaws are printed on their sides.
    post=R(8,6,58.7,-50.6)
    foot=B(22,22,13,11,11,2).cut(O(B(200,200,100,0,0,-48.4)))
    foot=_shape(foot).fuse(_shape(O(R(16,0,5,-48.4))))
    ear=B(22,4.8,10,11,15.8,6.5).cut(O(B(200,200,100,0,0,-43.4)))
    foot=foot.fuse(_shape(ear)).intersect(_shape(B(100,18.2,100,11,9.1,0)))
    foot=foot.cut(_shape(O(R(8.2,0,70,-60))))
    tie=cq.Solid.makeCylinder(1.7,30,cq.Vector(-4,15.8,10.5),cq.Vector(1,0,0))
    foot=foot.cut(tie)
    for x in (3.,19.):
        foot=foot.cut(_shape(R(1.6,0,3.2,2).translate((x,12,0))))
    for label,xc,shift in (('left',-39.15,.1),('right',61.15,-.1)):
        jaw=foot.intersect(_shape(B(100,100,100,xc,0,-1))).translate((shift,0,0)).clean()
        add(f'fixed_screen_clamp_{label}',jaw,'fixed_core',status='short split printed clamp; nominal 0.1 mm closure per jaw around 8 mm tube')
    add('fixed_screen_tube_8x6',O(post),'fixed_core',status='straight OD8 ID6 L58.7 metal tube; upper n4.7..8.1 M8x0.5 nominal thread, thin-wall strength and tool access require sample')
    shaft=cq.Solid.makeCylinder(1.5,25,cq.Vector(.1,15.8,10.5),cq.Vector(1,0,0))
    head=cq.Solid.makeCylinder(2.75,2,cq.Vector(-1.9,15.8,10.5),cq.Vector(1,0,0))
    nut=cq.Solid.makeCylinder(3.2,2.4,cq.Vector(21.9,15.8,10.5),cq.Vector(1,0,0)).cut(shaft)
    add('screen_clamp_M3x25',shaft.fuse(head),'metal',status='cross-bolt envelope; printed jaws close before floor screws tighten')
    add('screen_clamp_M3_nut',nut,'metal',status='nut envelope; removable and accessible at side of foot')
    for i,x in enumerate((3.,19.),1):
        add(f'screen_foot_M2x5_{i}',bolt_z(x,12,0,5),'metal',status='floor mounting; 2.4 clearance in floor and 1.6 pilot in jaw')
    tray=core.fixed_screen_tray()
    # R3 extruded solid bosses through the supplier board. Keep arms, then use
    # short hollow spacers under the real module's three mounting feet.
    tray=tray.intersect(B(100,100,.6,0,0,6.5))
    tray=tray.union(R(12,8.2,1.2,5.9))
    for x,y in core.SCREEN_HOLES:
        tray=tray.union(R(3.4,2.2,1.3,7.1).translate((x,y,0)))
    tray=tray.cut(R(8.2,0,3,5.5))
    add("internal_screen_tray_and_three_spacers",O(tray),"fixed_core",status='screen tray captured between two M8 round lockrings; module fasteners separate; clamp friction holds orientation')
    for i,(x,y) in enumerate(core.SCREEN_HOLES,1):
        screw=R(2,0,5,6.5).union(R(3.4,0,1,5.5)).translate((x,y,0))
        add(f'display_module_M2x5_{i}',O(screw),'fixed_core',status='official Waveshare 2D labels 3-M2.00; candidate5 mm under-head,1.9 mm tray/spacer stack,3.1 mm nominal thread entry; actual depth must be checked')
        thread_pair(f'display_module_M2x5_{i}','D_Waveshare_complete_module_supplier_valid_solids')
    for label,n in (('lower',4.9),('upper',7.1)):
        nut=R(10,7.45,1,n)
        for a in (0,180):
            nut=nut.cut(B(1.3,1.4,1.2,4.8,0,n-.1).rotate((0,0,0),(0,0,1),a))
        sn=f'screen_tube_M8x0p5_{label}_lockring'
        add(sn,O(nut),'fixed_core',status='OD10 thin slotted ring, captures tray on threaded tube; tool and clamp retention test pending')
        thread_pair(sn,'fixed_screen_tube_8x6')
    # Retain valid supplier solids individually. The official disk at local
    # Y=0..1.3 is the outer screen; PCB components extend toward negative Y.
    valid=[];invalid=[]
    for i,s in enumerate(_shape(display_local).Solids()):
        if s.isValid():valid.append(s)
        else:invalid.append(i)
    display=cq.Compound.makeCompound(valid).rotate((0,0,0),(1,0,0),90).translate((0,0,17.1))
    add("D_Waveshare_complete_module_supplier_valid_solids",O(display),"display",status="supplier solids; invalid tiny solids separately logged")
    # Eight exact support directions enclose the XY shape in an octagon. Its
    # circumcircle is a proven conservative bound, unlike a guessed diameter.
    supports=[]
    for angle in range(0,360,45):
        supports.append(display.rotate((0,0,0),(0,0,1),angle).BoundingBox().xmax)
    bound_radius=max(supports)/cos(radians(22.5))
    db=display.BoundingBox()
    KEEPOUTS["display_supplier_full_radial_envelope"]=O(R(2*bound_radius,0,db.zlen,db.zmin))
    # The supplier module already contains its front glass. No second floating
    # glass disk or independent floating bezel is included in the physical BOM.
    # Continuous optical window over twenty-four real LED package envelopes.
    add("continuous_low_light_diffuser",O(R(68.4,56.8,2.4,0)),"PCB_C",color=(.3,.77,.91,.62))
    add("light_inner_baffle",O(R(58,57.2,.8,-1)),"fixed_core",color=(.04,.06,.075,1))
    return {"invalid_display_solid_indices":invalid,"display_valid_solids":len(valid),
            "display_eight_support_mm":supports,"display_proven_radius_bound_mm":bound_radius}


def refine_assembly():
    """Restore the selected grip and turn fixed mounting islands into real parts."""
    grip=(cq.Workplane('XZ').moveTo(21.5,3.3).lineTo(28,3.3).lineTo(28,6.5)
        .bezier([(28,8.5),(26.8,11.5),(26.8,13.5)],includeCurrent=True)
        .bezier([(26.8,15.5),(27.2,16.5),(27.2,18.2)],includeCurrent=True)
        .bezier([(27.2,19),(27.1,19.7),(26.8,19.7)],includeCurrent=True)
        .lineTo(21.5,19.7).close().revolve(360,(0,0),(0,1)))
    grip=grip.union(R(56,53.6,2.7,.6))
    grip=grip.union(R(44,39.2,.8,18.9))
    for a in (0,120,240):
        x,y=24*cos(radians(a)),24*sin(radians(a))
        grip=grip.cut(R(1.6,0,3.2,3.3).translate((x,y,0)))
    add('rotating_ring',O(grip),'rotating',status='preserved waisted outside; integral upper lip replaces floating bezel; three blind M2 mounting pilots; blue indication belongs to LEDs')
    # A simple opal annulus drops into the 1.6 mm window from above. Leave
    # 0.1 mm radial clearance on both sides: a wide concealed flange would
    # trap this ring behind the integral inner cover during assembly.
    diffuser=R(67.0,64.2,.9,1.4)
    add('continuous_low_light_diffuser',O(diffuser),'PCB_C',color=(.64,.86,.89,1),status='single opal light guide; 1.6 mm visible radial window; optical sample pending')
    holder=R(69.6,67.2,5.8,-3.5)
    holder=holder.union(R(64,56.8,1.1,1.2)).union(R(58.4,56.8,3.3,-1))
    # Three hidden bridges join the masks and seat the drop-in light ring.
    for angle in (45,165,285):
        bridge=B(5.7,1.2,1.4,32,0,0).rotate((0,0,0),(0,0,1),angle)
        holder=holder.union(bridge)
    holder=holder.cut(B(14,14,2.8,-27,-21,-3.6))
    # Board C installs from the bottom against three upper stops. The former
    # lower shelf ears would trap the PCB beneath the now integral light mask.
    # Mount axes lie between LEDs, outside the turning skirt and away from the
    # offset pulley. These are small M1.6 fixing interfaces, not load stops.
    c_board=cq.Workplane('XY').add(LOCAL_BOARDS['C'])
    mounts=[]
    for index,angle in enumerate((52.5,142.5,262.5),1):
        x,y=30.3*cos(radians(angle)),30.3*sin(radians(angle))
        mounts.append([x,y])
        boss=R(3.2,0,3.3,-1).translate((x,y,0))
        holder=holder.union(boss)
        holder=holder.cut(R(1.3,0,2.9,-1).translate((x,y,0)))
        c_board=c_board.cut(R(1.8,0,2,-.4).translate((x,y,0)))
        screw=R(1.6,0,4,-2.2).union(R(2.8,0,1,-3.2)).translate((x,y,0))
        add(f'light_board_mount_screw_{index}',O(screw),'metal',status='M1.6x4 envelope; pilot bore represents intentional thread engagement')
    LOCAL_BOARDS['C']=_shape(c_board)
    add('PCB_C_24LED_annulus_service_lobe',O(c_board.translate((0,0,-2.2))),'PCB_C',status='mechanical outline with three bottom-access M1.6 fixing holes; no copper routing')
    add('continuous_low_light_diffuser',O(diffuser),'PCB_C',color=(.64,.86,.89,1),status='continuous opal guide above hidden bridges; optical uniformity pending')
    PARTS.pop('light_inner_baffle');META.pop('light_inner_baffle')
    # Preserve the holder separately for focused clearance evidence before
    # it is fused into the external housing below.
    KEEPOUTS['optical_holder_before_housing_union']=O(holder)
    # The right key carrier runs beside the fixed optical frame. A local
    # clearance pocket preserves both pieces and keeps its fasteners outside
    # the frame. This is also clear of the knob's continuous rotating sweep.
    carrier_optical_keepout=_shape(O(R(70.2,0,6.4,-3.8)))
    for side in ('left','right'):
        name=f'switch_{side}_pair_removable_carrier'
        PARTS[name]=PARTS[name].cut(carrier_optical_keepout).clean()

    # Explicit flange reserves replace the unrealistically narrow R4 cylinders.
    # The 6 mm belt remains short; profiles and bore interfaces need matching
    # supplier drawings rather than an invented 2GT tooth cutter.
    pulley=R(core.PITCH_DIAMETER+2,12.1,6,-8.9)
    pulley=pulley.union(R(27,12.1,.8,-9.7)).union(R(27,12.1,.8,-2.9))
    pulley=pulley.union(R(18,12.1,1.9,-2.1))
    add('driven_pulley',O(pulley),'rotating',status='36T/2GT envelope with integral inner-stack hub; axial face clamp transmits torque; teeth/profile and slip torque require matching supplier')
    add('belt_envelope',O(core.belt_envelope(-42,0).translate((0,0,.1))),'moving',status='156 mm pitch length / 6 mm width; belt plane raised 0.6 mm; matched tooth system pending')

    # Actual connected unions reduce independently positioned stationary parts.
    PARTS.pop('C_board_three_fixed_shelves');META.pop('C_board_three_fixed_shelves')
    housing_members=['housing_104x98_rounded']
    housing=_shape(O(holder))
    for name in housing_members:
        housing=housing.fuse(PARTS[name])
    # Recess the motor flange under the deck, including its full setup travel.
    # The roof above n=-2.35 and the visible light window stay intact.
    pocket=cq.Workplane('XY').center(-42,0).slot2D(29.2,28.2).extrude(12).translate((0,0,-12.35))
    housing=housing.cut(_shape(O(pocket)))
    # Shallow underside tunnel keeps the raised belt clear of the fixed light
    # mask. Its top n=-2.5 stays 0.3 below board C; the optical top is intact.
    tunnel=B(42,26.5,8,-21,0,-10.5)
    for x in (-42,0):
        tunnel=tunnel.union(R(26.5,0,8,-10.5).translate((x,0,0)))
    housing=housing.cut(_shape(O(tunnel)))
    # A surface transfer/print keeps the small mark without fine engraving
    # tools or a shallow recess that will be lost when a printed shell is sanded.
    wordmark=cq.Workplane('XY').text('GL30',3.4,.02,font='Arial',kind='bold',combine=False)
    wordmark=_shape(wordmark.translate((-28,36,REAR_DECK_Z)))
    add('GL30_surface_mark',wordmark,'marking',status='0.02 mm visual ink layer; omit from solid manufacturing geometry')
    housing=housing.clean()
    for name in housing_members:
        if name!='housing_104x98_rounded':PARTS.pop(name);META.pop(name)
    add('housing_104x98_rounded',housing,'housing',status='one-piece printed shell with key pockets, removable-carrier mounting shelves, LED stops and opaque labyrinth')
    floor=PARTS['bottom_cover']
    floor_members=[n for n in PARTS if n.startswith('spring_and_stop_floor_pedestal_')]
    for name in floor_members:
        floor=floor.fuse(PARTS.pop(name));META.pop(name)
    shoes=PARTS.pop('battery_end_shoes_and_cushion');META.pop('battery_end_shoes_and_cushion')
    for solid in shoes.Solids():
        if solid.BoundingBox().zlen<.6:
            add('battery_soft_cushion',solid,'base',color=(.22,.24,.26,1),status='separate soft insulating pad; do not print as a rigid clamp')
        else:
            floor=floor.fuse(solid)
    for x,y in ((3.,12.),(19.,12.),(-39.,43.),(20.,42.)):
        floor=floor.cut(_shape(R(2.4,0,4,-1).translate((x,y,0))))
    add('bottom_cover',floor.clean(),'base',status='one-piece floor with spring pedestals, battery locators and accessible support screws')
    REFINEMENT.update({'version':'R8','guide_foot_height_mm':24.5,'guide_foot_top_world_z_mm':28,'guide_neck_gap_closed':True,'profile_reference':'compact_knob.py R2 outside profile, normal offset -6.5 mm',
        'nominal_belt_pitch_length_mm':156,'belt_pitch_mm':2,'teeth_each':36,'ratio':1,
        'belt_width_mm':6,'centre_distance_setup_range_mm':[41.5,42.5],
        'tension_setting':'slide motor mount, measure tension, tighten two clamp screws; no spring idler',
        'yoke_main_plate_mm':3,'encoder_pocket_plate_mm':2,'base_plate_max_thickness_mm':4.5,
        'bearing_cartridge_bore_mm':24,'bearing_cartridge_spigot_mm':[18,1.2],
        'bearing_cartridge_plate_locating_bore_mm':18.2,'bearing_cartridge_fasteners':'3 x M3x8 at 12 mm pitch radius',
        'screen_tube_stock_mm':[8,6,58.7],'screen_clamp_jaws':2,
        'PCB_support_post_height_mm':25,'PCB_flat_tray_max_height_mm':2.2,
        'bearing_centre_span_mm':13,'R4_bearing_centre_span_mm':11,
        'optical_window_radial_width_mm':1.6,'moving_skirt_radial_gap_mm':.4,
        'light_guide_radial_width_mm':1.4,'light_guide_drop_in_radial_clearance_mm':.1,
        'deck_raise_normal_mm':DECK_RAISE_NORMAL,'deck_raise_vertical_mm':DECK_RAISE_Z,
        'rear_deck_height_mm':REAR_DECK_Z,'diffuser_top_normal_mm':2.3,
        'deck_break_y_mm':DECK_BREAK_Y,'nominal_roof_normal_thickness_mm':3,
        'side_button_centres_y_mm':SIDE_BUTTON_Y,'side_button_cap_bottom_z_mm':SIDE_BUTTON_Z,
        'side_button_face_recess_mm':.7,'side_button_free_gap_mm':SIDE_KEY_GAP,'side_button_travel_mm':SIDE_KEY_STOP,
        'bearing_inner_spacer_mm':7,'bearing_outer_nominal_axial_float_mm':.05,
        'pulley_to_retainer_axial_gap_mm':.65,'belt_plane_raise_mm':BELT_RAISE,
        'guide_carriage_hole_map_local_y_n':[[y,n] for y in (28,40) for n in (-30.75,-22.75)],
        'switch_leaf_candidate':{'material':'SUS301 spring sheet, properties not certified','thickness_mm':.4,'free_length_mm':7,'width_mm':4,'gap_mm':.05,'approx_k_N_per_mm_assuming_E193GPa':36.0},
        'return_spring_candidate':{'OD_mm':4.4,'wire_mm':.35,'active_turns':6,'free_length_mm':8,'installed_length_mm':5.5,'full_press_length_mm':5.15,'precompression_mm':2.5,'rate_and_loads_verified':False},
        'removable_side_switch_carriers':2,'side_carrier_fasteners':'four M2x4 screws, 2.4 mm carrier clearance / 1.7 mm housing pilot',
        'side_key_assembly_order':'empty shell -> caps from inside -> two carriers with switches -> four screws -> internal motor/board modules',
        'top_wordmark':'GL30; 3.4 mm text, surface transfer/print after finishing; no fine machined recess',
        'light_board_mount_axes_mm':mounts,'light_board_mounting':'from bottom, three M1.6 screws; light guide inserts from top',
        'skirt_bottom_clearance_to_C_board_at_full_press_mm':1.25,
        'labyrinth_overlap_at_rest_mm':1.7,'labyrinth_overlap_at_full_press_mm':2.05,
        'housing_merged_prior_items':housing_members,
        'floor_merged_prior_items':['bottom_cover',*floor_members,'two battery end locators'],
        'motor_no_through_channel':'confirmed by user on 2026-09-11'})


def build_model():
    PARTS.clear();META.clear();KEEPOUTS.clear();LOCAL_BOARDS.clear();REFINEMENT.clear()
    THREAD_PAIRS.clear();WIRE_PATHS.clear();OVERLAP_SHAPES.clear()
    cavity=enclosure();source=core_parts();battery();electronics();buttons_and_press();wiring=wiring_and_aux()
    refine_assembly()
    return cavity,source,wiring


def delivery_parts():
    """Keep board outlines and interfaces; omit individual electronic packages."""
    result={}
    interfaces=('FH12','XT30','USB_C','NTC_PH','SKRPASE010','landing')
    for name,shape in PARTS.items():
        if name.startswith(('A_','B_','C_LED_','C_buffer_')) and not any(k in name for k in interfaces):
            continue
        if name=='D_Waveshare_complete_module_supplier_valid_solids':
            # Keep mechanical mounting bosses as well as the main layers.
            # Omitting these small parts would make the new M2 fasteners look
            # unsupported in the STEP even though the full source was checked.
            layers=[];bosses=[]
            for s in shape.Solids():
                if abs(s.Volume())>=300:
                    layers.append(s);continue
                bb=local_shape(s).BoundingBox()
                if abs(bb.zmin-8.4)<1e-4 and abs(bb.zmax-13.1)<1e-4 and any(
                    abs((bb.xmin+bb.xmax)/2-x)<1e-4 and abs((bb.ymin+bb.ymax)/2-y)<1e-4
                    for x,y in core.SCREEN_HOLES):bosses.append(s)
            assert len(bosses)==3, 'all three supplier mounting bosses must remain visible'
            shape=cq.Compound.makeCompound(layers+bosses)
        result[name]=shape
    return result


def battery():
    pack=rounded(74,24,21,1,-2,-32,3.5)
    add("F_custom_3S_battery_complete_pack_74x24x21",pack,"battery",
        status="custom pack envelope incl protection, NTC, insulation; capacity pending supplier")
    reserve=rounded(78,28,23.5,2,-2,-32,3)
    KEEPOUTS["battery_installation_and_expansion"]=reserve
    # End shoes locate the pack; nothing compresses its broad faces.
    shoes=B(80,27.5,5,-2,-32,2).cut(B(78,28,7,-2,-32,3))
    shoes=shoes.cut(B(68,40,8,-2,-32,1))
    for x,y in ((-45,-39),(43,-39)):
        shoes=shoes.cut(R(8.2,0,10,0).translate((x,y,0)))
    add("battery_end_shoes_and_cushion",shoes.union(B(72,22,.5,-2,-32,3)),"base")
    add("battery_protection_zone_label",B(11,21,.06,28,-32,24.5),"battery",color=(.48,.29,.08,1),status="visual mark of included BMS zone")
    strap=B(81.5,8,.6,-1.75,-32,27).union(B(81.5,8,.6,-1.75,-32,-.6))
    for x in (-42.5,39.):
        strap=strap.union(B(.6,8,28.2,x,-32,-.6))
        PARTS['bottom_cover']=PARTS['bottom_cover'].cut(_shape(B(1.2,8.8,3,x,-32,-.5)))
    add('battery_retaining_strap_envelope',strap,'soft',status='8 mm woven strap through floor slots; upper restraint outside expansion reserve; adjust slack using dummy, not cell compression; envelope only')


def board(name,shape,z):
    LOCAL_BOARDS[name]=_shape(shape)
    add(f"PCB_{name}_1p2mm_outline",shape.translate((0,0,z)),f"PCB_{name}",status="mechanical outline; no copper routing")


def chip(name,x,y,w,d,h,z,group="component"):
    add(name,B(w,d,h,x,y,z),group,status="component/package placement envelope")


def electronics():
    A=rounded(74,26,T,2,-9,32).cut(R(43,0,3,-.5).translate((-31,0,0)))
    # Open cable pass at the right rear; same stack posts for both boards.
    A=A.cut(B(7,12,3,27,41,-.5))
    A=A.cut(R(4.8,0,3,-.5).translate((-8,43.5,0)))
    Bboard=rounded(74,26,T,2,-9,32)
    A=A.cut(B(4,30,3,29.5,32,-.5))
    Bboard=Bboard.cut(B(4,30,3,29.5,32,-.5))
    for name in ('A','B'):
        if name=='A':A=A.cut(B(16,29,3,31.3,32.5,-.5))
        else:Bboard=Bboard.cut(B(16,29,3,31.3,32.5,-.5))
    for y in (24,39):
        Bboard=Bboard.cut(B(8,8.4,3,-46.5,y,-.5))
    holes=[(-39,23),(-39,43),(21,23),(20,42)]
    for x,y in holes:
        A=A.cut(R(2.4,0,3,-.5).translate((x,y,0)))
        Bboard=Bboard.cut(R(2.4,0,3,-.5).translate((x,y,0)))
        # Long rear posts reach the floor; front ones start on the same bridge.
    board("A",A,29.2);board("B",Bboard,44)
    # An open-front U outline avoids the press carriage and saddle with a
    # constant-depth cut, instead of copying their shapes into deep 3D pockets.
    tray=rounded(77,29,1.5,2,-9,32,27).cut(B(63,48,4,-9,16.5,26))
    tray=tray.cut(R(46,0,6,25).translate((-31,0,0)))
    for x,y in holes:
        tray=tray.union(R(5,2.4,2.2,27).translate((x,y,0)))
        tray=tray.cut(R(2.4,0,4,26).translate((x,y,0)))
        add(f"PCB_stack_spacer_{x}_{y}",R(5,2.4,13.6,30.4).translate((x,y,0)),"metal")
    # Independent straight 25 mm spacers replace two tall fused legs. The A/B
    # mounting planes remain 29.2/44 mm; the flat tray is only 2.2 mm tall.
    for x,y in ((-39,43),(20,42)):
        add(f'PCB_support_post_25mm_{x}_{y}',R(5,1.6,25,2).translate((x,y,0)),'base',status='straight M2 female standoff, 25 mm; print dummy or source metal spacer')
        add(f'PCB_support_bottom_M2x6_{x}_{y}',bolt_z(x,y,0,6),'metal',status='bottom mount into 25 mm post')
    for x,y in holes:
        rear=(x,y) in ((-39,43),(20,42))
        length=25 if rear else 20
        screw=R(2,0,length,45.2-length).union(R(3.6,0,1.5,45.2)).translate((x,y,0))
        add(f'PCB_stack_top_M2x{length}_{x}_{y}',screw,'metal',status='stack clamp envelope; below-board nut on front points')
        if not rear:
            add(f'PCB_front_M2_nut_{x}_{y}',R(4.6,2,1.6,25.4).translate((x,y,0)),'metal',status='nut envelope under tray; tool access from below')
    tray=tray.intersect(rounded(97.2,91.2,100,5.6))
    tray=tray.cut(B(20,100,5,37.6,0,26))
    tray=tray.cut(B(16,29,5,31.3,32.5,26))
    tray=tray.cut(R(4.8,0,4,26).translate((-8,43.5,0)))
    add("PCB_flat_support_tray",tray,"base",status='flat printed tray, 1.5 web and 0.7 mm pads; unchanged board datums')
    # Compact A: retain role-level package sizes plus board space for passives.
    for row in [
        ("A_STM32G474_LQFP48",-22,29,9,9,1.6),
        ("A_DRV8316R_VQFN40_7x5",-5,28,7,5,1.1),
        ("A_TLV1704_1_TSSOP14",-32,36,5,6.4,1.2),
        ("A_TLV1704_2_TSSOP14",-24,40,5,6.4,1.2),
        ("A_motor_bulk_cap_bank",7,35,10,8,6.5),
        ("A_SWD_header_2x5_1p27",-32,24,7,5,4),
        ("A_XT30_cable_landing",16,34,8,6,3),
        ("A_FH12_AB",-38,30,9.7,4.8,2),
        ("A_FH12_C",-7,23,9.7,4.8,2),
        ("A_crystal_and_passives",-15,39,6,5,1.2),
    ]:
        chip(*row,30.4)
    # B: inexpensive inductors include their actual larger package maxima.
    for row in [
        ("B_USB_C_16pin",0,43,9,9,3.2),
        ("B_HUSB238_DFN10",-1,34,3,3,1),
        ("B_BQ25798_QFN",-13,28,4,4,1),
        ("B_MWSA0603S_1R0",-24,29,6.6,7,3),
        ("B_MWSA0804S_100_logic",-36,30.2,8.2,8.8,4),
        ("B_MWSA0804S_100_LED",12,32,8.2,8.8,4),
        ("B_LMR33630_logic_HSOIC8",-23,39.5,6.2,5.2,1.8),
        ("B_LMR33630_LED_HSOIC8",11,40,6.2,5.2,1.8),
        ("B_TPS26630_VQFN24",-12,39,4,4,1.2),
        ("B_INA228_VSSOP",-12,23,3,5,1.2),
        ("B_LM74800_WSON",2,25,3,3,1),
        ("B_MOSFET_1_SON5x6",-5,28,5,6,1),
        ("B_MOSFET_2_SON5x6",4,33,5,6,1),
        ("B_MOSFET_3_SON5x6",20,34.5,5,6,1),
        ("B_shunt_2512",-19,23,6.4,3.2,.8),
        ("B_fuse_0451003_2410",-32.5,23,6.1,2.6,2.6),
        ("B_TVS_SMBJ18A_SMB",-26,23,6,4,2.5),
        ("B_FH12_AB",-35,37.5,9.7,4.8,2),
        ("B_NTC_PH_2pin",21,28.2,8,6,6),
    ]:
        chip(*row,45.2)
    # The second face uses existing stack clearance for the small safety/logic
    # ICs and capacitor banks. Their final copper/thermal placement is not frozen.
    for name,x,y,w,d,h in [
        ("B_TPD4E05U06_USB_ESD",8,41,3,2,1),
        ("B_TLV75533_SOT23_5",-3,39,3.2,3,1.3),
        ("B_TPS2557_VSON8",16,37.5,3,3,1),
        ("B_TPS7A1650_HVSSOP8",-8,24,5,5,1.2),
        ("B_UCC27517_SOT23_5",2,24,3.2,3,1.3),
        ("B_LM393B_SOIC8",-25,38,6.2,5.2,1.8),
        ("B_TLV431_SOT23_3",-16,40,3,3,1.2),
        ("B_input_capacitor_bank_reserve",-33,31,10,7,4),
        ("B_5V_capacitor_bank_reserve",8,33,10,7,4),
    ]:
        chip(name,x,y,w,d,h,44-h)
    C=R(66,58,T).union(B(12,12,T,-27,-21))
    LOCAL_BOARDS["C"]=_shape(C)
    add("PCB_C_24LED_annulus_service_lobe",O(C.translate((0,0,-2.2))),"PCB_C",status="mechanical outline; no copper routing")
    for i in range(24):
        angle=radians(i*15)
        led=B(2,2,.8,31*cos(angle),31*sin(angle),-1)
        add(f"C_LED_{i+1:02d}_WS2812B2020",O(led),"PCB_C",color=(.82,.90,.82,1),status="2x2x0.8 package envelope")
    add("C_FH12_connector_under_lobe",O(B(9.7,4.8,2,-27,-21,-4.2)),"connector",status="connector package envelope")
    add("C_buffer_and_passive_zone",O(B(6,3,1.2,-28,-25,-3.4)),"component",status="reserve")
    # The LED board seats on three fixed shelf ears outside the moving root.
    supports=None
    for angle in (45,145,265):
        a=radians(angle)
        s=B(7,5,1.3,34*cos(a),34*sin(a),-3.5)
        supports=s if supports is None else supports.union(s)
    add("C_board_three_fixed_shelves",O(supports),"housing")


def buttons_and_press():
    # The BOM's top-actuated 4.2x3.2x2.5 switches are physically turned sideways
    # in captured insulating carriers. For one unit their leads are wired to A;
    # there are no invisible extra rigid daughterboards or incompatible MPNs.
    for side,label in ((-1,"left"),(1,"right")):
        carriers=[]
        for idx,y in enumerate(SIDE_BUTTON_Y,1):
            cap=B(2.4,11,4,side*50.1,y,SIDE_BUTTON_Z).union(B(.6,12.4,5.4,side*49,y,SIDE_BUTTON_Z-.7))
            # Extend through the retaining flange so every cap/stem is one
            # printable solid. The inner tip retains 0.2 mm switch clearance.
            stem=B(1.8,2,1.5,side*48.15,y,SIDE_BUTTON_Z+1.25)
            add(f"button_{label}_{idx}_cap_and_stem",cap.union(stem),"button")
            # Outermost switch face |x|=47.05; tip |x|=47.25 at rest.
            sw=B(2,4.2,3.2,side*45.55,y,SIDE_BUTTON_Z+.4)
            add(f"switch_{label}_{idx}_SKRPASE010",sw,"component",status="4.2x3.2x2.5 overall including separate 0.5-high actuation zone; body/stem split is a kinematic envelope")
            add(f'side_{label}_{idx}_switch_plunger',B(.5,2,1.5,side*46.8,y,SIDE_BUTTON_Z+1.25),'button',status='visible plunger envelope, nominal compression0.2; not a measured force curve')
            carrier=B(4.8,7.4,6.4,side*45.4,y,SIDE_BUTTON_Z-1.2).cut(B(3,4.6,3.6,side*45.8,y,SIDE_BUTTON_Z+.2))
            carrier=carrier.cut(B(7,2.4,2,side*47,y,SIDE_BUTTON_Z+1))
            carrier=carrier.cut(B(1.8,2,3,side*44,y,SIDE_BUTTON_Z-2.2))
            carrier=carrier.cut(B(5.8,2,1.6,side*45.5,y,44.8))
            carriers.append(carrier)
        carrier=carriers[0].union(carriers[1]).union(B(6,34,1.6,side*45.5,15.5,50.8))
        for y in SIDE_BUTTON_Y:
            # Top-open stem entry allows the carrier to lift behind already
            # installed caps. The rear portion of the beam remains continuous.
            carrier=carrier.cut(B(3,2.4,6,side*48.1,y,48))
            # Relieve the FULL retaining flange, not only the thin stem.
            # Remaining beam face at |x|48.3 is the 0.40 mm inward hard stop.
            carrier=carrier.cut(B(2,13,1.3,side*49.3,y,50.7))
        for index,y in enumerate((1.,30.),1):
            carrier=carrier.cut(R(2.4,0,3,50).translate((side*46.5,y,0)))
            screw=R(2,0,4,50.8).union(R(3.6,0,1.5,49.3)).translate((side*46.5,y,0))
            add(f'side_carrier_{label}_M2x4_{index}',screw,'metal',status='M2x4 envelope; pilot bore represents intentional thread engagement')
        add(f'switch_{label}_pair_removable_carrier',carrier,'base',status='one removable carrier for two switches; two bottom-access M2x4 screws')
    # Rear power key, same physical switch turned toward the rear face.
    cap=cq.Solid.makeCylinder(3.5,2,cq.Vector(20,46.7,53),cq.Vector(0,1,0))
    stem=cq.Solid.makeCylinder(1,2,cq.Vector(20,44.7,53),cq.Vector(0,1,0))
    flange=cq.Solid.makeCylinder(4.5,.6,cq.Vector(20,46.1,53),cq.Vector(0,1,0))
    add("rear_power_button_cap",cap.fuse(stem).fuse(flange),"button",status='captured Ø9 flange behind stepped rear opening; nominal inward hard stop0.25')
    chip("rear_power_switch_SKRPASE010",20,43.15,4.2,2,3.2,51.4)
    add('rear_power_switch_plunger',B(2,.5,1.5,20,44.4,52.25),'button',status='0.2 mm nominal switch compression after0.05 gap')
    holder=B(7.4,5.8,6.4,20,43.8,49.8).cut(B(4.6,3,3.6,20,43.4,51.2))
    holder=holder.cut(B(2.4,7,2.4,20,45,51.8))
    holder=holder.cut(B(20,8,20,20,50,45))
    holder=holder.union(B(16,3.8,1.6,20,43.2,49.8))
    holder=holder.cut(B(1.2,3,1.2,20,41.5,52.4))
    holder=holder.cut(cq.Solid.makeCylinder(4.7,1,cq.Vector(20,45.85,53),cq.Vector(0,1,0)))
    housing=PARTS['housing_104x98_rounded']
    housing=housing.cut(cq.Solid.makeCylinder(4.7,.7,cq.Vector(20,46,53),cq.Vector(0,1,0)))
    for i,x in enumerate((14.,26.),1):
        shelf=B(4.5,7.5,4.5,x,44.2,51.4)
        shelf=shelf.cut(R(1.6,0,4.1,51.4).translate((x,43.2,0)))
        housing=housing.fuse(_shape(shelf))
        holder=holder.cut(R(2.4,0,3,49.2).translate((x,43.2,0)))
        sn=f'rear_switch_holder_M2x6_{i}'
        screw=R(2,0,6,49.8).union(R(3.6,0,1.2,48.6)).translate((x,43.2,0))
        add(sn,screw,'metal',status='bottom-access rear carrier screw; nominal4.4 mm pilot engagement')
        thread_pair(sn,'housing_104x98_rounded')
    housing=housing.cut(cq.Solid.makeCylinder(4.7,.85,cq.Vector(20,45.85,53),cq.Vector(0,1,0)))
    add('housing_104x98_rounded',housing,'housing')
    add("rear_power_switch_holder",holder,"base",status='removable captured switch holder; two real mounting shelves/screws and cap travel stop')
    chip("A_reset_switch_SKRPASE010",-39,36.8,4.2,3.2,2.5,30.4)
    # Switch axis now agrees with the press normal. The leaf is actually bolted
    # to the L adapter, and the switch seat is joined to the fixed spine.
    add('ring_press_switch_SKRPASE010',O(B(4.2,3.2,2,28,22.5,-28.75)),'component',status='same MPN, wired from fixed bracket to board A; body/stem partition is kinematic')
    add('ring_press_switch_plunger',O(B(2,1.5,.5,28,22.5,-26.75)),'button',status='normal-axis actuation,0.2 mm nominal compression')
    seat=B(5.5,9,2,30.75,24.5,-30.75).union(B(8,5,2,29,22.5,-30.75))
    frame=B(7.4,5.8,1.4,28,22.5,-28.75).cut(B(4.6,3.6,2,28,22.5,-28.8))
    seat=seat.union(frame)
    seat=seat.cut(B(2,3,1.8,28,20,-29.2))
    add('fixed_rail_spine_and_floor_foot',PARTS['fixed_rail_spine_and_floor_foot'].fuse(_shape(O(seat))),'fixed_core',status='closed guide spine with rail fasteners, floor fasteners, release stop and fixed press-switch seat')
    tab=B(17,4,.4,21.5,22.5,-26.2)
    for i,x in enumerate((15.,19.),1):
        tab=tab.cut(R(2.4,0,1,-26.5).translate((x,22.5,0)))
        sn=f'press_leaf_root_M2x3_{i}'
        screw=R(2,0,3,-28.8).union(R(3.6,0,1.2,-25.8)).translate((x,22.5,0))
        add(sn,O(screw),'moving',status='leaf root clamped to moving L adapter; effective free span7 mm to plunger center')
        thread_pair(sn,'carriage_to_plate_L_adapter')
    add("ring_press_compliant_actuator",O(tab),"moving",status="0.4 mm SUS301 candidate leaf, actual root fixing;0.05 gap +0.20 switch +elastic deflection within0.35 press; force curve unverified")
    # Three parallel return springs and independent hard stops, attached to the
    # floor, support the moving yoke; switches never serve as the structural stop.
    for i,(x,y) in enumerate(((-18,-3),(20,-3),(0,13)),1):
        n_floor=-48
        spring=R(4.4,3.7,5.5,n_floor).translate((x,y,0))
        add(f"return_spring_{i}_envelope",O(spring),"fixed_core",status="candidate OD4.4 wire0.35 free8 installed5.5; envelope changes length during press, actual rate/friction unknown")
        stop=R(3,0,5.15,n_floor).translate((x,y,0))
        add(f"press_stop_{i}_0p35mm",O(stop),"fixed_core")
        px,py,pz=core.world_point(x,y,n_floor)
        # A wedge top meets the spring's inclined bottom; a horizontal box top
        # would intrude into the spring and consume its working travel.
        pedestal=B(8,8,22,px,py,2).cut(O(B(200,200,100,0,0,n_floor)))
        add(f"spring_and_stop_floor_pedestal_{i}",pedestal,"base")
        cup=R(5.6,4.6,.7,-43.2).translate((x,y,0))
        PARTS['press_base_plate']=PARTS['press_base_plate'].fuse(_shape(O(cup)))


def wiring_and_aux():
    def route(name,points,radius,terminals=(),note=''):
        add(name,cable(points,radius),'wire',status=note or 'bundle envelope; actual cable and minimum bend radius require sample')
        WIRE_PATHS[name]={'points_world_mm':points,'radius_mm':radius,'terminal_parts':list(terminals)}
    # Every purchased interconnect family is visible, with unplugging space.
    for i,(x,y,z) in enumerate(((-2,42,16),(-20,42,19),(35,12,24)),1):
        add(f"XT30_inline_pair_{i}",B(14,8,6,x,y,z),"connector",status="mated plug + socket envelope; supplier exact model pending")
    power=[(35,-26,14),(42,-26,14),(42,24,14),(26,24,15.4),(-2,34,15.4),(-2,34,18),(-2,42,19)]
    route('battery_power_pair_route',power,1.4,('F_custom_3S_battery_complete_pack_74x24x21','XT30_inline_pair_1'))
    route('motor_bus_supply_pair_route',[(-20,42,22),(-16,43,24),(-8,43.5,24),(-8,43.5,35.5),(15.5,42.5,35.5),(16,34,35.5),(16,34,33.4)],1.4,('XT30_inline_pair_2','A_XT30_cable_landing'),'DC bus pair through dedicated rear-edge notch; not the motor phase leads')
    add('C_power_solder_landing',O(B(2.6,1.6,.1,-23.5,-25.5,-2.3)),'connector',status='reserved underside solder pads; board copper and actual wire termination unfrozen')
    route('LED_power_pair_route',[(35,12,30),(35,-24,31),core.world_point(-23.5,-25.5,-7),core.world_point(-23.5,-25.5,-2.3)],1.1,('XT30_inline_pair_3','C_power_solder_landing'),'front corridor above battery expansion space; normal approach to board-C pads')
    add('A_motor_UVW_landing',B(9,4,2,6.5,22,30.4),'connector',status='three-phase solder/terminal reserve on board A; final connector family pending')
    add('A_encoder_signal_landing',B(6,3,1.6,-20,22.5,30.4),'connector',status='encoder signal landing reserve; pin count must match purchased encoder')
    route('motor_UVW_three_phase_bundle',[core.world_point(-48,-3,-43.8),(-37,29,10),(-6.5,31,10),(-6.5,31,24),(-.5,20,25),(-.5,17.2,27),(-.5,17.2,34),(6.5,17.2,34),(6.5,22,34),(6.5,22,32.4)],1.1,('A_motor_UVW_landing',),'three phase conductors represented separately from DC bus; motor-side free tail at open rear channel awaits actual lead exit')
    route('motor_encoder_signal_bundle',[core.world_point(-48,3,-43.8),(-37,29,12.5),(-18.8,31,12.5),(-18.8,31,24.5),(-18.8,17,27),(-18.8,17,34),(-20,22.5,34),(-20,22.5,32)],.9,('A_encoder_signal_landing',),'separate encoder service tail; real pin count/OD/connector and pigtail exit are unresolved supplier interfaces')
    path_c=[(-7,23,31.7),(-5.5,17,31.7),(-5.5,17,26),(-5.5,12,26),(-7,10,26),(-7,-5,26),(-7,-24,29),(-16,-27.4,36.5)]
    add("FFC_A_to_C_route",ribbon(path_c),"FFC",status="5.5mm ribbon route and slack reserve; endpoints require detailed fold layout")
    WIRE_PATHS['FFC_A_to_C_route']={'terminal_parts':['A_FH12_C','C_FH12_connector_under_lobe'],'points_world_mm':path_c,'width_mm':5.5,'thickness_mm':.2}
    # An A-to-B ribbon loops beside the rear board edge, never through a board.
    add("FFC_A_to_B_route",ribbon([(-38,30,31.8),(-32.5,30,35),(-32.5,45.7,36),(-32.5,45.7,50.5),(-35,42.5,50.5),(-35,37.5,46.4)]),"FFC",status="ribbon service loop envelope; final fold and slack placement pending")
    WIRE_PATHS['FFC_A_to_B_route']={'terminal_parts':['A_FH12_AB','B_FH12_AB'],'width_mm':5.5,'thickness_mm':.2}
    # Screen cable uses the stationary post. Connector housing stays at D, never
    # forced through a 5.5 mm bore; individual wires are threaded before crimping.
    add("display_stationary_post_wire_bundle",O(R(3.5,0,53,-46)),"wire",status="wires only; no rotating electrical joint")
    WIRE_PATHS['display_stationary_post_wire_bundle']={'terminal_parts':[],'radius_mm':1.75,'only_internal_tube_segment':True}
    add("speaker_20x14x4_reserved",B(20,14,4,-17,36,4),"speaker",status="speaker size reserve; MPN/impedance pending")
    add("brake_thermal_module_reserved",B(26,12,9,10,33,4),"metal",status="internal thermal reserve; shifted 2 mm forward to clear rear board post; rating pending")
    # Pair bundles terminate on carriers / A. Service ends shown separately to
    # avoid falsely suggesting the FFC replaces high-current motor wiring.
    add('A_left_keys_signal_landing',B(3,4,2,-43,35,30.4),'connector',status='four-wire solder landing reserve, no extra rigid daughterboard')
    add('A_right_controls_signal_landing',B(2.5,4,2,21.75,34,30.4),'connector',status='six-wire side/press-switch landing reserve')
    for side,label in ((-1,'left'),(1,'right')):
        branches=[]
        for y in SIDE_BUTTON_Y:
            branch=[(side*44,y,47.3),(side*44,y,45),(side*47,y,45)]
            branch.append((-47,33,45) if side<0 else (47,17,42))
            branches.append(branch)
        if side<0:
            main=[(-47,33,45),(-47,40,40),(-43.4,44,36),(-43.4,35,36),(-43.4,35,32.4)]
            terminal='A_left_keys_signal_landing'; radius=1.
        else:
            main=[(47,17,42),(47,15.5,36),(32,15.5,35),(16.5,15.5,35),(16.5,30,35),(16.5,30,38.5),(21.75,30,38.5),(21.75,34,38.5),(21.75,34,32.4)]
            branches.append([core.world_point(28,20.9,-28.45),core.world_point(28,18.5,-28.45),(35,15.5,33),(23,15.5,34),(21.75,15.5,35)])
            terminal='A_right_controls_signal_landing';radius=1.2
        shapes=[cable(p,.6) for p in branches]+[cable(main,radius)]
        name=f'{label}_controls_signal_harness'
        add(name,cq.Compound.makeCompound([s for sh in shapes for s in sh.Solids()]),'wire',status='one shared harness envelope with separate switch branches; not duplicated coincident cables')
        WIRE_PATHS[name]={'branches_world_mm':branches,'points_world_mm':main,'radius_mm':radius,'terminal_parts':[terminal,f'switch_{label}_1_SKRPASE010',f'switch_{label}_2_SKRPASE010']+(['ring_press_switch_SKRPASE010'] if side>0 else [])}
    route('rear_power_signal_pair',[(20,42.15,53),(20,39.5,53),(24.1,39.5,50),(24.1,36,50),(24.1,36,35),(24.1,36,32.8),(22.4,35.3,32.4)],.4,('rear_power_switch_SKRPASE010','A_right_controls_signal_landing'),'rear switch pair through board-edge service space; connector/strain relief sample pending')
    return {"C_route_polyline_mm":sum((cq.Vector(*b)-cq.Vector(*a)).Length for a,b in zip(path_c,path_c[1:])),
            "purchased_FFC_length_mm":100,"ribbon_bend_detail_frozen":False,
            'routes':WIRE_PATHS,'motor_exit_and_service_loops_verified_on_hardware':False}


def checks(cavity,source,wiring):
    from verify_full_knob import validate
    return validate(sys.modules[__name__],source,wiring)


def main(render=True,export=True):
    OUT.mkdir(parents=True,exist_ok=True)
    cavity,source,wiring=build_model()
    report=checks(cavity,source,wiring)
    assert hashlib.sha256(Path(__file__).read_bytes()).hexdigest()==MODEL_SOURCE_SHA256, 'CAD source changed during build; rerun before export'
    (OUT/"verification.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf8")
    if export:
        _export_step(OUT/"GL30_FULL_R8_ASSEMBLY.step","GL30_FULL_R8",[(n,s,META[n]["color"]) for n,s in delivery_parts().items()])
    (OUT/"parts").mkdir(exist_ok=True)
    for n,s in delivery_parts().items():
        cq.exporters.export(s,str(OUT/'parts'/(n+'.step')))
    for n,s in LOCAL_BOARDS.items():
        cq.exporters.export(cq.Workplane("XY").add(s).faces(">Z").wires(),str(OUT/"parts"/("PCB_"+n+"_mechanical_outline.dxf")))
    if render:
        opaque=[(n,s,META[n]["color"],(0,0,0)) for n,s in delivery_parts().items()]
        _render(OUT/"exterior.png",opaque,title="GL30 R8 | Flush deck | 104 x 98 mm",camera=(170,-200,150),focal=(0,0,32),parallel_scale_mm=80)
        internal=[(n,s,(*META[n]["color"][:3],.10) if META[n]["group"]=="housing" else META[n]["color"],(0,0,0)) for n,s in PARTS.items()]
        _render(OUT/"transparent.png",internal,title="R8 | A green / B blue / C LED ring / F orange battery",camera=(170,-190,190),focal=(0,0,28),parallel_scale_mm=80)
        _render(OUT/"rear.png",opaque,title="R8 | Recessed side keys + rear power + rear USB-C",camera=(-170,210,130),focal=(0,7,30),parallel_scale_mm=78)
        opened=[(n,s,META[n]["color"],(0,0,0)) for n,s in PARTS.items() if META[n]["group"]!="housing"]
        _render(OUT/"open_top.png",opened,title="R8 | Installed boards, battery, switches and cable routes",camera=(105,-90,260),focal=(0,0,25),parallel_scale_mm=78)
    print(json.dumps({'out':str(OUT),'parts':len(PARTS),'checks':report['checks'],
                      'static_collisions':report['static_collisions'],'press_collisions':report['press_collisions'],
                      'button_checks':report['button_checks'],'tension_collisions':report['tension_collisions']},ensure_ascii=False,indent=2))
    return 0 if report['all_specified_geometry_checks_pass'] else 2



if __name__=="__main__":
    parser=argparse.ArgumentParser();parser.add_argument("--no-render",action="store_true");parser.add_argument("--preview-only",action="store_true")
    args=parser.parse_args()
    try:code=main(not args.no_render,not args.preview_only)
    except Exception:
        import traceback;traceback.print_exc();code=1
    sys.stdout.flush();sys.stderr.flush();os._exit(code)
