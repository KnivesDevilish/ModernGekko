#!/usr/bin/env python3
"""Builds tools/launcher_cjk_font.inc: the Japanese characters the launcher
draws (its Japanese interface texts and a few names), cut out of Noto Sans CJK
JP (Regular and Bold) and embedded as byte arrays. The interface font has no
CJK glyphs, so these are merged into it.

Noto Sans CJK is under the SIL Open Font License 1.1, which allows bundling and
subsetting. The subsets keep the font's whole name table, so the copyright and
license notice travel inside them; tools/fonts/NotoSansCJK-OFL.txt has the
license text.

The characters come from the JAPANESE_TEXTS table of tools/launcher_text.hpp,
plus CHARACTERS below for Japanese drawn elsewhere, plus every --extra file:
the game's patch notes (MODERNGEKKO_LAUNCHER_PATCH_NOTES) and its 4:3 borders'
borders.txt (MODERNGEKKO_GAME_BORDERS), whose Japanese parts need their
characters too. After changing any of them, run from the
repository root (needs Python 3 and fonttools):
    python3 tools/make_launcher_cjk_font.py [--extra FILE]... [Regular.ttc Bold.ttc]
The default paths are those of Fedora/Bazzite and Debian/Ubuntu.
tests/launcher_text_test.cpp fails when a Japanese text has a character the
font lacks.
"""

import io
import pathlib
import sys

from fontTools import subset
from fontTools.ttLib import TTCollection

# Japanese the launcher draws outside JAPANESE_TEXTS: the Credits page's name
# (moderngekko_launcher.cpp) and the language menu's 日本語 (launcher_text.hpp),
# both written there as "\xE5..." escapes.
CHARACTERS = "奈依茲・悪魔的日本語"


def japanese_text_characters(root):
    """Every character past Latin-1 in launcher_text.hpp's JAPANESE_TEXTS."""
    text = (root / "launcher_text.hpp").read_text(encoding="utf-8")
    start = text.index("inline constexpr Translation JAPANESE_TEXTS[]")
    end = text.index("\n};", start)
    return {character for character in text[start:end] if ord(character) > 0xFF}


def file_characters(path):
    """Every character past Latin-1 in a UTF-8 text file."""
    text = pathlib.Path(path).read_text(encoding="utf-8")
    return {character for character in text if ord(character) > 0xFF}


def characters(root, extra_files):
    found = set(CHARACTERS) | japanese_text_characters(root)
    for path in extra_files:
        found |= file_characters(path)
    return "".join(sorted(found))

FAMILY = "Noto Sans CJK JP"
CANDIDATES = {
    "Regular": [
        "/usr/share/fonts/google-noto-sans-cjk-vf-fonts/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    ],
    "Bold": [
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    ],
}


def find_source(weight, given):
    paths = [given] if given else CANDIDATES[weight]
    for path in paths:
        if pathlib.Path(path).is_file():
            return path
    sys.exit(f"Noto Sans CJK {weight} not found; pass its .ttc path")


def japanese_face(path, weight):
    for font in TTCollection(path).fonts:
        name = font["name"]
        if name.getDebugName(1) == f"{FAMILY} {weight}" or (
            name.getDebugName(1) == FAMILY and name.getDebugName(2) == weight
        ):
            return font
    sys.exit(f"{path} has no {FAMILY} {weight} face")


def build_subset(path, weight, text):
    font = japanese_face(path, weight)
    options = subset.Options()
    options.name_IDs = ["*"]  # keep copyright (0) and license (13, 14)
    options.name_languages = ["*"]
    options.layout_features = []
    options.hinting = False
    options.desubroutinize = True
    options.notdef_outline = True
    subsetter = subset.Subsetter(options)
    subsetter.populate(text=text)
    subsetter.subset(font)
    # Keep the font's own date, so running this again gives the same bytes.
    font.recalcTimestamp = False
    data = io.BytesIO()
    font.save(data)
    return data.getvalue()


def as_array(name, data):
    # inline: one copy in a program whose several files include it.
    lines = [f"inline constexpr unsigned char {name}[] = {{"]
    for start in range(0, len(data), 16):
        chunk = ", ".join(f"0x{byte:02x}" for byte in data[start:start + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


def main():
    arguments = sys.argv[1:]
    extra_files = []
    while "--extra" in arguments:
        at = arguments.index("--extra")
        if at + 1 >= len(arguments):
            sys.exit("--extra needs a file")
        extra_files.append(arguments[at + 1])
        del arguments[at:at + 2]
    given = arguments[:2] + [None, None]
    root = pathlib.Path(__file__).resolve().parent
    text = characters(root, extra_files)
    regular = build_subset(find_source("Regular", given[0]), "Regular", text)
    bold = build_subset(find_source("Bold", given[1]), "Bold", text)
    output = root / "launcher_cjk_font.inc"
    # The header stays plain ASCII, so compilers read it the same in any
    # source encoding: the characters are listed by code point, 12 a line.
    points = [f"U+{ord(character):04X}" for character in text]
    code_points = "\n".join(
        "//   " + " ".join(points[start:start + 12]) for start in range(0, len(points), 12)
    )
    output.write_text(
        "// Generated by tools/make_launcher_cjk_font.py; do not edit by hand.\n"
        "#pragma once\n"
        f"// {FAMILY} (SIL Open Font License 1.1, see tools/fonts/NotoSansCJK-OFL.txt),\n"
        f"// cut down to the launcher's {len(points)} Japanese characters:\n"
        f"{code_points}\n"
        + as_array("LAUNCHER_CJK_REGULAR", regular)
        + "\n"
        + as_array("LAUNCHER_CJK_BOLD", bold)
        + "\n",
        encoding="utf-8",
    )
    print(f"{output}: {len(points)} characters, regular {len(regular)} bytes, "
          f"bold {len(bold)} bytes")


if __name__ == "__main__":
    main()
