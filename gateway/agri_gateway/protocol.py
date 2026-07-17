from __future__ import annotations

import re
from dataclasses import dataclass


MAX_FRAME_BYTES = 120
DEFAULT_DEVICE_ID = "GM001"
DEFAULT_SPECIES_ID = "pothos"
DEVICE_ID_PATTERN = re.compile(r"^[A-Z0-9][A-Z0-9_-]{0,15}$")
SPECIES_ID_PATTERN = re.compile(r"^[a-z][a-z0-9_]{0,15}$")


class ProtocolError(ValueError):
    def __init__(self, code: str, request_id: int = 0) -> None:
        super().__init__(code)
        self.code = code
        self.request_id = request_id


def validate_device_id(device_id: str) -> str:
    if not DEVICE_ID_PATTERN.fullmatch(device_id):
        raise ProtocolError("BAD_VALUE")
    return device_id


def validate_species_id(species_id: str) -> str:
    if not SPECIES_ID_PATTERN.fullmatch(species_id):
        raise ProtocolError("BAD_VALUE")
    return species_id


@dataclass(frozen=True)
class SensorRequest:
    request_id: int
    temperature: int
    humidity: int
    light: int
    light_level: str
    device_id: str = DEFAULT_DEVICE_ID
    species_id: str = DEFAULT_SPECIES_ID
    message_type: str = "AI_REQ"


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
    if len(fields) == 7 and fields[1] == "AI_REQ":
        message_type = "AI_REQ"
        device_id = DEFAULT_DEVICE_ID
        species_id = DEFAULT_SPECIES_ID
        value_offset = 3
    elif len(fields) == 9 and fields[1] == "PLANT_AI_REQ":
        message_type = "PLANT_AI_REQ"
        device_id = validate_device_id(fields[3])
        species_id = validate_species_id(fields[4])
        value_offset = 5
    else:
        raise ProtocolError("BAD_FRAME")
    try:
        request_id = int(fields[2])
        temperature = int(fields[value_offset])
        humidity = int(fields[value_offset + 1])
        light = int(fields[value_offset + 2])
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
    light_level = fields[value_offset + 3]
    if light_level not in {"DARK", "NORMAL", "STRONG"}:
        raise ProtocolError("BAD_VALUE", request_id)

    return SensorRequest(
        request_id=request_id,
        temperature=temperature,
        humidity=humidity,
        light=light,
        light_level=light_level,
        device_id=device_id,
        species_id=species_id,
        message_type=message_type,
    )
