"""Render real R6/R7 STEP parts to compare manufacturing geometry and orientation."""
from pathlib import Path
import hashlib
import json
import os
import sys

import cadquery as cq
from PIL import Image, ImageDraw, ImageFont, ImageOps

import hidden_screen_study as core
from v7_product_model import _render

ROOT=Path(__file__).resolve().parents[2]
OLD=ROOT/'output/models/GL30_FULL_R6/parts'
NEW=ROOT/'output/models/GL30_FULL_R7/parts'
OUT=ROOT/'output/models/GL30_FULL_R7'
FONT='C:/Windows/Fonts/simhei.ttf'

def read(folder,name,local=False):
    shape=cq.importers.importStep(str(folder/(name+'.step'))).val()
    if local:
        shape=shape.translate(tuple(-v for v in core.CENTER)).rotate((0,0,0),(1,0,0),-26)
    return shape

def ground(shape,dx=0,dy=0):
    b=shape.BoundingBox()
    return shape.translate((dx-(b.xmin+b.xmax)/2,dy-(b.ymin+b.ymax)/2,-b.zmin))

def frame(path,shapes,scale,focal=(0,0,15)):
    _render(path,[(str(i),s,(.24,.48,.55,1),(0,0,0)) for i,s in enumerate(shapes)],
            title='',camera=(140,-210,145),focal=focal,parallel_scale_mm=scale)

def main():
    validation=json.loads((OUT/'verification.json').read_text(encoding='utf8'))
    source=ROOT/'hardware/cad/full_knob_assembly.py'
    assert validation['all_checks_pass'] and validation['model_source_sha256']==hashlib.sha256(source.read_bytes()).hexdigest()
    rows=[
      ('承力架：连续轴承筒 + 浅平底板','31.5 mm 高一体框架','底板最高 4.5 mm；轴承筒车削',
       [ground(read(OLD,'one_piece_press_yoke_and_motor_saddle',True))],
       [ground(read(NEW,'press_base_plate',True),-12),ground(read(NEW,'turned_bearing_cartridge',True),48),ground(read(NEW,'bearing_outer_race_spacer_7mm',True),42,38),ground(read(NEW,'bearing_top_retainer_0p6mm',True),42,-38)],65),
      ('屏柱：直管 + 可开合短夹座','长斜孔与底脚连成一体','8 × 6 直管；夹座可平放打印',
       [ground(read(OLD,'fixed_hollow_screen_post_and_foot'))],
       [ground(read(NEW,'fixed_screen_tube_8x6',True),-18),ground(read(NEW,'fixed_screen_clamp_left'),8),ground(read(NEW,'fixed_screen_clamp_right'),31)],45),
      ('板架：平托盘 + 两只直隔柱','27.2 mm 高悬空横桥','托盘最高 2.2 mm；板子安装高度保持',
       [ground(read(OLD,'PCB_stack_bridge_and_rear_supports'))],
       [ground(read(NEW,'PCB_flat_support_tray')),ground(read(NEW,'PCB_support_post_25mm_-39_43'),-20,-26),ground(read(NEW,'PCB_support_post_25mm_-39_43'),20,-26)],50),
       ('导轨架：背面补平，侧卧打印','原打印方向高度约 56.5 mm','改为 8.5 mm 高；保留侧键让位口',
       [ground(read(OLD,'fixed_rail_spine_and_floor_foot'))],
       [ground(read(NEW,'fixed_rail_spine_and_floor_foot').rotate((0,0,0),(0,1,0),90))],39),
    ]
    canvas=Image.new('RGB',(1680,1900),'#f1f4f7')
    draw=ImageDraw.Draw(canvas)
    titlefont=ImageFont.truetype(FONT,34)
    bodyfont=ImageFont.truetype(FONT,23)
    smallfont=ImageFont.truetype(FONT,21)
    draw.text((36,20),'GL30 R7 | 单套加工与打印优化',font=titlefont,fill='#162f3c')
    for i,(title,before,after,old,new,scale) in enumerate(rows):
        top=88+i*434
        draw.text((36,top),title,font=bodyfont,fill='#1f4959')
        for j,shapes in enumerate((old,new)):
            temp=OUT/f'_dfm_{i}_{j}.png'
            frame(temp,shapes,scale)
            tile=Image.open(temp).convert('RGB')
            # Same CAD camera and scale on both sides; crop empty header only.
            tile=ImageOps.contain(tile,(796,342),Image.Resampling.LANCZOS)
            canvas.paste(tile,(36+j*828+(796-tile.width)//2,top+40+(342-tile.height)//2))
            draw.text((50+j*828,top+378),('R6  '+before) if j==0 else ('R7  '+after),font=smallfont,fill='#344c57')
    draw.text((36,1844),'真实 STEP 几何示意；不是加工报价、刚度试验或最终制造图。',font=smallfont,fill='#5b6770')
    target=OUT/'manufacturing_comparison_r7.png'
    canvas.save(target)
    (OUT/'manufacturing_render_record.json').write_text(json.dumps({'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'image':target.name,'source':'R6 and R7 exported STEP geometry','note':'same camera per comparison row; no simulated load or cost result'},ensure_ascii=False,indent=2),encoding='utf8')
    print(target,flush=True)

if __name__=='__main__':
    try:main();code=0
    except Exception:
        import traceback;traceback.print_exc();code=1
    sys.stdout.flush();sys.stderr.flush();os._exit(code)
