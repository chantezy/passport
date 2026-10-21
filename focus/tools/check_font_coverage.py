#!/usr/bin/env python3
"""Verify that the UI's own text is actually covered by the bundled fonts.

A missing glyph is silent at runtime: LVGL draws the placeholder box and the
label just looks wrong on a 240x320 panel nobody is watching during boot. So
this walks the rendering sources, collects every character that appears in a
string literal, and checks it against the cmap tables of the generated fonts.

It parses the *generated* `.c` files rather than the charset inventory on
purpose: the inventory is an input, the cmap tables are what the firmware will
actually look up.

Two deliberate choices, both about keeping the fonts small without letting a
gap through:

  * Only the sources that hand text to LVGL are scanned (see RENDER_SOURCES).
    Log strings are not rendered, and including them would make every log
    wording change require regenerating a font.
  * Coverage is required per font, not globally: the pixel font only ever draws
    digits and a colon, so making it carry printable ASCII would buy ~40 KB of
    glyphs nothing renders. Each font's obligation is declared in
    REQUIRED_PER_FONT, and the application's own text must be covered by the
    union.

Exit status is non-zero if any character used by the UI is uncovered.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# The files that build LVGL text. UI copy added anywhere else is not rendered and
# will not be seen by this check -- keep labels in focus_ui.c (and the phase
# names in focus_model.c) so the font inventory stays complete.
RENDER_SOURCES = (
    ROOT / "main" / "focus_ui.c",
    ROOT / "main" / "focus_model.c",
)

CHARSET_FILES = (
    ROOT / "assets" / "fonts" / "charset_focus.txt",
    ROOT / "assets" / "fonts" / "charset_focus_pixel_35.txt",
)

# Per-font obligations.
#
# Formatted values ("25 分钟", "4 个") put digits on screen that never appear as
# literals, and any label may carry them, so the text font has to cover the full
# printable ASCII range rather than just what the sources spell out.
#
# The pixel font is the opposite case: it draws exactly two format strings and
# nothing else, so it is held to exactly those strings' characters. The countdown
# draws "%02d:%02d" and the home picker draws "%d" -- between them that is digits
# and a colon. No space: neither string contains one, and the two stepper signs
# beside the home number are bitmap buttons (assets/sprites/focus_buttons.c),
# not glyphs, so they are not required here.
ASCII_PRINTABLE = set(range(0x20, 0x7F))
DIGITS = set(range(0x30, 0x3A))
REQUIRED_PER_FONT = {
    "focus_text_20": ASCII_PRINTABLE,
    "focus_pixel_35": DIGITS | {0x3A},
}

STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"', re.S)
CMAP_ENTRY = re.compile(r"\{[^{}]*LV_FONT_FMT_TXT_CMAP_(FORMAT0_TINY|SPARSE_TINY)[^{}]*\}", re.S)
U16_ARRAY = re.compile(r"static const uint16_t (\w+)\[\] = \{([^}]*)\}", re.S)
FONT_DEF = re.compile(r"^const lv_font_t (\w+) = \{", re.M)


def strip_comments(text: str) -> str:
    """Drop `//` and `/* */` comments, keeping string literals intact.

    A hand-rolled scanner rather than a couple of regexes because Chinese prose
    in comments is full of quote characters, and `"http://..."` must not be read
    as the start of a line comment.
    """
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch in ('"', "'"):
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2  # skip the escaped character
                    continue
                if text[j] == ch:
                    break
                j += 1
            out.append(text[i : j + 1])
            i = j + 1
        elif ch == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif ch == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def used_codepoints() -> dict[int, set[str]]:
    """Map codepoint -> names of rendering sources that use it in a literal."""
    used: dict[int, set[str]] = {}
    for path in RENDER_SOURCES:
        if not path.is_file():
            continue
        text = strip_comments(path.read_text(encoding="utf-8"))
        for match in STRING_LITERAL.finditer(text):
            body = match.group(1)
            # Drop escape sequences (`\n`, `\"`, ...) -- they are not glyphs.
            body = re.sub(r"\\.", "", body)
            for ch in body:
                if ord(ch) >= 0x20:
                    used.setdefault(ord(ch), set()).add(path.name)
    return used


def parse_u16_arrays(text: str) -> dict[str, list[int]]:
    arrays: dict[str, list[int]] = {}
    for symbol, body in U16_ARRAY.findall(text):
        values = [int(v) for v in re.findall(r"\d+", body)]
        arrays[symbol] = values
    return arrays


def font_coverage(path: Path) -> tuple[str, set[int]]:
    text = path.read_text(encoding="utf-8")
    found = FONT_DEF.search(text)
    if not found:
        raise ValueError(f"{path}: no `const lv_font_t` definition found")
    name = found.group(1)

    arrays = parse_u16_arrays(text)
    covered: set[int] = set()
    for entry in CMAP_ENTRY.finditer(text):
        kind = entry.group(1)
        body = entry.group(0)
        start = int(re.search(r"\.range_start = (\d+)", body).group(1))
        if kind == "FORMAT0_TINY":
            length = int(re.search(r"\.range_length = (\d+)", body).group(1))
            covered.update(range(start, start + length))
        else:
            symbol = re.search(r"\.unicode_list = (\w+)", body).group(1)
            covered.update(start + rel for rel in arrays[symbol])
    if not covered:
        raise ValueError(f"{path}: parsed no cmap coverage")
    return name, covered


def describe(codepoints: set[int]) -> str:
    return "".join(sorted(chr(cp) for cp in codepoints))


def listed_codepoints(path: Path) -> set[int]:
    """Read a charset inventory the way tools/lv_font_emit.py does: one token each.

    Re-implemented here rather than imported so this check does not depend on the
    generators it is checking (a broken loader would otherwise fail both sides).
    """
    text = re.sub(r"#[^\n]*", " ", path.read_text(encoding="utf-8"))
    return {
        int(token[2:], 16) if token.upper().startswith("U+") else ord(token)
        for token in text.split()
    }


def main() -> int:
    font_paths = sorted((ROOT / "assets" / "fonts").glob("*.c"))
    if not font_paths:
        print("ERROR: no generated fonts under assets/fonts/", file=sys.stderr)
        return 1

    fonts: dict[str, set[int]] = {}
    for path in font_paths:
        name, covered = font_coverage(path)
        fonts[name] = covered
        print(f"字体 {name}: 覆盖 {len(covered)} 个码位 ({path.name})")

    # A new font with no declared obligation would silently ship uncovered.
    undeclared = sorted(set(fonts) - set(REQUIRED_PER_FONT))
    if undeclared:
        print(
            f"ERROR: 新字体没有在 REQUIRED_PER_FONT 里登记必备字符集: {', '.join(undeclared)}",
            file=sys.stderr,
        )
        return 1

    for name, covered in sorted(fonts.items()):
        gap = REQUIRED_PER_FONT[name] - covered
        if gap:
            print(
                f"ERROR: {name} 未覆盖必备字符 {describe(gap)}",
                file=sys.stderr,
            )
            return 1

    union: set[int] = set()
    for covered in fonts.values():
        union |= covered

    used = used_codepoints()
    missing = {cp for cp in used if cp not in union}
    if missing:
        print(
            f"ERROR: {len(missing)} 个界面用到的字符不在任何字体里: {describe(missing)}",
            file=sys.stderr,
        )
        for cp in sorted(missing):
            print(f"  U+{cp:04X} {chr(cp)!r}  <- {', '.join(sorted(used[cp]))}", file=sys.stderr)
        return 1

    for path in CHARSET_FILES:
        if not path.is_file():
            print(f"ERROR: 缺少字符集清单 {path.relative_to(ROOT)}", file=sys.stderr)
            return 1
        unbuilt = listed_codepoints(path) - union
        if unbuilt:
            print(
                f"ERROR: {path.name} 里的字符没有生成进字体: {describe(unbuilt)}",
                file=sys.stderr,
            )
            return 1

    extra = sorted(cp for cp in union if cp not in used)
    print(f"界面字面量使用 {len(used)} 个码位,全部已覆盖。")
    if extra:
        print(f"字体里另有 {len(extra)} 个字符未被界面字面量直接引用(格式化输出与必备集)。")
    print("Font coverage: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
