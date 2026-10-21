#!/usr/bin/env python3
"""Emit an LVGL 9.x bitmap font (PLAIN, 4bpp) from a set of already-rasterized glyphs.

This is the shared back half of the two font generators:

  * tools/gen_font.py        —— 从 TTF/OTF 栅格化(Pillow/FreeType),生成中文标签字体
  * tools/gen_pixel_font.py  —— 从手写的点阵放大,生成像素数字体

真正的来源差别在它们各自的前半段;后半段(打包、查表、发射 C 代码)完全一样,而且
是最容易写错、错了也最难发现的那一半 —— 所以只有这一份实现。两份字体在设备上必须
能被同一个 lv_font_get_bitmap_fmt_txt 读出来,复用同一段打包逻辑是这件事的保证。

Why not `lv_font_conv`: the pinned 1.5.3 release emits corrupt bitmaps under
Node v24 (see docs/reference/shinku-chen/lvgl-cjk-font-subsets.md). We rasterize
ourselves and re-decode our own output with the exact algorithm LVGL uses at
runtime (components/lvgl/src/font/fmt_txt/lv_font_fmt_txt.c,
lv_font_get_bitmap_fmt_txt, the `bpp == 4` branch), so the emitted bytes are
verified, not assumed.

Output shape mirrors LVGL's own generated fonts (lv_font_montserrat_20.c):
glyph_dsc[0] is the reserved placeholder, cmaps are FORMAT0_TINY for dense runs
and SPARSE_TINY for the leftovers, glyph_dsc[] entries carry a byte-aligned
bitmap_index.

Layout facts this module depends on, all verified against LVGL 9.5.0 sources:

  * PLAIN 4bpp is one contiguous nibble stream, high nibble first, running
    across row boundaries with NO per-row padding.
  * Each glyph is still byte-aligned: its span is exactly ceil(box_w*box_h/2)
    bytes, and a trailing nibble is padded when the pixel count is odd.
  * `adv_w` is 8.4 fixed point (real value * 16).
  * `ofs_y` follows lv_font_conv's convention, which the draw code consumes as
    `.y = box_h + ofs_y`.
  * `get_glyph_dsc_id()` returns as soon as a cmap's range contains the
    codepoint, even when the sparse lookup misses. Ranges therefore must not
    overlap, which is what `plan_cmaps()` guarantees.
"""

from __future__ import annotations

from pathlib import Path
from typing import NamedTuple

# Repository root: charset paths are recorded in generated headers relative to
# it, so the comment reads the same no matter where a script is run from.
ROOT = Path(__file__).resolve().parents[1]

# A run of this many consecutive codepoints (or more) becomes a dense
# FORMAT0_TINY cmap; anything shorter falls through to the sparse buckets.
MIN_DENSE_RUN = 4

BPP = 4
NIBBLE_MAX = (1 << BPP) - 1


class Glyph(NamedTuple):
    """一个字形:已经栅格化好、还没打包的样子。

    alphas 是 box_w*box_h 个 0..255 的覆盖度,行优先;盒子为 0×0 时是空列表(空格
    就长这样)。adv_w 是 8.4 定点(真实推进宽度 × 16)。ofs_x / ofs_y 见模块开头的
    布局说明:ofs_y 满足 `墨迹顶 = 行框顶 + (line_height - base_line) - box_h - ofs_y`。
    """

    adv_w: int
    box_w: int
    box_h: int
    ofs_x: int
    ofs_y: int
    alphas: list[int]


# ---------------------------------------------------------------------------
# 字符集与 cmap 规划
# ---------------------------------------------------------------------------


def load_charset(path: Path) -> list[int]:
    """Read codepoints from a charset file: one token per whitespace-run.

    Tokens may be literal characters (including CJK) or `U+XXXX` escapes.
    Comment lines starting with `#` are ignored.
    """
    text = path.read_text(encoding="utf-8")
    cps: set[int] = set()
    for line in text.splitlines():
        line = line.split("#", 1)[0]
        for token in line.split():
            if token.upper().startswith("U+"):
                cps.add(int(token[2:], 16))
            else:
                cps.update(ord(ch) for ch in token)
    if not cps:
        raise ValueError(f"charset file is empty: {path}")
    return sorted(cps)


def plan_cmaps(cps: list[int]) -> list[tuple]:
    """Partition codepoints into non-overlapping cmap blocks.

    Returns blocks of `("dense", start, end)` or `("sparse", start, end, list)`,
    sorted by `start`. See the module docstring: overlap is a correctness bug,
    so the sparse buckets are cut wherever their span would swallow a dense run.
    """
    cps = sorted(set(cps))
    runs: list[tuple[int, int]] = []
    start = prev = cps[0]
    for cp in cps[1:]:
        if cp == prev + 1:
            prev = cp
        else:
            runs.append((start, prev))
            start = prev = cp
    runs.append((start, prev))

    dense = [r for r in runs if r[1] - r[0] + 1 >= MIN_DENSE_RUN]
    dense_members = {cp for s, e in dense for cp in range(s, e + 1)}

    rest = [cp for cp in cps if cp not in dense_members]
    buckets: list[list[int]] = []
    cur: list[int] = []
    for cp in rest:
        if cur and any(s <= cp and e >= cur[0] for s, e in dense):
            buckets.append(cur)
            cur = []
        cur.append(cp)
    if cur:
        buckets.append(cur)

    blocks = [("dense", s, e) for s, e in dense]
    blocks += [("sparse", b[0], b[-1], b) for b in buckets]
    blocks.sort(key=lambda b: b[1])
    return blocks


def glyph_order(blocks: list[tuple]) -> list[int]:
    """字形表里各字形对应的码点,按 cmap 块的顺序(字形 0 是保留位,不在其中)。"""
    order: list[int] = []
    for block in blocks:
        if block[0] == "dense":
            _, start, end = block
            order.extend(range(start, end + 1))
        else:
            _, _, _, members = block
            order.extend(members)
    return order


# ---------------------------------------------------------------------------
# 打包
# ---------------------------------------------------------------------------


def pack_nibbles(box_w: int, box_h: int, alphas: list[int]) -> bytes:
    """Pack coverage values into LVGL's contiguous high-nibble-first stream."""
    if box_w == 0 or box_h == 0:
        return b""
    nibbles = [(a * NIBBLE_MAX + 127) // 255 for a in alphas]
    if len(nibbles) % 2:
        nibbles.append(0)  # byte-align every glyph, as lv_font_conv does
    out = bytearray()
    for i in range(0, len(nibbles), 2):
        out.append((nibbles[i] << 4) | nibbles[i + 1])
    return bytes(out)


def decode_nibbles(box_w: int, box_h: int, data: bytes, index: int) -> list[int]:
    """Decode exactly the way lv_font_get_bitmap_fmt_txt does for bpp == 4.

    Mirrors components/lvgl/src/font/fmt_txt/lv_font_fmt_txt.c: `i` is a running
    parity across the whole glyph, the pointer only advances on the low nibble.
    """
    out: list[int] = []
    bitmap_in = index
    i = 0
    for _ in range(box_w * box_h):
        if i == 0:
            out.append(data[bitmap_in] >> 4)
            i = 1
        else:
            out.append(data[bitmap_in] & 0x0F)
            bitmap_in += 1
            i = 0
    return out


# ---------------------------------------------------------------------------
# 发射
# ---------------------------------------------------------------------------


def emit_bytes(out: list[str], symbol: str, data: bytes) -> None:
    out.append(f"static const uint8_t {symbol}[] = {{")
    for i in range(0, len(data), 16):
        row = ", ".join(f"0x{b:02X}" for b in data[i : i + 16])
        out.append(f"    {row},")
    out.append("};")
    out.append("")


def emit_u16(out: list[str], symbol: str, values: list[int]) -> None:
    out.append(f"static const uint16_t {symbol}[] = {{")
    for i in range(0, len(values), 12):
        row = ", ".join(str(v) for v in values[i : i + 12])
        out.append(f"    {row},")
    out.append("};")
    out.append("")


def check_glyph(cp: int, g: Glyph) -> None:
    """字段宽度与"每个字形字节对齐"这两条,越界时给出带码点的报错。"""
    for label, value in (("ofs_x", g.ofs_x), ("ofs_y", g.ofs_y)):
        if not -128 <= value <= 127:
            raise ValueError(f"U+{cp:04X}: {label} {value} does not fit int8")
    if g.box_w > 255 or g.box_h > 255:
        raise ValueError(f"U+{cp:04X}: box {g.box_w}x{g.box_h} does not fit uint8")
    if g.adv_w > 0xFFF:
        raise ValueError(f"U+{cp:04X}: adv_w {g.adv_w} does not fit the 12-bit field")
    if g.box_w and g.box_h and len(g.alphas) != g.box_w * g.box_h:
        raise ValueError(
            f"U+{cp:04X}: {len(g.alphas)} alphas for a {g.box_w}x{g.box_h} box"
        )
    if not (g.box_w and g.box_h) and g.alphas:
        raise ValueError(f"U+{cp:04X}: empty box but {len(g.alphas)} alphas")


def emit_font_c(
    *,
    name: str,
    glyphs: dict[int, Glyph],
    line_height: int,
    base_line: int,
    header: list[str],
) -> tuple[str, str]:
    """把一批字形发射成一份 LVGL 9 的 PLAIN 4bpp 位图字体 C 源码。

    header 是写在文件顶部的一段说明,每项一行、不带 `//`;调用方把来源与重新生成
    命令放进去(两份生成器的差别就在这几行)。
    返回 (源码文本, 一行摘要)。
    """
    cps = sorted(glyphs)
    if not cps:
        raise ValueError("no glyphs to emit")

    blocks = plan_cmaps(cps)
    order = glyph_order(blocks)

    bitmap = bytearray()
    entries: list[tuple] = [(0, 0, 0, 0, 0)]  # 字形 0 是保留位
    for cp in order:
        g = glyphs[cp]
        check_glyph(cp, g)
        index = len(bitmap)
        packed = pack_nibbles(g.box_w, g.box_h, g.alphas)
        if g.box_w and g.box_h:
            expect = (g.box_w * g.box_h + 1) // 2
            if len(packed) != expect:
                raise AssertionError(
                    f"U+{cp:04X}: glyph span {len(packed)} != ceil(px/2) {expect}"
                )
            roundtrip = decode_nibbles(g.box_w, g.box_h, packed, 0)
            if roundtrip != [(a * NIBBLE_MAX + 127) // 255 for a in g.alphas]:
                raise AssertionError(f"U+{cp:04X}: 4bpp round-trip mismatch")
        bitmap.extend(packed)
        entries.append((index, g.adv_w, g.box_w, g.box_h, g.ofs_x, g.ofs_y))

    if len(bitmap) > 0xFFFFF:
        raise ValueError("glyph_bitmap exceeds the 20-bit bitmap_index field")

    # cmap tables. glyph_id_start is the id of each block's first codepoint.
    cmap_entries: list[str] = []
    sparse_arrays: list[tuple[str, list[int]]] = []
    next_gid = 1
    for n, block in enumerate(blocks):
        if block[0] == "dense":
            _, start, end = block
            cmap_entries.append(
                "    {\n"
                f"        .range_start = {start},\n"
                f"        .range_length = {end - start + 1},\n"
                f"        .glyph_id_start = {next_gid},\n"
                "        .unicode_list = NULL,\n"
                "        .glyph_id_ofs_list = NULL,\n"
                "        .list_length = 0,\n"
                "        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,\n"
                "    },"
            )
            next_gid += end - start + 1
        else:
            _, start, end, members = block
            length = end - start + 1
            if length > 0xFFFF:
                raise ValueError(f"sparse cmap span {length} does not fit range_length")
            rel = [cp - start for cp in members]
            if any(r > 0xFFFF for r in rel):
                raise ValueError(
                    f"sparse cmap starting at 0x{start:X} has a relative codepoint "
                    "above 0xFFFF; split the charset into more blocks"
                )
            symbol = f"unicode_list_{n}"
            sparse_arrays.append((symbol, rel))
            cmap_entries.append(
                "    {\n"
                f"        .range_start = {start},\n"
                f"        .range_length = {length},\n"
                f"        .glyph_id_start = {next_gid},\n"
                f"        .unicode_list = {symbol},\n"
                "        .glyph_id_ofs_list = NULL,\n"
                f"        .list_length = {len(rel)},\n"
                "        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,\n"
                "    },"
            )
            next_gid += len(rel)

    out: list[str] = [f"// {line}" if line else "//" for line in header]
    out.append("")
    out.append('#include "lvgl.h"')
    out.append("")
    out.append("#if LV_VERSION_CHECK(9, 0, 0) || LVGL_VERSION_MAJOR >= 9")
    out.append("")

    emit_bytes(out, "glyph_bitmap", bytes(bitmap))

    out.append("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    out.append(
        "    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, "
        ".ofs_x = 0, .ofs_y = 0},  /* 保留位:字形 0 不参与映射 */"
    )
    for cp, entry in zip(order, entries[1:]):
        index, adv_w, box_w, box_h, ofs_x, ofs_y = entry
        out.append(
            f"    {{.bitmap_index = {index}, .adv_w = {adv_w}, "
            f".box_w = {box_w}, .box_h = {box_h}, "
            f".ofs_x = {ofs_x}, .ofs_y = {ofs_y}}},  /* U+{cp:04X} */"
        )
    out.append("};")
    out.append("")

    for symbol, rel in sparse_arrays:
        emit_u16(out, symbol, rel)

    out.append("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    out.extend(cmap_entries)
    out.append("};")
    out.append("")

    out.append("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    out.append("    .glyph_bitmap = glyph_bitmap,")
    out.append("    .glyph_dsc = glyph_dsc,")
    out.append("    .cmaps = cmaps,")
    out.append("    .kern_dsc = NULL,")
    out.append("    .kern_scale = 0,")
    out.append(f"    .cmap_num = {len(blocks)},")
    out.append(f"    .bpp = {BPP},")
    out.append("    .kern_classes = 0,")
    out.append("    .bitmap_format = 0,")
    out.append("    .stride = 0,")
    out.append("};")
    out.append("")

    out.append(f"const lv_font_t {name} = {{")
    out.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    out.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    out.append(f"    .line_height = {line_height},")
    out.append(f"    .base_line = {base_line},")
    out.append("    .subpx = LV_FONT_SUBPX_NONE,")
    out.append("    .underline_position = -1,")
    out.append("    .underline_thickness = 1,")
    out.append("    .dsc = &font_dsc,")
    out.append("};")
    out.append("")
    out.append("#endif")
    out.append("")

    summary = (
        f"{name}: {len(order)} 字形, {len(bitmap)} 字节位图, "
        f"{len(blocks)} 个 cmap 表, line_height={line_height}, base_line={base_line}"
    )
    return "\n".join(out), summary
