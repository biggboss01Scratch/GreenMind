from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


CELL_WIDTH = 12
CELL_HEIGHT = 12
FONT_ASCENT = 10
LEAD_START = 0xA1
LEAD_END = 0xF7
TRAIL_START = 0xA1
TRAIL_END = 0xFE
TRAIL_COUNT = TRAIL_END - TRAIL_START + 1
GLYPH_BYTES = CELL_WIDTH * CELL_HEIGHT // 8
SLOT_COUNT = (LEAD_END - LEAD_START + 1) * TRAIL_COUNT


def gb2312_slots() -> tuple[dict[int, int], set[int]]:
    codepoint_to_slot: dict[int, int] = {}
    assigned_slots: set[int] = set()
    for lead in range(LEAD_START, LEAD_END + 1):
        for trail in range(TRAIL_START, TRAIL_END + 1):
            slot = (lead - LEAD_START) * TRAIL_COUNT + trail - TRAIL_START
            try:
                character = bytes((lead, trail)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if len(character) != 1:
                continue
            codepoint_to_slot[ord(character)] = slot
            assigned_slots.add(slot)
    return codepoint_to_slot, assigned_slots


def pack_canvas(canvas: list[int]) -> bytes:
    output = bytearray(GLYPH_BYTES)
    for bit_index, value in enumerate(canvas):
        if value:
            output[bit_index // 8] |= 0x80 >> (bit_index % 8)
    return bytes(output)


def draw_bdf_glyph(
    bitmap_rows: list[str],
    width: int,
    height: int,
    x_offset: int,
    y_offset: int,
) -> bytes:
    canvas = [0] * (CELL_WIDTH * CELL_HEIGHT)
    top = FONT_ASCENT - (y_offset + height)
    for source_y, row_hex in enumerate(bitmap_rows[:height]):
        row_bits = len(row_hex) * 4
        row_value = int(row_hex, 16) if row_hex else 0
        target_y = top + source_y
        if not 0 <= target_y < CELL_HEIGHT:
            continue
        for source_x in range(width):
            target_x = x_offset + source_x
            if not 0 <= target_x < CELL_WIDTH:
                continue
            if row_value & (1 << (row_bits - source_x - 1)):
                canvas[target_y * CELL_WIDTH + target_x] = 1
    return pack_canvas(canvas)


def load_required_glyphs(
    bdf_path: Path, required_codepoints: set[int]
) -> dict[int, bytes]:
    glyphs: dict[int, bytes] = {}
    encoding: int | None = None
    bounding_box: tuple[int, int, int, int] | None = None
    bitmap_rows: list[str] = []
    reading_bitmap = False

    with bdf_path.open("r", encoding="ascii", errors="strict") as source:
        for raw_line in source:
            line = raw_line.strip()
            if line.startswith("ENCODING "):
                encoding = int(line.split()[1])
            elif line.startswith("BBX "):
                values = tuple(int(value) for value in line.split()[1:5])
                bounding_box = values
            elif line == "BITMAP":
                bitmap_rows = []
                reading_bitmap = True
            elif line == "ENDCHAR":
                if (
                    encoding in required_codepoints
                    and bounding_box is not None
                    and bitmap_rows
                ):
                    glyphs[encoding] = draw_bdf_glyph(
                        bitmap_rows, *bounding_box
                    )
                encoding = None
                bounding_box = None
                bitmap_rows = []
                reading_bitmap = False
            elif reading_bitmap:
                bitmap_rows.append(line)
    return glyphs


def load_fallback_glyphs(
    font_path: Path, required_codepoints: set[int]
) -> dict[int, bytes]:
    from PIL import Image, ImageDraw, ImageFont

    font = ImageFont.truetype(str(font_path), 12)
    glyphs: dict[int, bytes] = {}
    for codepoint in required_codepoints:
        character = chr(codepoint)
        bounding_box = font.getbbox(character)
        width = max(1, bounding_box[2] - bounding_box[0])
        height = max(1, bounding_box[3] - bounding_box[1])
        glyph_image = Image.new("L", (width, height), 0)
        draw = ImageDraw.Draw(glyph_image)
        draw.text(
            (-bounding_box[0], -bounding_box[1]),
            character,
            font=font,
            fill=255,
        )
        visible_box = glyph_image.getbbox()
        if visible_box is None:
            continue
        glyph_image = glyph_image.crop(visible_box)
        if glyph_image.width > CELL_WIDTH or glyph_image.height > CELL_HEIGHT:
            scale = min(
                CELL_WIDTH / glyph_image.width,
                CELL_HEIGHT / glyph_image.height,
            )
            glyph_image = glyph_image.resize(
                (
                    max(1, round(glyph_image.width * scale)),
                    max(1, round(glyph_image.height * scale)),
                ),
                Image.Resampling.LANCZOS,
            )
        canvas_image = Image.new("L", (CELL_WIDTH, CELL_HEIGHT), 0)
        canvas_image.paste(
            glyph_image,
            (
                (CELL_WIDTH - glyph_image.width) // 2,
                (CELL_HEIGHT - glyph_image.height) // 2,
            ),
        )
        canvas = [1 if value >= 96 else 0 for value in canvas_image.tobytes()]
        glyphs[codepoint] = pack_canvas(canvas)
    return glyphs


def write_font(
    bdf_path: Path,
    output_path: Path,
    metadata_path: Path,
    fallback_font_path: Path | None,
) -> None:
    codepoint_to_slot, assigned_slots = gb2312_slots()
    glyphs = load_required_glyphs(bdf_path, set(codepoint_to_slot))
    primary_glyph_count = len(glyphs)
    missing_codepoints = set(codepoint_to_slot) - set(glyphs)
    fallback_glyph_count = 0
    if missing_codepoints and fallback_font_path is not None:
        fallback_glyphs = load_fallback_glyphs(
            fallback_font_path, missing_codepoints
        )
        glyphs.update(fallback_glyphs)
        fallback_glyph_count = len(fallback_glyphs)
        missing_codepoints -= set(fallback_glyphs)
    missing_codepoints = sorted(missing_codepoints)
    if missing_codepoints:
        sample = ", ".join(f"U+{codepoint:04X}" for codepoint in missing_codepoints[:8])
        raise RuntimeError(
            f"source font is missing {len(missing_codepoints)} GB2312 glyphs: {sample}"
        )

    output = bytearray(SLOT_COUNT * GLYPH_BYTES)
    for codepoint, slot in codepoint_to_slot.items():
        start = slot * GLYPH_BYTES
        output[start : start + GLYPH_BYTES] = glyphs[codepoint]

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(output)
    digest = hashlib.sha256(output).hexdigest()
    metadata = {
        "format": "GreenMind GB2312 12x12 1bpp",
        "source_font": "Fusion Pixel Font 12px Mono zh_hans",
        "source_version": "2026.07.01",
        "fallback_font": (
            "Noto Sans CJK SC Regular"
            if fallback_font_path is not None
            else None
        ),
        "fallback_source_commit": (
            "165c01b46ea533872e002e0785ff17e44f6d97d8"
            if fallback_font_path is not None
            else None
        ),
        "license": "SIL Open Font License 1.1",
        "cell_width": CELL_WIDTH,
        "cell_height": CELL_HEIGHT,
        "font_ascent": FONT_ASCENT,
        "packing": "row-major continuous bits, MSB first",
        "lead_range": ["A1", "F7"],
        "trail_range": ["A1", "FE"],
        "slot_count": SLOT_COUNT,
        "assigned_glyph_count": len(assigned_slots),
        "han_glyph_count": 6763,
        "primary_glyph_count": primary_glyph_count,
        "fallback_glyph_count": fallback_glyph_count,
        "missing_glyph_count": len(missing_codepoints),
        "bytes_per_glyph": GLYPH_BYTES,
        "byte_size": len(output),
        "sha256": digest,
    }
    metadata_path.parent.mkdir(parents=True, exist_ok=True)
    metadata_path.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"font: {output_path}")
    print(f"metadata: {metadata_path}")
    print(
        f"slots={SLOT_COUNT} assigned={len(assigned_slots)} "
        f"primary={primary_glyph_count} fallback={fallback_glyph_count} "
        f"bytes={len(output)} sha256={digest}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert Fusion Pixel 12px BDF to a fixed-slot GB2312 font."
    )
    parser.add_argument("bdf", type=Path, help="zh_hans 12px monospaced BDF")
    parser.add_argument("output", type=Path, help="output binary path")
    parser.add_argument(
        "--metadata",
        type=Path,
        required=True,
        help="output JSON metadata path",
    )
    parser.add_argument(
        "--fallback-font",
        type=Path,
        help="optional OFL font used to fill symbols missing from the BDF",
    )
    args = parser.parse_args()
    write_font(
        args.bdf,
        args.output,
        args.metadata,
        args.fallback_font,
    )


if __name__ == "__main__":
    main()
