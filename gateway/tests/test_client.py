from __future__ import annotations

import socket
import unittest

from agri_gateway.client import GatewayClient
from agri_gateway.config import GatewayConfig
from agri_gateway.providers import MockProvider


class GatewayClientTests(unittest.TestCase):
    def setUp(self) -> None:
        self.gateway_socket, self.device_socket = socket.socketpair()
        self.device_socket.settimeout(1.0)
        self.client = GatewayClient(GatewayConfig(device_ip="127.0.0.1"), MockProvider())

    def tearDown(self) -> None:
        self.gateway_socket.close()
        self.device_socket.close()

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


if __name__ == "__main__":
    unittest.main()
