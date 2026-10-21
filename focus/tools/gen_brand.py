#!/usr/bin/env python3
"""把首页标题的 "Focus" 画成一张像素字位图,输出成固件用的 RGB565 常量。

素材就是一个 5×7 的点阵字模(见 GLYPHS):每个字母 5 列 7 行,用 '#' 标出笔画,
再用 '#' 之外的一个记号标出要换色的格子(目前只有番茄的绿蒂,见 ACCENT),
整体乘一个整数倍放到屏幕上。整数倍是这里唯一重要的事 —— 非整数缩放会让像素
格子大小不一,一眼就不是像素风了。7 倍下 29×7 的字模正好 203×49,在 240 宽的
屏幕上两侧各留 18 像素。

标题里的小写 "o" 不是字母,是一颗番茄:'o' 那个 5×7 的格子被番茄占满,绿蒂压着
红果子。它和别的字母同一格宽,所以总宽仍是 29 列 —— 换掉的是图案,不是排版。

不做抗锯齿、不做 alpha:和水果精灵走同一条路(见 tools/gen_sprites.py),
直接合成到纯黑底再转 RGB565,屏幕底色就是 #000000(见 main/focus_ui.c 的 COL_BG)。

输出是 LVGL 无关的纯数据(focus_sprite_t,见 main/focus_sprites.h),
LVGL 的图像描述符在 main/focus_ui.c 里现搭。

Usage:
  python3 tools/gen_brand.py [--out assets/sprites/focus_wordmark.c]
                             [--text Focus] [--scale 7] [--color FF6B5A]
                             [--preview DIR]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - 环境问题,不是代码路径
    print("ERROR: Pillow is required (pip3 install --user Pillow)", file=sys.stderr)
    raise SystemExit(1)


# 5 列 × 7 行的手写点阵。只收 "Focus" 用得到的五个字母,不收整套 ASCII ——
# 首页上不会再出现第二句像素字,补齐 26 个字母只是让这张表更难读。
# 小写字母只占第 3..7 行(x-height),大写 F 占满 7 行,字模自带升部。
#
# '#' = 主色, '+' = 点缀色(ACCENT)。番茄用 '+' 点出绿蒂,其余字母全是 '#'。
GLYPHS = {
    "F": (
        "#####",
        "#....",
        "#....",
        "####.",
        "#....",
        "#....",
        "#....",
    ),
    # 小写 "o" 换成一整颗番茄:第 2 行一格绿蒂,下面五行红果肉。
    # 果体把 x-height 那五行填成圆形(上下收成三格宽),比字母 o 更像一颗果子;
    # 绿蒂只多占它上面一行,基线因此和其它小写字母仍然齐平。
    "o": (
        ".....",
        "..+..",
        ".###.",
        "#####",
        "#####",
        "#####",
        ".###.",
    ),
    "c": (
        ".....",
        ".....",
        ".####",
        "#....",
        "#....",
        "#....",
        ".####",
    ),
    "u": (
        ".....",
        ".....",
        "#...#",
        "#...#",
        "#...#",
        "#...#",
        ".####",
    ),
    "s": (
        ".....",
        ".....",
        ".####",
        "#....",
        ".###.",
        "....#",
        "####.",
    ),
}

GLYPH_W = 5
GLYPH_H = 7

# 字距:一个空列。每个字母两侧本来就有半列留白,所以实际看起来是两像素的缝。
TRACKING = 1

# 默认取 main/focus_ui.c 的 COL_FOCUS(专注阶段的暖红),首页用它当主色。
DEFAULT_COLOR = 0xFF6B5A
# 点缀色:只用于番茄的绿蒂。挑的是偏黄的叶绿,压在暖红上分得开,
# 又不像纯绿那样在深色底上发荧。
DEFAULT_ACCENT = 0x57C15C


def to_rgb565(pixel: tuple[int, int, int]) -> int:
    """RGB888 → RGB565,和 LVGL 内部的小端布局一致 (R 高 5 位)。"""
    r, g, b = pixel
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def from_rgb565(value: int) -> tuple[int, int, int]:
    """RGB565 → RGB888,用于回读校验(按 5/6 位展开到 8 位)。"""
    r5 = (value >> 11) & 0x1F
    g6 = (value >> 5) & 0x3F
    b5 = value & 0x1F
    return (r5 * 255 // 31, g6 * 255 // 63, b5 * 255 // 31)


def compose(text: str, scale: int, color: tuple[int, int, int],
            accent: tuple[int, int, int]) -> Image.Image:
    """把 text 排成一条 1:1(未放大)的点阵掩码,再按整数倍放大上色。

    掩码里 255 = 主色、170 = 点缀色。NEAREST 放大不会在格子之间插值,所以这两档
    灰度原样带到放大后的图上,逐个像素查表上色即可 —— 不靠调色板,也不必先放大
    再分层合成。
    """
    unknown = sorted({ch for ch in text if ch not in GLYPHS})
    if unknown:
        raise SystemExit(f"ERROR: 字模里没有这些字符: {''.join(unknown)}")

    advance = GLYPH_W + TRACKING
    width = len(text) * advance - TRACKING
    height = GLYPH_H

    mask = Image.new("L", (width, height), 0)
    px = mask.load()
    for index, ch in enumerate(text):
        x0 = index * advance
        for row, line in enumerate(GLYPHS[ch]):
            if len(line) != GLYPH_W:
                raise SystemExit(f"ERROR: {ch!r} 第 {row + 1} 行宽度不是 {GLYPH_W}")
            for col, cell in enumerate(line):
                if cell == "#":
                    px[x0 + col, row] = 255
                elif cell == "+":
                    px[x0 + col, row] = 170
                elif cell != ".":
                    raise SystemExit(f"ERROR: {ch!r} 第 {row + 1} 行有认不出的记号 {cell!r}")

    big = mask.resize((width * scale, height * scale), Image.NEAREST)
    out = Image.new("RGB", big.size, (0, 0, 0))
    out_px = out.load()
    big_px = big.load()
    for y in range(big.height):
        for x in range(big.width):
            level = big_px[x, y]
            if level == 255:
                out_px[x, y] = color
            elif level == 170:
                out_px[x, y] = accent
    return out


def emit_c(out: list[str], symbol: str, image: Image.Image, text: str, scale: int) -> None:
    values = [to_rgb565(pixel) for pixel in image.getdata()]
    out.append(f"// {text}:{image.width}×{image.height},"
               f"{image.width * image.height * 2} 字节")
    out.append(f"static const uint16_t {symbol}[{image.width} * {image.height}] = {{")
    for start in range(0, len(values), 16):
        chunk = ",".join(f"0x{v:04X}" for v in values[start:start + 16])
        out.append(f"    {chunk},")
    out.append("};")
    out.append("")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=Path("assets/sprites/focus_wordmark.c"),
                        help="生成的 .c 输出路径")
    parser.add_argument("--text", default="Focus", help="要画的字(只能用 GLYPHS 里的字符)")
    parser.add_argument("--scale", type=int, default=7, help="整数放大倍数")
    parser.add_argument("--color", default=f"{DEFAULT_COLOR:06X}",
                        help="笔画颜色,RRGGBB")
    parser.add_argument("--accent", default=f"{DEFAULT_ACCENT:06X}",
                        help="点缀色(RGB 里的绿蒂),RRGGBB")
    parser.add_argument("--preview", type=Path, default=None,
                        help="可选:把 1:1 的 PNG 也写一份到这个目录,只为人眼检查")
    args = parser.parse_args(argv)

    if args.scale < 1:
        print("ERROR: --scale 必须是正整数", file=sys.stderr)
        return 1
    try:
        color = tuple(int(args.color[i:i + 2], 16) for i in (0, 2, 4))
        accent = tuple(int(args.accent[i:i + 2], 16) for i in (0, 2, 4))
    except (ValueError, IndexError):
        print(f"ERROR: --color / --accent 不是 RRGGBB: {args.color} / {args.accent}",
              file=sys.stderr)
        return 1

    image = compose(args.text, args.scale, color, accent)

    if args.preview is not None:
        args.preview.mkdir(parents=True, exist_ok=True)
        image.save(args.preview / f"{args.text.lower()}.png")

    # 回读校验:RGB565 解回来必须还是那个颜色,否则说明色深或者字节序写错了。
    worst = max(
        max(abs(a - b) for a, b in zip(from_rgb565(to_rgb565(pixel)), pixel))
        for pixel in image.getdata()
    )
    if worst > 16:
        print(f"ERROR: RGB565 回读误差 {worst} 超过容许的 16", file=sys.stderr)
        return 1

    lines = [
        "// 由 tools/gen_brand.py 生成,请勿手改。",
        "// 内容:像素字模 " + args.text + f",5×7 点阵 × {args.scale} 倍,"
        f"笔画 #{args.color}、点缀 #{args.accent}(小写 o 那颗番茄的绿蒂)",
        "// 重新生成(在仓库根目录):",
        f"//     python3 tools/gen_brand.py --out {args.out}",
        "// 命令不要写成续行的 // 注释:GCC 的 -Wcomment 会把行尾反斜杠接起来的下一行",
        "// 算进同一条注释,在 -Werror 下直接编译失败。",
        "//",
        "// 和水果精灵一样已合成到纯黑底上,所以是 RGB565 而不是带 alpha 的格式 ——",
        "// 屏幕底色就是 #000000(见 main/focus_ui.c 的 COL_BG)。",
        '#include "focus_sprites.h"',
        "",
    ]
    emit_c(lines, "PX_WORDMARK", image, args.text, args.scale)
    lines.append("const focus_sprite_t focus_wordmark = {")
    lines.append(f"    PX_WORDMARK, {image.width}, {image.height},")
    lines.append("};")
    lines.append("")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines), encoding="utf-8")
    print(f"回读校验通过(最大误差 {worst}/255)")
    print(f"写出 {args.out}:{image.width}×{image.height},"
          f"{image.width * image.height * 2} 字节 RGB565")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
