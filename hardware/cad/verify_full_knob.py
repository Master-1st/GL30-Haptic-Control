"""Specified R8 geometry checks. This file does not grant physical release.

Contacts have named partners and bounded physical regions. Cable/FFC and
members of one moving group are included, not exempted by their group name.
"""
from __future__ import annotations
import hashlib
from pathlib import Path
from math import cos,sin,radians
import cadquery as cq
from v7_product_model import _shape,_bbox

EPS=1e-4


def validate(m,source,wiring):
    p=m.PARTS; meta=m.META; B,R,O=m.B,m.R,m.O
    report={'status':'R8_UNPOWERED_PROTOTYPE_GEOMETRY','units':'mm',
            'source':source,'refinement':m.REFINEMENT,'wiring':wiring,
            'model_source_sha256':m.MODEL_SOURCE_SHA256,'parts':{},'checks':{},
            'static_collisions':[],'intentional_contacts':[],'press_collisions':[],
            'button_checks':[],'tension_collisions':[],'rotation_collisions':[],
            'interfaces':[],'limits':[
                'No physical fit, force, fatigue, friction, backlash, thermal or electrical test has been performed.',
                'Reference MGN7C and motor mounting maps require confirmation against purchased parts.',
                'Pulley and belt solids are tooth-system envelopes, not manufacturing tooth geometry.',
                'Nominal threads are envelopes; fits, insertion depths, clamp torques and anti-loosening remain to be confirmed.',
                'Wire bundle counts, bend radii, motor pigtail exit and service slack require real cables.',
                'PCB outlines and landing zones are packaging changes, not fabricated/routed electronics.'],
            'physical_release':False}
    for n,s in p.items():
        report['parts'][n]={**meta[n],'bbox':_bbox(s),'valid':s.isValid(),
                           'solids':len(s.Solids()),'volume_mm3':abs(s.Volume())}
    report['source_hashes']={str(f.relative_to(m.ROOT)):hashlib.sha256(f.read_bytes()).hexdigest()
        for f in (Path(m.__file__),Path(__file__),Path(m.__file__).with_name('r8_motor_module.py')) if f.exists()}
    rules={}
    def allow(a,b,region,kind='thread engagement'):
        if a in p and b in p:rules[frozenset((a,b))]=(kind,_shape(region))
    def local_thread(a,b,x,y,n,h,d):allow(a,b,O(R(d,0,h,n).translate((x,y,0))))
    def world_thread(a,b,x,y,z,h,d):allow(a,b,R(d,0,h,z).translate((x,y,0)))
    for x,y in ((-45,-39),(43,-39),(-46,32),(44,41)):
        world_thread(f'cover_screw_{x}_{y}',f'cover_M2_insert_{x}_{y}',x,y,3,4,2.01)
    for i,(x,y) in enumerate(m.CARTRIDGE_BOLTS,1):
        local_thread(f'cartridge_base_M3x8_{i}','turned_bearing_cartridge',x,y,-39.5,5,3.01)
    for i,y in enumerate((-16.,16.),1):
        local_thread(f'cartridge_cap_M2x4_{i}','turned_bearing_cartridge',0,y,-14.35,3.4,2.01)
    for i,(y,n) in enumerate([(y,n) for y in (28.,40.) for n in (-30.75,-22.75)],1):
        allow(f'guide_carriage_M2x4_{i}','MGN7C_carriage_envelope',O(m.cylinder_x(2.01,2,17,y,n)))
    for i,x in enumerate((18.,23.5),1):
        local_thread(f'guide_adapter_to_plate_M2x5_{i}','press_base_plate',x,21.5,-42.5,3,2.01)
    for i,n in enumerate((-34.,-19.),1):
        allow(f'guide_rail_M2x6_{i}','fixed_rail_spine_and_floor_foot',O(m.cylinder_x(2.01,2.4,25,34,n)))
    for name,n,h in (('fixed_rail_spine_and_floor_foot',-8,3),('release_stop_M3_jam_nut',-5,1.5)):
        local_thread('release_stop_M3x10_grub',name,18,35,n,h,3.01)
    for i,(x,y) in enumerate(((37.5,35.),(37.5,42.)),1):
        world_thread(f'guide_floor_M3x8_{i}','fixed_rail_spine_and_floor_foot',x,y,3.5,4.5,3.01)
    for i,x in enumerate((3.,19.),1):
        for jaw in ('left','right'):
            world_thread(f'screen_foot_M2x5_{i}',f'fixed_screen_clamp_{jaw}',x,12,2,3,2.01)
    for label,n in (('lower',4.9),('upper',7.1)):
        local_thread(f'screen_tube_M8x0p5_{label}_lockring','fixed_screen_tube_8x6',0,0,n,1,8.01)
    for i,(x,y) in enumerate(m.core.SCREEN_HOLES,1):
        local_thread(f'display_module_M2x5_{i}','D_Waveshare_complete_module_supplier_valid_solids',x,y,8.4,3.1,2.01)
    local_thread('output_M12x0p75_slotted_locknut','rotary_output_shaft',0,0,2.8,1.7,12.01)
    for x,y in ((-39,43),(20,42)):
        world_thread(f'PCB_support_bottom_M2x6_{x}_{y}',f'PCB_support_post_25mm_{x}_{y}',x,y,2,4,2.01)
        world_thread(f'PCB_stack_top_M2x25_{x}_{y}',f'PCB_support_post_25mm_{x}_{y}',x,y,20.2,6.8,2.01)
    for side,label in ((-1,'left'),(1,'right')):
        for i,y in enumerate((1.,30.),1):
            world_thread(f'side_carrier_{label}_M2x4_{i}','housing_104x98_rounded',side*46.5,y,52.4,2.4,2.01)
    for i,x in enumerate((14.,26.),1):
        world_thread(f'rear_switch_holder_M2x6_{i}','housing_104x98_rounded',x,43.2,51.4,4.4,2.01)
    for i,x in enumerate((15.,19.),1):
        local_thread(f'press_leaf_root_M2x3_{i}','carriage_to_plate_L_adapter',x,22.5,-28.2,2,2.01)
    for a in (0,120,240):
        local_thread(f'grip_to_spider_M2x5_{a}','rotating_ring',24*cos(radians(a)),24*sin(radians(a)),3.3,3,2.01)
    for i,(x,y) in enumerate(m.REFINEMENT['light_board_mount_axes_mm'],1):
        local_thread(f'light_board_mount_screw_{i}','housing_104x98_rounded',x,y,-1,2.8,1.61)
    motor=m.REFINEMENT.get('motor_module',{})
    for i,hole in enumerate(motor.get('vendor_source',{}).get('back_3_holes',[]),1):
        x,y=hole['xy_mm']
        local_thread(f'motor_stator_mount_M3x5_{i}','GL30_with_factory_encoder_E',x-42,y,-40.5,2.5,3.01)
    for i,hole in enumerate(motor.get('vendor_source',{}).get('front_4_holes',[]),1):
        x,y=hole['xy_mm']
        local_thread(f'motor_pulley_rotor_M3x3_{i}','GL30_with_factory_encoder_E',x-42,y,-14.3,2,3.01)
    local_thread('motor_pulley_locknut_M14x0p75','motor_pulley_hub',-42,0,-3.85,1.5,14.01)
    for row in motor.get('thread_contact_regions',[]):
        allow(row['a'],row['b'],O(R(row['diameter_mm'],0,row['n_max_mm']-row['n_min_mm'],row['n_min_mm']).translate((row['x_mm'],row['y_mm'],0))))
    for n in p:
        if n.startswith('motor_slide_lock_screw_'):
            s=m.local_shape(p[n]); bb=s.BoundingBox(); x=(bb.xmin+bb.xmax)/2; y=(bb.ymin+bb.ymax)/2
            local_thread(n,'press_base_plate',x,y,-42,2.5,2.51)
    for x,name in ((-42,'motor_pulley'),(0,'driven_pulley')):
        allow('belt_envelope',name,O(R(27,0,6,-8.9).translate((x,0,0))),'nominal belt/tooth engagement envelope')
    for n,route in m.WIRE_PATHS.items():
        for terminal in route.get('terminal_parts',[]):
            if terminal in p:allow(n,terminal,p[terminal],'named cable termination envelope')
    # Different harnesses can only share the immediate terminal region, not
    # a coincident path through the rest of the product.
    wires=list(m.WIRE_PATHS)
    for i,a in enumerate(wires):
        for b in wires[i+1:]:
            shared=set(m.WIRE_PATHS[a].get('terminal_parts',[])) & set(m.WIRE_PATHS[b].get('terminal_parts',[]))
            if shared:
                bounds=[p[n].BoundingBox() for n in shared if n in p]
                if bounds:
                    q=bounds[0]
                    allow(a,b,B(q.xlen+2,q.ylen+2,q.zlen+2,(q.xmin+q.xmax)/2,(q.ymin+q.ymax)/2,q.zmin-1),'shared terminal fanout only')
    def collision(a,b,sa=None,sb=None):
        sa=p[a] if sa is None else sa; sb=p[b] if sb is None else sb
        v=m.overlap(sa,sb)
        if v<EPS:return None
        rule=rules.get(frozenset((a,b)))
        row={'a':a,'b':b,'mm3':float(v)}
        if rule:
            if rule[0]=='named cable termination envelope':
                # The rule region is the actual named terminal solid. The
                # intersection with that terminal is already bounded by it.
                row['relationship']=rule[0];return ('allowed',row)
            # Cable envelopes have overlapping segment solids. A Boolean of
            # the entire compound can fail or double-count material. Evaluate
            # each solid pair, then measure how much lies inside the simple
            # rule region; never treat a failed Boolean as a pass.
            outside=0.
            for aa in sa.Solids():
                for bb in sb.Solids():
                    amount=m.overlap(aa,bb)
                    if amount<EPS:continue
                    cross=aa.intersect(bb)
                    covered=m.overlap(cross,rule[1])
                    outside+=max(0.,amount-covered)
            if outside<EPS:
                row['relationship']=rule[0];return ('allowed',row)
            row['outside_allowed_region_mm3']=outside
        return ('collision',row)
    names=[n for n in p if meta[n]['group']!='marking']
    tested=0
    for i,a in enumerate(names):
        if i%25==0:print(f'R8 static {i}/{len(names)}: {a}',flush=True)
        for b in names[i+1:]:
            tested+=1
            result=collision(a,b)
            if result:report['intentional_contacts' if result[0]=='allowed' else 'static_collisions'].append(result[1])
    print(f'R8 static complete: {len(report["static_collisions"])} collisions',flush=True)
    (m.OUT/'static_debug.json').write_text(__import__('json').dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    report['static_pair_count']=tested
    report['checks']['all_named_shapes_valid']=all(d['valid'] for d in report['parts'].values())
    made=('housing_104x98_rounded','bottom_cover','press_base_plate','turned_bearing_cartridge',
          'carriage_to_plate_L_adapter','fixed_rail_spine_and_floor_foot','motor_tension_slide_plate',
          'rotary_output_shaft','rotating_arm_spider','rotating_ring','rear_power_switch_holder',
          'guide_floor_backplate_1p5mm')
    report['checks']['principal_manufactured_parts_single_solid']=all(len(p[n].Solids())==1 for n in made)
    report['checks']['all_static_parts_and_wires_clear_except_bounded_contacts']=not report['static_collisions']
    report['checks']['no_added_cover_glass_or_physical_blue_marker']=not any(n in p for n in ('fixed_display_cover_glass','fixed_screen_bezel','blue_rotary_index'))
    # Verify finite-area bearing/fixing seats; zero-distance edge contact alone
    # is insufficient. A 0.001 mm normal intrusion measures the common seat.
    def seat(a,b,local_direction,min_area):
        av=m.local_shape(p[a]); bv=m.local_shape(p[b]); v0=m.core.overlap(av,bv)
        shifted=av.translate(tuple(.001*v for v in local_direction))
        area=max(0.,m.core.overlap(shifted,bv)-v0)/.001
        distance=p[a].distance(p[b]); ok=distance<1e-5 and v0<EPS and area>=min_area
        report['interfaces'].append({'a':a,'b':b,'distance_mm':distance,'contact_area_estimate_mm2':area,'minimum_area_check_mm2':min_area,'pass':ok})
    seat('carriage_to_plate_L_adapter','MGN7C_carriage_envelope',(1,0,0),100)
    seat('carriage_to_plate_L_adapter','press_base_plate',(0,0,-1),30)
    seat('turned_bearing_cartridge','press_base_plate',(0,0,-1),100)
    seat('bearing_top_retainer_0p6mm','turned_bearing_cartridge',(0,0,-1),100)
    seat('rotating_ring','rotating_arm_spider',(0,0,-1),20)
    seat('output_M12x0p75_slotted_locknut','rotating_arm_spider',(0,0,-1),30)
    seat('motor_pulley','motor_pulley_hub',(0,0,-1),5)
    seat('motor_pulley_locknut_M14x0p75','motor_pulley',(0,0,-1),20)
    seat('motor_tension_slide_plate','GL30_with_factory_encoder_E',(0,0,1),50)
    seat('internal_screen_tray_and_three_spacers','screen_tube_M8x0p5_lower_lockring',(0,0,-1),5)
    seat('screen_tube_M8x0p5_upper_lockring','internal_screen_tray_and_three_spacers',(0,0,-1),5)
    seat('ring_press_compliant_actuator','carriage_to_plate_L_adapter',(0,0,-1),10)
    report['checks']['specified_load_path_seats_have_finite_contact_area']=all(r['pass'] for r in report['interfaces'])
    clearances={
        'shaft_to_spider_radial':(p['rotary_output_shaft'].distance(p['rotating_arm_spider']),.049),
        'pulley_to_outer_retainer':(p['driven_pulley'].distance(p['bearing_top_retainer_0p6mm']),.649),
        'stationary_tube_to_output_shaft':(p['fixed_screen_tube_8x6'].distance(p['rotary_output_shaft']),.399),
        'tube_lower_lockring_to_output_shaft':(p['screen_tube_M8x0p5_lower_lockring'].distance(p['rotary_output_shaft']),.399),
    }
    report['measured_clearances']={k:{'distance_mm':a,'minimum_check_mm':b,'pass':a>=b} for k,(a,b) in clearances.items()}
    report['checks']['specified_positive_running_clearances']=all(a>=b for a,b in clearances.values())
    # Operating press: both supported axes translate together; side keys and
    # fixed guide/body remain still. Whole-model rest checks include all parts.
    movers=[n for n in names if meta[n]['group'] in ('moving','rotating','motor')]
    fixed=[n for n in names if n not in movers and not n.startswith('return_spring_') and n!='ring_press_switch_plunger']
    for stroke in (.175,.35):
        print(f'R8 press {stroke}',flush=True)
        for a in movers:
            shifted=m.core.press_shift(p[a],stroke)
            for b in fixed:
                result=collision(a,b,shifted,p[b])
                if result and result[0]=='collision':report['press_collisions'].append({'stroke_mm':stroke,**result[1]})
        for i,(x,y) in enumerate(((-18,-3),(20,-3),(0,13)),1):
            spring=_shape(O(R(4.4,3.7,5.5-stroke,-48).translate((x,y,0))))
            for a in movers:
                v=m.overlap(m.core.press_shift(p[a],stroke),spring)
                if v>EPS:report['press_collisions'].append({'stroke_mm':stroke,'a':a,'b':f'compressed_spring_{i}','mm3':v})
    report['checks']['press_motion_and_compressed_spring_envelopes_clear']=not report['press_collisions']
    report['stop_measurements']=[]
    for stroke in (0,.175,.35):
        gap=p['release_stop_M3x10_grub'].distance(m.core.press_shift(p['carriage_to_plate_L_adapter'],stroke))
        report['stop_measurements'].append({'stop':'release','stroke_mm':stroke,'gap_mm':gap,'expected_gap_mm':stroke,'pass':abs(gap-stroke)<1e-5})
        for i in (1,2,3):
            gap=p[f'press_stop_{i}_0p35mm'].distance(m.core.press_shift(p['press_base_plate'],stroke))
            report['stop_measurements'].append({'stop':f'down_{i}','stroke_mm':stroke,'gap_mm':gap,'expected_gap_mm':.35-stroke,'pass':abs(gap-(.35-stroke))<1e-5})
    report['checks']['release_and_three_down_stops_close_nominal_travel']=all(r['pass'] for r in report['stop_measurements'])
    report['press_actuation_budget']={'leaf_to_plunger_rest_gap_mm':p['ring_press_compliant_actuator'].distance(p['ring_press_switch_plunger']),
        'switch_nominal_travel_mm':.2,'rigid_travel_mm':.35,'elastic_accommodation_at_full_press_mm':.1,
        'candidate_leaf_stiffness_N_per_mm':36.,'assumed_reference_operating_force_N':2.55,
        'estimated_leaf_deflection_at_reference_force_mm':2.55/36.,
        'estimated_remaining_travel_margin_mm':.35-.05-.2-2.55/36.,
        'estimate_only_not_FEA_or_switch_FS_validation':True,'physical_trigger_release_validated':False}
    for side,label in ((-1,'left'),(1,'right')):
        print(f'R8 keys {label}',flush=True)
        for i in (1,2):
            n=f'button_{label}_{i}_cap_and_stem'; y=m.SIDE_BUTTON_Y[i-1]
            for stroke in (0,.2,.35,.4):
                moved=p[n].translate((-side*stroke,0,0))
                for b in names:
                    if b==n or b==f'side_{label}_{i}_switch_plunger':continue
                    v=m.overlap(moved,p[b])
                    if v>EPS:report['button_checks'].append({'key':n,'stroke_mm':stroke,'obstacle':b,'mm3':v})
                visible=.5-max(0,stroke-.2)
                plunger=_shape(B(visible,2,1.5,side*(46.55+visible/2),y,48.25))
                if m.overlap(moved,plunger)>EPS:report['button_checks'].append({'key':n,'stroke_mm':stroke,'reason':'cap penetrates displaced plunger'})
            beyond=m.overlap(p[n].translate((-side*.41,0,0)),p[f'switch_{label}_pair_removable_carrier'])
            if beyond<EPS:report['button_checks'].append({'key':n,'reason':'no hard stop at0.4; +0.01 probe did not contact'})
    for stroke in (0,.05,.2,.25):
        moved=p['rear_power_button_cap'].translate((0,-stroke,0))
        for b in names:
            if b in ('rear_power_button_cap','rear_power_switch_plunger'):continue
            v=m.overlap(moved,p[b])
            if v>EPS:report['button_checks'].append({'key':'rear_power_button_cap','stroke_mm':stroke,'obstacle':b,'mm3':v})
        visible=.5-max(0,stroke-.05)
        plunger=_shape(B(2,visible,1.5,20,44.15+visible/2,52.25))
        if m.overlap(moved,plunger)>EPS:report['button_checks'].append({'key':'rear_power_button_cap','stroke_mm':stroke,'reason':'cap penetrates displaced plunger'})
    if m.overlap(p['rear_power_button_cap'].translate((0,-.26,0)),p['rear_power_switch_holder'])<EPS:
        report['button_checks'].append({'key':'rear_power_button_cap','reason':'no rear hard stop at0.25; +0.01 probe did not contact'})
    report['checks']['four_side_keys_and_rear_key_reach_nominal_switch_travel']=not report['button_checks']
    turning=R(12,8.8,35.7,-31.2).union(R(16,8.8,1.2,-31.2)).union(R(27,12.1,7.6,-9.7))
    turning=turning.union(R(18,12.1,1.9,-2.1)).union(R(20,12.1,3,-.2)).union(R(52,18,3.2,.1))
    turning=turning.union(R(18,11.2,1.7,2.8)).union(R(56,43,16.4,3.3)).union(R(56,53.6,2.7,.6)).union(R(44,39.2,.8,18.9))
    for stroke in (0,.35):
        print(f'R8 output sweep {stroke}',flush=True)
        s=m.core.press_shift(O(turning),stroke)
        for b in fixed:
            v=m.overlap(s,p[b])
            if v>EPS:report['rotation_collisions'].append({'stroke_mm':stroke,'obstacle':b,'mm3':v})
    report['checks']['continuous_360_degree_output_sweep_clear']=not report['rotation_collisions']
    module_names=m.REFINEMENT.get('motor_module',{}).get('tension_moving_names',[])
    module_names=[n for n in module_names if n in p]
    if 'GL30_with_factory_encoder_E' not in module_names:module_names.append('GL30_with_factory_encoder_E')
    report['tension_checked_names']=module_names
    for delta in (-.5,.5):
        print(f'R8 tension {delta}',flush=True)
        for stroke in (0,.35):
            for a in module_names:
                moved=m.core.press_shift(p[a].translate((delta,0,0)),stroke)
                for b in names:
                    if b in module_names or b=='belt_envelope':continue
                    other=m.core.press_shift(p[b],stroke) if b in movers else p[b]
                    result=collision(a,b,moved,other)
                    if result and result[0]=='collision':report['tension_collisions'].append({'delta_x_mm':delta,'stroke_mm':stroke,**result[1]})
    report['checks']['complete_motor_module_at_tension_limits_clear']=len(module_names)>5 and not report['tension_collisions']
    # Circular conservative bounds cover every phase angle of the front rotor
    # adapter, its screws and active pulley. Vendor motor internals are not
    # re-created here; the original motor assembly is a purchased reference.
    motor_sweep=R(26,6.4,1,-12.3).union(R(18,6.4,1.6,-11.3))
    motor_sweep=motor_sweep.union(R(27,14,7.35,-9.7)).union(R(25.2,14.8,4.2,-14.3))
    motor_rotating={'motor_pulley','motor_pulley_hub','motor_pulley_locknut_M14x0p75','GL30_with_factory_encoder_E','belt_envelope'}
    motor_rotating.update(n for n in names if n.startswith('motor_pulley_rotor_'))
    report['motor_rotation_collisions']=[]
    for delta in (-.5,0,.5):
        for stroke in (0,.35):
            swept=m.core.press_shift(O(motor_sweep.translate((-42+delta,0,0))),stroke)
            for b in names:
                if b in motor_rotating:continue
                other=m.core.press_shift(p[b],stroke) if b in movers else p[b]
                v=m.overlap(swept,other)
                if v>EPS:report['motor_rotation_collisions'].append({'delta_x_mm':delta,'stroke_mm':stroke,'obstacle':b,'mm3':v})
    report['checks']['continuous_motor_front_rotation_sweep_clear']=not report['motor_rotation_collisions']
    report['all_specified_geometry_checks_pass']=all(report['checks'].values())
    return report
