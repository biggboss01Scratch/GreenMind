from __future__ import annotations

import socket
import sqlite3
import time
import zlib
from datetime import datetime
from pathlib import Path

from .config import GatewayConfig
from .database import ASSET_ROOT, GATEWAY_ROOT
from .plant_repository import PlantRepository, PlantRepositoryError
from .plant_rules import PlantRuleEvaluator
from .protocol import (
    ProtocolError,
    decode_frame,
    encode_frame,
    parse_ai_request,
    validate_device_id,
    validate_species_id,
)
from .providers import ModelProvider, ProviderError


PLANT_FRAME_PACE_SECONDS = 0.02
ASSET_FRAME_PACE_SECONDS = 0.02
ASSET_CHUNK_BYTES = 40
ASSET_STATES = {"NORMAL", "ATTENTION", "DANGER"}


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
    def __init__(
        self,
        config: GatewayConfig,
        provider: ModelProvider,
        repository: PlantRepository,
        evaluator: PlantRuleEvaluator | None = None,
    ) -> None:
        self.config = config
        self.provider = provider
        self.repository = repository
        self.evaluator = evaluator or PlantRuleEvaluator()

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
    def _send(
        sock: socket.socket, *fields: object, log_payload: bool = True
    ) -> None:
        payload = encode_frame(*fields)
        sock.sendall(payload)
        if log_payload:
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
        elif message_type in {"AI_REQ", "PLANT_AI_REQ"}:
            self._handle_ai_request(sock, fields)
        elif message_type == "PLANT_LIST_REQ":
            self._handle_plant_list_request(sock, fields)
        elif message_type == "PLANT_DETAIL_REQ":
            self._handle_plant_detail_request(sock, fields)
        elif message_type == "PLANT_SELECT":
            self._handle_plant_select(sock, fields)
        elif message_type == "ASSET_REQ":
            self._handle_asset_request(sock, fields)
        else:
            self._send(sock, "V1", "ERROR", 0, "BAD_FRAME")

    def _send_plant_error(self, sock: socket.socket, code: str) -> None:
        self._send(sock, "V1", "PLANT_ERROR", code)

    def _handle_plant_list_request(
        self, sock: socket.socket, fields: list[str]
    ) -> None:
        if len(fields) != 3:
            self._send_plant_error(sock, "BAD_FRAME")
            return
        try:
            device_id = validate_device_id(fields[2])
            current_species_id = self.repository.get_current_species_id(device_id)
            if current_species_id is None:
                self._send_plant_error(sock, "DEVICE_NOT_FOUND")
                return
            current_plant = self.repository.get_plant(current_species_id)
            if current_plant is None:
                self._send_plant_error(sock, "PLANT_NOT_FOUND")
                return
            plants = self.repository.list_plants()
        except ProtocolError as exc:
            self._send_plant_error(sock, exc.code)
            return
        except (PlantRepositoryError, sqlite3.Error) as exc:
            log("DATABASE", f"plant list failed: {exc}")
            self._send_plant_error(sock, "DATABASE_ERROR")
            return

        count = len(plants)
        self._send(
            sock,
            "V1",
            "PLANT_LIST_BEGIN",
            count,
            current_plant.summary.species_id,
            current_plant.summary.display_name,
        )
        time.sleep(PLANT_FRAME_PACE_SECONDS)
        for index, plant in enumerate(plants):
            self._send(
                sock,
                "V1",
                "PLANT_ITEM",
                index,
                plant.species_id,
                plant.display_name,
                plant.source_type,
            )
            time.sleep(PLANT_FRAME_PACE_SECONDS)
        self._send(sock, "V1", "PLANT_LIST_END", count)
        log("PLANT", f"list device={device_id} count={count}")

    def _handle_plant_detail_request(
        self, sock: socket.socket, fields: list[str]
    ) -> None:
        if len(fields) != 3:
            self._send_plant_error(sock, "BAD_FRAME")
            return
        try:
            species_id = validate_species_id(fields[2])
            plant = self.repository.get_plant(species_id)
        except ProtocolError as exc:
            self._send_plant_error(sock, exc.code)
            return
        except (PlantRepositoryError, sqlite3.Error) as exc:
            log("DATABASE", f"plant detail failed: {exc}")
            self._send_plant_error(sock, "DATABASE_ERROR")
            return
        if plant is None:
            self._send_plant_error(sock, "PLANT_NOT_FOUND")
            return

        self._send(
            sock,
            "V1",
            "PLANT_DETAIL",
            plant.summary.species_id,
            plant.summary.display_name,
            plant.temp_min_c,
            plant.temp_max_c,
            plant.humidity_min,
            plant.humidity_max,
            plant.light_min,
            plant.light_max,
            plant.summary.source_type,
        )
        log("PLANT", f"detail species={species_id}")

    def _handle_plant_select(self, sock: socket.socket, fields: list[str]) -> None:
        if len(fields) != 4:
            self._send_plant_error(sock, "BAD_FRAME")
            return
        try:
            device_id = validate_device_id(fields[2])
            species_id = validate_species_id(fields[3])
            plant = self.repository.get_plant(species_id)
            if plant is None:
                self._send_plant_error(sock, "PLANT_NOT_FOUND")
                return
            self.repository.set_current_plant(device_id, species_id)
        except ProtocolError as exc:
            self._send_plant_error(sock, exc.code)
            return
        except PlantRepositoryError as exc:
            code = (
                "DEVICE_NOT_FOUND"
                if "device not found" in str(exc)
                else "DATABASE_ERROR"
            )
            log("DATABASE", f"plant select failed: {exc}")
            self._send_plant_error(sock, code)
            return
        except sqlite3.Error as exc:
            log("DATABASE", f"plant select failed: {exc}")
            self._send_plant_error(sock, "DATABASE_ERROR")
            return

        self._send(
            sock,
            "V1",
            "PLANT_SELECTED",
            plant.summary.species_id,
            plant.summary.display_name,
        )
        log("PLANT", f"selected device={device_id} species={species_id}")

    def _send_asset_error(
        self, sock: socket.socket, transfer_id: int, code: str
    ) -> None:
        self._send(sock, "V1", "ASSET_ERROR", transfer_id, code)

    @staticmethod
    def _resolve_asset_path(relative_path: str) -> Path:
        candidate = (GATEWAY_ROOT / relative_path).resolve()
        if candidate != ASSET_ROOT and ASSET_ROOT not in candidate.parents:
            raise ValueError("asset path escapes asset root")
        return candidate

    def _handle_asset_request(self, sock: socket.socket, fields: list[str]) -> None:
        transfer_id = 0
        if len(fields) != 5:
            self._send_asset_error(sock, transfer_id, "BAD_FRAME")
            return
        try:
            transfer_id = int(fields[2])
            if not 0 <= transfer_id <= 65535:
                raise ValueError
            species_id = validate_species_id(fields[3])
            state = fields[4].upper()
            if state not in ASSET_STATES:
                raise ProtocolError("BAD_VALUE")
        except (ValueError, ProtocolError):
            self._send_asset_error(sock, transfer_id, "BAD_VALUE")
            return

        try:
            image = self.repository.get_image(species_id, state)
            if image is None:
                self._send_asset_error(sock, transfer_id, "ASSET_NOT_FOUND")
                return
            asset_path = self._resolve_asset_path(image.rgb565_path)
            payload = asset_path.read_bytes()
        except (PlantRepositoryError, sqlite3.Error) as exc:
            log("DATABASE", f"asset lookup failed: {exc}")
            self._send_asset_error(sock, transfer_id, "DATABASE_ERROR")
            return
        except (OSError, ValueError) as exc:
            log("ASSET", f"read failed: {exc}")
            self._send_asset_error(sock, transfer_id, "ASSET_READ_ERROR")
            return

        crc32 = f"{zlib.crc32(payload) & 0xFFFFFFFF:08X}"
        if (
            image.image_format != "RGB565_BE"
            or image.width != 48
            or image.height != 48
            or len(payload) != image.byte_size
            or crc32 != image.crc32.upper()
        ):
            log(
                "ASSET",
                f"invalid species={species_id} state={state} "
                f"bytes={len(payload)} crc={crc32}",
            )
            self._send_asset_error(sock, transfer_id, "ASSET_INVALID")
            return

        chunk_count = (len(payload) + ASSET_CHUNK_BYTES - 1) // ASSET_CHUNK_BYTES
        log(
            "ASSET",
            f"streaming id={transfer_id} species={species_id} state={state} "
            f"bytes={len(payload)} chunks={chunk_count}",
        )
        self._send(
            sock,
            "V1",
            "ASSET_BEGIN",
            transfer_id,
            species_id,
            state,
            image.width,
            image.height,
            len(payload),
            chunk_count,
            crc32,
        )
        time.sleep(ASSET_FRAME_PACE_SECONDS)
        for sequence, offset in enumerate(range(0, len(payload), ASSET_CHUNK_BYTES)):
            chunk = payload[offset : offset + ASSET_CHUNK_BYTES]
            self._send(
                sock,
                "V1",
                "ASSET_CHUNK",
                transfer_id,
                sequence,
                chunk.hex().upper(),
                log_payload=False,
            )
            if (sequence + 1) % 32 == 0:
                log("ASSET", f"progress id={transfer_id} {sequence + 1}/{chunk_count}")
            time.sleep(ASSET_FRAME_PACE_SECONDS)
        self._send(sock, "V1", "ASSET_END", transfer_id, chunk_count, crc32)
        log(
            "ASSET",
            f"sent id={transfer_id} species={species_id} state={state} "
            f"bytes={len(payload)} chunks={chunk_count} crc={crc32}",
        )

    def _handle_ai_request(self, sock: socket.socket, fields: list[str]) -> None:
        try:
            request = parse_ai_request(fields)
        except ProtocolError as exc:
            log("PROTOCOL", f"AI request rejected: {exc.code}")
            self._send(sock, "V1", "ERROR", exc.request_id, exc.code)
            return

        request_id = request.request_id
        try:
            plant = self.repository.get_plant(request.species_id)
            if plant is None:
                self._send(sock, "V1", "ERROR", request_id, "PLANT_NOT_FOUND")
                return
            if self.repository.get_current_species_id(request.device_id) is None:
                self._send(sock, "V1", "ERROR", request_id, "DEVICE_NOT_FOUND")
                return
            assessment = self.evaluator.evaluate(plant, request)
        except (PlantRepositoryError, sqlite3.Error) as exc:
            log("DATABASE", f"request={request_id} failed: {exc}")
            self._send(sock, "V1", "ERROR", request_id, "DATABASE_ERROR")
            return

        self._send(sock, "V1", "ACK", request_id, request.message_type)
        self._send(sock, "V1", "AI_STAGE", request_id, "PREPARING")
        self._send(sock, "V1", "AI_STAGE", request_id, "THINKING")
        log(
            "MODEL",
            f"THINKING... request={request_id} "
            f"device={request.device_id} plant={request.species_id} "
            f"T={request.temperature} H={request.humidity} "
            f"L={request.light}/{request.light_level} "
            f"fit={assessment.temperature_fit}/"
            f"{assessment.humidity_fit}/{assessment.light_fit}",
        )
        started = time.monotonic()
        try:
            result = self.provider.analyze(request, plant, assessment)
        except ProviderError as exc:
            elapsed = time.monotonic() - started
            log("MODEL", f"failed request={request_id} code={exc.code} elapsed={elapsed:.2f}s")
            self._send(sock, "V1", "ERROR", request_id, exc.code)
            return

        elapsed = time.monotonic() - started
        self._send(sock, "V1", "AI_STAGE", request_id, "VALIDATING")
        log("MODEL", f"VALIDATED request={request_id} elapsed={elapsed:.2f}s")
        log("ADVICE", result.suggestion_en)
        try:
            self.repository.record_analysis(
                request_id=request.request_id,
                device_id=request.device_id,
                species_id=request.species_id,
                temperature_c=request.temperature,
                humidity_percent=request.humidity,
                light_percent=request.light,
                light_level=request.light_level,
                status=result.status,
                issue=result.issue,
                watering=result.watering,
                action_code=result.advice,
                suggestion_en=result.suggestion_en,
                provider=self.config.provider,
            )
        except (PlantRepositoryError, sqlite3.Error) as exc:
            log("DATABASE", f"analysis log skipped request={request_id}: {exc}")
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
