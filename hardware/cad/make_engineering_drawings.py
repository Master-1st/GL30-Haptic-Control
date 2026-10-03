"""Dimension current STEP-derived views in six A3 prototype review sheets.

Coordinates are paper millimetres. View.scale is a real paper/model ratio.
Dimension witness points and real geometry use the identical coordinate map.
"""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas

FONT = "GL30_SimHei"
TITLES = ["整机外形与视图基准", "隐藏式侧后按键 / 壳体开口", "键帽零件 / 挡边与丝印",
          "旋环帽 / 收腰曲面与黑色过渡肩", "旋环袖套 / 轴承配合保留项", "试制公差 / 配合条件与验收要求"]


def text(c, x, y, value, size=10, align="left", color=(0, 0, 0)):
    value = str(value).replace("Ø", "Φ")  # SimHei has Greek Phi, not U+00D8.
    missing = {ch for ch in value if ord(ch) not in pdfmetrics.getFont(FONT).face.charToGlyph}
    if missing:
        raise ValueError(f"Missing drawing glyphs: {missing}")
    c.setFillColorRGB(*color)
    c.setFont(FONT, size / mm)
    getattr(c, {"left": "drawString", "center": "drawCentredString", "right": "drawRightString"}[align])(x, y, str(value))
    c.setFillColorRGB(0, 0, 0)


def notes(c, x, y, width, lines, size=10, leading=5.0):
    """Measure Chinese glyph widths; reject overflow rather than clip text."""
    for source in lines:
        source = str(source).replace("Ø", "Φ")
        line_text = ""
        for char in re.findall(r"[A-Za-z0-9_.+±Φ°/-]+|.", source):
            if line_text and char not in "。，；：、！）" and pdfmetrics.stringWidth(line_text + char, FONT, size) / mm > width:
                if y < 37:
                    raise ValueError("Notes overflow into title block")
                text(c, x, y, line_text, size)
                y -= leading
                line_text = ""
            line_text += char
        if y < 37:
            raise ValueError("Notes overflow into title block")
        text(c, x, y, line_text, size)
        y -= leading + 1
    return y


def line(c, a, b, width=0.18):
    c.setStrokeColorRGB(0, 0, 0)
    c.setLineWidth(width)
    c.line(*a, *b)


def arrow(c, x, y, dx, dy):
    length, half = 1.8, 0.45
    p = c.beginPath()
    p.moveTo(x, y)
    p.lineTo(x + length * dx - half * dy, y + length * dy + half * dx)
    p.lineTo(x + length * dx + half * dy, y + length * dy - half * dx)
    p.close()
    c.drawPath(p, stroke=0, fill=1)


def dh(c, a, b, y, label):
    a, b = sorted((a, b), key=lambda q: q[0])
    for px, py in (a, b):
        line(c, (px, py), (px, y + (1 if y > py else -1)), 0.12)
    line(c, (a[0], y), (b[0], y))
    arrow(c, a[0], y, 1, 0)
    arrow(c, b[0], y, -1, 0)
    text(c, (a[0] + b[0]) / 2, y + 1.4, label, 9, "center")


def dv(c, a, b, x, label):
    a, b = sorted((a, b), key=lambda q: q[1])
    for px, py in (a, b):
        line(c, (px, py), (x + (1 if x > px else -1), py), 0.12)
    line(c, (x, a[1]), (x, b[1]))
    arrow(c, x, a[1], 0, 1)
    arrow(c, x, b[1], 0, -1)
    c.saveState()
    c.translate(x - 1.4, (a[1] + b[1]) / 2)
    c.rotate(90)
    text(c, 0, 0, label, 9, "center")
    c.restoreState()


def leader(c, start, end, label):
    line(c, start, end, 0.15)
    c.circle(*start, 0.35, stroke=0, fill=1)
    text(c, end[0] + 1, end[1], label, 9)


class View:
    def __init__(self, c, data, name, cx, cy, scale, title, crop=None):
        self.c = c
        self.data = data["views"][name]
        self.bounds = crop or self.data["bounds"]
        u0, v0, u1, v1 = self.bounds
        self.scale = scale
        self.ox = cx - (u0 + u1) * scale / 2
        self.oy = cy - (v0 + v1) * scale / 2
        c.saveState()
        if crop:
            path = c.beginPath()
            x, y = self.map(u0, v0)
            path.rect(x, y, (u1 - u0) * scale, (v1 - v0) * scale)
            c.clipPath(path, stroke=0)
        for poly in self.data["polylines"]:
            path = c.beginPath()
            path.moveTo(*self.map(*poly[0]))
            for point in poly[1:]:
                path.lineTo(*self.map(*point))
            c.setLineWidth(0.20)
            c.drawPath(path, stroke=1, fill=0)
        for loop in self.data.get("section_loops", []):
            c.saveState()
            path = c.beginPath()
            path.moveTo(*self.map(*loop[0]))
            for point in loop[1:]:
                path.lineTo(*self.map(*point))
            path.close()
            c.clipPath(path, stroke=0)
            x0, y0 = self.map(u0, v0)
            x1, y1 = self.map(u1, v1)
            c.setLineWidth(0.10)
            for offset in range(-450, 451, 2):
                c.line(x0 + offset, y0 - 1, x0 + offset + y1 - y0 + 2, y1 + 1)
            c.restoreState()
        c.restoreState()
        text(c, cx, cy + (v1-v0)*scale/2 + 7, f"{title}   {scale:g}:1", 10, "center")

    def map(self, u, v):
        return self.ox + u * self.scale, self.oy + v * self.scale

    def centerline(self, a, b):
        self.c.setDash([4, 1, 1, 1])
        line(self.c, self.map(*a), self.map(*b), 0.12)
        self.c.setDash()


def frame(c, data, page):
    c.saveState()
    c.scale(mm, mm)
    c.setLineWidth(0.25)
    c.rect(8, 8, 404, 281, stroke=1, fill=0)
    line(c, (8, 30), (412, 30), 0.25)
    text(c, 16, 279, f"GL30   /   {TITLES[page-1]}", 17)
    text(c, 404, 279, "DRAFT · 试制设计评审", 11, "right", (0.70, 0.10, 0.08))
    text(c, 16, 21, f"图号 GL30-MECH-{page:02d}    当前工程 · 不升版", 10)
    text(c, 172, 21, "单位 mm | 第三角布局 | 比例见各图", 10)
    text(c, 404, 21, f"{page} / 6", 10, "right")
    text(c, 16, 13, "材料暂定：铝合金旋钮 + 打印壳体；禁止整机加工放行。图纸尺寸优先，禁止量图取数。", 9)
    text(c, 404, 13, "CAD ≠ 实物验证", 9, "right", (0.70, 0.10, 0.08))


def page1(c, d):
    f = View(c,d,"assembly_front",96,99,1,"正视图")
    t = View(c,d,"assembly_top",96,211,1,"俯视图")
    s = View(c,d,"assembly_side",246,99,1,"右视图")
    for view, dim_y in ((f,51),(s,51)):
        u0,v0,u1,v1=view.bounds
        dh(c,view.map(u0,v0),view.map(u1,v0),dim_y,f"{u1-u0:.2f} REF 总包络")
    u0,v0,u1,v1=f.bounds
    dv(c,f.map(u0,v0),f.map(u0,v1),33,f"{v1-v0:.2f} REF")
    f.centerline((0,-3),(0,66))
    t.centerline((0,-50),(0,50))
    notes(c,180,250,216,[
        "设计方向：黑色收腰旋钮、黑色固定屏幕、蓝色位置标记、浅银色底座。四个功能键移至侧后方浅槽，正面不增键。",
        "主壳体：96 × 96；前高20，后高52；斜面26°，后部有水平平台。总包络尺寸包含脚垫等突出部分。",
        "工程坐标：X向右、Y向后、Z向上。壳体中心X=Y=0；前端Y=-48，后端Y=+48；底壳外底面Z=0（不含脚垫）。",
        "本图仅列外形基准，轴承轴线、安装面及紧固件还未组成正式装配公差基准体系。",
        "保留原电机、显示板、轴承、电池和电子板包络；该图不能证明它们已经可装配、可受力或可通电。",
    ],11,6.0)


def page2(c,d):
    v=View(c,d,"assembly_side",119,208,5,"右侧键区放大（左侧镜像）",[10.5,39.3,44.5,46.7])
    v.centerline((11,43),(44,43))
    for u in (20,35): v.centerline((u,39),(u,47))
    dh(c,v.map(20,43),v.map(35,43),158,"15.00 中心距 REF")
    dh(c,v.map(11,43),v.map(44,43),146,"33.00 ±0.10")
    dv(c,v.map(27.5,39.7),v.map(27.5,46.3),227,"6.60 ±0.10")
    notes(c,248,244,146,[
        "每侧两个键，共四键；按键位于靠后的侧壁上段，手指可从侧后触达。便捷性需手握模型确认。",
        "槽中心：Y27.50、Z43.00；槽总长33、高6.6、深0.60；两端R3.30。",
        "键孔中心：Y20.00与Y35.00，Z43.00。距壳体前基准面分别68.00与83.00；位置试制目标±0.10。",
        "单键通孔11.40 × 4.40，各±0.10，两端R2.20。严禁误用12.40 × 5.40挡边尺寸开孔。",
        "键面内收0.30 ±0.10；比槽底高出0.30标称。不是向壳外突出。",
    ],10,5.3)
    s=View(c,d,"assembly_side",81,85,0.75,"键区位置参照")
    leader(c,s.map(27.5,43),(148,108),"侧后方")
    dv(c,s.map(35,0),s.map(35,43),133,"43.00 ±0.10")
    dh(c,s.map(-48,0),s.map(20,43),49,"68.00 ±0.10")
    dh(c,s.map(-48,0),s.map(35,43),39,"83.00 ±0.10")
    notes(c,180,115,215,[
        "侧壁厚3.00为当前CAD值。开口及凹槽尺寸均为涂装后的目标；打印工艺不能保证±0.10时，单独修配键孔。",
        "按键与壳体采用同色、同光泽表面；缝隙不刻意涂黑。单点/双点灰色小标记用于区分按键，不印大字和醒目功能图标。",
        "底盖已可拆，键帽从壳体内侧装入。真实开关、PCB、回弹、内向限位及整机装配顺序：HOLD。",
        "内收0.30是装配验收值，不能只靠3.00壁厚与2.70体深自动保证；需实测壁厚及挡边接触面，再修配键帽体深。",
    ],10,5.5)


def page3(c,d):
    f=View(c,d,"button_face",92,199,10,"键帽面视（示1号键）")
    s=View(c,d,"button_section",269,199,10,"A-A 中心剖面")
    f.centerline((0,-3.5),(0,3.5))
    f.centerline((-7,0),(7,0))
    c.setFillColorRGB(.43,.46,.50)
    c.circle(*f.map(0,0),3,stroke=0,fill=1)
    c.setFillColorRGB(0,0,0)
    text(c,*f.map(-.5,4.2),"A",9,"center")
    text(c,*f.map(0,-4.2),"A",9,"center")
    for z in (-3.4, 3.4):
        line(c,f.map(-.6,z),f.map(0,z),.35)
        arrow(c,*f.map(0,z),-1,0)
    dh(c,f.map(-5.5,-2),f.map(5.5,-2),151,"11.00 ±0.10")
    dh(c,f.map(-6.2,-2.7),f.map(6.2,-2.7),136,"12.40 ±0.10")
    dv(c,f.map(5.5,-2),f.map(5.5,2),163,"4.00 ±0.10")
    dv(c,f.map(6.2,-2.7),f.map(6.2,2.7),177,"5.40 ±0.10")
    dh(c,s.map(0,-2),s.map(2.7,-2),151,"2.70 ±0.10")
    dh(c,s.map(-.6,-2.7),s.map(0,-2.7),136,"0.60 ±0.10")
    dv(c,s.map(2.7,-2),s.map(2.7,2),301,"4.00 ±0.10")
    leader(c,s.map(-.3,2.5),(324,229),"内挡边：仅防向外脱出")
    notes(c,22,111,376,[
        "数量4；两端圆弧：键帽R2.00、内挡边R2.70。键帽体与内挡边为单个连续实体，未包含电气开关。材料先随打印壳体做形状样。",
        "导向孔11.40 × 4.40，与键帽名义单边缝0.20；挡边相对孔口名义单边搭接0.50。尺寸均在涂层/丝印完成后测量。",
        "低对比丝印：点径Ø0.60；1号键单点，2号键双点、点中心距1.30；左右相同。颜色灰、同光泽，位置以键面中心对称，不雕透。",
        "0.40向内移动仅通过CAD空间检查，不是规定工作行程。开关触发、回弹、过行程与内向硬限位未选定，禁止据此加工完整按键机构。",
        "先做一组孔与键帽的配合试片，去毛刺并完成涂装后检查四周间隙、不卡擦；最终按压行程和手感必须装真实开关验收。",
    ],11,5.8)


def page4(c,d):
    t=View(c,d,"ring_cap_top",84,204,2,"旋环帽顶视")
    s=View(c,d,"ring_cap_section",270,238,2,"A-A 整体剖面")
    t.centerline((-29,0),(29,0))
    text(c,*t.map(-30,0),"A",9,"right")
    text(c,*t.map(30,0),"A",9)
    dh(c,t.map(-27,0),t.map(27,0),139,"Ø54.00 REF 最大包络")
    dh(c,s.map(-20,7.8),s.map(20,7.8),218,"Ø40.00 ±0.05 有效包络")
    q=View(c,d,"ring_cap_section",270,169,8,"右侧母线局部",[19.8,7.6,27.2,13.4])
    dv(c,q.map(27,7.8),q.map(24.9,13.2),320,"5.40 轴向高度")
    leader(c,q.map(25,10.4),(334,176),"r25 @ z10.4")
    leader(c,q.map(25.5,12),(334,192),"r25.5 @ z12.0")
    leader(c,q.map(24.9,13.2),(334,207),"r24.9 @ z13.2")
    leader(c,q.map(20.6,13),(179,203),"黑色过渡肩")
    notes(c,18,115,385,[
        "旋环局部坐标：z沿旋转轴；z0为该轴与斜面基准的交点，r为离轴距离，不是曲线的圆角半径。帽件范围z7.8～13.2。",
        "外母线采用以下三段相切三次Bezier，点顺序P0/P1/P2/P3，单位(r,z)mm；精确曲面以同源STEP为准，不用样条自由重画。",
        "段1：(27,7.8) / (27,8.9) / (25,9.1) / (25,10.4)",
        "段2：(25,10.4) / (25,11.2) / (25.5,11.2) / (25.5,12.0)",
        "段3：(25.5,12.0) / (25.5,12.7) / (25.2,13.2) / (24.9,13.2)",
        "过渡肩：r20～21.2台面z13.0，外侧在r21.2～21.5升至z13.2。蓝标槽2.8×1.0、深0.10，径向中心r23.2。黑色处理；蓝色填漆。",
        "6061铝合金仅为报价/形状样件的材料候选；表面触感需样板确认。帽与袖套的连接及轴向定位未完成，禁止独立放行功能件。",
    ],10,5.1)


def page5(c,d):
    s=View(c,d,"ring_sleeve_section",112,224,3,"A-A 袖套轴向剖面")
    dh(c,s.map(-26.1,3),s.map(26.1,3),197,"Ø52.20 REF / HOLD")
    dh(c,s.map(-27,.8),s.map(27,.8),181,"Ø54.00 REF")
    dv(c,s.map(27,.8),s.map(27,7.8),207,"7.00 REF")
    q=View(c,d,"ring_sleeve_section",289,205,10,"单侧薄壁局部",[25.9,.6,27.2,8.0])
    leader(c,q.map(26.1,4.3),(325,218),"内壁r26.1 REF")
    leader(c,q.map(26.8,3.5),(325,186),"槽根外包络r26.8")
    text(c,20,158,"轴承配合未冻结：Ø52.20 不是加工公差，不得直接套 H7 / k6。",14,color=(.7,.1,.08))
    notes(c,20,141,377,[
        "轴承目前只预留6808的40×52×7尺寸包络。真实品牌、游隙、密封、承载及阻力预算未确定，不能由名义尺寸直接决定压配。",
        "袖套原始内孔Ø52.20，相对名义Ø52轴承外圈径向间隙0.10，只用于数字装配。该松间隙不能保证同心、无滑移或传递力矩。",
        "名义套壁0.90；±45°两向浅菱纹共用总去除深度0.20上限，槽根名义壁0.70。不可把两向各削0.20，也不可按强度已合格处理。",
        "纹理范围z1.60～7.00，周向每向64道，槽宽0.55；上下光面带各0.80；下缘圆顺内收0.20。整体高度z0.80～7.80。",
        "薄壁圈压配可改变轴承游隙和手感。配合、薄壁变形、阳极尺寸变化和轴向固定必须联合复核；先加工外形样，不直接做承载成品。",
        "袖套与旋环帽尚未设计传扭连接。两支架已各自连成单实体、底盖可拆；实际支承、电机和屏幕安装仍HOLD。",
    ],11,6.0)
    text(c,20,50,"依据：NSK · Getting a good fit（仅原则参考，不是本项目配合批准）",9)
    c.linkURL(d["sources"][0],(20,47,225,54),relative=1,thickness=0)


def table(c, rows, top):
    xs=[16,102,174,231,332,404]
    headers=["特征", "公称尺寸", "试制公差", "配合 / 条件", "状态"]
    y=top
    for a,b,h in zip(xs,xs[1:],headers): text(c,a+2,y,h,10)
    line(c,(16,y-2),(404,y-2),.3)
    y-=9
    for row in rows:
        cells=[row[k] for k in ("feature","nominal","tolerance","fit","status")]
        bottoms=[]
        for a,b,value in zip(xs,xs[1:],cells):
            bottoms.append(notes(c,a+2,y,b-a-4,[value],9,4.2))
        y=min(bottoms)
        line(c,(16,y+3),(404,y+3),.10)
        y-=1
    return y


def page6(c,d):
    y=table(c,d["tolerance_rows"],261)
    text(c,18,y-2,"配合条件与设计保留项（HOLD）",12)
    y=notes(c,18,y-10,382,[f"{i+1}. {s}" for i,s in enumerate(d["holds"])],9,4.4)
    y-=1
    text(c,18,y,"试制验收顺序",11)
    y=notes(c,18,y-6,382,[
        "外形样检查可见性/握持 → 涂装后键孔试片配合 → 实际开关的行程/限位/回弹 → 真实轴承与盖板整周清距 → 受力及耐久验证。",
        "公差为本项目试制建议，均指成品表面，不声称符合某一未核实的通用公差标准。标REF/HOLD项不适用未注公差。",
    ],9,4.4)
    text(c,18,45,f"来源校验：外观 STEP SHA-256 {d['source_sha256']['exterior'][:20]}…；完整值见 drawing_geometry.json。",8)
    text(c,18,39,"几何与STEP回读通过仅证明所列数字检查；未在SolidWorks桌面、真实装配或按键寿命试验中验证。",8)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input",type=Path,default=Path("hardware/cad/out/CONCEPT_FIT_DEFAULTS/drawing_geometry.json"))
    parser.add_argument("--output",type=Path,default=Path("output/pdf/GL30_Current_Prototype_Drawings_CN.pdf"))
    args=parser.parse_args()
    font_path=Path(r"C:\Windows\Fonts\simhei.ttf")
    if not font_path.exists(): raise FileNotFoundError("Required Chinese font: " + str(font_path))
    pdfmetrics.registerFont(TTFont(FONT,str(font_path)))
    data=json.loads(args.input.read_text(encoding="utf-8"))
    if not all(data["validation"].values()): raise ValueError("Unverified input geometry")
    args.output.parent.mkdir(parents=True,exist_ok=True)
    c=canvas.Canvas(str(args.output),pagesize=(420*mm,297*mm),pageCompression=1)
    c.setTitle("GL30 当前工程 · 试制设计评审图")
    c.setAuthor("GL30 project")
    for i,draw in enumerate((page1,page2,page3,page4,page5,page6),1):
        frame(c,data,i)
        draw(c,data)
        c.restoreState()
        c.showPage()
    c.save()
    print(f"Created 6 A3 sheets: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
