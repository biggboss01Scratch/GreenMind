from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


LOGICAL_SIZE = 16
SCALE = 3
IMAGE_SIZE = LOGICAL_SIZE * SCALE
ASSET_VERSION = 1
STATES = ("NORMAL", "ATTENTION", "DANGER")
SPECIES = ("pothos", "mint", "succulent", "cactus", "orchid", "tomato")

GATEWAY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_DIR = GATEWAY_ROOT / "assets" / "plants"


@dataclass(frozen=True)
class StatePalette:
    background: tuple[int, int, int]
    leaf: tuple[int, int, int]
    leaf_light: tuple[int, int, int]
    leaf_dark: tuple[int, int, int]
    pot: tuple[int, int, int]
    pot_dark: tuple[int, int, int]
    ink: tuple[int, int, int]
    accent: tuple[int, int, int]
    accent_light: tuple[int, int, int]


PALETTES = {
    "NORMAL": StatePalette(
        background=(239, 247, 239),
        leaf=(58, 166, 88),
        leaf_light=(121, 207, 112),
        leaf_dark=(34, 104, 58),
        pot=(226, 133, 68),
        pot_dark=(151, 72, 42),
        ink=(29, 48, 43),
        accent=(224, 73, 85),
        accent_light=(247, 164, 171),
    ),
    "ATTENTION": StatePalette(
        background=(255, 248, 221),
        leaf=(151, 157, 65),
        leaf_light=(225, 196, 66),
        leaf_dark=(100, 103, 42),
        pot=(211, 132, 69),
        pot_dark=(139, 76, 43),
        ink=(55, 49, 35),
        accent=(231, 146, 47),
        accent_light=(250, 206, 107),
    ),
    "DANGER": StatePalette(
        background=(251, 235, 231),
        leaf=(132, 105, 78),
        leaf_light=(181, 148, 108),
        leaf_dark=(84, 65, 49),
        pot=(153, 117, 99),
        pot_dark=(101, 72, 61),
        ink=(63, 45, 42),
        accent=(176, 75, 66),
        accent_light=(213, 139, 127),
    ),
}


class Canvas:
    def __init__(self) -> None:
        self.roles = [["background" for _ in range(LOGICAL_SIZE)] for _ in range(LOGICAL_SIZE)]

    def set(self, x: int, y: int, role: str) -> None:
        if 0 <= x < LOGICAL_SIZE and 0 <= y < LOGICAL_SIZE:
            self.roles[y][x] = role

    def rect(self, x1: int, y1: int, x2: int, y2: int, role: str) -> None:
        for y in range(y1, y2 + 1):
            for x in range(x1, x2 + 1):
                self.set(x, y, role)

    def points(self, points: tuple[tuple[int, int], ...], role: str) -> None:
        for x, y in points:
            self.set(x, y, role)

    def shift_plant_down(self) -> None:
        movable = {"leaf", "leaf_light", "leaf_dark", "accent", "accent_light"}
        for y in range(10, -1, -1):
            for x in range(LOGICAL_SIZE):
                if self.roles[y][x] in movable:
                    role = self.roles[y][x]
                    self.roles[y][x] = "background"
                    self.set(x, y + 1, role)


def draw_leaf_diamond(canvas: Canvas, cx: int, cy: int, flip: bool = False) -> None:
    canvas.points(((cx, cy - 1), (cx - 1, cy), (cx, cy), (cx + 1, cy), (cx, cy + 1)), "leaf")
    canvas.set(cx - 1 if flip else cx + 1, cy, "leaf_light")
    canvas.set(cx, cy + 1, "leaf_dark")


def draw_pothos(canvas: Canvas) -> None:
    canvas.rect(7, 4, 8, 10, "leaf_dark")
    canvas.points(((6, 7), (5, 8), (5, 9), (4, 10)), "leaf_dark")
    draw_leaf_diamond(canvas, 5, 5, True)
    draw_leaf_diamond(canvas, 10, 4)
    draw_leaf_diamond(canvas, 4, 8, True)
    draw_leaf_diamond(canvas, 10, 7)
    draw_leaf_diamond(canvas, 7, 3, True)
    canvas.points(((4, 4), (9, 3), (3, 8), (11, 7)), "leaf_light")


def draw_mint(canvas: Canvas) -> None:
    canvas.rect(7, 2, 8, 10, "leaf_dark")
    for y in (4, 6, 8):
        draw_leaf_diamond(canvas, 5, y, True)
        draw_leaf_diamond(canvas, 10, y - 1, False)
    canvas.points(((6, 3), (9, 2), (4, 6), (11, 5), (4, 8), (11, 7)), "leaf_light")
    canvas.set(7, 1, "leaf_light")
    canvas.set(8, 1, "leaf")


def draw_succulent(canvas: Canvas) -> None:
    canvas.points(
        (
            (7, 3), (8, 3),
            (6, 4), (7, 4), (8, 4), (9, 4),
            (5, 5), (6, 5), (7, 5), (8, 5), (9, 5), (10, 5),
            (4, 6), (5, 6), (6, 6), (7, 6), (8, 6), (9, 6), (10, 6), (11, 6),
            (5, 7), (6, 7), (7, 7), (8, 7), (9, 7), (10, 7),
            (6, 8), (7, 8), (8, 8), (9, 8),
        ),
        "leaf",
    )
    canvas.points(((7, 3), (6, 5), (9, 5), (4, 6), (11, 6), (7, 7), (8, 7)), "leaf_light")
    canvas.points(((7, 5), (8, 5), (6, 7), (9, 7), (7, 8), (8, 8)), "leaf_dark")
    canvas.rect(7, 9, 8, 10, "leaf_dark")


def draw_cactus(canvas: Canvas) -> None:
    canvas.rect(7, 2, 9, 10, "leaf")
    canvas.rect(6, 3, 10, 5, "leaf")
    canvas.rect(4, 5, 6, 8, "leaf")
    canvas.rect(3, 5, 4, 6, "leaf")
    canvas.rect(10, 4, 12, 7, "leaf")
    canvas.rect(12, 3, 13, 5, "leaf")
    canvas.points(((7, 2), (9, 2), (4, 5), (12, 3), (3, 5)), "leaf_light")
    canvas.points(((8, 4), (7, 7), (9, 9), (5, 7), (11, 5)), "leaf_dark")
    canvas.points(((8, 3), (9, 6), (7, 9), (4, 6), (12, 4)), "accent_light")


def draw_orchid(canvas: Canvas) -> None:
    canvas.rect(7, 3, 8, 10, "leaf_dark")
    canvas.points(((6, 3), (5, 3), (9, 2), (10, 2), (9, 5), (10, 5)), "leaf_dark")
    for cx, cy in ((4, 3), (11, 2), (11, 5)):
        canvas.points(
            ((cx, cy), (cx - 1, cy), (cx + 1, cy), (cx, cy - 1), (cx, cy + 1)),
            "accent",
        )
        canvas.set(cx, cy, "accent_light")
    canvas.points(((3, 8), (4, 8), (5, 9), (6, 9), (9, 9), (10, 8), (11, 8), (12, 7)), "leaf")
    canvas.points(((3, 9), (4, 9), (11, 9), (12, 8)), "leaf_light")


def draw_tomato(canvas: Canvas) -> None:
    canvas.rect(7, 2, 8, 10, "leaf_dark")
    canvas.points(((5, 4), (6, 4), (9, 4), (10, 4), (4, 6), (5, 6), (10, 6), (11, 6)), "leaf_dark")
    for cx, cy in ((5, 3), (10, 3), (4, 6), (11, 6), (6, 8), (9, 8)):
        draw_leaf_diamond(canvas, cx, cy, cx < 8)
    for cx, cy in ((5, 7), (10, 6), (9, 9)):
        canvas.points(((cx, cy), (cx + 1, cy), (cx, cy + 1), (cx + 1, cy + 1)), "accent")
        canvas.set(cx, cy, "accent_light")


DRAWERS: dict[str, Callable[[Canvas], None]] = {
    "pothos": draw_pothos,
    "mint": draw_mint,
    "succulent": draw_succulent,
    "cactus": draw_cactus,
    "orchid": draw_orchid,
    "tomato": draw_tomato,
}


def draw_pot_and_face(canvas: Canvas, state: str) -> None:
    canvas.rect(4, 10, 11, 11, "pot_dark")
    canvas.rect(5, 12, 10, 13, "pot")
    canvas.rect(6, 14, 9, 14, "pot")
    canvas.set(5, 12, "pot_dark")
    canvas.set(10, 12, "pot_dark")
    canvas.rect(6, 15, 9, 15, "pot_dark")
    canvas.set(6, 12, "ink")
    canvas.set(9, 12, "ink")
    if state == "NORMAL":
        canvas.set(6, 13, "ink")
        canvas.set(9, 13, "ink")
        canvas.rect(7, 14, 8, 14, "ink")
    elif state == "ATTENTION":
        canvas.rect(7, 13, 8, 13, "ink")
    else:
        canvas.rect(7, 13, 8, 13, "ink")
        canvas.set(6, 14, "ink")
        canvas.set(9, 14, "ink")
        canvas.set(11, 13, "accent_light")


def quantize_rgb565(color: tuple[int, int, int]) -> tuple[int, int, int]:
    red, green, blue = color
    r5 = (red * 31 + 127) // 255
    g6 = (green * 63 + 127) // 255
    b5 = (blue * 31 + 127) // 255
    return (
        (r5 * 255 + 15) // 31,
        (g6 * 255 + 31) // 63,
        (b5 * 255 + 15) // 31,
    )


def make_sprite(species_id: str, state: str) -> list[list[tuple[int, int, int]]]:
    canvas = Canvas()
    DRAWERS[species_id](canvas)
    if state == "DANGER" and species_id != "cactus":
        canvas.shift_plant_down()
    draw_pot_and_face(canvas, state)
    palette = PALETTES[state]

    logical = [
        [quantize_rgb565(getattr(palette, canvas.roles[y][x])) for x in range(LOGICAL_SIZE)]
        for y in range(LOGICAL_SIZE)
    ]
    return [
        [logical[y // SCALE][x // SCALE] for x in range(IMAGE_SIZE)]
        for y in range(IMAGE_SIZE)
    ]


def rgb565_bytes(pixels: list[list[tuple[int, int, int]]]) -> bytes:
    output = bytearray()
    for row in pixels:
        for red, green, blue in row:
            value = ((red * 31 // 255) << 11) | ((green * 63 // 255) << 5) | (blue * 31 // 255)
            output.extend(struct.pack(">H", value))
    return bytes(output)


def png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + chunk_type
        + payload
        + struct.pack(">I", zlib.crc32(chunk_type + payload) & 0xFFFFFFFF)
    )


def png_bytes(pixels: list[list[tuple[int, int, int]]]) -> bytes:
    height = len(pixels)
    width = len(pixels[0])
    scanlines = bytearray()
    for row in pixels:
        scanlines.append(0)
        for red, green, blue in row:
            scanlines.extend((red, green, blue))
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(bytes(scanlines), level=9))
        + png_chunk(b"IEND", b"")
    )


def compose_preview(
    sprites: dict[tuple[str, str], list[list[tuple[int, int, int]]]]
) -> list[list[tuple[int, int, int]]]:
    width = len(SPECIES) * IMAGE_SIZE
    height = len(STATES) * IMAGE_SIZE
    background = quantize_rgb565(PALETTES["NORMAL"].background)
    preview = [[background for _ in range(width)] for _ in range(height)]
    for column, species_id in enumerate(SPECIES):
        for row, state in enumerate(STATES):
            sprite = sprites[(species_id, state)]
            x_offset = column * IMAGE_SIZE
            y_offset = row * IMAGE_SIZE
            for y in range(IMAGE_SIZE):
                preview[y_offset + y][x_offset : x_offset + IMAGE_SIZE] = sprite[y]
    return preview


def generate_assets(output_dir: Path) -> dict[str, object]:
    output_dir.mkdir(parents=True, exist_ok=True)
    sprites: dict[tuple[str, str], list[list[tuple[int, int, int]]]] = {}
    entries: list[dict[str, object]] = []

    for species_id in SPECIES:
        for state in STATES:
            pixels = make_sprite(species_id, state)
            sprites[(species_id, state)] = pixels
            basename = f"{species_id}_{state.lower()}"
            png_file = output_dir / f"{basename}.png"
            raw_file = output_dir / f"{basename}.rgb565"
            png_payload = png_bytes(pixels)
            raw_payload = rgb565_bytes(pixels)
            png_file.write_bytes(png_payload)
            raw_file.write_bytes(raw_payload)
            entries.append(
                {
                    "species_id": species_id,
                    "state": state,
                    "width": IMAGE_SIZE,
                    "height": IMAGE_SIZE,
                    "image_format": "RGB565_BE",
                    "byte_size": len(raw_payload),
                    "png_path": f"assets/plants/{png_file.name}",
                    "rgb565_path": f"assets/plants/{raw_file.name}",
                    "crc32": f"{zlib.crc32(raw_payload) & 0xFFFFFFFF:08X}",
                    "sha256": hashlib.sha256(raw_payload).hexdigest(),
                    "image_version": ASSET_VERSION,
                }
            )

    preview_file = output_dir / "catalog_preview.png"
    preview_file.write_bytes(png_bytes(compose_preview(sprites)))
    manifest: dict[str, object] = {
        "asset_version": ASSET_VERSION,
        "logical_size": LOGICAL_SIZE,
        "scale": SCALE,
        "width": IMAGE_SIZE,
        "height": IMAGE_SIZE,
        "states": list(STATES),
        "species": list(SPECIES),
        "pixel_format": "RGB565_BE",
        "byte_order": "network byte order, most-significant byte first",
        "entries": entries,
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate deterministic 48x48 GreenMind plant sprites."
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="Asset output directory.",
    )
    args = parser.parse_args()
    manifest = generate_assets(args.output_dir.resolve())
    print(
        f"generated {len(manifest['entries'])} sprites in {args.output_dir.resolve()} "
        f"({manifest['width']}x{manifest['height']} {manifest['pixel_format']})"
    )


if __name__ == "__main__":
    main()
