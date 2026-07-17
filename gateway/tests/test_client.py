from __future__ import annotations

import socket
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

from agri_gateway.client import GatewayClient
from agri_gateway.config import GatewayConfig
from agri_gateway.database import initialize_database
from agri_gateway.plant_repository import PlantRepository
from agri_gateway.providers import MockProvider


class GatewayClientTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        database_path = Path(self.temporary_directory.name) / "greenmind.sqlite3"
        initialize_database(database_path)
        self.gateway_socket, self.device_socket = socket.socketpair()
        self.device_socket.settimeout(1.0)
        self.client = GatewayClient(
            GatewayConfig(device_ip="127.0.0.1", provider="mock"),
            MockProvider(),
            PlantRepository(database_path),
        )

    def tearDown(self) -> None:
        self.gateway_socket.close()
        self.device_socket.close()
        self.temporary_directory.cleanup()

    def _receive_lines(self, count: int) -> list[str]:
        buffer = bytearray()
        while buffer.count(b"\n") < count:
            buffer.extend(self.device_socket.recv(512))
        return [line.rstrip(b"\r").decode("ascii") for line in buffer.split(b"\n") if line]

    def test_hello_ack(self) -> None:
        self.client._handle_line(self.gateway_socket, b"V1|HELLO|STM32|READY")
        self.assertEqual(
            self._receive_lines(1),
            ["V1|HELLO_ACK|GATEWAY|READY"],
        )

    def test_ai_stages_and_result(self) -> None:
        self.client._handle_line(
            self.gateway_socket,
            b"V1|AI_REQ|9|33|39|82|STRONG",
        )
        lines = self._receive_lines(5)
        self.assertEqual(lines[0], "V1|ACK|9|AI_REQ")
        self.assertEqual(lines[1], "V1|AI_STAGE|9|PREPARING")
        self.assertEqual(lines[2], "V1|AI_STAGE|9|THINKING")
        self.assertEqual(lines[3], "V1|AI_STAGE|9|VALIDATING")
        self.assertTrue(lines[4].startswith("V1|AI_RESULT|9|WARN|HOT_AND_BRIGHT|"))

    def test_plant_ai_request_uses_database_profile(self) -> None:
        self.client._handle_line(
            self.gateway_socket,
            b"V1|PLANT_AI_REQ|10|GM001|pothos|30|45|80|STRONG",
        )
        lines = self._receive_lines(5)
        self.assertEqual(lines[0], "V1|ACK|10|PLANT_AI_REQ")
        self.assertEqual(
            lines[4],
            "V1|AI_RESULT|10|WARN|LIGHT_HIGH|NO_NEED|MOVE_TO_SHADE",
        )

    def test_unknown_plant_is_rejected(self) -> None:
        self.client._handle_line(
            self.gateway_socket,
            b"V1|PLANT_AI_REQ|11|GM001|missing|25|50|50|NORMAL",
        )
        self.assertEqual(
            self._receive_lines(1),
            ["V1|ERROR|11|PLANT_NOT_FOUND"],
        )

    def test_plant_list_comes_from_database(self) -> None:
        self.client._handle_line(
            self.gateway_socket,
            b"V1|PLANT_LIST_REQ|GM001",
        )
        lines = self._receive_lines(8)
        self.assertEqual(lines[0], "V1|PLANT_LIST_BEGIN|6|pothos|POTHOS")
        self.assertTrue(lines[1].startswith("V1|PLANT_ITEM|0|"))
        self.assertIn("V1|PLANT_ITEM|", lines[6])
        self.assertEqual(lines[7], "V1|PLANT_LIST_END|6")

    def test_plant_detail_contains_care_ranges(self) -> None:
        self.client._handle_line(
            self.gateway_socket,
            b"V1|PLANT_DETAIL_REQ|cactus",
        )
        self.assertEqual(
            self._receive_lines(1),
            ["V1|PLANT_DETAIL|cactus|CACTUS|18|35|20|55|65|100|ONLINE"],
        )

    def test_plant_select_updates_device_and_confirms(self) -> None:
        self.client._handle_line(
            self.gateway_socket,
            b"V1|PLANT_SELECT|GM001|cactus",
        )
        self.assertEqual(
            self._receive_lines(1),
            ["V1|PLANT_SELECTED|cactus|CACTUS"],
        )
        self.assertEqual(
            self.client.repository.get_current_species_id("GM001"),
            "cactus",
        )
        self.client._handle_line(
            self.gateway_socket,
            b"V1|PLANT_LIST_REQ|GM001",
        )
        self.assertEqual(
            self._receive_lines(8)[0],
            "V1|PLANT_LIST_BEGIN|6|cactus|CACTUS",
        )

    def test_plant_select_rejects_unknown_device(self) -> None:
        self.client._handle_line(
            self.gateway_socket,
            b"V1|PLANT_SELECT|UNKNOWN|cactus",
        )
        self.assertEqual(
            self._receive_lines(1),
            ["V1|PLANT_ERROR|DEVICE_NOT_FOUND"],
        )

    def test_asset_request_streams_database_rgb565_with_crc(self) -> None:
        with patch("agri_gateway.client.ASSET_FRAME_PACE_SECONDS", 0):
            self.client._handle_line(
                self.gateway_socket,
                b"V1|ASSET_REQ|7|cactus|NORMAL",
            )
        lines = self._receive_lines(118)
        self.assertEqual(
            lines[0],
            "V1|ASSET_BEGIN|7|cactus|NORMAL|48|48|4608|116|388CB035",
        )
        chunks = lines[1:-1]
        payload = bytearray()
        for expected_sequence, line in enumerate(chunks):
            fields = line.split("|")
            self.assertEqual(fields[:3], ["V1", "ASSET_CHUNK", "7"])
            self.assertEqual(int(fields[3]), expected_sequence)
            payload.extend(bytes.fromhex(fields[4]))
        self.assertEqual(len(payload), 4608)
        self.assertEqual(f"{zlib.crc32(payload) & 0xFFFFFFFF:08X}", "388CB035")
        self.assertEqual(lines[-1], "V1|ASSET_END|7|116|388CB035")

    def test_unknown_asset_is_rejected(self) -> None:
        self.client._handle_line(
            self.gateway_socket,
            b"V1|ASSET_REQ|8|missing|NORMAL",
        )
        self.assertEqual(
            self._receive_lines(1),
            ["V1|ASSET_ERROR|8|ASSET_NOT_FOUND"],
        )


if __name__ == "__main__":
    unittest.main()
