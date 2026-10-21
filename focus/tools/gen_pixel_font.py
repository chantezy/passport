#!/usr/bin/env python3
"""Generate an LVGL 9.x pixel bitmap font (PLAIN, 4bpp) from a hand-written 5x7 grid.

界面上的两处大数字(计时页那行倒计时 "05:00"、首页的专注时长 "25")要走像素风。
两处**共用同一份字体**:它们本来就是同一种东西画在两个页面上,分成两份字号只会让
"这两处该一样大"这件事多一个会走偏的地方。像素字放大整数倍才不会糊,所以这里不裁
任何字体文件,而是在脚本里写死一张 5×7 的点阵表,按 5 倍放大成实心方块:

  --scale 5  ->  35px 行高(focus_pixel_35)

每个字形是一个 7 行、等宽的点阵,每个格子放大成 scale×scale 个像素。行高就是点阵
本身(7×scale)、基线为 0、ofs_y 为 0 —— 由 lv_draw_label.c 的落笔位置
`y = 行框顶 + (line_height - base_line) - box_h - ofs_y` 可知,这时墨迹顶正好等于
行框顶,界面按"行框顶"定位就是按"墨迹顶"定位,中间的换算全部消失。

数字一律 5 格宽、6 格推进("1" 的墨迹窄,但盒子不窄),所以从 1 变到 8 时字不会
左右跳。冒号是单独一张 3 格宽的点阵,推进 4 格。没有空格字形:这两处要显示的字符串
是 "%d" 和 "%02d:%02d",一个空格都不含,不留用不上的字形。

Usage:
  python3 tools/gen_pixel_font.py --scale 5 \
      --charset assets/fonts/charset_focus_pixel_35.txt \
      --name focus_pixel_35 --out assets/fonts/focus_pixel_35.c
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from lv_font_emit import ROOT, Glyph, emit_font_c, load_charset

# 点阵的高度(格)。所有字形一样高,这样行框就是一个统一的格子。
ROWS = 7

# 字形表:字符 -> (推进格数, 7 行点阵)。'#' 是墨,'.' 是空。
# 推进宽于墨迹宽度的那一格就是字间距,写在表里而不是代码里,调间距不用改逻辑。
DIGITS = {
    "0": ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.",
    "1": "..#..|.##..|..#..|..#..|..#..|..#..|.###.",
    "2": ".###.|#...#|....#|...#.|..#..|.#...|#####",
    "3": ".###.|#...#|....#|.###.|....#|#...#|.###.",
    "4": "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.",
    "5": "#####|#....|####.|....#|....#|#...#|.###.",
    "6": "..##.|.#...|#....|####.|#...#|#...#|.###.",
    "7": "#####|....#|...#.|..#..|.#...|.#...|.#...",
    "8": ".###.|#...#|#...#|.###.|#...#|#...#|.###.",
    "9": ".###.|#...#|#...#|.####|....#|...#.|.##..",
}

# 冒号:上下各一颗两格高的点,上下留白对称。盒子 3 格宽,推进 4 格。
COLON = "...|.#.|.#.|...|.#.|.#.|..."

GLYPHS: dict[str, tuple[int, str]] = {ch: (6, pattern) for ch, pattern in DIGITS.items()}
GLYPHS[":"] = (4, COLON)


def pixels_for(char: str, scale: int) -> Glyph:
    """把一个字符的点阵放大成 Glyph。"""
    adv_cells, pattern = GLYPHS[char]
    rows = pattern.split("|")
    if len(rows) != ROWS:
        raise ValueError(f"{char!r}: {len(rows)} rows, expected {ROWS}")
    if len({len(r) for r in rows}) != 1:
        raise ValueError(f"{char!r}: rows are not all the same width")

    box_w = len(rows[0]) * scale
    box_h = ROWS * scale
    alphas = [0] * (box_w * box_h)
    for y, row in enumerate(rows):
        for x, cell in enumerate(row):
            if cell == "#":
                for dy in range(scale):
                    base = (y * scale + dy) * box_w + x * scale
                    for dx in range(scale):
                        alphas[base + dx] = 255
            elif cell != ".":
                raise ValueError(f"{char!r}: unknown cell {cell!r}")

    # 推进宽度是 8.4 定点(真实像素宽 × 16)。
    return Glyph(adv_cells * scale * 16, box_w, box_h, 0, 0, alphas)


def generate(scale: int, cps: list[int], name: str, charset: Path) -> tuple[str, str]:
    size = ROWS * scale

    wanted = "".join(chr(cp) for cp in cps)
    missing = [ch for ch in wanted if ch not in GLYPHS]
    if missing:
        raise ValueError(
            f"点阵表里没有这几个字符:{' '.join(repr(c) for c in missing)}"
            f" —— 补进 tools/gen_pixel_font.py 的 DIGITS / COLON,或从字符集里去掉"
        )
    unused = [ch for ch in GLYPHS if ch not in wanted]
    if unused:
        print(
            f"note: 点阵表里的 {' '.join(repr(c) for c in sorted(unused))} "
            f"不在 {charset} 里,这次不会生成",
            file=sys.stderr,
        )

    glyphs = {cp: pixels_for(chr(cp), scale) for cp in cps}

    # 每个 --flag 各占一行:行尾的反斜杠会把下一行拼进 `//` 注释里,
    # -Werror=comment 会直接报错。
    comment = [
        "由 tools/gen_pixel_font.py 生成,请勿手工修改。",
        "来源:点阵表写死在 tools/gen_pixel_font.py 里(5×7 手绘网格),不涉及任何字体文件。",
        f"字号:{size}px({ROWS}×{scale} 点阵 ×{scale}),格式:PLAIN 4bpp,字形数:{len(glyphs)}",
        f"行高与基线:{size} / 0 —— 行框就是点阵本身,墨迹顶与行框顶重合。",
        "重新生成:",
        "  python3 tools/gen_pixel_font.py --scale " + str(scale),
        f"      --charset {charset}",
        f"      --name {name} --out assets/fonts/{name}.c",
    ]
    return emit_font_c(
        name=name, glyphs=glyphs, line_height=size, base_line=0, header=comment
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--scale", required=True, type=int, help="点阵每格放大几倍")
    parser.add_argument("--charset", required=True, type=Path, help="字符集清单文件")
    parser.add_argument("--name", required=True, help="生成的 lv_font_t 符号名")
    parser.add_argument("--out", required=True, type=Path, help="输出的 .c 文件")
    args = parser.parse_args(argv)

    if args.scale < 1:
        print("ERROR: --scale 至少是 1", file=sys.stderr)
        return 1

    cps = load_charset(args.charset)
    try:
        charset_ref = args.charset.resolve().relative_to(ROOT)
    except ValueError:
        charset_ref = args.charset
    text, summary = generate(args.scale, cps, args.name, charset_ref)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(text, encoding="utf-8")
    print(summary, file=sys.stderr)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
