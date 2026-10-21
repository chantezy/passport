#!/usr/bin/env python3
"""Split the illustrated word sheet into 112x112 sprites and emit main/word_art.c.

Source art is assets/art/words-sheet.webp: one sheet holding all 60 pictures in a
10x6 grid, laid out row-major in exactly the order content/curriculum.json lists
the words. The sheet's geometry is detected from its alpha channel rather than
hard-coded, so a redrawn sheet with the same grid still works.

The sheet is a set of die-cut stickers: every picture sits on transparency, with a
soft edge of its own colour fading out around it. Alpha therefore also answers the
other question -- where one picture ends and the paper begins -- and each tile is
cropped to that silhouette, scaled with LANCZOS, and centred on the screen
background. The transparency inside the crop already holds that background, so no
sprite carries a visible square.

A source cell is about 157x174, so 112 is still a downscale: the bigger card does
not cost any sharpness.

Needs Pillow for the WebP decode; the PNG previews are written with the standard
library alone.
"""
from pathlib import Path
import hashlib
import json
import struct
import zlib

try:
    from PIL import Image
except ImportError:  # pragma: no cover - a build-machine problem, not a content one
    raise SystemExit("prepare_word_art.py needs Pillow: python3 -m pip install --user Pillow")

R = Path(__file__).resolve().parents[1]
SHEET = R / "assets/art/words-sheet.webp"
OUT = R / "assets/art/words"

COLS, ROWS = 10, 6
SIZE = 112

# Proportions of a tile, so the split survives a sheet drawn at another scale.
PAD = 0.06         # breathing room left around the subject in the sprite cell
# A row or column belongs to the grid once this share of it is opaque.
BAND = 0.10
# Alpha at or above which a pixel is artwork. Low, because a sticker's edge fades
# out over a dozen pixels and a hard cut anywhere in that ramp leaves a ring; high
# enough to shrug off the faintest tail of it.
ALPHA = 8
# The screen background every sprite is composited onto -- main/ui.c paints the
# same 0xF7F3E7 behind every page, so a sprite's transparent side is invisible.
PAGE = (0xF7, 0xF3, 0xE7)


def bands(profile, threshold):
    """Contiguous runs above `threshold`, as (start, end) inclusive pairs."""
    out, start = [], None
    for i, value in enumerate(profile):
        if value > threshold and start is None:
            start = i
        elif value <= threshold and start is not None:
            out.append((start, i - 1))
            start = None
    if start is not None:
        out.append((start, len(profile) - 1))
    return out


def load_sheet():
    """Return (flat RGB image, alpha pixel access, size).

    Compositing onto PAGE here is the whole background treatment: whatever the
    sheet leaves transparent arrives on the screen as the colour the page behind
    the sprite already is.
    """
    if not SHEET.exists():
        raise SystemExit("missing %s" % SHEET.relative_to(R))
    image = Image.open(SHEET)
    alpha = image.getchannel("A")
    flat = Image.new("RGB", image.size, PAGE)
    flat.paste(image, mask=alpha)
    return flat, alpha.load(), image.size


def detect_grid(ap, width, height):
    """Column and row bands of the tile grid, or die trying."""
    cols = bands([sum(1 for y in range(height) if ap[x, y] > 128) for x in range(width)],
                 height * BAND)
    rows = bands([sum(1 for x in range(width) if ap[x, y] > 128) for y in range(height)],
                 width * BAND)
    if len(cols) != COLS or len(rows) != ROWS:
        raise SystemExit("sheet has a %dx%d grid, expected %dx%d"
                         % (len(cols), len(rows), COLS, ROWS))
    return cols, rows


def subject_box(ap, tile, width, height):
    """Tight box of the artwork in one tile, read off the alpha channel.

    A colour test cannot do this job on this sheet: the white splash is as pale as
    the page, the black blob is as dark as any shadow, and the rabbit's body is the
    same cream as everything behind it. Alpha is the only thing here that separates
    picture from paper.

    The hunt opens six pixels past the tile because the grid bands are cut at a
    share of the cell rather than at the last pixel, so a round subject can lose
    its outermost column to them.
    """
    x0, y0, x1, y1 = tile
    bx0, bx1, by0, by1 = x1, x0, y1, y0
    for y in range(max(0, y0 - 6), min(height, y1 + 7)):
        for x in range(max(0, x0 - 6), min(width, x1 + 7)):
            if ap[x, y] >= ALPHA:
                bx0, bx1 = min(bx0, x), max(bx1, x)
                by0, by1 = min(by0, y), max(by1, y)
    if bx0 > bx1 or by0 > by1:
        raise SystemExit("no artwork in the tile at (%d,%d)" % (x0, y0))
    return bx0, by0, bx1, by1


def render(flat, box):
    """Crop, scale to fit SIZE keeping aspect, then centre on the page colour."""
    crop = flat.crop((box[0], box[1], box[2] + 1, box[3] + 1))
    cw, ch = crop.size
    inner = int(SIZE * (1 - 2 * PAD))
    scale = min(inner / cw, inner / ch)
    nw, nh = max(1, round(cw * scale)), max(1, round(ch * scale))
    if (nw, nh) != (cw, ch):
        crop = crop.resize((nw, nh), Image.LANCZOS)
    cell = Image.new("RGB", (SIZE, SIZE), PAGE)
    cell.paste(crop, ((SIZE - nw) // 2, (SIZE - nh) // 2))
    return list(cell.getdata())


def rgb565(rgb):
    return ((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3)


def pack_rgb565(pixels):
    out = bytearray()
    for rgb in pixels:
        out += struct.pack("<H", rgb565(rgb))
    return bytes(out)


def write_png(path, width, height, rows):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
    raw = b"".join(b"\x00" + r for r in rows)
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9))
                     + chunk(b"IEND", b""))


def rgb_rows(pixels):
    return [bytes(b for rgb in pixels[y * SIZE:(y + 1) * SIZE] for b in rgb)
            for y in range(SIZE)]


GLYPHS = {
    "0": [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
    "1": ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "2": [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    "3": ["#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."],
    "4": ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
    "5": ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    "6": ["..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."],
    "7": ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    "8": [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    "9": [".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."],
    "a": [".....", ".....", ".###.", "....#", ".####", "#...#", ".####"],
    "b": ["#....", "#....", "####.", "#...#", "#...#", "#...#", "####."],
    "c": [".....", ".....", ".###.", "#...#", "#....", "#...#", ".###."],
    "d": ["....#", "....#", ".####", "#...#", "#...#", "#...#", ".####"],
    "e": [".....", ".....", ".###.", "#...#", "#####", "#....", ".###."],
    "f": ["..##.", ".#..#", ".#...", "####.", ".#...", ".#...", ".#..."],
    "g": [".....", ".....", ".####", "#...#", ".####", "....#", ".###."],
    "h": ["#....", "#....", "####.", "#...#", "#...#", "#...#", "#...#"],
    "i": ["..#..", ".....", ".##..", "..#..", "..#..", "..#..", ".###."],
    "j": ["...#.", ".....", "..##.", "...#.", "...#.", "#..#.", ".##.."],
    "k": ["#....", "#....", "#..#.", "#.#..", "##...", "#.#..", "#..#."],
    "l": [".##..", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."],
    "m": [".....", ".....", "##.#.", "#.#.#", "#.#.#", "#...#", "#...#"],
    "n": [".....", ".....", "####.", "#...#", "#...#", "#...#", "#...#"],
    "o": [".....", ".....", ".###.", "#...#", "#...#", "#...#", ".###."],
    "p": [".....", ".....", "####.", "#...#", "####.", "#....", "#...."],
    "q": [".....", ".....", ".####", "#...#", ".####", "....#", "....#"],
    "r": [".....", ".....", "#.##.", "##..#", "#....", "#....", "#...."],
    "s": [".....", ".....", ".####", "#....", ".###.", "....#", "####."],
    "t": [".#...", ".#...", "####.", ".#...", ".#...", ".#..#", "..##."],
    "u": [".....", ".....", "#...#", "#...#", "#...#", "#...#", ".####"],
    "v": [".....", ".....", "#...#", "#...#", "#...#", ".#.#.", "..#.."],
    "w": [".....", ".....", "#...#", "#...#", "#.#.#", "#.#.#", ".#.#."],
    "x": [".....", ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#"],
    "y": [".....", ".....", "#...#", "#...#", ".####", "....#", ".###."],
    "z": [".....", ".....", "#####", "...#.", "..#..", ".#...", "#####"],
    " ": [".....", ".....", ".....", ".....", ".....", ".....", "....."],
    "-": [".....", ".....", ".....", "#####", ".....", ".....", "....."],
}
TEXT_SCALE = 3
TEXT_H = 7 * TEXT_SCALE


def draw_text(canvas, width, x, y, text, colour):
    for ch in text:
        glyph = GLYPHS.get(ch)
        if glyph is None:
            x += 6 * TEXT_SCALE
            continue
        for gy, line in enumerate(glyph):
            for gx, mark in enumerate(line):
                if mark != "#":
                    continue
                for dy in range(TEXT_SCALE):
                    for dx in range(TEXT_SCALE):
                        px, py = x + gx * TEXT_SCALE + dx, y + gy * TEXT_SCALE + dy
                        if 0 <= px < width:
                            canvas[py][px] = colour
        x += 6 * TEXT_SCALE
    return x


def build_sheet(sprites):
    """Labelled contact sheet, 5 across, so the sprites can be reviewed at a glance."""
    cols, cell, pad, label = 5, SIZE * 2, 8, 12 + TEXT_H
    rows_n = (len(sprites) + cols - 1) // cols
    w = cols * (cell + pad) + pad
    h = rows_n * (cell + pad + label) + pad
    cream, ink, edge = 0xF7F3E7, 0x241F1A, 0xC8C2B0
    canvas = [[cream] * w for _ in range(h)]
    for i, (word, pixels) in enumerate(sprites):
        x0 = pad + (i % cols) * (cell + pad)
        y0 = pad + (i // cols) * (cell + pad + label)
        for y in range(SIZE):
            for x in range(cell):
                px = pixels[y * SIZE + (x // 2)]
                colour = (px[0] << 16) | (px[1] << 8) | px[2]
                canvas[y0 + y * 2][x0 + x] = colour
                canvas[y0 + y * 2 + 1][x0 + x] = colour
        for x in range(cell):
            canvas[y0][x0 + x] = canvas[y0 + cell - 1][x0 + x] = edge
        for y in range(cell):
            canvas[y0 + y][x0] = canvas[y0 + y][x0 + cell - 1] = edge
        draw_text(canvas, w, x0 + 2, y0 + cell + 5, "%02d %s" % (i, word), ink)
    rows = [b"".join(bytes(((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF)) for p in row)
            for row in canvas]
    return w, h, rows


def main():
    curriculum = json.loads((R / "content/curriculum.json").read_text())
    units = curriculum["units"]
    words = [en for u in units for en, _zh in u["words"]]

    flat, ap, (width, height) = load_sheet()
    cols, rows = detect_grid(ap, width, height)
    if len(cols) * len(rows) != len(words):
        raise SystemExit("sheet holds %d tiles, the word list has %d"
                         % (len(cols) * len(rows), len(words)))

    sprites = []
    for i in range(len(words)):
        cx0, cx1 = cols[i % COLS]
        cy0, cy1 = rows[i // COLS]
        box = subject_box(ap, (cx0, cy0, cx1, cy1), width, height)
        sprites.append((words[i], render(flat, box)))

    code = ['#include "word_art.h"', '#include "content.h"', "",
            "/* %d sprites of %dx%d RGB565, split row-major out of" % (len(words), SIZE, SIZE),
            "   assets/art/words-sheet.webp in the order content/curriculum.json lists",
            "   the words -- see assets/art/words/manifest.json for the source hash. */",
            ""]
    descriptors, blob = [], bytearray()
    for i, (word, pixels) in enumerate(sprites):
        data = pack_rgb565(pixels)
        assert len(data) == SIZE * SIZE * 2
        blob += data
        key = "word_%02d_%s" % (i, word.replace("-", "_"))
        code.append("static const uint8_t %s[]={%s};" % (key, ",".join(map(str, data))))
        descriptors.append("{.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,"
                           ".w=%d,.h=%d,.stride=%d},.data_size=%d,.data=%s}"
                           % (SIZE, SIZE, SIZE * 2, len(data), key))
    code.append("")
    code.append("static const lv_image_dsc_t word_images[%d]={%s};" % (len(descriptors), ",".join(descriptors)))
    code.append("")
    code.append("/* A stale word_art.c must fail the build, not show the wrong picture. */")
    code.append("_Static_assert(sizeof(word_images)/sizeof(*word_images)==WORD_COUNT,"
                "\"word_art does not match WORD_COUNT\");")
    code.append("")
    code.append("const lv_image_dsc_t *word_image(unsigned index){return &word_images[index%WORD_COUNT];}")
    (R / "main/word_art.c").write_text("\n".join(code) + "\n")
    (R / "main/word_art.h").write_text(
        "#pragma once\n#include \"lvgl.h\"\n"
        "const lv_image_dsc_t *word_image(unsigned index);\n")

    OUT.mkdir(parents=True, exist_ok=True)
    for i, (word, pixels) in enumerate(sprites):
        write_png(OUT / ("%02d-%s.png" % (i, word)), SIZE, SIZE, rgb_rows(pixels))
    write_png(OUT / "contact-sheet.png", *build_sheet(sprites))
    (OUT / "manifest.json").write_text(json.dumps(dict(
        source="assets/art/words-sheet.webp",
        source_sha256=hashlib.sha256(SHEET.read_bytes()).hexdigest(),
        source_width=width, source_height=height,
        grid="%dx%d" % (COLS, ROWS), resample="LANCZOS",
        background="#%02X%02X%02X" % PAGE, crop="alpha bounding box, soft edge kept",
        width=SIZE, height=SIZE, count=len(sprites), format="RGB565",
        bytes=len(blob), sha256=hashlib.sha256(blob).hexdigest(),
    ), indent=2) + "\n")
    print("Split %s into %d sprites and wrote main/word_art.c (%d bytes each) plus %d previews"
          % (SHEET.relative_to(R), len(sprites), SIZE * SIZE * 2, len(sprites)))


if __name__ == "__main__":
    main()
