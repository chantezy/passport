#!/usr/bin/env python3
"""把首页时长选择器的两颗按钮切成固件用的 RGB565 常量。

素材是一张自己画的图(assets/sprites/source/picker-buttons.webp):两个带圆角与
立体下沿的方块按钮,左减右加,红色符号。这里要解决三件素材本身带来的事:

1. **背景是一圈半透明的白色辉光**,不是干净的透明底。辉光是画在浅色底上的柔和
   投影(alpha 约 30%),合到纯黑屏幕上会变成一圈灰蒙的脏边。所以先按 alpha 硬
   阈值二值化:辉光的 alpha 落在 30% 一档,按钮本体是 100%,一刀切在中间能把
   两者分开,留下按钮自己的硬边。

2. **缩放要走预乘**。直接对 RGBA 做 LANCZOS,透明像素的颜色(RGB 全是白)会混进
   边缘,合到黑底后是一圈浅色描边 —— 和 gen_sprites.py 里那个灰白描边是同一类
   错误。这里的做法是先把 RGB 乘上 alpha(ImageChops.multiply 正好是 /255 的
   定点乘法),再缩放;因为底色是纯黑,"合到黑底"就等于缩放后的预乘值本身,所以
   缩放结果直接当最终像素用,不必再除以 alpha。

3. **框是自动找出来的**,不是写死的坐标。按 alpha 二值图找列方向的连通块(允许
   中间断开几列,免得按钮边缘的锯齿把一块切成两块),从左到右正好两块,依次叫
   minus / plus。素材重画一版、位置挪了,只要仍是"左边减、右边加"就不用改代码。

4. **两颗共用同一个目标高度**。素材里两块本来等高,但各按各的宽高比缩放,四舍五入
   会把"宽度差 1 像素"放大成"高度差 1 像素"。屏幕上它们
   是一对,等高是这一对的属性,所以高度只按第一块算一次,两块都缩到那个高度;
   两块源高度真的不一致则直接失败。

输出是 LVGL 无关的纯数据(focus_sprite_t,见 main/focus_sprites.h),LVGL 的
图像描述符在 main/focus_ui.c 里现搭 —— 和水果精灵、首页字标走同一条路。

生成前会把自己写出的 RGB565 解码回 RGB 与原图逐像素比一遍,误差只允许来自 5/6 位
量化;块数不是两块、或者两块尺寸差太多,都直接失败。

Usage:
  python3 tools/gen_buttons.py [--source assets/sprites/source/picker-buttons.webp]
                               [--out assets/sprites/focus_buttons.c]
                               [--size 27] [--preview DIR]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image, ImageChops
except ImportError:  # pragma: no cover - 环境问题,不是代码路径
    print("ERROR: Pillow is required (pip3 install --user Pillow)", file=sys.stderr)
    raise SystemExit(1)


# 屏幕上每颗按钮的宽(像素)。高按原图比例算出来,不硬编码(两块共用一个高度)。
#
# 设计尺寸是 56:那版按钮(56×55)比中间那个 35px 的数字还高,整行的重心压在两侧
# 的按钮上,选择器看着像"两颗按钮夹着一个数"。先缩到六成(34)让数字重新成为这一行
# 的主角,这一版再缩到六成的八成(27)。
#
# 缩小按钮**不会**挪动中间那个数字:整行在屏幕中线上对称、两颗按钮同宽,于是数字的
# 横坐标只由"两个间距 + 数字宽度"决定(见 main/focus_ui.c 里 HOME_ROW_GAP 的说明)。
# 一行 27 + 14 + 60 + 14 + 27 = 142,在 240 宽的屏上两侧各留 49px。
DEFAULT_SIZE = 27

# alpha 二值化的分界(0..255)。素材的辉光在 79..92,按钮本体在 250 以上,
# 切在中间任何位置都一样,128 只是"取其正中间"。
ALPHA_CUTOFF = 128

# 列的连通块之间允许断开多少列还算是同一块。
COLUMN_GAP = 24

# 从左到右两块按钮的名字,和输出的符号名一一对应。
BUTTONS = ("minus", "plus")


def load_alpha_mask(path: Path, cutoff: int) -> Image.Image:
    """读素材并返回 alpha 二值图(0/255)。"""
    sheet = Image.open(path).convert("RGBA")
    return sheet.getchannel("A").point(lambda v: 255 if v >= cutoff else 0)


def find_blocks(mask: Image.Image, gap: int) -> list[tuple[int, int, int, int]]:
    """从左到右找出 mask 上每个连通块的外接矩形。

    只按列判连通:把"这一列有没有非零像素"排成一个布尔序列,取连续段,再把之间
    间隔小于 gap 列的段并起来。按钮是左右排列的,所以列方向足够;按行列两维连通
    需要整套并查集,对两个已知形状的方块是多余的。
    """
    w, h = mask.size
    data = list(mask.getdata())

    occupied: list[bool] = [False] * w
    for x in range(w):
        for y in range(h):
            if data[y * w + x]:
                occupied[x] = True
                break

    runs: list[list[int]] = []
    for x in range(w):
        if occupied[x]:
            if runs and x - runs[-1][1] <= gap:
                runs[-1][1] = x
            else:
                runs.append([x, x])

    blocks: list[tuple[int, int, int, int]] = []
    for x0, x1 in runs:
        top, bottom = h, -1
        for y in range(h):
            base = y * w
            for x in range(x0, x1 + 1):
                if data[base + x]:
                    if y < top:
                        top = y
                    bottom = y
                    break
        blocks.append((x0, top, x1 + 1, bottom + 1))
    return blocks


def render(sheet: Image.Image, box: tuple[int, int, int, int], width: int, height: int,
           cutoff: int) -> Image.Image:
    """裁出一块按钮,二值化 alpha,预乘后缩放到 width×height,返回合到黑底上的 RGB 图。"""
    piece = sheet.crop(box)
    alpha = piece.getchannel("A").point(lambda v: 255 if v >= cutoff else 0)
    red, green, blue, _ = piece.split()
    # ImageChops.multiply 就是 (a*b)/255,正好是预乘。缩放这些值等于"缩放了已经
    # 合到黑底上的图",所以结果直接可用,不需要回头再除 alpha。
    premultiplied = Image.merge("RGB", (
        ImageChops.multiply(red, alpha),
        ImageChops.multiply(green, alpha),
        ImageChops.multiply(blue, alpha),
    ))
    return premultiplied.resize((width, height), Image.LANCZOS)


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


def emit_c(lines: list[str], symbol: str, image: Image.Image, label: str) -> None:
    values = [to_rgb565(pixel) for pixel in image.getdata()]
    lines.append(f"// {label}:{image.width}×{image.height},"
                 f"{image.width * image.height * 2} 字节")
    lines.append(f"static const uint16_t {symbol}[{image.width} * {image.height}] = {{")
    for start in range(0, len(values), 16):
        chunk = ",".join(f"0x{v:04X}" for v in values[start:start + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path,
                        default=Path("assets/sprites/source/picker-buttons.webp"),
                        help="按钮素材图(两个方块,左减右加)")
    parser.add_argument("--out", type=Path, default=Path("assets/sprites/focus_buttons.c"),
                        help="生成的 .c 输出路径")
    parser.add_argument("--size", type=int, default=DEFAULT_SIZE,
                        help="按钮在屏幕上的宽度(像素),高度按原图比例算,两块共用")
    parser.add_argument("--cutoff", type=int, default=ALPHA_CUTOFF,
                        help="alpha 二值化分界 0..255,用来切掉素材的辉光")
    parser.add_argument("--preview", type=Path, default=None,
                        help="可选:把 1:1 的 PNG 也写一份到这个目录,只为人眼检查")
    args = parser.parse_args(argv)

    if args.size < 8:
        print("ERROR: --size 太小,按钮认不出来", file=sys.stderr)
        return 1
    if not args.source.is_file():
        print(f"ERROR: 找不到素材图 {args.source}", file=sys.stderr)
        return 1

    sheet = Image.open(args.source).convert("RGBA")
    blocks = find_blocks(load_alpha_mask(args.source, args.cutoff), COLUMN_GAP)
    if len(blocks) != len(BUTTONS):
        print(f"ERROR: 素材上找到 {len(blocks)} 块按钮,期望 {len(BUTTONS)} 块"
              f"(左减右加):{blocks}", file=sys.stderr)
        return 1

    widths = [box[2] - box[0] for box in blocks]
    if max(widths) - min(widths) > max(widths) // 16:
        print(f"ERROR: 两块按钮宽度差太多 {widths},素材里大概是两颗不一样的按钮",
              file=sys.stderr)
        return 1

    heights = [box[3] - box[1] for box in blocks]
    if max(heights) - min(heights) > 1:
        print(f"ERROR: 两块按钮源高度差太多 {heights},素材里大概是两颗不一样高的按钮",
              file=sys.stderr)
        return 1

    # 两颗共用同一个目标高度,而不是各按各的宽高比算。素材里两块本来等高(只差 1
    # 像素宽:450×443 与 449×443),各算各的会把这个差放大:56px 那版两块恰好都舍入
    # 到 55,尺寸一小就分家了(34px 那版是 33 与 34,一对按钮右边那颗高出一像素,
    # 肉眼看得出来)。当前这版 27px 两块恰好都舍到 27 —— 但那是这两个数字碰上运气,
    # 不是这条规则可以撤掉的理由。高度是"这一对按钮"的属性,所以只算一次。
    height = max(1, round(heights[0] * args.size / widths[0]))

    images = [render(sheet, box, args.size, height, args.cutoff) for box in blocks]
    for name, box, image in zip(BUTTONS, blocks, images):
        print(f"{name}: 素材 {box[2] - box[0]}×{box[3] - box[1]} @ {box[0]},{box[1]}"
              f" -> {image.width}×{image.height}")

    if args.preview is not None:
        args.preview.mkdir(parents=True, exist_ok=True)
        for name, image in zip(BUTTONS, images):
            image.save(args.preview / f"button-{name}.png")

    # 回读校验:RGB565 解回来必须还是那个颜色,否则说明色深或者字节序写错了。
    worst = 0
    for image in images:
        for pixel in image.getdata():
            worst = max(worst,
                        max(abs(a - b) for a, b in zip(from_rgb565(to_rgb565(pixel)), pixel)))
    if worst > 16:
        print(f"ERROR: RGB565 回读误差 {worst} 超过容许的 16", file=sys.stderr)
        return 1

    lines = [
        "// 由 tools/gen_buttons.py 生成,请勿手改。",
        "// 内容:首页时长选择器的两颗按钮,裁自素材图 "
        + args.source.name + ",已合成到纯黑底。",
        "// 重新生成(在仓库根目录):",
        "//     python3 tools/gen_buttons.py",
        "//",
        "// 和水果精灵、首页字标一样已合成到纯黑底上,所以是 RGB565 而不是带 alpha 的",
        "// 格式 —— 屏幕底色就是 #000000(见 main/focus_ui.c 的 COL_BG)。素材自带的一圈",
        "// 半透明白色辉光在合成前按 alpha 二值化切掉了,否则会在黑底上留一圈灰边。",
        '#include "focus_sprites.h"',
        "",
    ]
    for name, image in zip(BUTTONS, images):
        emit_c(lines, f"PX_BTN_{name.upper()}", image, f"按钮 {name}")
    for name, image in zip(BUTTONS, images):
        lines.append(f"const focus_sprite_t focus_button_{name} = {{")
        lines.append(f"    PX_BTN_{name.upper()}, {image.width}, {image.height},")
        lines.append("};")
    lines.append("")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines), encoding="utf-8")
    print(f"回读校验通过(最大误差 {worst}/255)")
    total = sum(image.width * image.height * 2 for image in images)
    print(f"写出 {args.out}:两张按钮共 {total} 字节 RGB565")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
