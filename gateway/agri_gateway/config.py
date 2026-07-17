from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class GatewayConfig:
    device_ip: str
    port: int = 8080
    provider: str = "deepseek"
    model: str = "deepseek-v4-flash"
    reconnect_seconds: float = 3.0
    socket_timeout_seconds: float = 1.0
    api_timeout_seconds: float = 45.0
    key_file: Path = Path(__file__).resolve().parents[2] / "myDeepseekApiKey.md"

    def validate(self) -> None:
        if not self.device_ip.strip():
            raise ValueError("device_ip cannot be empty")
        if not 1 <= self.port <= 65535:
            raise ValueError("port must be between 1 and 65535")
        if self.provider not in {"mock", "deepseek"}:
            raise ValueError("provider must be mock or deepseek")
        if self.api_timeout_seconds <= 0:
            raise ValueError("api timeout must be positive")
