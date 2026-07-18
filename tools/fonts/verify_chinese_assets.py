#!/usr/bin/env python3
"""Verify GreenMind font, Unicode mapping, and all localized UI strings."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


FONT_BYTES = 147204
GLYPH_BYTES = 18
MAP_ENTRIES = 7445


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("font", type=Path)
    parser.add_argument("mapping", type=Path)
    parser.add_argument("strings", type=Path)
    args = parser.parse_args()

    font = args.font.read_bytes()
    mapping_bytes = args.mapping.read_bytes()
    strings = json.loads(args.strings.read_text(encoding="utf-8"))

    if len(font) != FONT_BYTES:
        raise RuntimeError(f"font size mismatch: {len(font)}")
    if len(mapping_bytes) != MAP_ENTRIES * 4:
        raise RuntimeError(f"mapping size mismatch: {len(mapping_bytes)}")

    entries = [
        struct.unpack_from("<HH", mapping_bytes, offset)
        for offset in range(0, len(mapping_bytes), 4)
    ]
    if entries != sorted(entries):
        raise RuntimeError("Unicode mapping is not sorted")
    mapping = dict(entries)
    if len(mapping) != MAP_ENTRIES:
        raise RuntimeError("Unicode mapping contains duplicates")

    checked_characters: set[str] = set()
    for name, text in strings.items():
        for character in text:
            if ord(character) < 0x80:
                continue
            checked_characters.add(character)
            gb2312 = mapping.get(ord(character))
            if gb2312 is None:
                raise RuntimeError(f"{name}: unmapped character {character!r}")
            lead = gb2312 >> 8
            trail = gb2312 & 0xFF
            slot = (lead - 0xA1) * 94 + (trail - 0xA1)
            glyph = font[slot * GLYPH_BYTES : (slot + 1) * GLYPH_BYTES]
            if not any(glyph) and character != "\u3000":
                raise RuntimeError(f"{name}: blank glyph for {character!r}")

    print(
        "Chinese assets OK: "
        f"font_bytes={len(font)} mappings={len(entries)} "
        f"ui_strings={len(strings)} unique_non_ascii={len(checked_characters)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
