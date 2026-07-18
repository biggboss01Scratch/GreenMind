from __future__ import annotations

import socket
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import MagicMock, patch

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

    def _receive_until_ai_result(self) -> list[str]:
        buffer = bytearray()
        while b"\nV1|AI_RESULT|" not in b"\n" + buffer:
            buffer.extend(self.device_socket.recv(1024))
        return [
            line.rstrip(b"\r").decode("ascii")
            for line in buffer.split(b"\n")
            if line
        ]

    def _assert_dialog_transfer(self, lines: list[str], request_id: int) -> str:
        begin_index = next(
            index
            for index, line in enumerate(lines)
            if line.startswith(f"V1|AI_TEXT_BEGIN|{request_id}|")
        )
        begin = lines[begin_index].split("|")
        byte_size = int(begin[3])
        chunk_count = int(begin[4])
        expected_crc = begin[5]
        chunks = lines[begin_index + 1 : begin_index + 1 + chunk_count]
        payload = bytearray()
        for expected_sequence, line in enumerate(chunks):
            fields = line.split("|")
            self.assertEqual(fields[:3], ["V1", "AI_TEXT_CHUNK", str(request_id)])
            self.assertEqual(int(fields[3]), expected_sequence)
            payload.extend(bytes.fromhex(fields[4]))
        self.assertEqual(len(payload), byte_size)
        self.assertEqual(f"{zlib.crc32(payload) & 0xFFFFFFFF:08X}", expected_crc)
        self.assertEqual(
            lines[begin_index + 1 + chunk_count],
            f"V1|AI_TEXT_END|{request_id}|{chunk_count}|{expected_crc}",
        )
        return payload.decode("utf-8")

    def test_hello_ack(self) -> None:
        self.client._handle_line(self.gateway_socket, b"V1|HELLO|STM32|READY")
        self.assertEqual(
            self._receive_lines(1),
            ["V1|HELLO_ACK|GATEWAY|READY"],
        )

    def test_pong_is_accepted_without_reply(self) -> None:
        self.device_socket.settimeout(0.05)
        with patch("agri_gateway.client.log") as log_mock:
            self.client._handle_line(self.gateway_socket, b"V1|PONG")
        log_mock.assert_not_called()
        with self.assertRaises(socket.timeout):
            self.device_socket.recv(64)

    def test_idle_connection_sends_heartbeat(self) -> None:
        sock = MagicMock()
        sock.__enter__.return_value = sock
        reader = MagicMock()
        reader.receive.side_effect = [[], ConnectionError("test complete")]
        with (
            patch("agri_gateway.client.socket.create_connection", return_value=sock),
            patch("agri_gateway.client.SocketLineReader", return_value=reader),
            patch("agri_gateway.client.time.monotonic", side_effect=[0.0, 3.0]),
        ):
            with self.assertRaisesRegex(ConnectionError, "test complete"):
                self.client._run_connection()
        sock.sendall.assert_called_once_with(b"V1|PING\r\n")

    def test_silent_connection_reaches_heartbeat_timeout(self) -> None:
        sock = MagicMock()
        sock.__enter__.return_value = sock
        reader = MagicMock()
        reader.receive.return_value = []
        with (
            patch("agri_gateway.client.socket.create_connection", return_value=sock),
            patch("agri_gateway.client.SocketLineReader", return_value=reader),
            patch("agri_gateway.client.time.monotonic", side_effect=[0.0, 12.0]),
        ):
            with self.assertRaisesRegex(ConnectionError, "heartbeat timeout"):
                self.client._run_connection()
        sock.sendall.assert_not_called()

    def test_ai_stages_and_result(self) -> None:
        with patch("agri_gateway.client.AI_TEXT_FRAME_PACE_SECONDS", 0):
            self.client._handle_line(
                self.gateway_socket,
                b"V1|AI_REQ|9|33|39|82|STRONG",
            )
        lines = self._receive_until_ai_result()
        self.assertEqual(lines[0], "V1|ACK|9|AI_REQ")
        self.assertEqual(lines[1], "V1|AI_STAGE|9|PREPARING")
        self.assertEqual(lines[2], "V1|AI_STAGE|9|THINKING")
        self.assertEqual(lines[3], "V1|AI_STAGE|9|VALIDATING")
        dialog = self._assert_dialog_transfer(lines, 9)
        self.assertIn("绿萝", dialog)
        self.assertTrue(lines[-1].startswith("V1|AI_RESULT|9|WARN|HOT_AND_BRIGHT|"))

    def test_plant_ai_request_uses_database_profile(self) -> None:
        with patch("agri_gateway.client.AI_TEXT_FRAME_PACE_SECONDS", 0):
            self.client._handle_line(
                self.gateway_socket,
                b"V1|PLANT_AI_REQ|10|GM001|pothos|30|45|80|STRONG",
            )
        lines = self._receive_until_ai_result()
        self.assertEqual(lines[0], "V1|ACK|10|PLANT_AI_REQ")
        self.assertEqual(
            lines[-1],
            "V1|AI_RESULT|10|WARN|LIGHT_HIGH|NO_NEED|MOVE_TO_SHADE",
        )
        self.assertIn("气生根", self._assert_dialog_transfer(lines, 10))

    def test_ai_dialog_can_be_disabled_for_legacy_firmware(self) -> None:
        self.client.config = GatewayConfig(
            device_ip="127.0.0.1",
            provider="mock",
            ai_dialog_enabled=False,
        )
        self.client._handle_line(
            self.gateway_socket,
            b"V1|PLANT_AI_REQ|12|GM001|pothos|26|65|50|NORMAL",
        )
        lines = self._receive_lines(5)
        self.assertFalse(any("AI_TEXT_" in line for line in lines))
        self.assertTrue(lines[-1].startswith("V1|AI_RESULT|12|"))

    def test_oversize_dialog_is_utf8_truncated_not_dropped(self) -> None:
        with patch("agri_gateway.client.AI_TEXT_FRAME_PACE_SECONDS", 0):
            self.client._send_ai_text(
                self.gateway_socket,
                13,
                "绿萝" * 400,
            )
        lines = self._receive_lines(26)
        dialog = self._assert_dialog_transfer(lines, 13)
        self.assertEqual(len(dialog.encode("utf-8")), 768)
        self.assertTrue(dialog.startswith("绿萝绿萝"))

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
