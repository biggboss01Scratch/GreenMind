from __future__ import annotations

from dataclasses import dataclass

from .plant_repository import PlantDetails
from .protocol import SensorRequest


@dataclass(frozen=True)
class PlantAssessment:
    status: str
    issue: str
    watering: str
    advice: str
    temperature_fit: str
    humidity_fit: str
    light_fit: str
    suggestion_en: str


def _fit(value: int, minimum: int, maximum: int) -> str:
    if value < minimum:
        return "LOW"
    if value > maximum:
        return "HIGH"
    return "SUITABLE"


def _is_severe(
    request: SensorRequest,
    plant: PlantDetails,
    temperature_fit: str,
    humidity_fit: str,
    light_fit: str,
) -> bool:
    if temperature_fit == "LOW" and plant.temp_min_c - request.temperature >= 6:
        return True
    if temperature_fit == "HIGH" and request.temperature - plant.temp_max_c >= 6:
        return True
    if humidity_fit == "LOW" and plant.humidity_min - request.humidity >= 25:
        return True
    if humidity_fit == "HIGH" and request.humidity - plant.humidity_max >= 25:
        return True
    if light_fit == "LOW" and plant.light_min - request.light >= 30:
        return True
    if light_fit == "HIGH" and request.light - plant.light_max >= 30:
        return True
    return False


class PlantRuleEvaluator:
    def evaluate(self, plant: PlantDetails, request: SensorRequest) -> PlantAssessment:
        temperature_fit = _fit(
            request.temperature, plant.temp_min_c, plant.temp_max_c
        )
        humidity_fit = _fit(
            request.humidity, plant.humidity_min, plant.humidity_max
        )
        light_fit = _fit(request.light, plant.light_min, plant.light_max)
        plant_name = plant.summary.name_en

        if (
            temperature_fit == "SUITABLE"
            and humidity_fit == "SUITABLE"
            and light_fit == "SUITABLE"
        ):
            return PlantAssessment(
                status="NORMAL",
                issue="NONE",
                watering="NO_NEED",
                advice="KEEP_CURRENT",
                temperature_fit=temperature_fit,
                humidity_fit=humidity_fit,
                light_fit=light_fit,
                suggestion_en=(
                    f"{plant_name} conditions fit the stored profile. "
                    "Keep the current setup."
                ),
            )

        status = (
            "DANGER"
            if _is_severe(
                request,
                plant,
                temperature_fit,
                humidity_fit,
                light_fit,
            )
            else "WARN"
        )

        if temperature_fit == "HIGH" and light_fit == "HIGH":
            issue = "HOT_AND_BRIGHT"
            watering = "CHECK_SOIL"
            advice = "MOVE_TO_SHADE"
            suggestion = (
                f"{plant_name} is above its temperature and light profile. "
                "Reduce direct light and check the soil before watering."
            )
        elif temperature_fit == "HIGH":
            issue = "TEMP_HIGH"
            watering = "CHECK_SOIL"
            advice = "IMPROVE_VENTILATION"
            suggestion = (
                f"{plant_name} is warmer than its stored profile. "
                "Improve ventilation and check the soil before watering."
            )
        elif temperature_fit == "LOW":
            issue = "TEMP_LOW"
            watering = "NO_NEED"
            advice = "OBSERVE_PLANT"
            suggestion = (
                f"{plant_name} is colder than its stored profile. "
                "Move it to a stable warmer position and observe it."
            )
        elif light_fit == "HIGH":
            issue = "LIGHT_HIGH"
            watering = "NO_NEED"
            advice = "MOVE_TO_SHADE"
            suggestion = (
                f"Light is above the stored profile for {plant_name}. "
                "Reduce direct light and continue monitoring."
            )
        elif light_fit == "LOW":
            issue = "LIGHT_LOW"
            watering = "NO_NEED"
            advice = "INCREASE_LIGHT"
            suggestion = (
                f"Light is below the stored profile for {plant_name}. "
                "Increase suitable indirect light."
            )
        elif humidity_fit == "HIGH":
            issue = "HUMIDITY_HIGH"
            watering = "NO_NEED"
            advice = "IMPROVE_VENTILATION"
            suggestion = (
                f"Air humidity is above the stored profile for {plant_name}. "
                "Improve ventilation and keep observing."
            )
        else:
            issue = "HUMIDITY_LOW"
            watering = "CHECK_SOIL"
            advice = "CHECK_SOIL"
            suggestion = (
                f"Air humidity is below the stored profile for {plant_name}. "
                "Check the soil separately before deciding to water."
            )

        return PlantAssessment(
            status=status,
            issue=issue,
            watering=watering,
            advice=advice,
            temperature_fit=temperature_fit,
            humidity_fit=humidity_fit,
            light_fit=light_fit,
            suggestion_en=suggestion,
        )
