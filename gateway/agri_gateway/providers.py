from __future__ import annotations

import json
import os
import re
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

from .protocol import SensorRequest


API_URL = "https://api.deepseek.com/chat/completions"

STATUS_MAP = {
    "normal": "NORMAL",
    "warning": "WARN",
    "danger": "DANGER",
    "error": "ERROR",
}
ISSUE_MAP = {
    "none": "NONE",
    "temperature_low": "TEMP_LOW",
    "temperature_high": "TEMP_HIGH",
    "humidity_low": "HUMIDITY_LOW",
    "humidity_high": "HUMIDITY_HIGH",
    "light_low": "LIGHT_LOW",
    "light_high": "LIGHT_HIGH",
    "high_temperature_and_strong_light": "HOT_AND_BRIGHT",
    "sensor_invalid": "SENSOR_INVALID",
    "unknown": "UNKNOWN",
}
WATERING_MAP = {
    "no_need": "NO_NEED",
    "check_soil": "CHECK_SOIL",
    "water_if_dry": "WATER_IF_DRY",
    "unknown": "UNKNOWN",
}
ADVICE_MAP = {
    "keep_current": "KEEP_CURRENT",
    "move_to_shade": "MOVE_TO_SHADE",
    "increase_light": "INCREASE_LIGHT",
    "improve_ventilation": "IMPROVE_VENTILATION",
    "check_soil": "CHECK_SOIL",
    "check_sensor": "CHECK_SENSOR",
    "observe_plant": "OBSERVE_PLANT",
}


class ProviderError(RuntimeError):
    def __init__(self, code: str) -> None:
        super().__init__(code)
        self.code = code


@dataclass(frozen=True)
class AnalysisResult:
    status: str
    issue: str
    watering: str
    advice: str
    suggestion_en: str


class ModelProvider(Protocol):
    def analyze(self, request: SensorRequest) -> AnalysisResult: ...


def load_api_key(path: Path) -> str:
    environment_key = os.environ.get("DEEPSEEK_API_KEY", "").strip()
    if environment_key:
        return environment_key

    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ProviderError("API_KEY_MISSING") from exc
    match = re.search(r"sk-[A-Za-z0-9_-]{16,}", text)
    if not match:
        raise ProviderError("API_KEY_INVALID")
    return match.group(0)


class MockProvider:
    def analyze(self, request: SensorRequest) -> AnalysisResult:
        if request.temperature > 35:
            return AnalysisResult(
                "DANGER",
                "TEMP_HIGH",
                "CHECK_SOIL",
                "MOVE_TO_SHADE",
                "Temperature is high. Move the plant to shade and check the soil moisture.",
            )
        if request.temperature > 30 and request.light_level == "STRONG":
            return AnalysisResult(
                "WARN",
                "HOT_AND_BRIGHT",
                "CHECK_SOIL",
                "MOVE_TO_SHADE",
                "Temperature and light are high. Avoid direct sunlight and check the soil.",
            )
        if request.light_level == "DARK":
            return AnalysisResult(
                "WARN",
                "LIGHT_LOW",
                "NO_NEED",
                "INCREASE_LIGHT",
                "Light is low. Increase indirect light and continue monitoring the plant.",
            )
        return AnalysisResult(
            "NORMAL",
            "NONE",
            "NO_NEED",
            "KEEP_CURRENT",
            "Temperature, humidity, and light are normal. Keep the current conditions.",
        )


class DeepSeekProvider:
    def __init__(self, api_key: str, model: str, timeout_seconds: float) -> None:
        self._api_key = api_key
        self._model = model
        self._timeout_seconds = timeout_seconds

    def analyze(self, request: SensorRequest) -> AnalysisResult:
        last_error: ProviderError | None = None
        for _ in range(2):
            try:
                content = self._request(request)
                if not content.strip():
                    raise ProviderError("MODEL_EMPTY_OUTPUT")
                return self._validate(json.loads(content))
            except json.JSONDecodeError as exc:
                last_error = ProviderError("MODEL_BAD_OUTPUT")
                last_error.__cause__ = exc
            except ProviderError as exc:
                last_error = exc
                if exc.code not in {"MODEL_EMPTY_OUTPUT", "MODEL_BAD_OUTPUT"}:
                    break
        raise last_error or ProviderError("MODEL_ERROR")

    def _request(self, request: SensorRequest) -> str:
        system_prompt = (
            "You are a plant environment care assistant. Analyze air temperature, air "
            "humidity, and relative light. Air humidity is not soil moisture, so watering "
            "advice may only recommend checking the soil. Never control a pump or any "
            "hardware. Return only one JSON object with no extra text. The required fields "
            "are status, main_issue, watering_advice, advice_code, and suggestion_en. "
            "Use plain ASCII English for suggestion_en and keep it within 160 characters. "
            "status must be one of normal/warning/danger/error; "
            f"main_issue must be one of {list(ISSUE_MAP)}; "
            f"watering_advice must be one of {list(WATERING_MAP)}; "
            f"advice_code must be one of {list(ADVICE_MAP)}."
        )
        user_prompt = (
            f"Temperature={request.temperature} C, air humidity={request.humidity}%, "
            f"relative light={request.light}/100, light level={request.light_level}. "
            "Return the JSON plant care assessment."
        )
        payload = {
            "model": self._model,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ],
            "response_format": {"type": "json_object"},
            "thinking": {"type": "disabled"},
            "max_tokens": 300,
            "temperature": 0.2,
            "stream": False,
        }
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        http_request = urllib.request.Request(
            API_URL,
            data=body,
            method="POST",
            headers={
                "Authorization": f"Bearer {self._api_key}",
                "Content-Type": "application/json",
                "User-Agent": "AgricultureSys-MVP/1.0",
            },
        )
        try:
            with urllib.request.urlopen(http_request, timeout=self._timeout_seconds) as response:
                response_data = json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as exc:
            code = {
                401: "API_AUTH_ERROR",
                402: "API_BALANCE_ERROR",
                429: "API_RATE_LIMIT",
            }.get(exc.code, "MODEL_HTTP_ERROR")
            raise ProviderError(code) from exc
        except urllib.error.URLError as exc:
            raise ProviderError("MODEL_NETWORK_ERROR") from exc
        except TimeoutError as exc:
            raise ProviderError("MODEL_TIMEOUT") from exc
        except (UnicodeDecodeError, json.JSONDecodeError, KeyError, IndexError) as exc:
            raise ProviderError("MODEL_BAD_RESPONSE") from exc

        try:
            return str(response_data["choices"][0]["message"]["content"] or "")
        except (KeyError, IndexError, TypeError) as exc:
            raise ProviderError("MODEL_BAD_RESPONSE") from exc

    @staticmethod
    def _validate(data: object) -> AnalysisResult:
        if not isinstance(data, dict):
            raise ProviderError("MODEL_BAD_OUTPUT")
        try:
            status = STATUS_MAP[str(data["status"])]
            issue = ISSUE_MAP[str(data["main_issue"])]
            watering = WATERING_MAP[str(data["watering_advice"])]
            advice = ADVICE_MAP[str(data["advice_code"])]
            suggestion = str(data["suggestion_en"]).replace("\r", " ").replace("\n", " ")
        except (KeyError, TypeError) as exc:
            raise ProviderError("MODEL_BAD_OUTPUT") from exc
        if not suggestion or len(suggestion) > 160 or not suggestion.isascii():
            raise ProviderError("MODEL_BAD_OUTPUT")
        return AnalysisResult(status, issue, watering, advice, suggestion)
