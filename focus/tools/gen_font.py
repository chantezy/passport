#!/usr/bin/env python3
"""Generate an LVGL 9.x bitmap font (PLAIN, 4bpp) from a TTF/OTF via Pillow.

这个脚本只负责前半段:按字符集把字形从字体文件里栅格化出来。打包与发射 C 代码在
tools/lv_font_emit.py —— 那份实现和像素数字体(tools/gen_pixel_font.py)共用,
因为设备上两种字体要被同一个 lv_font_get_bitmap_fmt_txt 读出来。

Usage:
  python3 tools/gen_font.py --font FONT --size 20 --charset CHARSET.txt \
      --name focus_text_20 --out assets/fonts/focus_text_20.c
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from lv_font_emit import ROOT, Glyph, emit_font_c, load_charset

try:
    from PIL import ImageFont
except ImportError:  # pragma: no cover - environment problem, not a code path
    print("ERROR: Pillow is required (pip3 install --user Pillow)", file=sys.stderr)
    raise SystemExit(1)


def rasterize(font: ImageFont.FreeTypeFont, cp: int) -> Glyph:
    """Rasterize one codepoint into a Glyph (coverage values in 0..255)."""
    ch = chr(cp)
    bbox = font.getbbox(ch, anchor="ls")
    mask, offset = font.getmask2(ch, mode="L", anchor="ls")

    x0, y0, x1, y1 = bbox
    box_w = x1 - x0
    box_h = y1 - y0

    # Pillow's two baseline-anchored APIs must agree; if they ever stop doing so
    # the offsets below are silently wrong, so fail loudly instead.
    if mask.size != (box_w, box_h):
        raise AssertionError(
            f"U+{cp:04X}: getmask2 size {mask.size} != getbbox size {(box_w, box_h)}"
        )
    if offset != (x0, y0):
        raise AssertionError(f"U+{cp:04X}: getmask2 offset {offset} != getbbox origin {(x0, y0)}")

    adv_w = round(font.getlength(ch) * 16)
    if box_w <= 0 or box_h <= 0:
        return Glyph(adv_w, 0, 0, 0, 0, [])

    # getmask2 hands back an ImagingCore, not an Image: one byte per pixel in
    # mode "L", row-major, no padding -- exactly the coverage plane we want.
    alphas = list(bytes(mask))
    if len(alphas) != box_w * box_h:
        raise AssertionError(
            f"U+{cp:04X}: mask has {len(alphas)} bytes, expected {box_w * box_h}"
        )

    # y0/y1 are image coordinates measured downward from the baseline, so the
    # ink's top edge sits at y0 and lv_font_conv's ofs_y is -y1.
    return Glyph(adv_w, box_w, box_h, x0, -y1, alphas)


def generate(
    font_path: Path, size: int, cps: list[int], name: str, header: str, charset: Path
) -> tuple[str, str]:
    font = ImageFont.truetype(str(font_path), size)

    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    glyphs = {cp: rasterize(font, cp) for cp in cps}

    # 每个 --flag 各占一行:行尾的反斜杠会把下一行拼进 `//` 注释里,
    # -Werror=comment 会直接报错。
    comment = [
        "由 tools/gen_font.py 生成,请勿手工修改。",
        f"源字体:{header}",
        f"字号:{size}px,格式:PLAIN 4bpp,字形数:{len(glyphs)}",
        "重新生成:",
        "  python3 tools/gen_font.py --font <字体文件>",
        f"      --size {size} --charset {charset}",
        f"      --name {name} --out assets/fonts/{name}.c",
    ]
    return emit_font_c(
        name=name,
        glyphs=glyphs,
        line_height=line_height,
        base_line=descent,
        header=comment,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--font", required=True, type=Path, help="源 TTF/OTF 路径")
    parser.add_argument("--size", required=True, type=int, help="像素字号")
    parser.add_argument("--charset", required=True, type=Path, help="字符集清单文件")
    parser.add_argument("--name", required=True, help="生成的 lv_font_t 符号名")
    parser.add_argument("--out", required=True, type=Path, help="输出的 .c 文件")
    parser.add_argument(
        "--source-note",
        default="",
        help="写进生成文件头部注释的来源说明(字体名与许可)",
    )
    args = parser.parse_args(argv)

    if not args.font.is_file():
        print(f"ERROR: font not found: {args.font}", file=sys.stderr)
        return 1

    cps = load_charset(args.charset)
    header = args.source_note or args.font.name
    try:
        charset_ref = args.charset.resolve().relative_to(ROOT)
    except ValueError:
        charset_ref = args.charset
    text, summary = generate(args.font, args.size, cps, args.name, header, charset_ref)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding="utf-8")
    print(summary, file=sys.stderr)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
