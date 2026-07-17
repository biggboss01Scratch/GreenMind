from __future__ import annotations

import argparse
import struct
from pathlib import Path


GATEWAY_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = GATEWAY_ROOT.parent
DEFAULT_ASSET_DIR = GATEWAY_ROOT / "assets" / "plants"
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "firmware" / "APP" / "agriculture"
STATES = ("normal", "attention", "danger")
PIXEL_COUNT = 48 * 48


def _format_array(symbol: str, payload: bytes) -> str:
    if len(payload) != PIXEL_COUNT * 2:
        raise ValueError(f"{symbol}: expected {PIXEL_COUNT * 2} bytes, got {len(payload)}")
    values = struct.unpack(f">{PIXEL_COUNT}H", payload)
    lines = []
    for offset in range(0, len(values), 12):
        row = ", ".join(f"0x{value:04X}" for value in values[offset : offset + 12])
        lines.append(f"\t{row},")
    return (
        f"const u16 {symbol}[PLANT_ASSET_PIXEL_COUNT]=\n"
        "{\n"
        + "\n".join(lines)
        + "\n};\n"
    )


def export_assets(asset_dir: Path, output_dir: Path) -> tuple[Path, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    header_path = output_dir / "plant_assets_builtin.h"
    source_path = output_dir / "plant_assets_builtin.c"

    header = """#ifndef _PLANT_ASSETS_BUILTIN_H
#define _PLANT_ASSETS_BUILTIN_H

#include "system.h"

#define PLANT_ASSET_WIDTH       48
#define PLANT_ASSET_HEIGHT      48
#define PLANT_ASSET_PIXEL_COUNT (PLANT_ASSET_WIDTH*PLANT_ASSET_HEIGHT)
#define PLANT_ASSET_BYTE_SIZE   (PLANT_ASSET_PIXEL_COUNT*2)

extern const u16 g_pothos_normal[PLANT_ASSET_PIXEL_COUNT];
extern const u16 g_pothos_attention[PLANT_ASSET_PIXEL_COUNT];
extern const u16 g_pothos_danger[PLANT_ASSET_PIXEL_COUNT];

#endif
"""
    source_parts = [
        '#include "plant_assets_builtin.h"\n\n',
        "/* Generated from gateway/assets/plants/pothos_*.rgb565. */\n\n",
    ]
    for state in STATES:
        payload = (asset_dir / f"pothos_{state}.rgb565").read_bytes()
        source_parts.append(_format_array(f"g_pothos_{state}", payload))
        source_parts.append("\n")

    header_path.write_text(header, encoding="ascii", newline="\n")
    source_path.write_text("".join(source_parts), encoding="ascii", newline="\n")
    return header_path, source_path


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Export built-in Pothos RGB565 sprites as ARMCC C arrays."
    )
    parser.add_argument("--asset-dir", type=Path, default=DEFAULT_ASSET_DIR)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    args = parser.parse_args()
    header_path, source_path = export_assets(
        args.asset_dir.resolve(), args.output_dir.resolve()
    )
    print(f"generated {header_path}")
    print(f"generated {source_path}")


if __name__ == "__main__":
    main()
