"""Dimensioned mechanical review PDF from the current layout JSON."""
import json
from math import cos, sin, radians, sqrt
from pathlib import Path

from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.units import mm
from pypdf import PdfReader

ROOT=Path(__file__).resolve().parents[2]
MODEL=ROOT/'output/models/GL30_PCB_1P2'
OUT=ROOT/'output/pdf/GL30_1p2mm_PCB_尺寸与装配设计.pdf'
OUT.parent.mkdir(parents=True,exist_ok=True)
D=json.loads((MODEL/'layout.json').read_text(encoding='utf-8'))
pdfmetrics.registerFont(TTFont('CN','C:/Windows/Fonts/simhei.ttf'))
C=canvas.Canvas(str(OUT),pagesize=(297*mm,210*mm))
C.setTitle('GL30 1.2 mm PCB 板框与装配设计 - 2026-09-10')
C.setAuthor('GL30 project')
BLUE='#23415C'; GREY='#546577'; BLACK='#142331'

def text(x,y,s,size=10,color=BLACK,align='left'):
    C.setFont('CN',size);C.setFillColor(color)
    method={'left':C.drawString,'center':C.drawCentredString,'right':C.drawRightString}[align]
    method(x*mm,y*mm,str(s).replace('Ø','Φ'))

def line(x1,y1,x2,y2,color=GREY,width=.3):
    C.setStrokeColor(color);C.setLineWidth(width*mm)
    C.line(x1*mm,y1*mm,x2*mm,y2*mm)

def rect(x,y,w,h,fill=None,stroke=GREY,r=0):
    C.setStrokeColor(stroke);C.setLineWidth(.25*mm)
    if fill:C.setFillColor(fill)
    C.roundRect(x*mm,y*mm,w*mm,h*mm,r*mm,stroke=1,fill=int(fill is not None))

def circle(x,y,r,fill=None,stroke=GREY):
    C.setStrokeColor(stroke);C.setLineWidth(.25*mm)
    if fill:C.setFillColor(fill)
    C.circle(x*mm,y*mm,r*mm,stroke=1,fill=int(fill is not None))

def arrow(x,y,dx,dy):
    L=sqrt(dx*dx+dy*dy);dx/=L;dy/=L
    p=C.beginPath();p.moveTo(x*mm,y*mm)
    p.lineTo((x+dx*1.8-dy*.5)*mm,(y+dy*1.8+dx*.5)*mm)
    p.lineTo((x+dx*1.8+dy*.5)*mm,(y+dy*1.8-dx*.5)*mm);p.close()
    C.setFillColor(GREY);C.drawPath(p,fill=1,stroke=0)

def dimx(x1,x2,y,ref,label):
    line(x1,ref,x1,y+1);line(x2,ref,x2,y+1);line(x1,y,x2,y)
    arrow(x1,y,1,0);arrow(x2,y,-1,0);text((x1+x2)/2,y+1.7,label,9,align='center')

def dimy(y1,y2,x,ref,label):
    line(ref,y1,x+1,y1);line(ref,y2,x+1,y2);line(x,y1,x,y2)
    arrow(x,y1,0,1);arrow(x,y2,0,-1)
    C.saveState();C.translate((x-2)*mm,(y1+y2)/2*mm);C.rotate(90)
    C.setFont('CN',9);C.setFillColor(GREY);C.drawCentredString(0,0,label);C.restoreState()

def notes(x,y,lines,w=116,size=9,leading=5.3):
    for value in lines:
        row=''
        for char in value:
            if pdfmetrics.stringWidth(row+char,'CN',size)>w*mm and char not in '，。；：、）】':
                text(x,y,row,size);y-=leading;row=''
            row+=char
        text(x,y,row,size);y-=leading
    return y

def base(page,title,subtitle):
    rect(0,188,297,22,fill=BLUE,stroke=BLUE)
    text(12,198,title,17,'#FFFFFF');text(12,191,subtitle,9,'#DCE8F2')
    line(12,16,285,16)
    text(12,10,'GL30 / PCB_1P2_20260910 / 单位 mm / 未布线，供机械设计评审',8,GREY)
    text(285,10,f'{page} / 4',9,GREY,'right')

def image(path,x,y,w,h):
    C.drawImage(str(path),x*mm,y*mm,width=w*mm,height=h*mm,preserveAspectRatio=True,anchor='c')

def tab(x,y,widths,rows,height=10,head=True):
    for i,row in enumerate(rows):
        xx=x
        for w,v in zip(widths,row):
            fill=BLUE if i==0 and head else ('#EFF4F7' if i%2 else '#FFFFFF')
            rect(xx,y-height,w,height,fill=fill,stroke='#D2DCE3')
            text(xx+2,y-height/2-1,str(v),9,'#FFFFFF' if i==0 and head else BLACK)
            xx+=w
        y-=height
    return y

base(1,'三块 1.2 mm 板：板框与整机装配','118 × 114 mm 底座 / 3S 电池横置后仓 / 固定连续灯环')
image(MODEL/'assembly.png',10,38,157,143)
tab(173,174,[18,60,28],[
 ['板件','外形','层数 / 厚度'],['A','98 × 34，四角 R3','4 / 1.2'],
 ['B','98 × 34，四角 R3','4 / 1.2'],['C','Φ74 / Φ56，带侧耳','2 / 1.2']],12)
notes(173,113,[
 'C 板最大包络 74 × 84；局部 Y 正向为接线耳，装配时旋转 -90°，耳朝底座右侧。',
 'A/B 共用 88 × 26 安装孔距；4-Φ2.7 非金属化孔，配 M2.5 支撑件。',
 '电池基准：LPHD5919096，3S 11.1 V / 1100 mAh；厂家最大 101 × 20 × 18。',
 '电池腔：107 × 26 × 24。三块板、支架及电池均已进入真实电机和屏幕的装配模型。',
 '握环在本布板模型中简化纹理，保留收腰轮廓与最大直径；无固定蓝漆标记、无旧前灯条。',
],w=106,size=10,leading=5.5)
text(15,28,'蓝色是连续扩散环的示意色；灯效、亮度与均匀性需要实物光学验证。',9,GREY)
C.showPage()

base(2,'A / B 板外形与安装基准','A：电机主控；B：电源接口。两块板框相同，器件布置不同。')
for k,cy in [('A',140),('B',65)]:
    cx=79;s=1.22;b=D['boards'][k]
    rect(cx-49*s,cy-17*s,98*s,34*s,fill='#E8F1EC' if k=='A' else '#E4EFF6',r=3*s)
    for x,y,d in b['holes']:
        circle(cx+x*s,cy+y*s,d/2*s,fill='#FFFFFF');circle(cx+x*s,cy+y*s,3.1*s,stroke='#C1CDD6')
    for name,x,y,w,h,z in D['components'][k]:
        rect(cx+(x-w/2)*s,cy+(y-h/2)*s,w*s,h*s,fill='#97AAB8',stroke='#6A8293')
    text(cx,cy,k,18,BLUE,'center')
    dimx(cx-49*s,cx+49*s,cy+27*s,cy+17*s,'98.00')
    dimy(cy-17*s,cy+17*s,cx-57*s,cx-49*s,'34.00')
    dimx(cx-44*s,cx+44*s,cy-25*s,cy-13*s,'88.00 孔距')
    dimy(cy-13*s,cy+13*s,cx+56*s,cx+44*s,'26.00')
tab(160,174,[35,32,37],[['安装孔','X','Y'],['1','-44.00','-13.00'],
 ['2','-44.00','+13.00'],['3','+44.00','-13.00'],['4','+44.00','+13.00']],10)
notes(160,116,[
 '坐标原点：板外形中心；+Y 朝 USB-C 所在后沿。厚度 1.20，4-Φ2.70 NPTH，外角 R3。',
 '孔周 Φ6.2 为机械支撑接触区；建议同区禁放器件、禁布铜，层叠和孔边工艺由 PCB 厂确认。',
 '板底装配位置：B 的 Z=32.00；A 的 Z=45.20。B 上表面到 A 下表面用 12.00 mm 支柱。',
 '本轮元件框是主要器件/连接器的空间预留，包含装配余量；不等于正式封装或已完成布线。',
 'B 后沿 USB-C 预留跨出板边；穿壳开口 10.4 × 4.6，中心 Z=34.8。最终以 USB4105 实物与封装图核对。',
 '尺寸建议公差：板框 ±0.15、孔位 ±0.10、板厚 ±0.10。这里是设计目标，非工厂承诺。',
],w=111,size=9,leading=5)
C.showPage()

base(3,'C 板：固定灯环、按压与侧耳','板局部坐标 / Φ74 外圆 + 24 mm 宽耳 / 最大 74 × 84 / 2 层 × 1.2 mm')
cx=85;cy=96;s=1.5
# Circle and ear are drawn with exact intersecting outer contour.
r=37.;ex=12.;ey=sqrt(r*r-ex*ex);ang=__import__('math').degrees(__import__('math').atan2(ey,ex))
p=C.beginPath();p.moveTo((cx+ex*s)*mm,(cy+47*s)*mm)
p.lineTo((cx-ex*s)*mm,(cy+47*s)*mm);p.lineTo((cx-ex*s)*mm,(cy+ey*s)*mm)
p.arcTo((cx-r*s)*mm,(cy-r*s)*mm,(cx+r*s)*mm,(cy+r*s)*mm,startAng=180-ang,extent=180+2*ang)
p.lineTo((cx+ex*s)*mm,(cy+47*s)*mm);p.close()
C.setFillColor('#EAF1F5');C.setStrokeColor(BLUE);C.drawPath(p,stroke=1,fill=1)
circle(cx,cy,28*s,fill='#FFFFFF',stroke=BLUE)
for i in range(24):
    a=radians(i*15);x=32*cos(a);y=32*sin(a)
    C.saveState();C.translate((cx+x*s)*mm,(cy+y*s)*mm);C.rotate(i*15)
    rect(-s,-s,2*s,2*s,fill='#38A9D5');C.restoreState()
for x,y,d in D['boards']['C']['holes']:circle(cx+x*s,cy+y*s,d/2*s,fill='#FFFFFF')
for name,x,y,w,h,z in D['components']['C']:
    rect(cx+(x-w/2)*s,cy+(y-h/2)*s,w*s,h*s,fill='#A5B7C2')
line(cx-7,cy,cx+7,cy);line(cx,cy-7,cx,cy+7)
text(cx,cy+22,'内孔 Φ56.00',11,BLUE,'center');text(cx,cy+16,'24 LED / R32.00',10,GREY,'center')
dimx(cx-37*s,cx+37*s,cy-46*s,cy-37*s,'外圆 Φ74.00')
dimx(cx-12*s,cx+12*s,cy+54*s,cy+47*s,'耳宽 24.00')
dimy(cy-37*s,cy+47*s,cx-47*s,cx-37*s,'总高 84.00')
tab(165,174,[20,30,30,24],[['孔','X','Y','孔径']]+[
 [i+1,f'{x:.3f}',f'{y:.3f}',f'{d:.2f}'] for i,(x,y,d) in enumerate(D['boards']['C']['holes'])],10)
notes(165,124,[
 '孔分布圆半径 R34；角度 7.5°、127.5°、247.5°，从 +X 逆时针计。',
 'LED 中心 R32，24 等分，间隔 15°；2 × 2 × 0.9 为封装空间预留。',
 '连续扩散罩：外径 74、内径 56、厚 1.2。灯腔不设置径向分隔墙；内外侧黑色遮光。',
 'PCB 底面在斜面法向 n=-5.2；LED 顶 n=-3.1；扩散罩底 n=-0.2，留 2.9 mm 混光距离。',
 '旋转套最大 Φ54，对固定灯环内孔名义径向间隙 1.0。0.4 mm 全行程空间已检查。',
 '侧耳为按压开关、缓冲器、接插件、环境光窗口留位。按压传力机构、开关触发行程另行定型。',
],w=111,size=9,leading=5.1)
C.showPage()

base(4,'装配剖面、电池与验证边界','A/B 水平叠放；C 随 26° 斜面固定。标注均为名义设计值。')
image(MODEL/'section.png',8,62,161,115)
tab(171,174,[67,38],[['项目','尺寸 / 位置'],
 ['底座宽 × 深','118 × 114'],['壳体前高 / 后高','20 / 58'],['壳壁 / 底盖','3 / 3'],
 ['电池最大包络','101 × 20 × 18'],['电池腔净包络','107 × 26 × 24'],
 ['后部线束通道','101 × 7 × 18'],['A 高器件至内顶',f"{D['board_A_roof_clearance_mm']:.2f}"],
 ['B 高器件至 A 板底',f"{D['board_B_to_A_max_height_clearance_mm']:.2f}"],
 ['电池顶至 B 板底',f"{D['battery_to_B_bottom_clearance_mm']:.2f}"]],10)
notes(15,61,[
 '电池依据：LiPol LPHD5919096 3S 官方数据表（2022-02-15）。含 PCM，无 NTC、无接插件；50±5 mm 引线。',
 '设计限制：最大充电 0.5 A、放电 3 A；充电环境 10~45°C。均衡、逐节保护及接口兼容性须向供应商确认。',
 '本模型验证了板框导出回读、主要空间预留与整机干涉。未完成 PCB 布线、ERC/DRC、热验证或实物按压验证。',
 '电池腔是设计余量，不是厂家承诺的膨胀上限。NTC 位置和绝缘安装需随电池样品确认。25 W 制动电阻为壳外台架件。',
 '官方屏幕 STEP 存在无效几何；保留原始引用，避让检查改用包含其完整包络的有效圆柱。58 为后平台标高，灯环唇边约 58.66。',
],w=264,size=9,leading=5)
text(15,22,'资料：LiPol 官方电池尺寸与电气数据表',8,BLUE)
C.linkURL(D['battery']['source'],(15*mm,20*mm,160*mm,25*mm),relative=0)
C.showPage();C.save()
r=PdfReader(str(OUT))
assert len(r.pages)==4
content='\n'.join(p.extract_text() for p in r.pages)
for required in ['98.00','84.00','74.00','1.20','1100','118']:
    assert required in content,required
print(json.dumps({'output':str(OUT),'pages':len(r.pages),'text_checks':True},ensure_ascii=False))
