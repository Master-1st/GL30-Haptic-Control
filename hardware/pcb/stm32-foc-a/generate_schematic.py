"""Rebuild the editable A-board schematic from the reviewed purchased-parts input.

No PCB, tracks or hardware configuration is created. KiCad must independently
export and check the result; this generator is not an ERC implementation.
"""
from __future__ import annotations
import argparse, copy, csv, hashlib, json, math, re, shutil, uuid
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE = HERE / 'design_input.json'
PROJECT = 'GL30_A_STM32_FOC'
NAMESPACE = uuid.UUID('6a8eb7b2-bca7-4201-b6ae-a941cd00464b')
DATA = json.loads(SOURCE.read_text(encoding='utf-8'))
PARTS = {c['ref']: c for c in DATA['components'] if c['pins']}
PARTS['D1']['pins'] = {'1': 'MOTOR_BUS', '2': 'GND'}  # KiCad/SMB: 1=cathode, 2=anode.
for t in DATA['testpoints']:
    PARTS[t['ref']] = dict(ref=t['ref'], value=t['net'], name='Test point',
        package='PCB exposed pad', mpn='', pins={'1':t['net']}, fitted=1, note='')

def uid(s): return str(uuid.uuid5(NAMESPACE, s))
def q(s): return json.dumps(str(s), ensure_ascii=False)
def n(v): return f'{v:.5f}'.rstrip('0').rstrip('.') if v else '0'
def fx(size=1.0, justify='', hide=False):
    return f'(effects (font (size {size} {size}))'+(f' (justify {justify})' if justify else '')+(' (hide yes)' if hide else '')+')'
def prop(key, value, x=0, y=0, hidden=True, size=1.0, justify='', angle=0):
    return f'(property {q(key)} {q(value)} (at {n(x)} {n(y)} {angle}) '+('(hide yes) ' if hidden else '')+fx(size,justify)+')'
def rect(x1,y1,x2,y2,fill='background'):
    return f'(rectangle (start {n(x1)} {n(y1)}) (end {n(x2)} {n(y2)}) (stroke (width 0.254) (type default)) (fill (type {fill})))'
def line(points):
    return '(polyline (pts '+''.join(f'(xy {n(x)} {n(y)})' for x,y in points)+') (stroke (width 0.254) (type default)) (fill (type none)))'
def pin(number,name,kind,x,y,angle,length=5.08):
    return f'(pin {kind} line (at {n(x)} {n(y)} {angle}) (length {n(length)}) (name {q(name)} {fx(1.0)}) (number {q(number)} {fx(1.0)}))'

class Atom(str): pass
def parse(s):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[^\s()]+|[()]', s)
    stack=[]; out=None
    for t in tokens:
        if t=='(':
            a=[]
            if stack: stack[-1].append(a)
            stack.append(a)
        elif t==')': out=stack.pop()
        else: stack[-1].append(json.loads(t) if t.startswith('"') else Atom(t))
    return out
def sexpr(obj):
    if isinstance(obj,list): return '('+' '.join(sexpr(v) for v in obj)+')'
    return str(obj) if isinstance(obj,Atom) else q(obj)
def children(v,key): return [x for x in v if isinstance(x,list) and x and x[0]==key]
def child(v,key): return next(iter(children(v,key)), None)

class Library:
    def __init__(self, runtime): self.runtime=runtime; self.symbols={}; self.cache={}; self.footprints={}
    def official(self, category, name, target=None):
        if category not in self.cache:
            p=self.runtime/'share/kicad/symbols'/f'{category}.kicad_sym'
            self.cache[category]={x[1]:x for x in children(parse(p.read_text(encoding='utf-8')),'symbol')}
        def flat(name):
            a=copy.deepcopy(self.cache[category][name]); ext=child(a,'extends')
            if ext:
                base=flat(ext[1]); old=base[1]; base[1]=name
                for x in children(base,'symbol'): x[1]=name+x[1][len(old):]
                keys={x[1] for x in children(a,'property')}
                base=[x for x in base if not(isinstance(x,list) and x[0]=='property' and x[1] in keys)]
                base += children(a,'property')
                return base
            return a
        a=flat(name); target=target or name; old=a[1]; a[1]=target
        for x in children(a,'symbol'): x[1]=target+x[1][len(old):]
        self.symbols[target]=sexpr(a)
        return target
    def custom(self,name,groups,graphics=None):
        # groups = unit -> (graphics string, list of pin S-expressions)
        s=f'(symbol {q(name)} (pin_names (offset 0.762)) (in_bom yes) (on_board yes) '
        s+=prop('Reference','U')+prop('Value',name)+prop('Footprint','')+prop('Datasheet','')
        for unit,(g,ps) in groups.items(): s+=f'(symbol {q(name+"_"+str(unit)+"_1")} {g} {" ".join(ps)})'
        self.symbols[name]=s+')'
        return name
    def box(self,name,left,right,width=30.48):
        height=(max(len(left),len(right))+1)*2.54
        ps=[]
        for side,rows in [(-1,left),(1,right)]:
            for i,(num,pname,kind) in enumerate(rows):
                y=(len(rows)-1)*1.27-i*2.54
                ps.append(pin(num,pname,kind,side*(width/2+5.08),y,0 if side<0 else 180))
        return self.custom(name,{1:(rect(-width/2,height/2,width/2,-height/2),ps)})
    def pins(self,name,unit=1):
        a=parse(self.symbols[name]); result={}
        for block in children(a,'symbol'):
            u=int(block[1].rsplit('_',2)[1])
            if u not in (0,unit): continue
            for p in children(block,'pin'):
                pos=child(p,'at')
                raw=str(child(p,'number')[1]); nums=raw[1:-1].split(',') if raw.startswith('[') else [raw]
                for num in nums:result[num]=dict(x=float(pos[1]),y=float(pos[2]),angle=int(pos[3]),
                    name=str(child(p,'name')[1]),kind=str(p[1]),stack=raw)
        return result
    def footprint(self, fullname):
        if not fullname: return ''
        category,name=fullname.split(':')
        src=self.runtime/'share/kicad/footprints'/f'{category}.pretty'/f'{name}.kicad_mod'
        if not src.exists(): raise FileNotFoundError(src)
        dest=HERE/'GL30.pretty'/src.name; dest.parent.mkdir(exist_ok=True)
        # Keep official pads, remove only external 3D-model links: no mandatory huge model download.
        obj=parse(src.read_text(encoding='utf-8'))
        obj=[x for x in obj if not(isinstance(x,list) and x[0]=='model')]
        dest.write_text(sexpr(obj)+'\n',encoding='utf-8')
        self.footprints[fullname]=dict(source=str(src),sha256=hashlib.sha256(src.read_bytes()).hexdigest(),local=dest.name)
        return 'GL30:'+name

DATASHEETS={
'U1':'https://www.st.com/resource/en/datasheet/stm32g474ce.pdf',
'U2':'https://www.ti.com/lit/ds/symlink/drv8316.pdf',
'U3':'https://www.ti.com/lit/ds/symlink/tlv1704.pdf',
'U4':'https://www.ti.com/lit/ds/symlink/tlv1704.pdf',
'U5':'https://www.ti.com/lit/ds/symlink/tlv755p.pdf',
'U6':'https://www.ti.com/lit/ds/symlink/sn74lvc1g07.pdf',
'U7':'https://www.ti.com/lit/ds/symlink/sn74lvc1g07.pdf',
'U8':'https://www.ti.com/lit/ds/symlink/txu0202.pdf',
'U9':'https://www.ti.com/lit/ds/symlink/tmux1511.pdf'}

def libraries(runtime):
    lib=Library(runtime)
    for category,name in [('Device','R'),('Device','C'),('Device','C_Polarized'),('Device','D_TVS'),
            ('Device','Crystal_GND24'),('power','PWR_FLAG'),('Connector','TestPoint')]: lib.official(category,name)
    # A real four-channel comparator, with each pin number explicit and a separate power unit.
    groups={}
    for unit,(plus,minus,out) in enumerate([(5,4,2),(7,6,1),(9,8,14),(11,10,13)],1):
        groups[unit]=(line([(-5.08,5.08),(-5.08,-5.08),(5.08,0),(-5.08,5.08)]),
            [pin(plus,'+','input',-10.16,2.54,0),pin(minus,'-','input',-10.16,-2.54,0),pin(out,'OUT','open_collector',10.16,0,180)])
    groups[5]=(rect(-5.08,5.08,5.08,-5.08),[pin(3,'V+','power_in',-10.16,2.54,0),pin(12,'V-','power_in',-10.16,-2.54,0)])
    lib.custom('TLV1704AIPWR',groups)
    power={1,19,20,21,23,24,35,36,47,48}
    outs={2,6,10,12,13,15,22,27,28,29,30,31,32,33,34,40,42,43,46}
    ins={5,8,9,14,16,17,18,25,26,38,41,45}
    names={int(row[0]):row[1] for row in DATA['mcu']}
    def mcurow(i):return (i,names[i], 'power_in' if i in power else 'open_collector' if i==11 else 'output' if i in outs else 'input' if i in ins else 'bidirectional')
    left=[1,24,36,48,21,20,19,23,35,47,5,6,7,45,37,38,3,4]
    right=[8,9,16,17,18,30,27,31,28,32,29,33,11,34,26,10,46,12,13,14,15,22,25,39,44,40,41,42,43,2]
    lib.box('STM32G474CET6',[mcurow(i) for i in left],[mcurow(i) for i in right],35.56)
    drvnames={int(k):v for k,v in DATA['drv_names'].items()}
    def drvrow(i):
        kind='no_connect' if i in(1,24) else 'power_in' if i in(2,4,9,10,11,12,15,18,26,41) else 'power_out' if i==25 else 'open_collector' if i==22 else 'output' if i in(13,14,16,17,19,20,33,38,39,40) else 'passive' if i in(5,6,7,8) else 'input'
        # Native KiCad 10 pin stacks retain all physical pads and one real output driver.
        # These are duplicated pins of the same half-bridge, not independent outputs.
        return ({13:'[13,14]',16:'[16,17]',19:'[19,20]'}.get(i,i),drvnames[i],kind)
    lib.box('DRV8316RRGFR',[drvrow(i) for i in [21,23,27,28,29,30,31,32,34,35,36,37,1,24]],
        [drvrow(i) for i in [9,10,11,13,16,19,40,39,38,33,22,25,8,7,6,5,3,2,4,12,15,18,26,41]],35.56)
    lib.box('TLV75533PDBVR',[(1,'IN','power_in'),(3,'EN','input'),(2,'GND','power_in')],[(5,'OUT','power_out'),(4,'NC','no_connect')],17.78)
    lib.box('SN74LVC1G07DBVR',[(2,'A','input'),(5,'VCC','power_in'),(3,'GND','power_in'),(1,'NC','no_connect')],[(4,'Y_OD','open_collector')],20.32)
    lib.box('TXU0202DCUR',[(5,'A1','input'),(4,'A2Y','output'),(3,'VCCA','power_in'),(6,'OE','input'),(2,'GND','power_in')],[(8,'B1Y','output'),(1,'B2','input'),(7,'VCCB','power_in')],25.4)
    lib.box('TMUX1511PWR',[(2,'S1','passive'),(5,'S2','passive'),(9,'S3','passive'),(12,'S4','passive'),(1,'SEL1','input'),(4,'SEL2','input'),(10,'SEL3','input'),(13,'SEL4','input')],
        [(3,'D1','passive'),(6,'D2','passive'),(8,'D3','passive'),(11,'D4','passive'),(14,'VDD','power_in'),(7,'GND','power_in')],25.4)
    # All four pads of SKRPASE010 are represented; 1/2 and 3/4 are the internal pairs.
    lib.custom('SKRPASE010',{1:(line([(-5.08,2.54),(-5.08,-2.54)])+line([(5.08,2.54),(5.08,-2.54)])+line([(-5.08,0),(3.81,2.54)]),
        [pin(1,'1','passive',-10.16,2.54,0),pin(2,'2','passive',-10.16,-2.54,0),pin(3,'3','passive',10.16,2.54,180),pin(4,'4','passive',10.16,-2.54,180)])})
    lib.custom('SolderJumper_2_Open',{1:(rect(-1.27,1.27,-.254,-1.27,'none')+rect(.254,1.27,1.27,-1.27,'none'),
        [pin(1,'1','passive',-5.08,0,0,3.81),pin(2,'2','passive',5.08,0,180,3.81)])})
    for count in (2,3,6,10):
        lib.box(f'Connector_{count}',[(i,str(i),'passive') for i in range(1,count+1)],[],10.16)
    return lib

def symbol_for(ref):
    c=PARTS[ref]
    if ref.startswith('TP'): return 'TestPoint'
    if ref.startswith('U'): return c['mpn']
    if ref.startswith('R'): return 'R'
    if ref.startswith('C'): return 'C_Polarized' if ref in ('C28','C29','C53','C54') else 'C'
    return {'X1':'Crystal_GND24','D1':'D_TVS','SW1':'SKRPASE010','JP1':'SolderJumper_2_Open'}.get(ref, f'Connector_{len(c["pins"])}')
def value_for(ref):
    c=PARTS[ref]
    if ref.startswith('R'): return c['value'].split(' Ω')[0]+(' 0.1%' if '0.1%' in c['value'] else '')+(' 0.5W' if ref=='R41' else '')
    if ref.startswith('C'): return c['value'].split(' / ')[0].replace(' ','').replace('µ','u')+'/'+c['value'].split(' / ')[1].split(' ')[0]+'V'
    if ref=='X1':return '24MHz CL=18pF'
    if ref=='D1':return 'SMBJ18A'
    if ref=='SW1':return 'SKRPASE010 RESET'
    if ref=='JP1':return 'BOOT0 / OPEN'
    if ref.startswith('J'):return {'J1':'MOTOR DC INPUT','J2':'MOTOR UVW','J3':'AS5048A 6-WIRE','J4':'SWD 1.27mm 2x5','J5':'POWER BOARD / FFC10','J6':'DISPLAY BOARD / FFC10','J7':'NTC 10k B3950'}[ref]
    return c.get('mpn') or c['value']

def footprints(lib):
    result={}
    fixed={'U1':'Package_QFP:LQFP-48_7x7mm_P0.5mm',
        'U3':'Package_SO:TSSOP-14_4.4x5mm_P0.65mm','U4':'Package_SO:TSSOP-14_4.4x5mm_P0.65mm',
        'U9':'Package_SO:TSSOP-14_4.4x5mm_P0.65mm','U5':'Package_TO_SOT_SMD:SOT-23-5',
        'U6':'Package_TO_SOT_SMD:SOT-23-5','U7':'Package_TO_SOT_SMD:SOT-23-5',
        'U8':'Package_SO:VSSOP-8_2.3x2mm_P0.5mm',
        'J5':'Connector_FFC-FPC:Hirose_FH12-10S-0.5SH_1x10-1MP_P0.50mm_Horizontal',
        'J6':'Connector_FFC-FPC:Hirose_FH12-10S-0.5SH_1x10-1MP_P0.50mm_Horizontal',
        'J7':'Connector_JST:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical',
        'D1':'Diode_SMD:D_SMB','JP1':'Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm',
        'X1':'Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm'}
    for ref,c in PARTS.items():
        fp=fixed.get(ref,'')
        if ref.startswith('R'):fp=f'Resistor_SMD:R_{c["package"]}_'+{'0603':'1608','0805':'2012','1206':'3216'}[c['package']]+'Metric'
        if ref.startswith('C') and c['package'] in ('0603','0805','1210'):
            fp=f'Capacitor_SMD:C_{c["package"]}_'+{'0603':'1608','0805':'2012','1210':'3225'}[c['package']]+'Metric'
        if ref.startswith('TP'):fp='TestPoint:TestPoint_Pad_D1.0mm'
        result[ref]=lib.footprint(fp)
    return result

PLACED={}
class Sheet:
    def __init__(self,lib,fp,key,title,page):
        self.lib=lib; self.fp=fp; self.key=key; self.title=title; self.page=page
        self.id=uid(key); self.items=[]; self.used=set(); self.count=0
    def add(self,s):self.items.append(s)
    def unique(self,k):self.count+=1;return uid(f'{self.key}:{k}:{self.count}')
    def text(self,text,x,y,size=1.4):self.add(f'(text {q(text)} (at {n(x)} {n(y)} 0) {fx(size,"left top")} (uuid {q(self.unique("text"))}))')
    def wire(self,a,b):
        if a==b:return
        self.add(f'(wire (pts (xy {n(a[0])} {n(a[1])}) (xy {n(b[0])} {n(b[1])})) (stroke (width 0) (type default)) (uuid {q(self.unique("wire"))}))')
    def label(self,net,x,y,angle=0):
        self.add(f'(global_label {q(net)} (shape passive) (at {n(x)} {n(y)} {angle}) {fx(1.0,"left" if angle in(180,90) else "right")} (uuid {q(self.unique("label"))}))')
    def junction(self,x,y):self.add(f'(junction (at {n(x)} {n(y)}) (diameter 0) (color 0 0 0 0) (uuid {q(self.unique("junction"))}))')
    def component(self,ref,x,y,angle=0,unit=1,auto=True,labels=None):
        sym=symbol_for(ref); self.used.add(sym); c=PARTS[ref]
        ps=self.lib.pins(sym,unit); a=math.radians(angle)
        points={k:(round(x+p['x']*math.cos(a)-p['y']*math.sin(a),5),round(y-p['x']*math.sin(a)-p['y']*math.cos(a),5)) for k,p in ps.items()}
        if ref not in PLACED:PLACED[ref]=dict(sheet=self.key,units=[],pins={})
        assert unit not in PLACED[ref]['units'],(ref,unit)
        PLACED[ref]['units'].append(unit);PLACED[ref]['pins'].update({k:c['pins'][k] for k in ps})
        height=max([abs(p['y']) for p in ps.values()]+[2.54])
        is_box=ref.startswith(('U','J')) and not ref.startswith('JP')
        if is_box and unit==1:tx=x;ty=y-height-6.35
        elif ref.startswith('U'):tx=x;ty=y-8.89
        else:tx=x;ty=y-4.445
        if ref in ('X1','SW1'):tx=x;ty=y-10.16
        if ref=='D1':tx=x+5.08;ty=y-1.27
        if ref.startswith(('R','C')) and angle==0:tx=x+3.81;ty=y-1.27
        show_value=not((ref.startswith('U') and unit!=1) or ref.startswith('TP'))
        text=f'(symbol (lib_id {q("GL30:"+sym)}) (at {n(x)} {n(y)} {angle}) (unit {unit}) (in_bom {"no" if ref.startswith("TP") else "yes"}) (on_board yes) (dnp {"yes" if not c.get("fitted",1) else "no"}) (uuid {q(uid("comp:"+ref+":"+str(unit)))}) '
        field_angle=angle if ref.startswith(('R','C')) else 0
        text+=prop('Reference',ref,tx,ty,False,1.1,'left' if tx!=x else '',field_angle)
        text+=prop('Value',value_for(ref),tx,ty+2.3,not show_value,1.0,'left' if tx!=x else '',field_angle)
        text+=prop('Footprint',self.fp.get(ref,''))+prop('Datasheet',DATASHEETS.get(ref,''))
        for key,val in [('MPN',c.get('mpn','')),('Package',c['package']),('Specification',c['value']),('Design_note',c.get('note','')),('Footprint_status','LIBRARY_SELECTED_PENDING_LAYOUT_REVIEW' if self.fp.get(ref) else 'PENDING_PHYSICAL_FOOTPRINT_REVIEW')]:text+=prop(key,val)
        text+=''.join(f'(pin {q(k)} (uuid {q(uid("pin:"+ref+":"+k))}))' for k in dict.fromkeys(p['stack'] for p in ps.values()))
        text+=f'(instances (project {q(PROJECT)} (path {q("/"+uid(PROJECT)+"/"+self.id)} (reference {q(ref)}) (unit {unit}))))'
        self.add(text+')')
        connected_stacks=set()
        for k,p in ps.items():
            net=c['pins'][k];pt=points[k]
            if p['stack'] in connected_stacks:continue
            connected_stacks.add(p['stack'])
            if net=='NC':self.add(f'(no_connect (at {n(pt[0])} {n(pt[1])}) (uuid {q(self.unique("nc"))}))');continue
            if not auto or (labels is not None and k not in labels):continue
            ori=(p['angle']+angle)%360;v={0:(-5.08,0),180:(5.08,0),90:(0,5.08),270:(0,-5.08)}[ori]
            end=(pt[0]+v[0],pt[1]+v[1]);self.wire(pt,end);self.label(net,*end,ori)
        return points
    def pwrflag(self,net,x,y):
        self.used.add('PWR_FLAG');ref='#FLG'+str(self.page)+str(self.count+1).zfill(3)
        text=f'(symbol (lib_id "GL30:PWR_FLAG") (at {n(x)} {n(y)} 0) (unit 1) (in_bom no) (on_board no) (dnp no) (uuid {q(self.unique(ref))}) '
        text+=prop('Reference',ref)+prop('Value','PWR_FLAG',x,y-4,False,0.8)
        text+=f'(instances (project {q(PROJECT)} (path {q("/"+uid(PROJECT)+"/"+self.id)} (reference {q(ref)}) (unit 1)))))'
        self.add(text);self.wire((x,y),(x,y+2.54));self.label(net,x,y+2.54,0)
    def caprow(self,refs,x,y,spacing=25.4):
        for i,r in enumerate(refs):self.component(r,x+i*spacing,y)
    def series_rc(self,res,cap,x,y):
        """A visible series resistor / shunt capacitor, with named input/output."""
        p=self.component(res,x,y,90,auto=False)
        c=self.component(cap,x+27.94,y+12.7,auto=False)
        self.wire(p['1'],(x-20.32,y));self.label(PARTS[res]['pins']['1'],x-20.32,y,0)
        self.wire(p['2'],(x+40.64,y));self.label(PARTS[res]['pins']['2'],x+40.64,y,180)
        self.wire(c['1'],(x+27.94,y));self.junction(x+27.94,y)
        self.wire(c['2'],(x+27.94,y+20.32));self.label('GND',x+27.94,y+20.32,0)
    def divider(self,top,bottom,cap,x,y):
        a=self.component(top,x,y,auto=False);b=self.component(bottom,x,y+25.4,auto=False)
        c=self.component(cap,x+25.4,y+25.4,auto=False);ym=y+12.7
        self.wire(a['1'],(x,y-8.89));self.label(PARTS[top]['pins']['1'],x,y-8.89,0)
        self.wire(a['2'],b['1']);self.junction(x,ym)
        self.wire((x,ym),(x+38.1,ym));self.label(PARTS[top]['pins']['2'],x+38.1,ym,180)
        self.wire(c['1'],(x+25.4,ym));self.junction(x+25.4,ym)
        self.wire(b['2'],(x,y+34.29));self.wire(c['2'],(x+25.4,y+34.29))
        self.wire((x,y+34.29),(x+25.4,y+34.29));self.label('GND',x,y+34.29,0)
    def save(self):
        libs='\n'.join(self.lib.symbols[s].replace(f'(symbol {q(s)}',f'(symbol {q("GL30:"+s)}',1) for s in sorted(self.used))
        text=f'(kicad_sch (version 20250114) (generator "gl30_schematic") (uuid {q(self.id)}) (paper "A3") (title_block (title {q(self.title)}) (date "2026-09-27") (rev "A-SCH-R1") (company "GL30 Haptic Control") (comment 1 "SCHEMATIC REVIEW - NOT PCB RELEASE")) (lib_symbols {libs})\n'+ '\n'.join(self.items)+'\n)\n'
        (HERE/(self.key+'.kicad_sch')).write_text(text,encoding='utf-8')

def build(runtime):
    lib=libraries(runtime);fp=footprints(lib); sheets=[]
    def sheet(key,title):
        s=Sheet(lib,fp,key,title,len(sheets)+2);sheets.append(s);s.text(title,15.24,12.7,2.3);return s
    s=sheet('A01_POWER_MCU','A01 - STM32G474CET6 / 5V to 3V3')
    s.component('U1',86.36,100.33)
    s.text('160 MHz: HSE 24 MHz / PLL M6 N80 R2\nPB9 = MOTOR_PWR_EN; PA12 = nSLEEP\nPC13 drives only U7 logic input. AOE = 0.',15.24,160.02)
    s.text('LOCAL REGULATOR / EXTERNAL 5V INPUT',228.6,33.02)
    s.component('U5',299.72,58.42);s.component('C13',241.3,83.82);s.component('C14',335.28,83.82)
    s.text('3V3 total initial budget <= 150 mA\n3V3_AUX budget <= 30 mA. No parallel supplies.',228.6,99.06)
    for r,x in [('R1',238.76),('R2',299.72),('R3',360.68)]:s.component(r,x,121.92,90)
    s.pwrflag('VBAT_3V3',238.76,144.78);s.pwrflag('3V3A',299.72,144.78);s.pwrflag('3V3_AUX',360.68,144.78)
    s.text('24 MHz CRYSTAL (CL=18 pF; load caps to verify on PCB)',218.44,165.1)
    s.component('X1',289.56,186.69);s.component('R6',355.6,186.69,90)
    s.component('C11',238.76,214.63);s.component('C12',289.56,214.63)
    s.text('RESET / BOOT0 (jumper normally open)',218.44,231.14)
    for r,x in [('R4',223.52),('R5',274.32),('JP1',340.36)]:s.component(r,x,243.84 if r=='JP1' else 254,90 if r.startswith('R') else 0)
    s.component('C10',198.12,254)
    s.caprow(['C1','C2','C3','C4'],27.94,213.36,43.18)
    s.caprow(['C5','C6','C7','C8','C9'],27.94,251.46,33.02)

    s=sheet('A02_DRV8316','A02 - DRV8316R / six PWM / local motor bus')
    s.component('U2',86.36,109.22)
    s.text('PWM INPUTS - driver-side 100k pulldowns',208.28,33.02)
    for i,(series,pull) in enumerate(zip(range(20,26),[26,27,28,29,42,43])):
        y=50.8+i*25.4
        p=s.component(f'R{series}',261.62,y,90,auto=False)
        s.wire(p['1'],(223.52,y));s.label(PARTS[f'R{series}']['pins']['1'],223.52,y,0)
        s.wire(p['2'],(299.72,y));s.label(PARTS[f'R{series}']['pins']['2'],299.72,y,180)
        t=s.component(f'R{pull}',281.94,y+7.62,auto=False)
        s.wire(t['1'],(281.94,y));s.junction(281.94,y);s.label('GND',*t['2'],0)
    s.text('CHARGE PUMP / BUCK DISABLED CONFIGURATION',15.24,172.72)
    for r,x in [('C30',30.48),('C31',78.74),('C32',127),('C33',175.26)]:s.component(r,x,190.5)
    s.component('R41',43.18,228.6,90);s.component('C34',101.6,228.6);s.component('R48',165.1,228.6,90)
    s.text('CP capacitor returns to VM, NOT GND.\nSet BUCK_PS_DIS=1 before disabling buck.\nU2 EP / pad 41 -> GND.',17.78,251.46)
    s.text('VM bypass: two 100nF per VM pin (9 / 10 / 11)',208.28,203.2)
    s.caprow(['C21','C22','C23','C24','C25','C26'],215.9,228.6,33.02)
    s.text('Bulk/TVS and MOTOR connectors: A07.\n3V3 first, then motor bus. Keep bridge off at reset.',208.28,246.38)

    s=sheet('A03_CURRENT_ADC','A03 - current sensing / powered-off ADC isolation')
    s.component('U9',281.94,72.39);s.component('C19',363.22,72.39)
    for idx,phase in enumerate('ABC'):
        y=50.8+idx*53.34; a,b,c=[49,50,51] if idx==0 else [52,53,54] if idx==1 else [55,56,57]
        s.text(f'PHASE {phase} - comparator branch bypasses U9',15.24,y-15.24)
        p=s.component(f'R{a}',55.88,y,90,auto=False);r=s.component(f'R{c}',137.16,y,90,auto=False)
        cap=s.component(f'C{42+2*idx}',165.1,y+12.7,auto=False)
        s.wire(p['1'],(35.56,y));s.label(f'SO{phase}_RAW',35.56,y,0)
        s.wire(p['2'],r['1']);s.label(f'SO{phase}_FAN',91.44,y,180);s.junction(91.44,y)
        s.wire(r['2'],(190.5,y));s.label(f'SO{phase}_CMP',190.5,y,180)
        s.wire(cap['1'],(165.1,y));s.junction(165.1,y);s.label('GND',*cap['2'],0)
        s.series_rc(f'R{b}',f'C{41+2*idx}',137.16,y+27.94)
    s.text('VBUS ADC DIVIDER = VM / 11',246.38,109.22)
    a=s.component('R67',279.4,132.08,auto=False);b=s.component('R68',279.4,157.48,auto=False)
    c=s.component('R69',279.4,182.88,auto=False);cap=s.component('C48',337.82,182.88,auto=False)
    s.label('MOTOR_BUS',*a['1'],0);s.wire(a['2'],b['1']);s.label('VBUS_DIV_MID',279.4,144.78,180)
    s.wire(b['2'],c['1']);s.wire((279.4,170.18),(365.76,170.18));s.label('VBUS_DIV11',365.76,170.18,180)
    s.wire(cap['1'],(337.82,170.18));s.junction(279.4,170.18);s.junction(337.82,170.18)
    s.wire(c['2'],(279.4,195.58));s.wire(cap['2'],(337.82,195.58));s.wire((279.4,195.58),(337.82,195.58));s.label('GND',279.4,195.58,0)
    s.series_rc('R70','C49',287.02,220.98)
    s.text('U9 signal maximum: 3.6 V while unpowered. Never connect raw VM to U9.\n33pF ADC / 100pF comparator filters; acquisition time needs bench validation.\nRemove R49/R52/R55 for comparator injection. SO is low-side current sensing.',15.24,249.86)

    s=sheet('A04_HARD_PROTECT','A04 - independent comparator protection / low-active BKIN')
    for ref,unit,x,y in [('U3',1,63.5,60.96),('U3',2,63.5,93.98),('U3',3,180.34,60.96),('U3',4,180.34,93.98),('U4',1,297.18,60.96),('U4',2,297.18,93.98),('U4',3,63.5,139.7),('U4',4,180.34,139.7)]:s.component(ref,x,y,unit=unit)
    s.component('U3',292.1,137.16,unit=5);s.component('U4',373.38,137.16,unit=5)
    s.component('C35',287.02,175.26);s.component('C36',332.74,175.26);s.component('C37',378.46,175.26)
    for title,a,b,cap,x in [('VLOW ~0.573 V',58,59,38,38.1),('VHIGH ~2.727 V',60,61,39,124.46),('OVP ~16.006 V',62,63,47,210.82),('VBUS_TRIP ~2.668 V',64,65,40,297.18)]:
        s.text(title,x-17.78,195.58)
        s.divider(f'R{a}',f'R{b}',f'C{cap}',x,215.9)
    s.component('R66',360.68,195.58,90)
    s.text('Seven open-collector outputs share HARD_FAULT_N.\nTypical current window +/-1.795 A at 0.600 V/A (not calibrated).\nBKIN active low, no digital filter; automatic restart disabled.',15.24,165.1)
    s.text('Fault pullup and U6 buffer on A06. U4D output deliberately NC.\nHARD_FAULT_N high alone does not prove VM / DRV ready.',15.24,269.24)

    s=sheet('A05_ENCODER_NTC','A05 - factory AS5048A / motor NTC probe')
    s.component('J3',78.74,73.66)
    for i,r in enumerate(['R31','R32','R33','R34']):s.component(r,215.9,55.88+i*27.94,90)
    s.component('R30',335.28,55.88,90);s.component('R35',335.28,116.84,90)
    s.component('C51',302.26,154.94);s.component('C52',360.68,154.94);s.pwrflag('5V_ENC',241.3,154.94)
    s.text('J3 pad numbers are A-board labels, not vendor connector geometry.\n1 BLACK GND / 2 RED 5V / 3 GREEN MISO\n4 YELLOW MOSI / 5 BLUE CLK / 6 WHITE CSn\n5V supply, 3V3 SPI logic. Mode 1, initially 2.5 MHz.\nFactory board contains sensor + two capacitors.\nConfirm local 10uF from AS5048A VDD3V to GND before power-on.',17.78,121.92)
    s.text('MOTOR NTC - external insulated 10k@25C / B3950',17.78,190.5)
    s.component('J7',73.66,226.06);s.component('R39',154.94,210.82,90);s.component('R40',233.68,236.22,90);s.component('C50',309.88,236.22)
    s.text('NTC is a separate probe, not confirmed inside the motor.\nSoftware checks open/short; beta conversion at 200 Hz.\n70C warning / 85C fault are initial limits pending thermal validation.',139.7,258.06)

    s=sheet('A06_LINKS_POWER','A06 - board links / fail-low status / bus connectors')
    s.component('J5',60.96,68.58);s.component('J6',177.8,68.58);s.component('J4',327.66,68.58)
    s.text('10P / 0.5mm / lower contact\nNo motor or LED power in FFC.',17.78,99.06)
    s.text('3V3_ESP is a separate power domain.\nVerify FFC pin 1 and cable contact orientation.',139.7,99.06)
    s.component('SW1',335.28,118.11)
    s.component('U8',114.3,147.32)
    for r,x,y in [('R37',48.26,175.26),('R38',172.72,175.26),('R71',48.26,200.66),('R72',172.72,200.66),('R7',48.26,226.06),('R8',172.72,226.06)]:s.component(r,x,y,90)
    s.component('U6',274.32,154.94);s.component('U7',274.32,203.2)
    for r,x,y in [('R13',360.68,144.78),('R19',360.68,170.18),('R9',360.68,200.66),('R36',360.68,226.06)]:s.component(r,x,y,90)
    s.caprow(['C15','C16','C17','C18'],43.18,260.35,45.72)
    s.text('U6/U7 non-inverting open drain.\nPC13 low = not ready / fault.\nDo not merge 3V3 and 3V3_ESP.',241.3,247.65)

    s=sheet('A07_BUS_CONTROL','A07 - motor bus / reset defaults / driver SPI')
    s.component('J1',60.96,53.34);s.component('J2',157.48,53.34);s.component('D1',228.6,53.34,90)
    s.caprow(['C27','C28','C29','C53','C54'],266.7,62.23,30.48)
    s.text('C53/C54 DNP. Local bulk is not a regeneration brake.\nUpstream current limit / reverse protection required.\nIndependent brake remains connected to MOTOR_BUS after isolation.',17.78,93.98)
    s.text('RESET DEFAULTS / CONTROL REQUESTS',17.78,127)
    for r,x,y in [('R10',58.42,149.86),('R11',157.48,149.86),('R12',248.92,149.86),('R44',58.42,195.58),('R45',157.48,195.58),('R46',248.92,195.58),('R47',340.36,195.58)]:s.component(r,x,y,90)
    s.component('C20',332.74,149.86)
    s.text('SPI3 / SDO external 1k pullup / disable UCPD dead-battery pulls',17.78,220.98)
    for r,x,y in [('R14',55.88,246.38),('R15',157.48,246.38),('R16',259.08,246.38),('R17',353.06,246.38),('R18',251.46,271.78)]:s.component(r,x,y,90)
    for net,x in [('MOTOR_BUS',210.82),('5V_LOGIC',271.78),('3V3_ESP',332.74),('GND',386.08)]:s.pwrflag(net,x,116.84)

    s=sheet('A08_TESTPOINTS','A08 - test points / exposed pads (no tall headers)')
    s.text('Test points share the exact named nets on sheets A01-A07. Four ground pads are intentional.\nFinal pad access and spacing are PCB placement tasks. J4 VTREF is not a supply input.',17.78,27.94)
    for i,t in enumerate(DATA['testpoints']):
        x=27.94+(i//14)*76.2;y=63.5+(i%14)*12.7
        s.component(t['ref'],x,y,auto=False)
        pins=lib.pins('TestPoint');p=pins['1'];pt=(x+p['x'],y-p['y'])
        s.wire(pt,(x+15.24,pt[1]));s.label(t['net'],x+15.24,pt[1],180)

    for s in sheets:s.save()
    missing=set(PARTS)-set(PLACED);assert not missing,missing
    for ref,c in PARTS.items():assert PLACED[ref]['pins']==c['pins'],(ref,PLACED[ref]['pins'],c['pins'])
    # Real hierarchical sheets, with global labels carrying cross-sheet connectivity.
    top=f'(kicad_sch (version 20250114) (generator "gl30_schematic") (uuid {q(uid(PROJECT))}) (paper "A3") (title_block (title "GL30 A-board / STM32 + FOC") (date "2026-09-27") (rev "A-SCH-R1") (comment 1 "SCHEMATIC REVIEW - NOT PCB RELEASE")) (lib_symbols)\n'
    top+=f'(text {q("GL30  /  STM32 + FOC"+chr(10)+"A-board schematic review R1")} (at 22.86 20.32 0) {fx(3.2,"left top")} (uuid {q(uid("cover-title"))}))\n'
    for i,s in enumerate(sheets):
        x=25.4+(i%2)*190.5;y=60.96+(i//2)*45.72
        top+=f'(sheet (at {x} {y}) (size 165.1 25.4) (stroke (width 0.254) (type default)) (fill (color 0 0 0 0)) (uuid {q(s.id)}) '+prop('Sheetname',s.title,x,y-2.54,False,1.27,'left')+prop('Sheetfile',s.key+'.kicad_sch',x,y+27.94,False,1.0,'left')+f'(instances (project {q(PROJECT)} (path {q("/"+uid(PROJECT))} (page {q(s.page)})))))\n'
    cover_note='Based on purchased-parts A-SCH-INPUT-20260914 and 2026-09-27 firmware pin reconciliation.\nExternal AS5048A encoder, motor and NTC are outside this PCB. Power/regeneration board B is separate.\nNo PCB routing or hardware qualification is included. See README_CN.md and verification report.'
    top+=f'(text {q(cover_note)} (at 22.86 243.84 0) {fx(1.4,"left top")} (uuid {q(uid("cover-note"))}))\n(sheet_instances (path "/" (page "1"))))\n'
    (HERE/(PROJECT+'.kicad_sch')).write_text(top,encoding='utf-8')
    (HERE/'GL30.kicad_sym').write_text('(kicad_symbol_lib (version 20251024) (generator "gl30_schematic")\n'+'\n'.join(lib.symbols.values())+'\n)\n',encoding='utf-8')
    (HERE/'sym-lib-table').write_text('(sym_lib_table (version 7) (lib (name "GL30") (type "KiCad") (uri "${KIPRJMOD}/GL30.kicad_sym") (options "") (descr "Project-local reviewed symbols")))\n',encoding='utf-8')
    (HERE/'fp-lib-table').write_text('(fp_lib_table (version 7) (lib (name "GL30") (type "KiCad") (uri "${KIPRJMOD}/GL30.pretty") (options "") (descr "Selected official KiCad footprints; review before PCB layout")))\n',encoding='utf-8')
    pro=HERE/(PROJECT+'.kicad_pro')
    if not pro.exists():pro.write_text(json.dumps({'meta':{'filename':pro.name,'version':1},'schematic':{'drawing':{'default_text_size':50.0},'erc':{'erc_exclusions':[]}}},indent=2)+'\n',encoding='utf-8')
    with (HERE/'BOM_board.csv').open('w',encoding='utf-8-sig',newline='') as f:
        writer=csv.writer(f);writer.writerow(['Reference','Value','MPN','Package','Fitted','Footprint','Footprint status','Note'])
        for ref,c in PARTS.items():
            if not ref.startswith('TP'):writer.writerow([ref,c['value'],c.get('mpn',''),c['package'],c.get('fitted',1),fp[ref],'selected - verify' if fp[ref] else 'pending',c.get('note','')])
    manifest={'source':str(SOURCE),'source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'schematic_components':len(PARTS),'placed':PLACED,'source_to_kicad_pin_mapping':{'D1':{'K':'1','A':'2'}},'footprints':lib.footprints,'unassigned_footprints':[r for r in PARTS if not fp[r]],'status':'GENERATED_NOT_YET_ERC_CHECKED'}
    (HERE/'generation_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'sheets':len(sheets)+1,'components':len(PARTS),'unassigned_footprints':manifest['unassigned_footprints']},ensure_ascii=False))

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--runtime',type=Path,required=True);build(ap.parse_args().runtime)
