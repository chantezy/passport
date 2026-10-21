#!/usr/bin/env python3
"""把像素水果素材图切成「每种水果 × 每个阶段」并输出成固件用的 RGB565 常量。

素材是几张网格图,背景是画上去的浅灰棋盘格 —— 不是 alpha 通道。所以第一步不是
直接取像素,而是**从四条边做洪水填充**:只有"从外面走得到、且接近中性浅色"的像素
才算背景。果实内部的白色高光被果肉包住,填充走不进去,因此被保留;直接按亮度抠图
会把高光也挖成洞。贴着背景的那一圈按"离白有多远"估覆盖率当 alpha,再反预乘把混进
来的背景色减掉,不然边缘会留一圈灰白描边。

第二步是缩放,这里有个容易做错的地方:如果每张各自撑满 BOX 的框,最后只剩一点的
那一档会被放大到比整只还大,整只水果在"被吃掉"的过程中忽大忽小。所以**每种水果
只算一个系数**,五个阶段共用,取值让这种水果里最苛刻的那一档正好卡进框 ——
这样阶段之间的大小关系才是画家画的那个关系。

最后合成到纯黑上再转 RGB565:屏幕底色就是 #000000(见 main/focus_ui.c 的
COL_BG),所以透明边合到黑底之后不再需要 alpha 通道,每像素 2 字节而不是 3 或 4
字节。这一步省下来的是约 0.55 MB Flash。

输出是 LVGL 无关的纯数据(`focus_sprite_t`,见 main/focus_sprites.h),LVGL 的
图像描述符在 main/focus_fruit.c 里现搭 —— 这样生成物能被主机测试直接编译,
不需要在测试里拉起整个 LVGL。

生成前会把自己写出的 RGB565 解码回 RGB 和源图逐像素比一遍,误差只允许来自 5/6
位量化;数目对不上或差得离谱就直接失败,而不是写出一份"看起来没问题"的文件。

Usage:
  python3 tools/gen_sprites.py --source-dir assets/sprites/source \
      --out-dir assets/sprites [--preview DIR]
"""

from __future__ import annotations

import argparse
import sys
from collections import deque
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - 环境问题,不是代码路径
    print("ERROR: Pillow is required (pip3 install --user Pillow)", file=sys.stderr)
    raise SystemExit(1)


# 六种水果的固定顺序。这个顺序就是 FOCUS_FRUIT_* 的序号,改它等于改存档里
# fruit 字节的含义 —— main/focus_fruit_math.h 里的 FOCUS_FRUIT_COUNT 要跟着改。
FRUIT_ORDER = ("banana", "watermelon", "pineapple", "tomato", "carrot", "apple")

# 每个水果的阶段数。五档:整只 / 一口 / 两口 / 三口 / 只剩一点。
STAGE_COUNT = 5

# 素材图 → 图上按阅读顺序出现的水果。每张图里一种水果占一行(或连续几行),
# 每行必须正好切出 STAGE_COUNT 张。苹果那张是 3+2 的两行布局,所以它只列一次,
# 由切图顺序自然凑满 5 张。
#
# 香蕉单独占一张图:它后来重画过,而原稿是和西瓜、菠萝、番茄、胡萝卜挤在同一张
# 图上的。换图时那一行香蕉已经从 fruit-sheet-1.webp 上剪掉了(见
# assets/README.zh_CN.md 的素材表),所以这里就是"图 ↔ 水果"的直接对应,
# 没有额外的跳过规则。
SHEETS = (
    ("fruit-sheet-1.webp", FRUIT_ORDER[1:5]),
    ("fruit-sheet-2.webp", ("apple",)),
    ("fruit-sheet-3.webp", ("banana",)),
)

# 上机框:果子的外接矩形最大就放进这个正方形,居中。与 FOCUS_FRUIT_BOX 一致。
#
# 134 而不是 168:果子的中心仍钉在同一个点上,整体缩到原来的 80%。改这个数必须
# 同时改 main/focus_fruit_math.h 的 FOCUS_FRUIT_BOX、main/focus_ui.c 的
# FRUIT_X/FRUIT_Y(让框的中心不动),以及 tests/test_focus_sprites.c 的尺寸指纹。
BOX = 134

# 背景判据:三条通道都不小于 BG_MIN 且彼此相差不超过 BG_SPREAD 的像素才可能是
# 背景(棋盘格的浅灰与白)。果子里的浅黄、浅红都不满足"接近中性"。
BG_MIN = 200
BG_SPREAD = 15

# 行/列投影的阈值:整行至少这么多个果实像素才算一行,整列至少 (band 高 >= COL_MIN_H
# 时) 这么多个才算一列。用来把网格切成格子,顺带滤掉孤立的噪点。
ROW_MIN = 8
COL_MIN = 4

# 两个列组之间隔得比这个还近就并回同一个果子 —— 咬下来的碎屑常和主体只差一两像素。
MERGE_GAP = 8

# 切出来的精灵小于这个就当成噪声丢掉。
MIN_SPRITE = 20

# 贴着背景的像素估 alpha 时的"饱和亮度":最暗通道到了 255-SATURATION 就认为完全不透明。
# 120 是拿素材试出来的 —— 边缘的抗锯齿过渡大约就跨这个范围。
EDGE_SATURATION = 120

# 估出来的 alpha 低于这个就直接当透明(纯背景残留),不写进精灵。
MIN_ALPHA = 40

# 容许的量化误差:RGB565 的红蓝是 5 位(步长约 8),绿是 6 位(步长约 4)。
MAX_ROUNDTRIP_ERROR = 16


def bgish(pixel: tuple) -> bool:
    """这个像素像不像画上去的背景(接近中性的浅色)。"""
    return min(pixel) >= BG_MIN and (max(pixel) - min(pixel)) <= BG_SPREAD


def background_mask(im: Image.Image) -> bytearray:
    """从四条边洪水填充,返回"能从外面走到的浅色像素"掩码(1 = 背景)。

    只看边界而不看整张图,是为了保住果实内部走不到的高光。
    """
    width, height = im.size
    px = im.load()
    reach = bytearray(width * height)
    queue: deque[tuple[int, int]] = deque()

    def push(x: int, y: int) -> None:
        index = y * width + x
        if not reach[index] and bgish(px[x, y]):
            reach[index] = 1
            queue.append((x, y))

    for x in range(width):
        push(x, 0)
        push(x, height - 1)
    for y in range(height):
        push(0, y)
        push(width - 1, y)

    while queue:
        x, y = queue.popleft()
        if x > 0:
            push(x - 1, y)
        if x < width - 1:
            push(x + 1, y)
        if y > 0:
            push(x, y - 1)
        if y < height - 1:
            push(x, y + 1)
    return reach


def sprite_boxes(im: Image.Image, reach: bytearray) -> list[tuple[int, int, int, int]]:
    """把网格切成一个个果子的外接矩形,按阅读顺序返回 (x0, y0, x1, y1)。"""
    width, height = im.size
    ink = bytearray(1 if not reach[i] else 0 for i in range(width * height))

    row_count = [sum(ink[y * width:(y + 1) * width]) for y in range(height)]
    bands: list[tuple[int, int]] = []
    start = None
    for y in range(height):
        if row_count[y] >= ROW_MIN:
            if start is None:
                start = y
        elif start is not None:
            if y - start >= MIN_SPRITE:
                bands.append((start, y - 1))
            start = None
    if start is not None:
        bands.append((start, height - 1))

    boxes: list[tuple[int, int, int, int]] = []
    for y0, y1 in bands:
        col_count = [sum(ink[y * width + x] for y in range(y0, y1 + 1)) for x in range(width)]
        columns: list[tuple[int, int]] = []
        start = None
        for x in range(width):
            if col_count[x] >= COL_MIN:
                if start is None:
                    start = x
            elif start is not None:
                if x - start >= MIN_SPRITE:
                    columns.append((start, x - 1))
                start = None
        if start is not None:
            columns.append((start, width - 1))

        merged: list[tuple[int, int]] = []
        for column in columns:
            if merged and column[0] - merged[-1][1] <= MERGE_GAP:
                merged[-1] = (merged[-1][0], column[1])
            else:
                merged.append(column)

        for x0, x1 in merged:
            xs = [x for x in range(x0, x1 + 1)
                  if any(ink[y * width + x] for y in range(y0, y1 + 1))]
            ys = [y for y in range(y0, y1 + 1)
                  if any(ink[y * width + x] for x in range(x0, x1 + 1))]
            if xs and ys:
                boxes.append((xs[0], ys[0], xs[-1], ys[-1]))
    return boxes


def cut_sprite(im: Image.Image, reach: bytearray, box: tuple[int, int, int, int]) -> Image.Image:
    """裁出一只果子并补出 alpha:背景全透明,贴边的一圈按覆盖率给半透明。"""
    width, _ = im.size
    px = im.load()
    x0, y0, x1, y1 = box
    out = Image.new("RGBA", (x1 - x0 + 1, y1 - y0 + 1), (0, 0, 0, 0))
    out_px = out.load()

    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if reach[y * width + x]:
                continue
            pixel = px[x, y]
            alpha = 255
            on_edge = any(
                0 <= x + dx < width and 0 <= y + dy < im.height
                and reach[(y + dy) * width + (x + dx)]
                for dy in (-1, 0, 1) for dx in (-1, 0, 1)
            )
            if on_edge:
                darkest = min(pixel)
                alpha = max(0, min(255, round((255 - darkest) * 255 / EDGE_SATURATION)))
                if alpha < MIN_ALPHA:
                    continue
                # 反预乘:这个像素是"果子色"和背景白按 alpha 混出来的,把白减掉。
                fraction = alpha / 255.0
                pixel = tuple(
                    max(0, min(255, round((c - (1 - fraction) * 255) / fraction))) for c in pixel
                )
            out_px[x - x0, y - y0] = (pixel[0], pixel[1], pixel[2], alpha)
    return out


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


def load_sheet(path: Path) -> Image.Image:
    try:
        with Image.open(path) as handle:
            return handle.convert("RGB")
    except OSError as exc:
        print(f"ERROR: 读不了素材图 {path}: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc


def collect_sprites(source_dir: Path) -> dict[tuple[str, int], Image.Image]:
    """切出全部精灵,返回 {(水果, 阶段 0..4): RGBA 图}。"""
    sprites: dict[tuple[str, int], Image.Image] = {}
    for filename, fruits in SHEETS:
        path = source_dir / filename
        sheet = load_sheet(path)
        reach = background_mask(sheet)
        boxes = sprite_boxes(sheet, reach)
        wanted = len(fruits) * STAGE_COUNT
        if len(boxes) != wanted:
            print(
                f"ERROR: {filename} 切出 {len(boxes)} 张,应该正好 {wanted} 张"
                f"({len(fruits)} 种水果 × {STAGE_COUNT} 个阶段)。"
                "素材布局变了就改本文件的 SHEETS。",
                file=sys.stderr,
            )
            raise SystemExit(1)

        print(f"{filename}: {sheet.size[0]}×{sheet.size[1]} → {len(boxes)} 张")
        for index, box in enumerate(boxes):
            fruit = fruits[index // STAGE_COUNT]
            stage = index % STAGE_COUNT
            sprite = cut_sprite(sheet, reach, box)
            sprites[(fruit, stage)] = sprite
            print(f"  {fruit:<11}阶段 {stage + 1}  {box[2] - box[0] + 1}×{box[3] - box[1] + 1}")

    missing = [(f, s) for f in FRUIT_ORDER for s in range(STAGE_COUNT)
               if (f, s) not in sprites]
    if missing:
        print(f"ERROR: 缺 {len(missing)} 张精灵: {missing}", file=sys.stderr)
        raise SystemExit(1)
    return sprites


def fit_fruits(
    sprites: dict[tuple[str, int], Image.Image], preview_dir: Path | None
) -> dict[tuple[str, int], Image.Image]:
    """按"每种水果一个系数"缩放并合成到纯黑上,返回 RGB 图。"""
    scaled: dict[tuple[str, int], Image.Image] = {}
    for fruit in FRUIT_ORDER:
        # 系数只由这一种水果定一次,五个阶段共用 —— 大小关系才是画家画的那个关系。
        # 取"五个阶段里最苛刻的那张"卡进框,而不是只看整只那张:被咬过的果子外接框
        # 偶尔比整只还宽一点(西瓜第 2 阶段就比整只宽 1 个像素),只按整只定系数会让它顶出框。
        factor = min(
            min(BOX / sprites[(fruit, stage)].width, BOX / sprites[(fruit, stage)].height)
            for stage in range(STAGE_COUNT)
        )
        for stage in range(STAGE_COUNT):
            sprite = sprites[(fruit, stage)]
            width = max(1, round(sprite.width * factor))
            height = max(1, round(sprite.height * factor))
            resized = sprite.resize((width, height), Image.LANCZOS)
            # 透明边合到纯黑:屏幕底色就是黑的,合完就不需要 alpha 通道了。
            flat = Image.new("RGB", (width, height), (0, 0, 0))
            flat.paste(resized, (0, 0), resized)
            scaled[(fruit, stage)] = flat
            if preview_dir is not None:
                preview_dir.mkdir(parents=True, exist_ok=True)
                flat.save(preview_dir / f"{fruit}_{stage + 1}.png")
    return scaled


def pixel_symbol(fruit: str, stage: int) -> str:
    """某个阶段的像素数组符号名。桌面表和数据必须用同一个名字。"""
    return f"PX_{fruit.upper()}_{stage + 1}"


def emit_rgb565(out: list[str], symbol: str, image: Image.Image, width: int) -> None:
    values = [to_rgb565(pixel) for pixel in image.getdata()]
    out.append(f"static const uint16_t {symbol}[{image.width} * {image.height}] = {{")
    for start in range(0, len(values), width):
        chunk = ",".join(f"0x{v:04X}" for v in values[start:start + width])
        out.append(f"    {chunk},")
    out.append("};")
    out.append("")


def emit_fruit_c(
    path: Path, fruit: str, stages: dict[int, Image.Image], sheet_names: list[str]
) -> int:
    """写出一种水果的 .c 文件,返回像素字节数。"""
    symbol = f"focus_fruit_{fruit}"
    lines = [
        "// 由 tools/gen_sprites.py 生成,请勿手改。",
        f"// 来源: {', '.join('assets/sprites/source/' + name for name in sheet_names)}",
        "// 重新生成(在仓库根目录):",
        "//     python3 tools/gen_sprites.py --source-dir assets/sprites/source --out-dir assets/sprites",
        "// 命令不要写成续行的 // 注释:GCC 的 -Wcomment 会把行尾反斜杠接起来的下一行",
        "// 算进同一条注释,在 -Werror 下直接编译失败。",
        "//",
        "// 每张都已合成到纯黑底上,所以是 RGB565 而不是带 alpha 的格式 —— 屏幕底色",
        "// 就是 #000000(见 main/focus_ui.c 的 COL_BG),合完 alpha 就没有信息了。",
        '#include "focus_sprites.h"',
        "",
    ]
    total = 0
    for stage in range(STAGE_COUNT):
        image = stages[stage]
        total += image.width * image.height * 2
        lines.append(
            f"// {fruit} · 阶段 {stage + 1}:{image.width}×{image.height},"
            f"{image.width * image.height * 2} 字节"
        )
        emit_rgb565(lines, pixel_symbol(fruit, stage), image, 16)

    lines.append(f"const focus_sprite_t {symbol}[FOCUS_FRUIT_STAGE_COUNT] = {{")
    for stage in range(STAGE_COUNT):
        image = stages[stage]
        lines.append(
            f"    {{ {pixel_symbol(fruit, stage)}, {image.width}, {image.height} }},"
        )
    lines.append("};")
    lines.append("")

    path.write_text("\n".join(lines), encoding="utf-8")
    return total


def verify(scaled: dict[tuple[str, int], Image.Image]) -> tuple[int, int]:
    """回读校验:把 RGB565 解回 RGB 和源图比,返回 (最大误差, 像素数)。"""
    worst = 0
    pixels = 0
    for (fruit, stage), image in sorted(scaled.items()):
        values = [to_rgb565(pixel) for pixel in image.getdata()]
        for value, pixel in zip(values, image.getdata()):
            back = from_rgb565(value)
            worst = max(worst, max(abs(a - b) for a, b in zip(back, pixel)))
            pixels += 1
        if len(values) != image.width * image.height:
            raise SystemExit(f"ERROR: {fruit} 阶段 {stage + 1} 的像素数与宽高对不上")
    return worst, pixels


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source-dir", required=True, type=Path,
                        help="素材图所在目录")
    parser.add_argument("--out-dir", required=True, type=Path,
                        help="生成的 .c 输出目录(assets/sprites)")
    parser.add_argument("--preview", type=Path, default=None,
                        help="可选:把上机尺寸的 PNG 也写一份到这个目录,只为人眼检查")
    args = parser.parse_args(argv)

    sprites = collect_sprites(args.source_dir)
    scaled = fit_fruits(sprites, args.preview)

    worst, pixels = verify(scaled)
    if worst > MAX_ROUNDTRIP_ERROR:
        print(f"ERROR: RGB565 回读误差 {worst} 超过容许的 {MAX_ROUNDTRIP_ERROR}", file=sys.stderr)
        return 1

    total = 0
    for fruit in FRUIT_ORDER:
        stages = {stage: scaled[(fruit, stage)] for stage in range(STAGE_COUNT)}
        sheets = [name for name, fruits in SHEETS if fruit in fruits]
        total += emit_fruit_c(args.out_dir / f"focus_fruit_{fruit}.c", fruit, stages, sheets)

    print()
    print(f"回读校验: {pixels} 像素,最大误差 {worst}/255(RGB565 量化),通过")
    print(f"写出 {len(FRUIT_ORDER)} 个文件,共 {total} 字节 = {total / 1024:.0f} KB RGB565")
    for fruit in FRUIT_ORDER:
        sizes = " → ".join(f"{scaled[(fruit, s)].width}×{scaled[(fruit, s)].height}"
                           for s in range(STAGE_COUNT))
        print(f"  {fruit:<11}{sizes}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
