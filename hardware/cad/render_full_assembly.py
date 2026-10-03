"""Render the GL30 R7 delivery model for spatial review.

The renderer deliberately consumes the assembly module's two stable review
interfaces: ``build_model()`` populates the shared model registry and
``delivery_parts()`` returns the already-filtered delivery geometry.  It does
not render the hundreds of tiny supplier or PCB package solids from ``PARTS``.
The images are engineering-layout evidence, not manufacturing drawings.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
from typing import Iterable, Mapping

import cadquery as cq
from PIL import Image, ImageDraw, ImageFont
import vtk


ROOT = Path(__file__).resolve().parents[2]
CAD = Path(__file__).resolve().parent
OUT = ROOT / "output/models/GL30_FULL_R7"
# Keep review evidence beside the other task-scoped GL30 artifacts.  This is
# intentionally separate from the model output directory so a render can be
# copied without losing the short review record.
REVIEW_OUT = ROOT / "outputs/r7-dfm-review"
# VTK's FreeType reader may ignore glyphs in a TTC collection on Windows;
# use the installed single-file CJK font for reliable Chinese labels.
FONT = Path("C:/Windows/Fonts/simhei.ttf")

if str(CAD) not in sys.path:
    sys.path.insert(0, str(CAD))

import full_knob_assembly as assembly  # noqa: E402
import hidden_screen_study as core_layout  # noqa: E402
from v7_product_model import _vtk_actor  # noqa: E402


# Brighter review colors make the role mapping survive a screenshot and keep
# the board/component identity independent of the source model's CMF colors.
ROLE_COLORS: dict[str, tuple[float, float, float, float]] = {
    "A": (0.04, 0.58, 0.25, 1.0),       # motor/control board
    "B": (0.05, 0.32, 0.88, 1.0),       # power/interface board
    "C": (0.06, 0.68, 0.78, 1.0),       # RGB/press ring board
    "D": (0.48, 0.27, 0.72, 1.0),       # fixed display module
    "E": (0.52, 0.55, 0.60, 1.0),       # factory motor + encoder
    "F": (0.97, 0.52, 0.08, 1.0),       # protected 3S pack envelope
    "K": (0.91, 0.24, 0.28, 1.0),       # seven keys / captured switches
    "G": (0.76, 0.70, 0.25, 1.0),       # screen post and press guide
    "M": (0.20, 0.25, 0.30, 1.0),       # central moving/rotating mechanism
    "W": (0.90, 0.68, 0.27, 1.0),       # connectors and wiring
    "X": (0.55, 0.60, 0.66, 1.0),       # auxiliary / reserve geometry
}

_DELIVERY: dict[str, cq.Shape] = {}

# The R7 optical stack is raised with the panel: the nominal light guide spans
# N=1.4..2.3 mm and the fixed inner shade terminates at N=2.3 mm.  The clip
# includes a small amount of neighboring housing so the interface is visible,
# while the labels below retain the actual nominal stack dimensions.
SECTION_N_MIN = 0.9
SECTION_N_MAX = 2.8
SECTION_RADIAL_MIN = 26.0
SECTION_RADIAL_MAX = 36.0
SECTION_LOCAL_Y_MIN = -3.0
SECTION_LOCAL_Y_MAX = 3.0


def _group_for(name: str) -> str:
    """Read a part group without making display geometry responsible for it."""
    return str(assembly.META.get(name, {}).get("group", ""))


def _shape_map(value: Mapping[str, cq.Shape]) -> dict[str, cq.Shape]:
    """Validate and copy the delivery mapping at the renderer boundary."""
    if not isinstance(value, Mapping):
        raise TypeError("assembly.delivery_parts() must return a mapping")
    result: dict[str, cq.Shape] = {}
    for name, shape in value.items():
        if not isinstance(name, str) or not name:
            raise TypeError("delivery part names must be non-empty strings")
        if not isinstance(shape, cq.Shape):
            raise TypeError(f"delivery part {name!r} is not a cadquery Shape")
        result[name] = shape
    if not result:
        raise RuntimeError("assembly.delivery_parts() returned no delivery geometry")
    return result


def reset_model() -> tuple[object, object, object]:
    """Populate the shared model through the R7 assembly interface."""
    # build_model() is the only model-construction entry point used here.  The
    # explicit clears make repeated in-process renders deterministic while the
    # assembly remains the owner of how the model is built.
    assembly.PARTS.clear()
    assembly.META.clear()
    assembly.KEEPOUTS.clear()
    assembly.LOCAL_BOARDS.clear()
    built = assembly.build_model()
    if not isinstance(built, tuple) or len(built) != 3:
        raise TypeError("assembly.build_model() must return (cavity, source, wiring)")
    _DELIVERY.clear()
    _DELIVERY.update(_shape_map(assembly.delivery_parts()))
    return built


def role_for(name: str) -> str:
    """Map a part name to the review role shown in the legend."""
    group = _group_for(name)
    # The top-surface wordmark is an exterior marking, not a functional
    # assembly role.  Keep it out of exploded/internal role counts while still
    # allowing the exterior/detail views to render it explicitly.
    if group == "marking" or name == "GL30_surface_mark":
        return "X"
    # The two A-side switches are controls, not PCB-A package geometry.
    if (name.startswith(("button_", "switch_", "rear_power_"))
            or name in {"A_reset_switch_SKRPASE010", "A_ring_press_switch_SKRPASE010"}
            or group == "button"):
        return "K"
    if name.startswith("PCB_A_") or name.startswith("A_") or group == "PCB_A":
        return "A"
    if name.startswith("PCB_B_") or name.startswith("B_") or group == "PCB_B":
        return "B"
    if (name.startswith("PCB_C_") or name.startswith("C_")
            or group == "PCB_C"
            or name in {"continuous_low_light_diffuser", "light_inner_baffle"}):
        return "C"
    if (name.startswith("D_") or name.startswith("display_")
            or group == "display"
            or name == "fixed_screen_bezel"):
        return "D"
    if name == "GL30_with_factory_encoder_E" or group == "motor":
        return "E"
    if name.startswith("F_") or group == "battery":
        return "F"
    if (name.startswith("MGN7") or name.startswith("fixed_rail")
            or name.startswith("fixed_screen_")
            or name.startswith("internal_screen_tray")
            or name.startswith("ring_press_compliant")
            or name == "fixed_optical_labyrinth_holder"):
        return "G"
    if (group in {"wire", "FFC", "connector"}
            or "wire" in name.lower() or "FFC" in name):
        return "W"
    if group in {"moving", "rotating"}:
        return "M"
    if group in {"housing", "base", "fixed_core", "metal", "speaker"}:
        return "G"
    return "X"


def color_for(
    name: str,
    *,
    opacity: float | None = None,
    view: str = "internal",
) -> tuple[float, float, float, float]:
    if name in {"continuous_low_light_diffuser", "light_diffuser", "front_light_strip"}:
        # The delivery view intentionally shows one continuous, opaque optical
        # window.  Individual LED package dots are filtered before rendering.
        color = (1.0, 0.93, 0.80, 1.0)
        return (*color[:3], color[3] if opacity is None else opacity)
    if name == "GL30_surface_mark":
        # Light-gray fill against the graphite top surface makes the small
        # marking legible without introducing a second product subtitle.
        color = (0.78, 0.80, 0.82, 1.0)
        if opacity is not None:
            return (*color[:3], opacity)
        return color
    if view == "exterior":
        # The exterior review has four intentional visual cues: compact dark
        # knob, one blue index mark, a continuous warm-white diffuser, and the
        # shallow dark-gray side keys.
        if name == "blue_rotary_index":
            color = (0.02, 0.36, 0.96, 1.0)
        elif role_for(name) == "M":
            color = (0.055, 0.065, 0.075, 1.0)
        elif role_for(name) == "D":
            color = (0.025, 0.035, 0.050, 1.0)
        elif role_for(name) == "K":
            color = (0.16, 0.17, 0.18, 1.0)
        elif name == "housing_104x98_rounded":
            color = (0.22, 0.235, 0.25, 1.0)
        else:
            color = ROLE_COLORS[role_for(name)]
    else:
        color = ROLE_COLORS[role_for(name)]
    if opacity is not None:
        return (*color[:3], opacity)
    return color


def shape_for_render(name: str, shape: cq.Shape) -> cq.Shape:
    """Keep the main display layers while avoiding a 400-solid CAD explosion.

    The official module STEP contains hundreds of tiny component solids.  A
    review image needs the screen, board, PMMA and adhesive layers to establish
    the module envelope; tessellating every vendor detail is both unreadable
    and unstable in the bundled OCC/VTK pipeline.  No source geometry is
    changed by this display-only reduction.
    """
    if name == "D_Waveshare_complete_module_supplier_valid_solids":
        solids = shape.Solids()
        major = [solid for solid in solids if abs(solid.Volume()) >= 300.0]
        if major:
            return cq.Compound.makeCompound(major)
    return shape


def _delivery_items() -> Iterable[tuple[str, cq.Shape]]:
    """Iterate only the assembly's compact delivery geometry."""
    return _DELIVERY.items()


def items_for_internal() -> list[tuple[str, cq.Shape, tuple[float, float, float, float], tuple[float, float, float]]]:
    """Return an open internal view with the housing removed."""
    items = []
    for name, shape in _delivery_items():
        if name == "bottom_cover" or _group_for(name) == "marking":
            continue
        # Keep a faint body silhouette so the internal positions retain the
        # compact envelope context, while every functional part stays readable.
        if name == "housing_104x98_rounded":
            opacity = 0.10
        elif role_for(name) in {"W", "X"}:
            opacity = 0.84
        else:
            opacity = None
        items.append((name, shape_for_render(name, shape), color_for(name, opacity=opacity), (0.0, 0.0, 0.0)))
    return items


EXPLODE_OFFSETS: dict[str, tuple[float, float, float]] = {
    # Board outlines and their package envelopes share the exact same offset.
    "A": (-56.0, 24.0, 20.0),
    "B": (56.0, 24.0, 20.0),
    "C": (0.0, 58.0, 24.0),
    "D": (0.0, -54.0, 26.0),
    "E": (-54.0, -36.0, 8.0),
    "F": (0.0, -65.0, 2.0),
    "K": (67.0, -8.0, 12.0),
    "G": (58.0, -48.0, 18.0),
    "M": (0.0, 0.0, 0.0),
    "W": (0.0, 0.0, 0.0),
    "X": (0.0, 0.0, 0.0),
}


def items_for_exploded() -> list[tuple[str, cq.Shape, tuple[float, float, float, float], tuple[float, float, float]]]:
    """Return a grouped explosion; board chips never receive individual offsets."""
    items = []
    for name, shape in _delivery_items():
        role = role_for(name)
        # Keep the grouped drawing readable: delivery_parts() has already
        # removed package clutter, and unknown auxiliary geometry is omitted.
        if role == "X":
            continue
        if role == "W":
            continue
        if name == "housing_104x98_rounded":
            offset = (0.0, 0.0, 0.0)
            rgba = color_for(name, opacity=0.16)
        elif name == "bottom_cover":
            offset = (0.0, 0.0, -42.0)
            rgba = color_for(name)
        else:
            offset = EXPLODE_OFFSETS[role]
            rgba = color_for(name)
        items.append((name, shape_for_render(name, shape), rgba, offset))
    return items


def items_for_exterior() -> list[tuple[str, cq.Shape, tuple[float, float, float, float], tuple[float, float, float]]]:
    """Return the compact exterior silhouette and its R7 visual cues."""
    shown: list[tuple[str, cq.Shape, tuple[float, float, float, float], tuple[float, float, float]]] = []
    visible_names = {
        "housing_104x98_rounded",
        "bottom_cover",
        "rotating_ring",
        "rotary_output_shaft",
        "blue_rotary_index",
        "continuous_low_light_diffuser",
        "light_diffuser",
        "front_light_strip",
        "fixed_screen_bezel",
        "fixed_display_cover_glass",
        "display_screen_layer",
        "display_pcb_layer",
        "GL30_surface_mark",
    }
    for name, shape in _delivery_items():
        if (name in visible_names or (name.startswith("button_") and name.endswith("_cap_and_stem"))
                or name.startswith("rear_power_button")):
            shown.append((name, shape_for_render(name, shape), color_for(name, view="exterior"), (0.0, 0.0, 0.0)))
    if not any(name == "housing_104x98_rounded" for name, *_ in shown):
        raise RuntimeError("delivery_parts() omitted housing_104x98_rounded required by exterior review")
    return shown


def items_for_side_key_detail() -> list[tuple[str, cq.Shape, tuple[float, float, float, float], tuple[float, float, float]]]:
    """Return a close exterior view of the recessed side keys and GL30 mark."""
    shown: list[tuple[str, cq.Shape, tuple[float, float, float, float], tuple[float, float, float]]] = []
    for name, shape in _delivery_items():
        is_key = (
            (name.startswith("button_") and (name.endswith("_cap_and_stem") or "cap" in name))
            or name.startswith("rear_power_button")
        )
        if name in {"housing_104x98_rounded", "GL30_surface_mark"} or is_key:
            shown.append((name, shape_for_render(name, shape), color_for(name, view="exterior"), (0.0, 0.0, 0.0)))
    if not any(name == "housing_104x98_rounded" for name, *_ in shown):
        raise RuntimeError("delivery_parts() omitted housing_104x98_rounded required by side-key detail")
    if not any(name == "GL30_surface_mark" for name, *_ in shown):
        raise RuntimeError("delivery_parts() omitted GL30_surface_mark required by side-key detail")
    return shown


def _to_local(shape: cq.Shape) -> cq.Shape:
    """Map a world shape back to the core's tangent/tangent/normal frame."""
    center = core_layout.CENTER
    return shape.translate((-center[0], -center[1], -center[2])).rotate(
        (0.0, 0.0, 0.0), (1.0, 0.0, 0.0), -core_layout.ANGLE * 180.0 / 3.141592653589793
    )


def items_for_light_ring_section() -> list[tuple[str, cq.Shape, tuple[float, float, float, float], tuple[float, float, float]]]:
    """Clip real delivery solids to the R7 radial optical stack."""
    # The local XY annulus is the radial window; local Z is the normal N axis.
    # A narrow local-Y slice turns the annulus into a readable radial section.
    section_height = SECTION_N_MAX - SECTION_N_MIN
    section_radius = (SECTION_RADIAL_MIN + SECTION_RADIAL_MAX) / 2.0
    section_n = (SECTION_N_MIN + SECTION_N_MAX) / 2.0
    annulus = (
        cq.Workplane("XY")
        .circle(SECTION_RADIAL_MAX)
        .circle(SECTION_RADIAL_MIN)
        .extrude(section_height)
        .translate((0.0, 0.0, SECTION_N_MIN))
        .val()
    )
    window = cq.Workplane("XY").box(
        SECTION_RADIAL_MAX - SECTION_RADIAL_MIN + 2.0,
        SECTION_LOCAL_Y_MAX - SECTION_LOCAL_Y_MIN,
        section_height,
    ).translate((section_radius, (SECTION_LOCAL_Y_MIN + SECTION_LOCAL_Y_MAX) / 2.0, section_n)).val()
    clip = annulus.intersect(window)
    names = {
        "housing_104x98_rounded",
        "rotating_ring",
        "continuous_low_light_diffuser",
        "PCB_C_24LED_annulus_service_lobe",
        "light_inner_baffle",
        "fixed_screen_bezel",
        "fixed_display_cover_glass",
    }
    items: list[tuple[str, cq.Shape, tuple[float, float, float, float], tuple[float, float, float]]] = []
    for name, shape in _delivery_items():
        if name not in names:
            continue
        local = _to_local(shape).intersect(clip)
        if local.isNull() or abs(local.Volume()) <= 1.0e-6:
            continue
        if name == "continuous_low_light_diffuser":
            rgba = (1.0, 0.93, 0.80, 1.0)
        elif name == "housing_104x98_rounded":
            rgba = (0.22, 0.235, 0.25, 0.34)
        elif role_for(name) == "M":
            rgba = (0.12, 0.14, 0.16, 1.0)
        else:
            rgba = color_for(name)
        items.append((name, local, rgba, (0.0, 0.0, 0.0)))
    if not items:
        raise RuntimeError("light-ring section clip returned no delivery geometry")
    return items


def _font(prop: vtk.vtkTextProperty, size: int) -> None:
    if FONT.is_file():
        prop.SetFontFile(str(FONT))
    else:
        prop.SetFontFamilyToArial()
    prop.SetFontSize(size)
    prop.SetBold(False)
    prop.SetShadow(False)


def text_actor(
    text: str,
    position: tuple[int, int],
    *,
    size: int = 20,
    color: tuple[float, float, float] = (0.08, 0.10, 0.12),
    background: tuple[float, float, float] | None = None,
    background_opacity: float = 0.84,
) -> vtk.vtkTextActor:
    actor = vtk.vtkTextActor()
    actor.SetInput(text)
    prop = actor.GetTextProperty()
    _font(prop, size)
    prop.SetColor(*color)
    prop.SetJustificationToLeft()
    prop.SetVerticalJustificationToTop()
    prop.SetLineSpacing(0.15)
    if background is not None:
        prop.SetBackgroundColor(*background)
        prop.SetBackgroundOpacity(background_opacity)
    actor.SetDisplayPosition(*position)
    return actor


def draw_chinese_overlay(path: Path, *, view: str) -> None:
    """Paint CJK labels after VTK writes the geometry layer.

    VTK's text actor is retained as a fallback for environments with a CJK
    FreeType build, but the bundled Windows VTK currently drops Chinese glyphs
    from both TTC and TTF files.  Pillow uses the same installed font reliably;
    this overlay contains only review labels and does not alter CAD geometry.
    """
    image = Image.open(path).convert("RGBA")
    draw = ImageDraw.Draw(image, "RGBA")
    font_path = str(FONT) if FONT.is_file() else "Arial"

    def font(size: int) -> ImageFont.FreeTypeFont:
        return ImageFont.truetype(font_path, size) if FONT.is_file() else ImageFont.load_default()

    def label(
        text: str,
        xy: tuple[int, int],
        *,
        size: int,
        fill: tuple[int, int, int, int],
        box: tuple[int, int, int, int] | None = None,
    ) -> None:
        f = font(size)
        x, y = xy
        bounds = draw.multiline_textbbox((x, y), text, font=f, spacing=5)
        if box is not None:
            pad = 6
            draw.rounded_rectangle(
                (bounds[0] - pad, bounds[1] - pad, bounds[2] + pad, bounds[3] + pad),
                radius=4,
                fill=box,
            )
        draw.multiline_text((x, y), text, font=f, fill=fill, spacing=5)

    width, height = image.size
    panel_x = int(width * 0.76)
    draw.rectangle((panel_x, 0, width, height), fill=(248, 250, 253, 246))
    draw.line((panel_x, 0, panel_x, height), fill=(190, 198, 208, 255), width=2)
    if view == "exterior":
        title = "R7 外观主图"
        subtitle = "深石墨壳体 · 哑黑旋钮 · 连续奶白灯"
    elif view == "closeup":
        title = "R7 灯圈剖切局部"
        subtitle = "局部Y向 · 实体截取"
    elif view == "detail":
        title = "R7 侧键与顶面字样"
        subtitle = "侧键内陷 · 顶面浅灰印字"
    elif view == "exploded":
        title = "R7 分组装配图"
        subtitle = "同组零件同向展开；用于空间审查"
    else:
        title = "R7 内部空间布置"
        subtitle = "交付件视图；显示安装层级与线束空间"
    label(title, (panel_x + 24, 28), size=24, fill=(16, 20, 26, 255))
    label(subtitle, (panel_x + 26, 70), size=15, fill=(45, 55, 66, 255))
    if view == "exterior":
        lines = [
            ("浅灰印字：GL30", "X"),
            ("收腰哑黑旋钮", "M"),
            ("连续奶白灯窗 1.6 mm", "C"),
            ("深灰内陷侧键", "K"),
        ]
    elif view == "closeup":
        lines = [
            ("灯窗开口 1.6 mm", "C"),
            ("乳白光导 N 1.4–2.3 mm", "C"),
            ("固定遮光罩顶 N 2.3 mm", "G"),
            ("径向动隙 0.4 mm", "G"),
            ("后平面 Z=67 mm", "G"),
        ]
    elif view == "detail":
        lines = [
            ("GL30 浅灰印字", "X"),
            ("上下各两枚内陷侧键", "K"),
            ("侧键内陷 0.7 mm", "K"),
            ("中心前移 16–17 mm", "K"),
        ]
    else:
        lines = [
            ("A  PCB A：电机控制板", "A"),
            ("B  PCB B：电源 / 接口板", "B"),
            ("C  PCB C：灯环板", "C"),
            ("D  显示主屏 / 板层", "D"),
            ("E  原厂电机 / 编码器", "E"),
            ("F  电池与保护区域", "F"),
            ("K  原四侧键 + 后部电源键", "K"),
            ("G  固定屏柱 / 导轨 / 遮光圈", "G"),
            ("M  旋钮 / 电机 / 移动件", "M"),
        ]
    y = 126
    for text, role in lines:
        cream = (view == "exterior" and role == "C")
        cream = cream or (view == "closeup" and text in {"灯窗开口 1.6 mm", "乳白光导 N 1.4–2.3 mm"})
        mark = role == "X" and ("GL30" in text or "印字" in text)
        exterior_key = role == "K" and view in {"exterior", "detail"}
        exterior_motor = role == "M" and view == "exterior"
        swatch = (
            (1.0, 0.93, 0.80, 1.0) if cream
            else (0.78, 0.80, 0.82, 1.0) if mark
            else (0.16, 0.17, 0.18, 1.0) if exterior_key
            else (0.055, 0.065, 0.075, 1.0) if exterior_motor
            else ROLE_COLORS[role]
        )
        color = tuple(int(v * 255) for v in swatch[:3]) + (255,)
        draw.rounded_rectangle((panel_x + 24, y + 4, panel_x + 47, y + 27), radius=4, fill=color)
        label(text, (panel_x + 60, y), size=15, fill=(36, 43, 52, 255))
        y += 38
    if view == "exterior":
        note = "收腰圆肩 · 顶面仅留 GL30\n图示为配色方案，非涂层样板"
    elif view == "closeup":
        note = "局部为交付几何真实截取\n截取区：半径 26–36；N 0.9–2.8\n光导名义层：N 1.4–2.3；固定罩顶 N 2.3\n后平面抬高至 Z=67；非制造公差保证"
    elif view == "detail":
        note = "局部外观用于字样与按键位置审查\n按钮位置采用 R7 名义尺寸；图标按工艺说明\n不是按压力、手感或制造公差保证"
    else:
        note = "工程代码渲染，空间布置审查\n非制造定版；供应商包络、PCB 与光学件\n仍需实物 / 原理图核验"
    label(note, (panel_x + 24, height - 132), size=14, fill=(148, 31, 22, 255), box=(255, 244, 238, 236))
    image.save(path)


def render_png(
    path: Path,
    items: Iterable[tuple[str, cq.Shape, tuple[float, float, float, float], tuple[float, float, float]]],
    *,
    camera: tuple[float, float, float],
    focal: tuple[float, float, float],
    parallel_scale: float,
    view: str,
) -> None:
    renderer = vtk.vtkRenderer()
    # Reserve a clean right sidebar for CJK labels.  Geometry therefore never
    # competes with the legend or disclaimer for the mechanical center.
    renderer.SetViewport(0.0, 0.0, 0.76, 1.0)
    renderer.SetBackground(0.96, 0.97, 0.98)
    renderer.SetBackground2(0.80, 0.85, 0.91)
    renderer.GradientBackgroundOn()
    for name, shape, rgba, offset in items:
        actor = _vtk_actor(shape, rgba, name=name)
        actor.SetPosition(*offset)
        renderer.AddActor(actor)
    lights = vtk.vtkLightKit()
    lights.SetKeyLightIntensity(0.9)
    lights.SetKeyLightWarmth(0.5)
    lights.SetFillLightWarmth(0.5)
    lights.SetBackLightWarmth(0.5)
    lights.AddLightsToRenderer(renderer)
    highlight = vtk.vtkLight()
    highlight.SetLightTypeToSceneLight()
    highlight.SetPosition(camera[0] - 100.0, camera[1] + 40.0, camera[2] + 110.0)
    highlight.SetFocalPoint(*focal)
    highlight.SetIntensity(0.85)
    highlight.SetPositional(False)
    renderer.AddLight(highlight)
    cam = renderer.GetActiveCamera()
    cam.SetPosition(*camera)
    cam.SetFocalPoint(*focal)
    cam.SetViewUp(0.0, 0.0, 1.0)
    cam.ParallelProjectionOn()
    cam.SetParallelScale(parallel_scale)
    renderer.ResetCameraClippingRange()

    window = vtk.vtkRenderWindow()
    window.SetOffScreenRendering(1)
    window.SetMultiSamples(8)
    window.SetSize(1400, 1000)
    window.AddRenderer(renderer)
    window.Render()
    capture = vtk.vtkWindowToImageFilter()
    capture.SetInput(window)
    capture.SetScale(1)
    capture.ReadFrontBufferOff()
    capture.Update()
    writer = vtk.vtkPNGWriter()
    writer.SetFileName(str(path))
    writer.SetInputConnection(capture.GetOutputPort())
    writer.Write()
    window.Finalize()
    draw_chinese_overlay(path, view=view)


def render(output_dir: Path = OUT) -> dict[str, object]:
    reset_model()
    output_dir.mkdir(parents=True, exist_ok=True)
    internal_path = output_dir / "internal_layout_r7.png"
    exploded_path = output_dir / "grouped_assembly_r7.png"
    exterior_path = output_dir / "exterior_r7.png"
    section_path = output_dir / "light_ring_section_r7.png"
    detail_path = output_dir / "side_key_wordmark_r7.png"
    render_png(
        internal_path,
        items_for_internal(),
        camera=(160.0, 190.0, 180.0),
        focal=(0.0, 0.0, 28.0),
        parallel_scale=110.0,
        view="internal",
    )
    render_png(
        exploded_path,
        items_for_exploded(),
        camera=(165.0, -205.0, 170.0),
        focal=(0.0, 0.0, 28.0),
        parallel_scale=170.0,
        view="exploded",
    )
    render_png(
        exterior_path,
        items_for_exterior(),
        camera=(174.0, -220.0, 151.0),
        focal=(0.0, 0.0, 31.0),
        parallel_scale=100.0,
        view="exterior",
    )
    render_png(
        section_path,
        items_for_light_ring_section(),
        camera=(31.0, -160.0, 1.0),
        focal=(31.0, 0.0, 1.0),
        parallel_scale=8.0,
        view="closeup",
    )
    render_png(
        detail_path,
        items_for_side_key_detail(),
        camera=(170.0, -235.0, 175.0),
        focal=(0.0, 8.0, 38.0),
        parallel_scale=90.0,
        view="detail",
    )
    roles = {role_for(name) for name in _DELIVERY}
    required = {"A", "B", "C", "D", "E", "F", "G", "K", "M"}
    missing = sorted(required - roles)
    if missing:
        raise RuntimeError(f"missing review roles: {missing}")
    required_names = {
        "housing_104x98_rounded",
        "bottom_cover",
        "press_base_plate",
        "rotating_ring",
        "motor_tension_slide_plate",
        "GL30_surface_mark",
    }
    missing_names = sorted(required_names - set(_DELIVERY))
    if missing_names:
        raise RuntimeError(f"missing R7 delivery part names: {missing_names}")
    filtered_out = sorted(set(assembly.PARTS) - set(_DELIVERY))
    report = {
        "status": "R7_RENDER_REVIEW",
        "evidence_boundary": "工程代码渲染，内部空间与外观形态审查，非制造定版",
        "source_sha256": hashlib.sha256(Path(assembly.__file__).read_bytes()).hexdigest(),
        "renderer_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "model_output": str(output_dir),
        "review_output": str(REVIEW_OUT),
        "images": [
            {"path": str(exterior_path), "purpose": "深石墨收腰圆肩外观、浅灰 GL30 印字、哑黑旋钮、连续奶白灯窗与内陷侧键"},
            {"path": str(internal_path), "purpose": "交付件内部位置、PCB A/B/C、电池、按键与线束"},
            {"path": str(exploded_path), "purpose": "按角色分组的空间展开审查"},
            {"path": str(section_path), "purpose": "沿 localY 的灯圈真实几何剖切；光导 N 1.4–2.3、固定罩顶 N 2.3"},
            {"path": str(detail_path), "purpose": "侧键内陷与顶面浅灰 GL30 印字近景"},
        ],
        "parts": {"model_registry": len(assembly.PARTS), "delivery": len(_DELIVERY)},
        "roles": sorted(roles),
        "font": str(FONT) if FONT.is_file() else "fallback",
        "missing_required_roles": missing,
        "required_delivery_part_names": sorted(required_names),
        "missing_required_delivery_part_names": missing_names,
        "delivery_filtered_out_count": len(filtered_out),
        "delivery_filtered_out_examples": filtered_out[:20],
        "rendered_item_counts": {
            "exterior": len(items_for_exterior()),
            "internal": len(items_for_internal()),
            "grouped_assembly": len(items_for_exploded()),
            "light_ring_section": len(items_for_light_ring_section()),
            "side_key_wordmark_detail": len(items_for_side_key_detail()),
        },
        "checks": {
            "uses_build_model": True,
            "uses_delivery_parts": True,
            "supplier_electronics_not_rendered_as_individual_packages": True,
            "screen_render_uses_delivery_geometry": True,
            "sidebar_reserved_for_chinese_legend": True,
            "mechanical_center_kept_clear_of_text": True,
            "light_ring_section_is_geometry_clip": True,
            "wordmark_is_delivery_geometry": True,
            "wordmark_is_light_gray_and_exterior_only": True,
            "side_key_detail_is_delivery_geometry": True,
        },
        "section_view": {
            "clip": {
                "radius_mm": [SECTION_RADIAL_MIN, SECTION_RADIAL_MAX],
                "normal_N_mm": [SECTION_N_MIN, SECTION_N_MAX],
                "local_y_slice_mm": [SECTION_LOCAL_Y_MIN, SECTION_LOCAL_Y_MAX],
            },
            "panel_N_mm": [0.0, 2.3],
            "light_guide_N_mm": [1.4, 2.3],
            "fixed_shade_top_N_mm": 2.3,
            "radial_motion_gap_mm": 0.4,
            "rear_plane_Z_mm": 67.0,
        },
        "notes": [
            "交付模型已由 assembly.delivery_parts() 负责过滤 A/B/C 电子封装，并简化显示供应商件。",
            "外观只标注顶面 GL30 浅灰印字，不添加 HAPTIC CONTROL 副标题；侧键采用深灰内陷表达。",
            "侧键功能标识沿用现有 UI：左后计时器、左前音量、右后加、右前减、后部电源；本轮仅写工艺说明，不增加 CAD 凹刻。",
            "侧键装配采用左右各一条可拆双键座；先装键帽再装支座，每侧两颗 M2x4。",
            "图片用于工程空间布置审查；板件、供应商包络、光学件、固定与运动间隙仍需后续实物/原理图核验。",
        ],
    }
    REVIEW_OUT.mkdir(parents=True, exist_ok=True)
    (REVIEW_OUT / "render-review.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=OUT)
    args = parser.parse_args()
    try:
        print(json.dumps(render(args.output_dir), ensure_ascii=False, indent=2))
        code = 0
    except Exception:
        import traceback
        traceback.print_exc()
        code = 1
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(code)
