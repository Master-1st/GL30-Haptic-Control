"""Explicit offline asset conversion / LEDFont generation for the GL30 demo.

Regenerate only intentionally; normal builds use checked-in generated assets.
Font protocol was read from the live agent-api.md before first use.
"""
from pathlib import Path
import ctypes
import json
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'components/gl30_ui/assets'
OUT.mkdir(parents=True, exist_ok=True)
TEXT = ('日照金山星期一二三四五六年月日晴多云天气示例温度湿度微风'
        '计时器音量秒表闹钟手感设置日历灯效调整旋转选择单击确认双击返回'
        '开始暂停继续完成已到下限上限静音运行中小时分钟秒'
        '效果颜色亮度随功能常亮呼吸流动橙色金色绿色紫色玫红浅柠黄'
        '屏幕待接入待校时轻柔标准清晰预览未开启启用关闭编辑重置'
        '按下开始转动设置时间计时结束轻按清零取消保存桌面本地进入'
        '照片表盘无操作长按保留今天现在低高确定设备日期提示'
        '显示与操作教程功能当前打开步进提醒单位摄氏华氏周起始'
        '返回上一级主菜单调整功能页应用确认退出帮助系统百语言中文'
        '电机失联自检对齐就绪故障异常'
        'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789%:/.- °·—&')
TEXT = ''.join(dict.fromkeys(TEXT))
TITLE_TEXT = ''.join(dict.fromkeys(
    '日照金山晴计时器运行中暂停计时结束准备音量静音开启天气日历待校时'
    '系统设置显示与亮度操作教程单击双击长按功能闹钟关闭轻柔语言中文'
    'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789% °·&/-'))

def _float32(value):
    return ctypes.c_float(value).value

def _scrim_alpha(y):
    """Return the exact uint8 alpha produced by gl30_render.c's float path."""
    if y < 190:
        slope = _float32(_float32(_float32(0.25) * _float32(y)) /
                         _float32(190))
        alpha = _float32(_float32(0.42) - slope)
    elif y > 295:
        slope = _float32(_float32(_float32(0.57) * _float32(y - 295)) /
                         _float32(171))
        alpha = _float32(_float32(0.22) + slope)
    else:
        alpha = _float32(0.12)
    scaled = _float32(_float32(alpha) * _float32(255))
    return int(max(0.0, min(255.0, scaled)))

def _blend_black_rgb565(pixel, alpha):
    """Match kk_oled.c's +127/255 RGB565 blend with a black source."""
    inverse = 255 - alpha
    red = (((pixel >> 11) * inverse) + 127) // 255
    green = ((((pixel >> 5) & 0x3f) * inverse) + 127) // 255
    blue = (((pixel & 0x1f) * inverse) + 127) // 255
    return (red << 11) | (green << 5) | blue

def fonts():
    base = 'https://ledfont.botelvdong.com'
    with urllib.request.urlopen(base + '/api/v1/compressed-fonts') as r:
        catalog = json.load(r)
    selected = next(f for f in catalog['data'] if f['id'] == 'WenQuanDengKuanWeiMiHei')
    metadata = {'service': base, 'font_catalog_entry': selected, 'fonts': []}
    for name, size, chars in [('gl30_font_body',34,TEXT), ('gl30_font_title',40,TITLE_TEXT),
                               ('gl30_font_digits',64,'0123456789:.-% '),
                               ('gl30_font_small',26,'SMTWF0123456789日一二三四五六')]:
        payload = {'font': selected['id'], 'size': size, 'text': chars, 'array_name': name}
        req = urllib.request.Request(base + '/api/v1/compressed-fonts/generate',
              json.dumps(payload,ensure_ascii=False).encode('utf-8'),
              {'Content-Type':'application/json; charset=utf-8','Accept':'application/json'})
        with urllib.request.urlopen(req,timeout=240) as r:
            result = json.load(r)
        if result['code'] != 200: raise RuntimeError(result)
        data = result['data']
        actual = {int(p[2:],16) for p in data['codepoints']}
        missing = {ord(c) for c in chars} - actual
        if missing: raise RuntimeError(('missing glyphs',missing))
        (OUT / (name + '.c')).write_text('#include <stdint.h>\n' + data['c_source'],encoding='utf-8')
        metadata['fonts'].append({k:v for k,v in data.items() if k != 'c_source'})
        print(name, data['byte_count'], data['glyph_count'], flush=True)
    (OUT / 'font-generation.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf-8')

def photo():
    from PIL import Image
    source = ROOT / 'ui/preview/assets/summit-dawn.png'
    im = Image.open(source).convert('RGB').resize((466,466),Image.Resampling.LANCZOS)
    pixels = [(r>>3)<<11 | (g>>2)<<5 | b>>3 for r,g,b in im.getdata()]
    for y in range(466):
        alpha = _scrim_alpha(y)
        start = y * 466
        end = start + 466
        for index in range(start, end):
            pixels[index] = _blend_black_rgb565(pixels[index], alpha)
    lines = ['#include <stdint.h>','const uint16_t gl30_summit[466 * 466] = {']
    lines += [','.join(f'0x{p:04x}' for p in pixels[i:i+24])+',' for i in range(0,len(pixels),24)]
    lines += ['};','']
    (OUT/'gl30_summit.c').write_text('\n'.join(lines),encoding='ascii')
    print('photo',len(pixels)*2,flush=True)

if __name__ == '__main__':
    fonts()
    photo()
