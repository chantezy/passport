<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, sprites, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

### Focus application fonts

The Focus timer under `main/` draws its interface with two bitmap fonts: a 20 px text font cut from Noto Sans SC Regular, and one pixel font for the large numbers. The generated `.c` files are committed; the source `.otf` is not.

| File | Contents | Use and source |
| --- | --- | --- |
| [`fonts/focus_text_20.c`](fonts/focus_text_20.c) | 139 glyphs, 16,093 bytes of glyph bitmap, line height 30 | Every label, hint, and status line drawn by [`main/focus_ui.c`](../main/focus_ui.c). Generated 2026-10-11 from Noto Sans SC Regular at 20 px. |
| [`fonts/focus_pixel_35.c`](fonts/focus_pixel_35.c) | 11 glyphs, 4,643 bytes of glyph bitmap, line height 35 | Both large numbers: the countdown readout under the main visual on the timer screen, formatted as `%02d:%02d`, and the focus-length number in the middle of the home screen's picker, formatted as `%d`. The 5 × 7 matrix below at ×5. |
| [`fonts/charset_focus.txt`](fonts/charset_focus.txt) | Character inventory, one token per line | Code points for the 20 px font: printable ASCII, `U+00B7`, and the CJK characters the UI copy uses. |
| [`fonts/charset_focus_pixel_35.txt`](fonts/charset_focus_pixel_35.txt) | Character inventory, one token per line | Code points for the pixel font: digits and a colon. No space — neither format string contains one. The `-` / `+` that used to sit beside the home number are now the two button images described below, so they are images on both sides of the label rather than glyphs within it. |

- **Source font**: Noto Sans SC Regular, SIL Open Font License 1.1. Downloaded 2026-10-11 from `https://cdn.jsdelivr.net/gh/googlefonts/noto-cjk@main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf` (8,331,336 bytes). The `.otf` is not committed: keep it outside the repository and pass its path to the generator. No license file is shipped because no font binary is redistributed here. The pixel font touches no font file at all: its glyphs are a hand-written 5 × 7 dot matrix inside the generator, so there is nothing to license.
- **Format**: LVGL 9.5.0 (the version pinned in `dependencies.lock`) PLAIN, 4 bpp, uncompressed, with UTF-8 text encoding (`CONFIG_LV_TXT_ENC_UTF8`). The bitmap arrays are `const` and stay in Flash; input decoding may use internal RAM.
- **Pixel font**: emitted by [`tools/gen_pixel_font.py`](../tools/gen_pixel_font.py) from one matrix at ×5, and only integer scaling keeps pixel art crisp. Both large numbers share this one font rather than having a size each: they are the same thing drawn on two screens, and splitting them into two sizes only adds a way for "these should match" to drift. Digits are five cells wide with a fixed six-cell advance, so the number does not jitter sideways as it counts down; the colon is a separate three-cell glyph. Line height equals the matrix and the baseline is 0, which makes a glyph's ink top coincide exactly with its line box — the layout positions these labels by line-box top and needs no conversion between the two.
- **Converter**: [`tools/gen_font.py`](../tools/gen_font.py) on Python 3.9 with Pillow 11.3.0. It rasterizes through FreeType and re-decodes its own output with the same routine LVGL runs at runtime before writing the file, so the emitted bytes are verified rather than assumed. Both front ends share [`tools/lv_font_emit.py`](../tools/lv_font_emit.py), which plans the cmaps, packs the 4 bpp nibbles, and runs that round-trip check; the two generators differ only in where glyphs come from. The pinned `lv_font_conv` 1.5.3 is not used because it writes corrupt glyph bitmaps under current Node.js; see the [CJK bitmap font reference](../docs/reference/shinku-chen/lvgl-cjk-font-subsets.md).
- **Regeneration** below assumes the source `.otf` is at `/path/to/NotoSansSC-Regular.otf`. Run from the repository root:

```bash
python3 tools/gen_font.py --font /path/to/NotoSansSC-Regular.otf --size 20 \
    --charset assets/fonts/charset_focus.txt \
    --name focus_text_20 --out assets/fonts/focus_text_20.c \
    --source-note "<provenance line, see below>"
python3 tools/gen_pixel_font.py --scale 5 \
    --charset assets/fonts/charset_focus_pixel_35.txt \
    --name focus_pixel_35 --out assets/fonts/focus_pixel_35.c
```

- **Provenance line**: `--source-note` is written verbatim into the generated header's second line, and the committed text font carries one. Its exact text is that second line in the generated `.c` file, and it is reproduced in full in [`README.zh_CN.md`](README.zh_CN.md) — this file is checked for English-only prose, so the non-English note cannot be quoted here. Pass the same string byte for byte, or a regeneration shows a changed provenance line instead of the glyph change you meant to make. The pixel fonts need no note: their source is the script itself, and that is what their header says.
- **Coverage**: [`tools/check_font_coverage.py`](../tools/check_font_coverage.py) reads the string literals of `main/focus_ui.c` and `main/focus_model.c` and fails when any character they contain is absent from the generated cmap tables. It runs as part of `./tools/validate.sh`, which also requires the 20 px font to carry printable ASCII and the pixel font to carry digits and a colon. Each font is held only to what it actually draws: making the pixel font carry printable ASCII would buy tens of KB of glyphs nothing renders. New UI copy must be added to the matching inventory file and the font regenerated.
- **Destination**: both `.c` files are compiled into the `main` component through `target_sources()` in [`main/CMakeLists.txt`](../main/CMakeLists.txt), and declared in [`main/focus_fonts.h`](../main/focus_fonts.h).

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Sprites

Store generated display bitmaps that the application draws directly, together with their editable sources, in `sprites/`.

### Focus fruit sprites

The Focus timer's main visual is a piece of fruit that gets eaten as the current phase runs down. Each phase picks one of six fruits at random, and the fruit changes shape five times over the phase. The images come from three grid sheets authored for this project, split into 30 sprites (6 fruits × 5 stages) by a committed generator.

| File | Contents | Use and source |
| --- | --- | --- |
| [`source/fruit-sheet-1.webp`](sprites/source/fruit-sheet-1.webp) | 1568 × 831, WebP RGB, no alpha | Editable source: watermelon, pineapple, tomato, and carrot, five stages each in one row. The banana used to be the first row on this same sheet; after it was redrawn, that row was cropped away (see `fruit-sheet-3.webp`). The crop was re-saved **losslessly**, so the sprites of the other four fruits are byte-for-byte what they were before the crop. Authored for this project; redistribution permitted. |
| [`source/fruit-sheet-2.webp`](sprites/source/fruit-sheet-2.webp) | 1536 × 1024, WebP RGB, no alpha | Editable source: apple, five stages laid out 3 + 2 across two rows. Same provenance. |
| [`source/fruit-sheet-3.webp`](sprites/source/fruit-sheet-3.webp) | 1536 × 1024, WebP RGB, no alpha | Editable source: banana (redrawn), five stages in one row. The whole fruit is noticeably tall, and what is left as it is eaten is the peel curling down. Same provenance. |
| `focus_fruit_<fruit>.c` (6 files) | 30 RGB565 arrays, 685,802 bytes of pixel data total | One file per fruit, five stages each, emitted by [`tools/gen_sprites.py`](../tools/gen_sprites.py). Compiled into the `main` component through `target_sources()` in [`main/CMakeLists.txt`](../main/CMakeLists.txt). |

- **Format**: RGB565, no alpha channel. The timer screen background is pure black (`COL_BG` in [`main/focus_ui.c`](../main/focus_ui.c)), so the generator composites each sprite onto black and drops the alpha channel. That is 2 bytes per pixel instead of 3 or 4: about 0.65 MB rather than 0.98 MB. Byte order matches LVGL's native little-endian RGB565, because [`components/bsp/src/bsp_display_lvgl.c`](../components/bsp/src/bsp_display_lvgl.c) sets `.swap_bytes = true` when it hands the buffer to the ST7789 over SPI.
- **Background removal**: the sheets carry a painted light-grey checkerboard, not an alpha channel. The generator flood-fills inward from the four borders over a "neutral and light" test, so white highlights enclosed by fruit flesh survive instead of being punched out. Edge pixels touching that background get an estimated coverage alpha, which is then un-premultiplied against white so no grey halo is left behind.
- **Scaling**: each fruit gets exactly one scale factor for all five stages, chosen so the most demanding of its stages fits the 134 × 134 box. One factor per fruit is what preserves the artist's own sense of the fruit shrinking; deriving a factor per stage would inflate the last stage past the first. That box is a hard bound, not a preference: the countdown text sits directly below it. It was 168 × 168 until the artwork was shrunk to 80%; the box is still centered on the same point, so the fruit shrinks in place rather than moving. `BOX` in [`tools/gen_sprites.py`](../tools/gen_sprites.py), `FOCUS_FRUIT_BOX` in [`main/focus_fruit_math.h`](../main/focus_fruit_math.h), and the `FRUIT_X`/`FRUIT_Y` pair in [`main/focus_ui.c`](../main/focus_ui.c) have to change together.
- **Generated, then verified**: the generator decodes its own RGB565 output back to RGB and compares it against the source pixel by pixel, failing if the difference exceeds what 5/6-bit quantization alone can explain. The source sheets stay in `source/` so the sprites can be regenerated; do not hand-edit the `.c` files.
- **Regeneration** below assumes Pillow is installed. Run from the repository root:

```bash
python3 tools/gen_sprites.py --source-dir assets/sprites/source --out-dir assets/sprites
```

- **Coverage**: [`tests/test_focus_sprites.c`](../tests/test_focus_sprites.c) links all 30 arrays and asserts that none is missing, that every one fits the box, that the table index matches `focus_fruit_name()`, and that the last stage is genuinely smaller than the first. It runs as part of `./tools/validate.sh`. Total size is budgeted there, so a regenerated set that is unexpectedly large fails rather than silently eating Flash.

### Focus home wordmark

The home screen is titled with the pixel word "Focus", drawn as a bitmap rather than from a font so the letterforms stay crisp and the LVGL heap carries none of it. The lowercase `o` is not that letter: it is a single tomato drawn in the same dot grid, so the home title and the fruit on the timer screen are the same idea.

| File | Contents | Use and source |
| --- | --- | --- |
| `focus_wordmark.c` | One RGB565 array, 203 × 49, 19,894 bytes of pixel data | Home screen title. Hand-authored 5 × 7 dot-matrix glyphs for F, tomato, c, u, and s, scaled 7× and composited onto black by [`tools/gen_brand.py`](../tools/gen_brand.py). Compiled into the `main` component through `target_sources()` in [`main/CMakeLists.txt`](../main/CMakeLists.txt). |

- **Format**: identical to the fruit sprites — RGB565 onto black, no alpha. Two colours: the letterforms and the tomato body in `#FF6B5A`, the tomato's stem in `#57C15C`. Only integer scaling is used: a fractional factor makes the pixel cells uneven in size, which reads as "not pixel art" at a glance. At 7× the 29 × 7 dot matrix is exactly 203 × 49, leaving 18 pixels of margin on each side of the 240-wide screen.
- **Regeneration** assumes Pillow is installed. Run from the repository root:

```bash
python3 tools/gen_brand.py --out assets/sprites/focus_wordmark.c
```

- **Coverage**: the sprite test above budgets the 30 fruit arrays; this file is separate and not counted there. Its 19,894 bytes bring the committed sprite pixel data to 721,130 bytes.

### Focus picker buttons

The `-` and `+` flanking the focus-length number on the home screen are images, not glyphs. They replaced two glyphs that used to be rendered from a font, and those glyphs were removed from that font's inventory so nothing renders them.

| File | Contents | Use and source |
| --- | --- | --- |
| [`source/picker-buttons.webp`](sprites/source/picker-buttons.webp) | 1568 × 784, WebP RGBA | Editable source: a minus button and a plus button on a transparent ground. Authored for this project; redistribution permitted. |
| `focus_buttons.c` | Two RGB565 arrays, 27 × 27 each, 2,916 bytes of pixel data total | The two stepper buttons around the home screen's focus-length number, emitted by [`tools/gen_buttons.py`](../tools/gen_buttons.py). Compiled into the `main` component through `target_sources()` in [`main/CMakeLists.txt`](../main/CMakeLists.txt). |

- **Format**: identical to the fruit sprites — RGB565 onto black, no alpha, little-endian to match the display path.
- **Slicing**: the generator does not take coordinates. It reads the sheet's alpha channel, thresholds it, and finds column-connected blocks of opaque pixels, so moving the buttons inside the artwork does not invalidate the command line. It fails if it does not find exactly two blocks, or if the two blocks differ in width by more than a sixteenth — a silent mis-slice would put the minus sign on the right-hand button.
- **Background**: the sheet's ground is a soft white glow at roughly a third opacity, not clean transparency, so the threshold is deliberately hard (alpha ≥ 128). Compositing a partly transparent white glow onto a black screen leaves a grey halo along every edge.
- **Scaling**: the pieces are pre-multiplied against the thresholded alpha before the LANCZOS resize. Resizing straight RGBA blends the transparent white into the edge pixels, which is the same halo by a slower route. The source blocks are 450 × 443 and 449 × 443, and both are scaled to one width *and one height*: computing the height per block would magnify the one-pixel width difference into a one-pixel height difference at some sizes (33 and 34 at 34 px wide), leaving one button taller than the other. The pair is one thing on screen, so its height is computed once. It fails if the two source blocks do not actually share a height. At the current 27 px both blocks happen to round to 27, but that is these two numbers getting lucky, not a reason to drop the rule.
- **Size**: 27 px wide is 80 % of the 34 px the pair was cut to before, which was itself 60 % of the 56 px it was drawn at. At 56 px the buttons stood taller than the 35 px number between them, which made the row read as two buttons holding a number; shrinking them hands the row back to the number, with the buttons as the operators beside it. Shrinking them does not move the number: the row is centred on the screen with two equal-width buttons, so the digits' x position is a function of the two gaps and the digit width alone. That row is now 27 + 14 + 60 + 14 + 27 = 142 px, leaving 49 px on each side of a 240 px screen.
- **Press feedback**: while the matching key is held down, [`main/focus_ui.c`](../main/focus_ui.c) dims that button's image to 40 % opacity via `lv_obj_set_style_image_opa`. The opacity is the only thing that changes — there is no second, "pressed" bitmap.
- **Regeneration** assumes Pillow is installed. Run from the repository root:

```bash
python3 tools/gen_buttons.py --out assets/sprites/focus_buttons.c
```

- **Coverage**: [`tests/test_focus_sprites.c`](../tests/test_focus_sprites.c) asserts that both buttons are present, are the same size as each other, are not blank, fit the picker row, and match a pinned 27 × 27 fingerprint. Their 2,916 bytes bring the committed sprite pixel data to 724,046 bytes.

### Focus scene images

The timer screen's main slot shows three different things over the life of a phase, chosen by `focus_ui_scene()` in [`main/focus_ui.c`](../main/focus_ui.c): the fruit being eaten while focus runs, a finished card the moment focus completes, and — while the break runs — three sleeping poses that rotate every 700 ms. The card and the three poses come from two sheets authored for this project.

| File | Contents | Use and source |
| --- | --- | --- |
| [`source/done-card.webp`](sprites/source/done-card.webp) | 1254 × 1254, WebP RGB, no alpha | Editable source: the finished card — a boy with closed eyes, a green check, and four grey corner brackets. Authored for this project; redistribution permitted. |
| [`source/sleep-frames.webp`](sprites/source/sleep-frames.webp) | 1254 × 1254, WebP RGB, no alpha | Editable source: the same boy asleep in bed, three poses, the `z` balloon growing from frame to frame. Same provenance. |
| `focus_scenes.c` | Four RGB565 arrays, 104,520 bytes of pixel data total | One 134 × 126 finished card and three 88 × 134 sleep frames, emitted by [`tools/gen_scenes.py`](../tools/gen_scenes.py). Compiled into the `main` component through `target_sources()` in [`main/CMakeLists.txt`](../main/CMakeLists.txt). |

- **Format and background**: identical to the fruit sprites — RGB565 onto black, no alpha, little-endian to match the display path. Both sheets carry the same painted checkerboard rather than an alpha channel, and the generator reuses `background_mask()` and `cut_sprite()` from [`tools/gen_sprites.py`](../tools/gen_sprites.py) rather than restating those rules, so both families of artwork are cut by the same flood-fill.
- **The three sleep frames share one crop**: they draw the same character on the same bed, but the balloon grows from frame to frame, so their ink bounding boxes are all different (411 × 573, 375 × 563, 408 × 628). Scaling each to its own box would make the character swell and jump between frames, which is exactly what an animation must not do. Instead the generator measures the bed in each frame — its bottom edge, and the left end of its widest row — and takes the union of the three ink extents in bed-relative coordinates, which is one 411 × 628 crop that all three are cut with. All three land on exactly 88 × 134, and the generator asserts they came out identical before writing the file.
- **The card is one piece**: it has no alignment problem, so its own bounding box is the crop. The corner brackets are part of the artwork and stay in.
- **Generated, then verified**: the same decode-and-compare round trip as the fruit sprites, plus a hard count — the card sheet must yield exactly one block and the sleep sheet exactly three, so replacing the artwork without revisiting the crop fails loudly instead of producing a plausible-looking file.
- **Regeneration** assumes Pillow is installed. Run from the repository root:

```bash
python3 tools/gen_scenes.py --source-dir assets/sprites/source \
    --out assets/sprites/focus_scenes.c
```

- **No pause, so no dimming either**: a pomodoro is indivisible and the timer has no stopped state at all (see `focus_run_t` in [`main/focus_model.h`](../main/focus_model.h)), so this slot never needs a dim or an overlay — it only ever swaps between three pictures: the fruit being eaten, the focus-finished card, and the three sleep frames.
- **Coverage**: [`tests/test_focus_sprites.c`](../tests/test_focus_sprites.c) links all four arrays, asserts that none is blank and that each fits the shared box, pins their size fingerprints, and asserts that the three sleep frames match one another — a mismatch there is what a jumping animation looks like in code. Their 104,520 bytes bring the committed sprite pixel data to 828,566 bytes.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
