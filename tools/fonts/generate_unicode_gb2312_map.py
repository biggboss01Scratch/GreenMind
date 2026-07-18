#!/usr/bin/env python3
"""Generate the Unicode-to-GB2312 lookup table used by GreenMind firmware."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


EXPECTED_ENTRY_COUNT = 7445


def build_entries() -> list[tuple[int, int]]:
    entries: list[tuple[int, int]] = []
    for lead in range(0xA1, 0xF8):
        for trail in range(0xA1, 0xFF):
            raw = bytes((lead, trail))
            try:
                text = raw.decode("gb2312")
            except UnicodeDecodeError:
                continue
            if len(text) != 1:
                continue
            entries.append((ord(text), (lead << 8) | trail))

    entries.sort()
    if len(entries) != EXPECTED_ENTRY_COUNT:
        raise RuntimeError(
            f"expected {EXPECTED_ENTRY_COUNT} mappings, got {len(entries)}"
        )
    if len({codepoint for codepoint, _ in entries}) != len(entries):
        raise RuntimeError("duplicate Unicode codepoint in GB2312 mapping")
    return entries


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--metadata", type=Path)
    args = parser.parse_args()

    entries = build_entries()
    payload = b"".join(
        struct.pack("<HH", codepoint, gb2312) for codepoint, gb2312 in entries
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)

    digest = hashlib.sha256(payload).hexdigest()
    metadata = {
        "format": "sorted uint16_le unicode, uint16_le gb2312",
        "entry_count": len(entries),
        "bytes_per_entry": 4,
        "byte_size": len(payload),
        "first_unicode": f"U+{entries[0][0]:04X}",
        "last_unicode": f"U+{entries[-1][0]:04X}",
        "sha256": digest,
    }
    if args.metadata:
        args.metadata.parent.mkdir(parents=True, exist_ok=True)
        args.metadata.write_text(
            json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )

    print(
        f"generated {args.output}: entries={len(entries)} "
        f"bytes={len(payload)} sha256={digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
