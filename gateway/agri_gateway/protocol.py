from __future__ import annotations

from dataclasses import dataclass


MAX_FRAME_BYTES = 120


class ProtocolError(ValueError):
    def __init__(self, code: str, request_id: int = 0) -> None:
        super().__init__(code)
        self.code = code
        self.request_id = request_id


@dataclass(frozen=True)
class SensorRequest:
    request_id: int
    temperature: int
    humidity: int
    light: int
    light_level: str


def encode_frame(*fields: object) -> bytes:
    text = "|".join(str(field) for field in fields) + "\r\n"
    payload = text.encode("ascii")
    if len(payload) > MAX_FRAME_BYTES:
        raise ProtocolError("FRAME_TOO_LONG")
    return payload


def decode_frame(line: bytes) -> list[str]:
    if len(line) > MAX_FRAME_BYTES:
        raise ProtocolError("FRAME_TOO_LONG")
    try:
        text = line.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ProtocolError("BAD_FRAME") from exc
    fields = text.rstrip("\r\n").split("|")
    if len(fields) < 2 or fields[0] != "V1":
        raise ProtocolError("BAD_VERSION")
    return fields


def parse_ai_request(fields: list[str]) -> SensorRequest:
    if len(fields) != 7 or fields[1] != "AI_REQ":
        raise ProtocolError("BAD_FRAME")
    try:
        request_id = int(fields[2])
        temperature = int(fields[3])
        humidity = int(fields[4])
        light = int(fields[5])
    except ValueError as exc:
        raise ProtocolError("BAD_VALUE") from exc

    if not 0 <= request_id <= 65535:
        raise ProtocolError("BAD_VALUE")
    if not 0 <= temperature <= 60:
        raise ProtocolError("BAD_VALUE", request_id)
    if not 0 <= humidity <= 100:
        raise ProtocolError("BAD_VALUE", request_id)
    if not 0 <= light <= 100:
        raise ProtocolError("BAD_VALUE", request_id)
    if fields[6] not in {"DARK", "NORMAL", "STRONG"}:
        raise ProtocolError("BAD_VALUE", request_id)

    return SensorRequest(
        request_id=request_id,
        temperature=temperature,
        humidity=humidity,
        light=light,
        light_level=fields[6],
    )
