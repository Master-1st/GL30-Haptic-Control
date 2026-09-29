"""Assemble review sheets from gl30_capture_icons PPMs; never redraw UI in Python.
Requires Pillow only for offline sheet assembly. Native glyph pixels are shown
1:1; physical millimetres are nominal, not a measurement of an attached panel.
"""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

NAMES = [("计时器", "Timer"), ("音量", "Volume"), ("秒表", "Stopwatch"),
         ("闹钟", "Alarm"), ("天气", "Weather"), ("手感", "Haptics"),
         ("设置", "Settings"), ("日历", "Calendar"), ("灯效", "Lighting")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--font", required=True, type=Path,
                        help="Local CJK font for sheet captions only; not copied")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    fonts = {n: ImageFont.truetype(str(args.font), n) for n in (17, 20, 28, 38)}

    def frame(name):
        with Image.open(args.frames / (name + ".ppm")) as im:
            if im.size != (466, 466):
                raise ValueError("Unexpected C-renderer dimensions: " + name)
            return im.convert("RGB")

    sheet = Image.new("RGB", (1302, 1058), "#10171d")
    d = ImageDraw.Draw(sheet)
    d.text((26, 18), "GL30 · 统一圆角双色图标", font=fonts[38], fill="#e6edf3")
    d.text((28, 73), "实际 C 渲染像素，按 1:1 展示；尺寸为标称换算，并非实物测量。", font=fonts[20], fill="#9aaab5")
    for app, (zh, en) in enumerate(NAMES):
        x, y = 22 + (app % 3) * 430, 118 + (app // 3) * 304
        d.rounded_rectangle((x, y, x+398, y+280), radius=16, fill="black", outline="#293641")
        d.text((x+16, y+10), zh, font=fonts[28], fill="#e6edf3")
        d.text((x+160, y+18), en, font=fonts[17], fill="#9aaab5")
        for size, crop, left, top, label in [(0, 204, 10, 51, "14 mm"),
                                           (1, 94, 230, 107, "6 mm"),
                                           (2, 56, 336, 126, "3.2 mm")]:
            im = frame(f"icon-{app}-{size}")
            c = 233-crop//2
            sheet.paste(im.crop((c, c, c+crop, c+crop)), (x+left, y+top))
            d.text((x+left+4, y+250), label, font=fonts[17], fill="#9aaab5")
    sheet.save(args.output / "icons-unified.png")

    def screen_sheet(items, name, heading):
        canvas = Image.new("RGB", (1944, 1120), "#10171d")
        draw = ImageDraw.Draw(canvas)
        draw.text((20, 12), heading, font=fonts[28], fill="#e6edf3")
        draw.text((20, 53), "软件预览，来源为当前固件 C renderer；不是面板实拍或 FPS 实测。", font=fonts[20], fill="#9aaab5")
        # Simulate only the round panel's invisible corners for presentation.
        # The C bounds regression inspects UNMASKED pixels, so this cannot
        # hide clipping failures from the tests.
        aperture = Image.new("L", (466, 466), 0)
        ImageDraw.Draw(aperture).ellipse((0, 0, 465, 465), fill=255)
        for i, (filename, label) in enumerate(items):
            x, y = 12+(i % 4)*484, 94+(i//4)*504
            canvas.paste(frame(filename), (x, y), aperture)
            draw.text((x+12, y+472), label, font=fonts[20], fill="#c3cdd5")
        canvas.save(args.output/name)
    screen_sheet([(f"menu-0-{n}", NAMES[n][0]) for n in range(8)],
                 "menus-unified.png", "GL30 · 八项菜单逐项预览")
    screen_sheet([(f"{scene}-{lang}{suffix}", label+" · "+("中文" if lang else "English"))
                  for lang in (0,1) for scene,suffix,label in
                  [("home","","桌面"),("menu","-5","手感菜单"),
                   ("app","-7","日历"),("app","-6","设置")]],
                 "ui-preview-current.png", "GL30 · 桌面、环形菜单、日历与设置")
    print("Review sheets assembled from 63 native C-renderer captures.")


if __name__ == "__main__":
    main()
