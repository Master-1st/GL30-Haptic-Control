"""Verify checked-in UI assets without network access or local font tools.

This read-only check covers C array lengths, font metadata, label coverage and
the RGB565 background. It does not regenerate assets or edit upstream skills.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re


C_STRING = r'"(?:[^"\\]|\\.)*"'
FONT_NAMES = {"gl30_font_body", "gl30_font_title", "gl30_font_digits", "gl30_font_small"}


def font_payload(source, name, expected_bytes):
    declaration = re.search(
        rf"\bconst\s+uint8_t\s+{re.escape(name)}\[(\d+)\]\s*=\s*",
        source,
        re.DOTALL,
    )
    if declaration is None or int(declaration[1]) != expected_bytes:
        raise ValueError(f"{name}: C array length differs from font metadata")
    strings = re.match(rf"((?:{C_STRING}\s*)+);", source[declaration.end():])
    if strings is None:
        raise ValueError(f"{name}: expected generated C byte strings")
    initializer = strings[1]
    payload = b"".join(ast.literal_eval("b" + literal)
                       for literal in re.findall(C_STRING, initializer))
    # Each generated array includes the implicit C string terminator.
    if len(payload) + 1 != expected_bytes:
        raise ValueError(f"{name}: expected {expected_bytes} bytes, got {len(payload) + 1}")
    return payload + b"\0"


def verify(root):
    assets = root / "firmware-esp32/components/gl30_ui/assets"
    metadata = json.loads((assets / "font-generation.json").read_text(encoding="utf-8"))
    fonts = metadata["fonts"]
    if len(fonts) != len(FONT_NAMES) or {f["array_name"] for f in fonts} != FONT_NAMES:
        raise ValueError("Expected the four checked-in GL30 fonts")
    coverage = {}
    hashes = {}
    for font in fonts:
        name = font["array_name"]
        codepoints = {int(value[2:], 16) for value in font["codepoints"]}
        if (len(codepoints) != font["glyph_count"]
                or codepoints != {ord(character) for character in font["characters"]}):
            raise ValueError(f"{name}: inconsistent glyph metadata")
        data = font_payload((assets / f"{name}.c").read_text(encoding="utf-8"),
                            name, font["byte_count"])
        hashes[name] = hashlib.sha256(data).hexdigest()
        coverage[name] = codepoints

    renderer = root / "firmware-esp32/components/gl30_ui/src/gl30_render.c"
    labels = "".join(re.findall(C_STRING, renderer.read_text(encoding="utf-8")))
    required = {ord(character) for character in labels if "\u4e00" <= character <= "\u9fff"}
    missing = required - coverage["gl30_font_body"]
    if missing:
        raise ValueError("Body fallback is missing labels: " + "".join(map(chr, sorted(missing))))

    background = (assets / "gl30_summit.c").read_text(encoding="utf-8")
    pixels = [int(value, 16) for value in re.findall(r"0x[0-9a-fA-F]+", background)]
    if len(pixels) != 466 * 466 or any(value > 0xffff for value in pixels):
        raise ValueError("Expected a complete 466x466 RGB565 summit background")
    for license_name in ("LICENSE_WQY_Apache2.txt", "WQY_README.txt"):
        if not (assets / license_name).is_file():
            raise ValueError(f"Missing font attribution: {license_name}")
    return {"result": "PASS", "fonts": len(fonts), "Chinese_glyphs_checked": len(required),
            "background_pixels": len(pixels), "font_data_sha256": hashes}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    print(json.dumps(verify(args.root.resolve()), indent=2))
