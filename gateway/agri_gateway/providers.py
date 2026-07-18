from __future__ import annotations

import json
import os
import re
import unicodedata
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

from .plant_repository import PlantDetails
from .plant_rules import PlantAssessment
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
SUGGESTION_MAX_CHARS = 160
DIALOG_MAX_BYTES = 768
DIALOG_MIN_CHARS = 24
SUGGESTION_REPLACEMENTS = {
    "°C": " degrees C",
    "°F": " degrees F",
    "°": " degrees ",
    "\u2018": "'",
    "\u2019": "'",
    "\u201c": '"',
    "\u201d": '"',
    "\u2013": "-",
    "\u2014": "-",
    "\u2026": "...",
    "\u00a0": " ",
}

PLANT_FUN_FACTS = {
    "pothos": "绿萝会用气生根攀援，明亮的散射光更容易让叶片舒展",
    "mint": "薄荷的清凉香气藏在叶片腺体里，勤修剪和通风会让它长得更紧凑",
    "succulent": "多肉把水分储在肥厚叶片里，比起频繁浇水，它更喜欢干湿分明",
    "cactus": "仙人掌用肉质茎储水，刺还能减少水分散失，因此最怕盆土长期潮湿",
    "orchid": "蝴蝶兰的气生根也会参与呼吸，透气和避免叶心积水都很重要",
    "tomato": "番茄在开花结果期尤其喜欢充足光照和空气流动，这会帮助它稳定结果",
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
    dialog_zh: str


class ModelProvider(Protocol):
    def analyze(
        self,
        request: SensorRequest,
        plant: PlantDetails,
        assessment: PlantAssessment,
    ) -> AnalysisResult: ...


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
    def analyze(
        self,
        request: SensorRequest,
        plant: PlantDetails,
        assessment: PlantAssessment,
    ) -> AnalysisResult:
        return AnalysisResult(
            assessment.status,
            assessment.issue,
            assessment.watering,
            assessment.advice,
            assessment.suggestion_en,
            build_fallback_dialog_zh(request, plant, assessment),
        )


def _truncate_utf8(text: str, max_bytes: int) -> str:
    encoded = text.encode("utf-8")
    if len(encoded) <= max_bytes:
        return text
    shortened = encoded[:max_bytes]
    while shortened:
        try:
            return shortened.decode("utf-8").rstrip("，。；、： ")
        except UnicodeDecodeError:
            shortened = shortened[:-1]
    return ""


def _gb2312_safe_text(value: object) -> str:
    text = str(value).replace("\r", " ").replace("\n", " ").replace("|", "，")
    text = " ".join(text.split())
    safe: list[str] = []
    for character in text:
        if ord(character) < 0x20:
            continue
        try:
            character.encode("gb2312")
        except UnicodeEncodeError:
            safe.append("?")
        else:
            safe.append(character)
    return "".join(safe)


def build_fallback_dialog_zh(
    request: SensorRequest,
    plant: PlantDetails,
    assessment: PlantAssessment,
) -> str:
    name = plant.summary.name_zh
    fact = PLANT_FUN_FACTS.get(
        plant.summary.species_id,
        f"{name}有自己的生长节奏，稳定观察往往比频繁调整更重要",
    )
    if assessment.issue == "LIGHT_HIGH":
        observation = "现在的光照比它的档案范围偏强，可以移到柔和的散射光处"
    elif assessment.issue == "LIGHT_LOW":
        observation = "现在的光照低于它的偏好，可以逐步增加明亮但不过晒的光线"
    elif assessment.issue in {"TEMP_HIGH", "HOT_AND_BRIGHT"}:
        observation = "当前温度偏高，先避开强光并加强通风，让叶片慢慢降温"
    elif assessment.issue == "TEMP_LOW":
        observation = "当前温度偏低，尽量远离冷风和温差突然变化的位置"
    elif assessment.issue == "HUMIDITY_HIGH":
        observation = "空气湿度偏高，保持通风并留意叶面是否长时间潮湿"
    elif assessment.issue == "HUMIDITY_LOW":
        observation = "空气湿度偏低，但浇水前仍要单独摸一摸盆土，别把空气干当成缺水"
    else:
        observation = "温度、空气湿度和光照都比较合拍，保持现在的摆放并继续观察即可"
    dialog = (
        f"我是{name}。现在温度{request.temperature}度、空气湿度"
        f"{request.humidity}%、光照{request.light}%，{observation}。"
        f"小知识：{fact}。养护档案还提醒我：{plant.care_summary_zh}"
        "今天先调整最关键的一项，过一阵再看看叶片和盆土的反应，不用一次改变太多。"
    )
    return _truncate_utf8(_gb2312_safe_text(dialog), DIALOG_MAX_BYTES)


def normalize_dialog_zh(value: object, fallback: str) -> str:
    dialog = _gb2312_safe_text(value)
    if len(dialog) < DIALOG_MIN_CHARS:
        dialog = fallback
    dialog = _truncate_utf8(dialog, DIALOG_MAX_BYTES)
    if len(dialog) < DIALOG_MIN_CHARS:
        dialog = _truncate_utf8(fallback, DIALOG_MAX_BYTES)
    return dialog


class DeepSeekProvider:
    def __init__(self, api_key: str, model: str, timeout_seconds: float) -> None:
        self._api_key = api_key
        self._model = model
        self._timeout_seconds = timeout_seconds

    def analyze(
        self,
        request: SensorRequest,
        plant: PlantDetails,
        assessment: PlantAssessment,
    ) -> AnalysisResult:
        last_error: ProviderError | None = None
        for _ in range(2):
            try:
                content = self._request(request, plant, assessment)
                if not content.strip():
                    raise ProviderError("MODEL_EMPTY_OUTPUT")
                return self._validate(json.loads(content), request, plant, assessment)
            except json.JSONDecodeError as exc:
                last_error = ProviderError("MODEL_BAD_OUTPUT")
                last_error.__cause__ = exc
            except ProviderError as exc:
                last_error = exc
                if exc.code not in {"MODEL_EMPTY_OUTPUT", "MODEL_BAD_OUTPUT"}:
                    break
        raise last_error or ProviderError("MODEL_ERROR")

    def _request(
        self,
        request: SensorRequest,
        plant: PlantDetails,
        assessment: PlantAssessment,
    ) -> str:
        system_prompt = (
            "You explain a deterministic GreenMind plant assessment. The backend rule "
            "result is authoritative: do not change its status, issue, watering advice, "
            "or action code. Air humidity is not soil moisture, so watering advice may "
            "only recommend checking the soil. Never control a pump or any hardware. "
            "Return only one JSON object with no extra text. The required fields are "
            "status, main_issue, watering_advice, advice_code, suggestion_en, and "
            "dialog_zh. Use "
            "plain ASCII English for suggestion_en and keep it within 160 characters. "
            "dialog_zh must be 100 to 180 Chinese characters in common Simplified Chinese "
            "that can be represented by GB2312. Write it like a warm visual-novel plant "
            "dialogue: mention the current readings, explain how they fit this species, "
            "include one genuinely species-specific fun care fact, and finish with one "
            "safe actionable suggestion. Light personification is welcome, but do not "
            "be childish. Do not use emoji, vertical bars, or line breaks. "
            "status must be one of normal/warning/danger/error; "
            f"main_issue must be one of {list(ISSUE_MAP)}; "
            f"watering_advice must be one of {list(WATERING_MAP)}; "
            f"advice_code must be one of {list(ADVICE_MAP)}."
        )
        user_prompt = (
            f"Device={request.device_id}; plant={plant.summary.name_en} "
            f"({plant.summary.species_id}); preferred temperature="
            f"{plant.temp_min_c}-{plant.temp_max_c} C; preferred air humidity="
            f"{plant.humidity_min}-{plant.humidity_max}%; preferred relative light="
            f"{plant.light_min}-{plant.light_max}/100. Current temperature="
            f"{request.temperature} C, air humidity={request.humidity}%, relative light="
            f"{request.light}/100. Backend result: "
            f"status={next(key for key, value in STATUS_MAP.items() if value == assessment.status)}, "
            f"main_issue={next(key for key, value in ISSUE_MAP.items() if value == assessment.issue)}, "
            f"watering_advice={next(key for key, value in WATERING_MAP.items() if value == assessment.watering)}, "
            f"advice_code={next(key for key, value in ADVICE_MAP.items() if value == assessment.advice)}. "
            f"Plant description: {plant.description_zh}. Care profile: "
            f"{plant.care_summary_zh}. Known risks: {'；'.join(plant.risk_notes)}. "
            "Return the same enums, a short English explanation, and the requested "
            "personalized Chinese dialogue."
        )
        payload = {
            "model": self._model,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ],
            "response_format": {"type": "json_object"},
            "thinking": {"type": "disabled"},
            "max_tokens": 700,
            "temperature": 0.6,
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
                "User-Agent": "GreenMind-MVP/1.0",
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
    def _normalize_suggestion(value: object) -> str:
        suggestion = str(value).replace("\r", " ").replace("\n", " ")
        for source, replacement in SUGGESTION_REPLACEMENTS.items():
            suggestion = suggestion.replace(source, replacement)
        suggestion = (
            unicodedata.normalize("NFKD", suggestion)
            .encode("ascii", "ignore")
            .decode("ascii")
        )
        suggestion = " ".join(suggestion.split())
        if len(suggestion) > SUGGESTION_MAX_CHARS:
            shortened = suggestion[: SUGGESTION_MAX_CHARS - 3]
            if " " in shortened:
                shortened = shortened.rsplit(" ", 1)[0]
            suggestion = shortened.rstrip(" ,;:-") + "..."
        return suggestion

    @staticmethod
    def _validate(
        data: object,
        request: SensorRequest,
        plant: PlantDetails,
        assessment: PlantAssessment,
    ) -> AnalysisResult:
        if not isinstance(data, dict):
            raise ProviderError("MODEL_BAD_OUTPUT")
        fallback_dialog = build_fallback_dialog_zh(request, plant, assessment)
        try:
            status = STATUS_MAP[str(data["status"])]
            issue = ISSUE_MAP[str(data["main_issue"])]
            watering = WATERING_MAP[str(data["watering_advice"])]
            advice = ADVICE_MAP[str(data["advice_code"])]
        except (KeyError, TypeError) as exc:
            raise ProviderError("MODEL_BAD_OUTPUT") from exc
        suggestion = DeepSeekProvider._normalize_suggestion(
            data.get("suggestion_en", "")
        )
        dialog = normalize_dialog_zh(data.get("dialog_zh", ""), fallback_dialog)
        if not suggestion:
            suggestion = assessment.suggestion_en
        if (
            status,
            issue,
            watering,
            advice,
        ) != (
            assessment.status,
            assessment.issue,
            assessment.watering,
            assessment.advice,
        ):
            suggestion = assessment.suggestion_en
            dialog = fallback_dialog
        return AnalysisResult(
            assessment.status,
            assessment.issue,
            assessment.watering,
            assessment.advice,
            suggestion,
            dialog,
        )
