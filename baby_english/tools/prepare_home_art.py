#!/usr/bin/env python3
"""Turn the supplied home-page illustration into a full-bleed 240x320 backdrop.

The source is 1086x1448 -- exactly the 240:320 screen ratio (0.75), so the
whole picture fits with no cropping or letterboxing. It is resampled once with
LANCZOS down to the panel resolution, packed as RGB565 and emitted as main/home_art.c.

The packed array is emitted as \\xNN string literals rather than a comma-separated
uint8 list: 153600 bytes spelled out in decimal is ~500 KB of C source, which is
larger than every other art file in main/ combined. Chunks stay at 32 bytes so
no single string literal approaches the 4095-character C99 translation limit.
"""
from pathlib import Path
from PIL import Image
import hashlib
import json
import struct

R = Path(__file__).resolve().parents[1]
SOURCE = R / "assets/art/home/home-source.png"
OUT = R / "assets/art/home"
W, H = 240, 320
CHUNK = 32


def rgb565(image):
    data = bytearray()
    for red, green, blue in image.getdata():
        data += struct.pack("<H", ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3))
    return bytes(data)


def literals(data):
    """One 32-byte \\xNN literal per line; \\x stops at 2 digits, so they never merge."""
    rows = []
    for i in range(0, len(data), CHUNK):
        rows.append('"%s"' % "".join("\\x%02x" % b for b in data[i:i + CHUNK]))
    return "\n".join(rows)


def main():
    image = Image.open(SOURCE).convert("RGB")
    ratio = image.width / image.height
    if abs(ratio - W / H) > 0.005:
        raise SystemExit("home-source.png is %dx%d (ratio %.3f), screen is %.3f -- "
                         "crop it before running this, don't letterbox it"
                         % (image.width, image.height, ratio, W / H))
    fitted = image.resize((W, H), Image.Resampling.LANCZOS)
    data = rgb565(fitted)
    assert len(data) == W * H * 2, len(data)

    fitted.save(OUT / "home-240.png")

    code = ['#include "home_art.h"', "",
            "/* Full-bleed home backdrop, %dx%d RGB565, resampled from" % (W, H),
            "   assets/art/home/home-source.png -- see assets/art/home/manifest.json. */",
            "/* Dimensioned exactly, so the string's NUL terminator is dropped and",
            "   sizeof stays the real pixel byte count. */",
            "static const uint8_t home_rgb565[%d]=" % len(data),
            literals(data) + ";",
            "",
            "static const lv_image_dsc_t home_images[1]={{",
            " .header={.magic=LV_IMAGE_HEADER_MAGIC,.cf=LV_COLOR_FORMAT_RGB565,.w=%d,.h=%d,.stride=%d},"
            % (W, H, W * 2),
            " .data_size=sizeof home_rgb565,.data=home_rgb565}};",
            "",
            "_Static_assert(sizeof home_rgb565==%d,\"home backdrop is not %dx%d RGB565\");" % (len(data), W, H),
            "",
            "const lv_image_dsc_t *home_image(void){return home_images;}",
            ""]
    (R / "main/home_art.c").write_text("\n".join(code))
    (R / "main/home_art.h").write_text(
        '#pragma once\n#include "lvgl.h"\n'
        "/* 240x320 full-bleed backdrop drawn at (0,0); the bottom start hint sits on it. */\n"
        "const lv_image_dsc_t *home_image(void);\n")

    (OUT / "manifest.json").write_text(json.dumps(dict(
        source="assets/art/home/home-source.png",
        source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        source_width=image.width, source_height=image.height,
        resample="LANCZOS", width=W, height=H, format="RGB565",
        bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
    ), indent=2) + "\n")
    print("Wrote main/home_art.c (%d bytes of RGB565 from %dx%d) and %s"
          % (len(data), image.width, image.height, OUT / "home-240.png"))


if __name__ == "__main__":
    main()
