from __future__ import annotations

import socket
import time
from datetime import datetime

from .config import GatewayConfig
from .protocol import ProtocolError, decode_frame, encode_frame, parse_ai_request
from .providers import ModelProvider, ProviderError


def log(category: str, message: str) -> None:
    timestamp = datetime.now().strftime("%H:%M:%S")
    print(f"{timestamp} [{category}] {message}", flush=True)


class SocketLineReader:
    def __init__(self, sock: socket.socket) -> None:
        self.sock = sock
        self.buffer = bytearray()

    def receive(self) -> list[bytes]:
        try:
            chunk = self.sock.recv(256)
        except socket.timeout:
            return []
        if not chunk:
            raise ConnectionError("device closed the connection")
        self.buffer.extend(chunk)
        if len(self.buffer) > 1024 and b"\n" not in self.buffer:
            self.buffer.clear()
            raise ProtocolError("FRAME_TOO_LONG")

        lines: list[bytes] = []
        while b"\n" in self.buffer:
            line, _, rest = self.buffer.partition(b"\n")
            self.buffer = bytearray(rest)
            lines.append(bytes(line).rstrip(b"\r"))
        return lines


class GatewayClient:
    def __init__(self, config: GatewayConfig, provider: ModelProvider) -> None:
        self.config = config
        self.provider = provider

    def run_forever(self) -> None:
        while True:
            try:
                self._run_connection()
            except KeyboardInterrupt:
                raise
            except (OSError, ConnectionError, ProtocolError) as exc:
                log("CONNECTION", f"offline: {exc}")
            log("CONNECTION", f"retrying in {self.config.reconnect_seconds:.1f}s")
            time.sleep(self.config.reconnect_seconds)

    def _run_connection(self) -> None:
        address = (self.config.device_ip, self.config.port)
        log("CONNECTION", f"connecting to {address[0]}:{address[1]}")
        with socket.create_connection(address, timeout=5.0) as sock:
            sock.settimeout(self.config.socket_timeout_seconds)
            reader = SocketLineReader(sock)
            log("CONNECTION", "TCP connected; waiting for STM32 HELLO")
            while True:
                for line in reader.receive():
                    if not line:
                        continue
                    self._handle_line(sock, line)

    @staticmethod
    def _send(sock: socket.socket, *fields: object) -> None:
        payload = encode_frame(*fields)
        sock.sendall(payload)
        safe_text = payload.decode("ascii").rstrip()
        log("TX", safe_text)

    def _handle_line(self, sock: socket.socket, line: bytes) -> None:
        try:
            fields = decode_frame(line)
        except ProtocolError as exc:
            log("PROTOCOL", exc.code)
            self._send(sock, "V1", "ERROR", exc.request_id, exc.code)
            return

        log("RX", "|".join(fields))
        message_type = fields[1]
        if fields == ["V1", "HELLO", "STM32", "READY"]:
            self._send(sock, "V1", "HELLO_ACK", "GATEWAY", "READY")
            log("GATEWAY", "READY")
        elif message_type == "PING" and len(fields) == 2:
            self._send(sock, "V1", "PONG")
        elif message_type == "AI_REQ":
            self._handle_ai_request(sock, fields)
        else:
            self._send(sock, "V1", "ERROR", 0, "BAD_FRAME")

    def _handle_ai_request(self, sock: socket.socket, fields: list[str]) -> None:
        try:
            request = parse_ai_request(fields)
        except ProtocolError as exc:
            log("PROTOCOL", f"AI request rejected: {exc.code}")
            self._send(sock, "V1", "ERROR", exc.request_id, exc.code)
            return

        request_id = request.request_id
        self._send(sock, "V1", "ACK", request_id, "AI_REQ")
        self._send(sock, "V1", "AI_STAGE", request_id, "PREPARING")
        self._send(sock, "V1", "AI_STAGE", request_id, "THINKING")
        log(
            "MODEL",
            f"THINKING... request={request_id} "
            f"T={request.temperature} H={request.humidity} "
            f"L={request.light}/{request.light_level}",
        )
        started = time.monotonic()
        try:
            result = self.provider.analyze(request)
        except ProviderError as exc:
            elapsed = time.monotonic() - started
            log("MODEL", f"failed request={request_id} code={exc.code} elapsed={elapsed:.2f}s")
            self._send(sock, "V1", "ERROR", request_id, exc.code)
            return

        elapsed = time.monotonic() - started
        self._send(sock, "V1", "AI_STAGE", request_id, "VALIDATING")
        log("MODEL", f"VALIDATED request={request_id} elapsed={elapsed:.2f}s")
        log("ADVICE", result.suggestion_en)
        self._send(
            sock,
            "V1",
            "AI_RESULT",
            request_id,
            result.status,
            result.issue,
            result.watering,
            result.advice,
        )
