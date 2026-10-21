#!/usr/bin/env python3
"""把两张场景素材图切成固件用的 RGB565 常量:专注结束卡 + 睡觉三帧。

计时页那一格原来只放水果,现在按阶段换画面(见 main/focus_ui.c 的 focus_ui_scene):
专注刚走完画一张"合眼比了个勾"的结束卡,休息中轮换三帧睡姿。这两组画面来自两张
自己画的图(assets/sprites/source/),这里把它们切出来、缩到上机尺寸、合成到纯黑,
输出成 LVGL 无关的 RGB565 数组(形状见 main/focus_sprites.h)。

素材和水果那两张是同一种画法:**背景是画上去的浅灰棋盘格,不是 alpha 通道**。所以
抠图和补 alpha 那两步直接复用 tools/gen_sprites.py 里的 background_mask() 与
cut_sprite() —— 从四条边洪水填充,只有"从外面走得到的浅色"才算背景,果子/被子内部
的白色高光被包在外面进不去,因此被保住;贴着背景的一圈按"离白有多远"估覆盖率当
alpha,再反预乘把混进来的背景色减掉。

这里独有的三件事:

1. **睡觉三帧必须落进同一个框**。三帧画的是同一张床上的同一个角色,但嘴边那个 z
   一帧比一帧大,所以三帧各自的墨迹外接框宽高都不一样(343..915 / 353..915 /
   287..915)。要是各按各的外接框缩放,换帧时角色会忽大忽小、上下跳,动图就散了。
   做法是**按床对齐**:每帧量出床的左沿与下沿当锚点(见 bed_anchor),再把三帧的
   墨迹范围换算到锚点相对坐标里取并集,得到**一个公共裁切框**,三帧都用它裁。这样
   三帧上机后尺寸逐字节相同,位置也就一分不差 —— 这是"有动图的感觉"的前提。

2. **结束卡只切一张**。它没有对齐问题,外接框本身就是裁切框;四角的灰色取景框是
   画面的一部分(人物的"相框"),所以照常切进来。

3. **两张图的方块数是硬约束**。结束卡必须正好 1 块、睡觉图必须正好 3 块,多一块少
   一块都可能意味着素材换版了而裁切框还是旧的 —— 直接失败,不猜。

生成前会把自己写出的 RGB565 解码回 RGB 与源图逐像素比一遍,误差只允许来自 5/6 位
量化;数目对不上或差得离谱就直接失败,而不是写出一份"看起来没问题"的文件。

Usage:
  python3 tools/gen_scenes.py --source-dir assets/sprites/source \
      --out assets/sprites/focus_scenes.c [--preview DIR]
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

# 上机框、抠图判据、量化容差全部沿用水果那一套:同一个屏幕、同一种素材、同一个
# 槽位,没有理由各写一份。BOX 是屏幕上的正方形容器,与 FOCUS_FRUIT_BOX 一致。
from gen_sprites import (
    BOX,
    MAX_ROUNDTRIP_ERROR,
    background_mask,
    cut_sprite,
    from_rgb565,
    load_sheet,
    sprite_boxes,
    to_rgb565,
)

# 素材文件名 → 它们在画面里的角色。
DONE_SHEET = "done-card.webp"
SLEEP_SHEET = "sleep-frames.webp"

# 睡觉图上的帧数,以及每一帧在输出数组里的位置。
SLEEP_FRAMES = 3

# 床沿判据。下沿取"还有成片墨迹的最低一行":底边在抗锯齿之后还会拖出一两行只有
# 三五个像素的碎点,按"非空"找最低行会被那几粒带偏几十像素,BED_ROW_MIN 把它们
# 滤掉。整张图上只有被子/床这一条边能一次给出上百个像素。
BED_ROW_MIN = 100

# 在床的下沿往上这么多行里找最宽的一行,取它的最左墨迹当水平锚点。床的底边是整
# 张图最宽的一条水平边;直接用外接框会跟着嘴边的气泡左右偏,用床不会。
BED_WINDOW = 24


def ink_counts(im: Image.Image, reach: bytearray, box: tuple[int, int, int, int]) -> list[int]:
    """box 内每一行有多少个非背景像素,下标 0 对应 box 的顶行。"""
    width, _ = im.size
    x0, y0, x1, y1 = box
    return [sum(1 for x in range(x0, x1 + 1) if not reach[y * width + x])
            for y in range(y0, y1 + 1)]


def bed_anchor(im: Image.Image, reach: bytearray,
               box: tuple[int, int, int, int]) -> tuple[int, int]:
    """量出一帧睡姿里那张床的 (左沿, 下沿),当作这一帧的对齐锚点。

    三帧画的是同一张床,所以"床在哪里"在三帧之间是同一个物理位置 —— 用它对齐,
    帧与帧之间的唯一差别就只剩角色的表情和嘴边那个 z。
    """
    width, _ = im.size
    x0, y0, x1, y1 = box
    rows = ink_counts(im, reach, box)

    candidates = [y for y, n in enumerate(rows) if n >= BED_ROW_MIN]
    if not candidates:
        raise SystemExit(
            f"ERROR: 在 {box} 里找不到床沿(没有一行的墨迹达到 {BED_ROW_MIN} 像素)"
        )
    bottom = y0 + candidates[-1]

    # 同宽时取更低的那一行:底边往下通常还有一两行等宽的实心行。
    widest = max(range(max(y0, bottom - BED_WINDOW), bottom + 1),
                 key=lambda y: (rows[y - y0], y))
    left = min(x for x in range(x0, x1 + 1) if not reach[widest * width + x])
    return left, bottom


def common_crop(boxes: list[tuple[int, int, int, int]],
                anchors: list[tuple[int, int]]) -> tuple[int, int, int, int]:
    """把三帧的墨迹范围换算到锚点相对坐标后取并集,得到一个公共裁切框的偏移。"""
    spans = [(box[0] - ax, box[1] - ay, box[2] - ax, box[3] - ay)
             for box, (ax, ay) in zip(boxes, anchors)]
    return (min(s[0] for s in spans), min(s[1] for s in spans),
            max(s[2] for s in spans), max(s[3] for s in spans))


def fit_box(sprite: Image.Image, box: int) -> Image.Image:
    """等比缩到最长边不超过 box,再把透明边合到纯黑上,返回 RGB 图。

    和水果那边同一套做法:透明边合到黑底之后就不需要 alpha 通道了 —— 屏幕底色就是
    #000000(见 main/focus_ui.c 的 COL_BG),每像素因此只要 2 字节。
    """
    factor = min(box / sprite.width, box / sprite.height)
    width = max(1, round(sprite.width * factor))
    height = max(1, round(sprite.height * factor))
    resized = sprite.resize((width, height), Image.LANCZOS)
    flat = Image.new("RGB", (width, height), (0, 0, 0))
    flat.paste(resized, (0, 0), resized)
    return flat


def cut_scenes(source_dir: Path, preview_dir: Path | None
               ) -> tuple[Image.Image, list[Image.Image], tuple[int, int]]:
    """切出结束卡与睡觉三帧,返回 (结束卡, 三帧, 公共裁切框尺寸),都已缩到上机尺寸。"""
    done_sheet = load_sheet(source_dir / DONE_SHEET)
    done_reach = background_mask(done_sheet)
    done_boxes = sprite_boxes(done_sheet, done_reach)
    if len(done_boxes) != 1:
        raise SystemExit(
            f"ERROR: {DONE_SHEET} 切出 {len(done_boxes)} 张,结束卡应该正好 1 张:"
            f"{done_boxes}。素材换版了就改本文件。"
        )
    done = fit_box(cut_sprite(done_sheet, done_reach, done_boxes[0]), BOX)
    print(f"{DONE_SHEET}: {done_sheet.width}×{done_sheet.height} → 结束卡 {done.width}×{done.height}")

    sleep_sheet = load_sheet(source_dir / SLEEP_SHEET)
    sleep_reach = background_mask(sleep_sheet)
    sleep_boxes = sprite_boxes(sleep_sheet, sleep_reach)
    if len(sleep_boxes) != SLEEP_FRAMES:
        raise SystemExit(
            f"ERROR: {SLEEP_SHEET} 切出 {len(sleep_boxes)} 张,睡觉三帧应该正好"
            f" {SLEEP_FRAMES} 张:{sleep_boxes}。素材换版了就改本文件。"
        )

    anchors = [bed_anchor(sleep_sheet, sleep_reach, box) for box in sleep_boxes]
    rx0, ry0, rx1, ry1 = common_crop(sleep_boxes, anchors)
    print(f"{SLEEP_SHEET}: {sleep_sheet.width}×{sleep_sheet.height} → {SLEEP_FRAMES} 帧,"
          f"公共裁切框 {rx1 - rx0 + 1}×{ry1 - ry0 + 1}(相对床沿 {rx0},{ry0})")

    frames: list[Image.Image] = []
    for index, (box, (ax, ay)) in enumerate(zip(sleep_boxes, anchors)):
        frame = fit_box(
            cut_sprite(sleep_sheet, sleep_reach,
                       (ax + rx0, ay + ry0, ax + rx1, ay + ry1)),
            BOX,
        )
        frames.append(frame)
        print(f"  帧 {index + 1}: 床沿 {ax},{ay}  墨迹 {box[2] - box[0] + 1}×{box[3] - box[1] + 1}"
              f" → {frame.width}×{frame.height}")

    # 三帧尺寸必须一模一样,否则换帧时画面会跳 —— 这是公共裁切框存在的全部理由,
    # 所以在这里硬断言一次,而不是等上了设备用眼睛看出来。
    sizes = {(frame.width, frame.height) for frame in frames}
    if len(sizes) != 1:
        raise SystemExit(f"ERROR: 三帧上机尺寸不一致 {sorted(sizes)},公共裁切框没起作用")

    if preview_dir is not None:
        preview_dir.mkdir(parents=True, exist_ok=True)
        done.save(preview_dir / "scene-done.png")
        for index, frame in enumerate(frames):
            frame.save(preview_dir / f"scene-sleep-{index + 1}.png")

    return done, frames, (rx1 - rx0 + 1, ry1 - ry0 + 1)


def verify(images: dict[str, Image.Image]) -> tuple[int, int]:
    """回读校验:把 RGB565 解回 RGB 和源图比,返回 (最大误差, 像素数)。"""
    worst = 0
    pixels = 0
    for label, image in images.items():
        values = [to_rgb565(pixel) for pixel in image.getdata()]
        if len(values) != image.width * image.height:
            raise SystemExit(f"ERROR: {label} 的像素数与宽高对不上")
        for value, pixel in zip(values, image.getdata()):
            back = from_rgb565(value)
            worst = max(worst, max(abs(a - b) for a, b in zip(back, pixel)))
            pixels += 1
    return worst, pixels


def emit_rgb565(out: list[str], symbol: str, image: Image.Image, label: str) -> int:
    """写一段像素数组,返回字节数。"""
    values = [to_rgb565(pixel) for pixel in image.getdata()]
    out.append(f"// {label}:{image.width}×{image.height},"
               f"{image.width * image.height * 2} 字节")
    out.append(f"static const uint16_t {symbol}[{image.width} * {image.height}] = {{")
    for start in range(0, len(values), 16):
        chunk = ",".join(f"0x{v:04X}" for v in values[start:start + 16])
        out.append(f"    {chunk},")
    out.append("};")
    out.append("")
    return len(values) * 2


def emit_c(path: Path, done: Image.Image, frames: list[Image.Image],
           source_names: list[str], crop: tuple[int, int], size: tuple[int, int]) -> int:
    """写出 focus_scenes.c,返回像素字节数。"""
    lines = [
        "// 由 tools/gen_scenes.py 生成,请勿手改。",
        "// 来源: " + ", ".join("assets/sprites/source/" + name for name in source_names)
        + "(自己画的,可随仓库分发)。",
        "// 内容:计时页那一格上水果之外的画面 —— 专注结束卡 1 张 + 休息中轮换的睡觉 3 帧。",
        "// 重新生成(在仓库根目录):",
        "//     python3 tools/gen_scenes.py --source-dir assets/sprites/source",
        "//         --out assets/sprites/focus_scenes.c",
        "// 命令不要写成续行的 // 注释:GCC 的 -Wcomment 会把行尾反斜杠接起来的下一行",
        "// 算进同一条注释,在 -Werror 下直接编译失败。",
        "//",
        "// 每张都已合成到纯黑底上,所以是 RGB565 而不是带 alpha 的格式 —— 屏幕底色",
        "// 就是 #000000(见 main/focus_ui.c 的 COL_BG),合完 alpha 就没有信息了。",
        "//",
        f"// 睡觉三帧在素材上按床沿对齐、共用同一个 {crop[0]}×{crop[1]} 的裁切框,所以",
        f"// 三帧都是 {size[0]}×{size[1]} —— 换帧时角色不会忽大忽小或上下跳。按床沿对齐的",
        "// 道理写在 tools/gen_scenes.py 的文件注释里,那里才是动手改的地方。",
        '#include "focus_sprites.h"',
        "",
    ]

    total = emit_rgb565(lines, "PX_SCENE_DONE", done, "专注结束卡")
    lines.append("const focus_sprite_t focus_scene_done = {")
    lines.append(f"    PX_SCENE_DONE, {done.width}, {done.height},")
    lines.append("};")
    lines.append("")

    for index, frame in enumerate(frames):
        total += emit_rgb565(lines, f"PX_SCENE_SLEEP_{index + 1}", frame, f"睡觉帧 {index + 1}")

    lines.append("const focus_sprite_t "
                 "focus_scene_sleep[FOCUS_PIC_COUNT - FOCUS_PIC_SLEEP_0] = {")
    for index, frame in enumerate(frames):
        lines.append(f"    {{ PX_SCENE_SLEEP_{index + 1}, {frame.width}, {frame.height} }},")
    lines.append("};")
    lines.append("")

    path.write_text("\n".join(lines), encoding="utf-8")
    return total


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source-dir", type=Path, required=True,
                        help=f"两张素材图所在目录({DONE_SHEET} / {SLEEP_SHEET})")
    parser.add_argument("--out", type=Path, required=True,
                        help="生成的 .c 输出路径(assets/sprites/focus_scenes.c)")
    parser.add_argument("--preview", type=Path, default=None,
                        help="可选:把上机尺寸的 PNG 也写一份到这个目录,只为人眼检查")
    args = parser.parse_args(argv)

    done, frames, crop = cut_scenes(args.source_dir, args.preview)

    images = {"结束卡": done}
    for index, frame in enumerate(frames):
        images[f"睡觉帧 {index + 1}"] = frame
    worst, pixels = verify(images)
    if worst > MAX_ROUNDTRIP_ERROR:
        print(f"ERROR: RGB565 回读误差 {worst} 超过容许的 {MAX_ROUNDTRIP_ERROR}",
              file=sys.stderr)
        return 1

    total = emit_c(args.out, done, frames, [DONE_SHEET, SLEEP_SHEET], crop,
                   (frames[0].width, frames[0].height))

    print()
    print(f"回读校验: {pixels} 像素,最大误差 {worst}/255(RGB565 量化),通过")
    print(f"写出 {args.out}:共 {total} 字节 = {total / 1024:.0f} KB RGB565")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
