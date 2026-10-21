"""Draw the UI icons the pages need, now that the bear mascot is gone.

Hand-written 16x16 ASCII, nearest neighbour x4 to 64x64 RGB565, emitted as
lv_image_dsc_t. Word art used to be built the same way, but it is now cut out of
a supplied illustration sheet (see tools/prepare_word_art.py); these two files
stay deliberately separate either way -- word art is content that changes with
the word list, these are fixed chrome.
"""
from pathlib import Path
import struct
import zlib

R = Path(__file__).resolve().parents[1]
GRID = 16
SCALE = 4
SIZE = GRID * SCALE  # 64

PALETTE = {
    ".": 0xF7F3E7,  # cream background, matches the card in main/ui.c
    "K": 0x241F1A,  # outline / black
    "W": 0xFFFFFF,  # white
    "S": 0x6E6A63,  # mid grey
    "G": 0x205C48,  # the UI's green, used for accents
}

SPEAKER = """
................
................
.........K......
........KK...K..
.......KKK..K.K.
......KKKK.K...K
.KKKKKKKKK.K...K
.KKKKKKKKK.K...K
.KKKKKKKKK.K...K
.KKKKKKKKK.K...K
......KKKK.K...K
.......KKK..K.K.
........KK...K..
.........K......
................
................
"""

ICONS = [("speaker", SPEAKER)]


def lines_of(text):
    rows = [r for r in text.strip("\n").split("\n")]
    if len(rows) != GRID or any(len(r) != GRID for r in rows):
        raise SystemExit("sprite must be %dx%d, got %dx%d"
                         % (GRID, GRID, len(rows), len(rows[0]) if rows else 0))
    return rows


def rgb565(hex_colour):
    return (((hex_colour >> 19) & 0x1F) << 11) | (((hex_colour >> 10) & 0x3F) << 5) | ((hex_colour >> 3) & 0x1F)


def upscale(rows):
    out = []
    for line in rows:
        row = bytearray()
        for ch in line:
            row += struct.pack("<H", rgb565(PALETTE[ch])) * SCALE
        out.extend([bytes(row)] * SCALE)
    assert len(out) == SIZE and len(out[0]) == SIZE * 2
    return b"".join(out)


def upscale_rgb(rows):
    grid = []
    for line in rows:
        row = [PALETTE[ch] for ch in line for _ in range(SCALE)]
        grid.extend([row] * SCALE)
    return [bytes(b for p in row for b in ((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF)) for row in grid]


def write_png(path, width, height, rows):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
    raw = b"".join(b"\x00" + r for r in rows)
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9))
                     + chunk(b"IEND", b""))


def build_sheet(sprites):
    """One row: every icon at 1x, in order, on the card colour."""
    pad = 8
    width = pad + len(sprites) * (SIZE + pad)
    height = SIZE + pad * 2
    bg = bytes(((0xF7F3E7 >> 16) & 0xFF, (0xF7F3E7 >> 8) & 0xFF, 0xF7F3E7 & 0xFF))
    canvas = [bytearray(bg * width) for _ in range(height)]
    for i, (_name, rows) in enumerate(sprites):
        x0 = pad + i * (SIZE + pad)
        for y, row in enumerate(upscale_rgb(rows)):
            canvas[pad + y][x0 * 3:(x0 + SIZE) * 3] = row
    return width, height, [bytes(r) for r in canvas]


def main():
    sprites = [(name, lines_of(text)) for name, text in ICONS]
    code = ['#include "ui_art.h"']
    descriptors = []
    for name, rows in sprites:
        data = upscale(rows)
        key = "ui_%s" % name
        code.append("static const uint8_t %s[]={%s};" % (key, ",".join(map(str, data))))
        descriptors.append("{.header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,"
                           ".w=%d,.h=%d,.stride=%d},.data_size=%d,.data=%s}"
                           % (SIZE, SIZE, SIZE * 2, len(data), key))
    code.append("static const lv_image_dsc_t ui_art_images[UI_ART_COUNT]={%s};" % ",".join(descriptors))
    code.append("const lv_image_dsc_t *ui_art_image(unsigned id){return &ui_art_images[id%UI_ART_COUNT];}")
    (R / "main/ui_art.c").write_text("\n".join(code) + "\n")
    (R / "main/ui_art.h").write_text(
        "#pragma once\n#include \"lvgl.h\"\n"
        "/* %s -- fixed UI chrome, not content. */\n" % " / ".join(n for n, _ in sprites)
        + "typedef enum {%s,UI_ART_COUNT} ui_art_t;\n" % ",".join("UI_ART_" + n.upper() for n, _ in sprites)
        + "const lv_image_dsc_t *ui_art_image(unsigned id);\n")

    out = R / "assets/art/ui"
    out.mkdir(parents=True, exist_ok=True)
    for name, rows in sprites:
        write_png(out / (name + ".png"), SIZE, SIZE, upscale_rgb(rows))
    write_png(out / "contact-sheet.png", *build_sheet(sprites))
    print("Wrote main/ui_art.c (%d icons) and %d previews" % (len(sprites), len(sprites) + 1))


if __name__ == "__main__":
    main()
