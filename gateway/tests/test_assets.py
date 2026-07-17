from __future__ import annotations

import hashlib
import json
import re
import struct
import unittest
import zlib
from pathlib import Path


GATEWAY_ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = GATEWAY_ROOT / "assets" / "plants"
FIRMWARE_ASSET_SOURCE = (
    GATEWAY_ROOT.parent
    / "firmware"
    / "APP"
    / "agriculture"
    / "plant_assets_builtin.c"
)


class PlantAssetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads(
            (ASSET_DIR / "manifest.json").read_text(encoding="utf-8")
        )

    def test_manifest_has_all_species_and_states(self) -> None:
        entries = self.manifest["entries"]
        self.assertEqual(len(entries), 18)
        keys = {(entry["species_id"], entry["state"]) for entry in entries}
        expected = {
            (species_id, state)
            for species_id in self.manifest["species"]
            for state in self.manifest["states"]
        }
        self.assertEqual(keys, expected)

    def test_rgb565_files_match_manifest(self) -> None:
        for entry in self.manifest["entries"]:
            path = GATEWAY_ROOT / entry["rgb565_path"]
            payload = path.read_bytes()
            self.assertEqual(len(payload), 4608)
            self.assertEqual(entry["byte_size"], len(payload))
            self.assertEqual(
                entry["crc32"],
                f"{zlib.crc32(payload) & 0xFFFFFFFF:08X}",
            )
            self.assertEqual(entry["sha256"], hashlib.sha256(payload).hexdigest())

    def test_png_files_are_48_by_48_rgb(self) -> None:
        for entry in self.manifest["entries"]:
            path = GATEWAY_ROOT / entry["png_path"]
            payload = path.read_bytes()
            self.assertEqual(payload[:8], b"\x89PNG\r\n\x1a\n")
            width, height, bit_depth, color_type = struct.unpack(
                ">IIBB", payload[16:26]
            )
            self.assertEqual((width, height), (48, 48))
            self.assertEqual(bit_depth, 8)
            self.assertEqual(color_type, 2)

    def test_each_state_has_distinct_device_payload(self) -> None:
        by_species: dict[str, set[str]] = {}
        for entry in self.manifest["entries"]:
            by_species.setdefault(entry["species_id"], set()).add(entry["sha256"])
        for hashes in by_species.values():
            self.assertEqual(len(hashes), 3)

    def test_builtin_pothos_arrays_match_gateway_assets(self) -> None:
        source = FIRMWARE_ASSET_SOURCE.read_text(encoding="ascii")
        for state in ("normal", "attention", "danger"):
            match = re.search(
                rf"const u16 g_pothos_{state}\[[^\]]+\]=\s*\{{(.*?)\}};",
                source,
                re.DOTALL,
            )
            self.assertIsNotNone(match)
            values = [
                int(value, 16)
                for value in re.findall(r"0x([0-9A-F]{4})", match.group(1))
            ]
            self.assertEqual(len(values), 48 * 48)
            firmware_payload = struct.pack(f">{len(values)}H", *values)
            gateway_payload = (ASSET_DIR / f"pothos_{state}.rgb565").read_bytes()
            self.assertEqual(firmware_payload, gateway_payload)


if __name__ == "__main__":
    unittest.main()
