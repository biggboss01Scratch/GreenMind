from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from agri_gateway.database import initialize_database
from agri_gateway.plant_repository import PlantRepository
from agri_gateway.plant_rules import PlantRuleEvaluator
from agri_gateway.protocol import SensorRequest
from agri_gateway.providers import DeepSeekProvider, MockProvider


class PlantRuleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary_directory = tempfile.TemporaryDirectory()
        database_path = Path(cls.temporary_directory.name) / "greenmind.sqlite3"
        initialize_database(database_path)
        cls.repository = PlantRepository(database_path)
        cls.evaluator = PlantRuleEvaluator()
        cls.provider = MockProvider()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary_directory.cleanup()

    def _request(self, species_id: str) -> SensorRequest:
        return SensorRequest(
            request_id=20,
            temperature=30,
            humidity=45,
            light=80,
            light_level="STRONG",
            device_id="GM001",
            species_id=species_id,
            message_type="PLANT_AI_REQ",
        )

    def test_same_environment_differs_by_plant(self) -> None:
        pothos = self.repository.get_plant("pothos")
        cactus = self.repository.get_plant("cactus")
        assert pothos is not None and cactus is not None

        pothos_result = self.evaluator.evaluate(pothos, self._request("pothos"))
        cactus_result = self.evaluator.evaluate(cactus, self._request("cactus"))

        self.assertEqual(
            (pothos_result.status, pothos_result.issue, pothos_result.advice),
            ("WARN", "LIGHT_HIGH", "MOVE_TO_SHADE"),
        )
        self.assertEqual(
            (cactus_result.status, cactus_result.issue, cactus_result.advice),
            ("NORMAL", "NONE", "KEEP_CURRENT"),
        )

    def test_mock_provider_uses_rule_result_and_plant_name(self) -> None:
        plant = self.repository.get_plant("pothos")
        assert plant is not None
        request = self._request("pothos")
        assessment = self.evaluator.evaluate(plant, request)
        result = self.provider.analyze(request, plant, assessment)
        self.assertEqual(result.status, assessment.status)
        self.assertEqual(result.issue, assessment.issue)
        self.assertNotIn("PUMP", result.advice)
        self.assertIn("Pothos", result.suggestion_en)
        self.assertTrue(result.suggestion_en.isascii())
        self.assertIn("绿萝", result.dialog_zh)
        self.assertIn("30度", result.dialog_zh)
        self.assertLessEqual(len(result.dialog_zh.encode("utf-8")), 768)

    def test_model_cannot_override_backend_rule_enums(self) -> None:
        plant = self.repository.get_plant("pothos")
        assert plant is not None
        request = self._request("pothos")
        assessment = self.evaluator.evaluate(plant, request)
        result = DeepSeekProvider._validate(
            {
                "status": "normal",
                "main_issue": "none",
                "watering_advice": "no_need",
                "advice_code": "keep_current",
                "suggestion_en": "Everything is fine.",
                "dialog_zh": "这段内容与后端结论不一致，必须回退到安全文案。",
            },
            request,
            plant,
            assessment,
        )
        self.assertEqual(
            (result.status, result.issue, result.watering, result.advice),
            (
                assessment.status,
                assessment.issue,
                assessment.watering,
                assessment.advice,
            ),
        )
        self.assertEqual(result.suggestion_en, assessment.suggestion_en)
        self.assertIn("绿萝", result.dialog_zh)

    def test_model_suggestion_normalizes_display_unsafe_unicode(self) -> None:
        plant = self.repository.get_plant("pothos")
        assert plant is not None
        request = self._request("pothos")
        assessment = self.evaluator.evaluate(plant, request)
        result = DeepSeekProvider._validate(
            {
                "status": "warning",
                "main_issue": "light_high",
                "watering_advice": "no_need",
                "advice_code": "move_to_shade",
                "suggestion_en": (
                    "At 30°C — Pothos’s leaves would enjoy softer light."
                ),
                "dialog_zh": (
                    "我是绿萝，现在温度30度、空气湿度45%、光照80%，光线有点强。"
                    "我的气生根会帮助攀援，请把我移到柔和的散射光处慢慢适应。"
                ),
            },
            request,
            plant,
            assessment,
        )
        self.assertEqual(
            result.suggestion_en,
            "At 30 degrees C - Pothos's leaves would enjoy softer light.",
        )
        self.assertTrue(result.suggestion_en.isascii())
        self.assertIn("气生根", result.dialog_zh)

    def test_model_suggestion_is_shortened_at_a_word_boundary(self) -> None:
        suggestion = DeepSeekProvider._normalize_suggestion(
            "Orchid " + ("likes bright filtered light " * 10)
        )
        self.assertLessEqual(len(suggestion), 160)
        self.assertTrue(suggestion.endswith("..."))
        self.assertTrue(suggestion.isascii())


if __name__ == "__main__":
    unittest.main()
