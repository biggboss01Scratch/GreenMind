from __future__ import annotations

import sqlite3
import tempfile
import unittest
from pathlib import Path

from agri_gateway.database import database_session, initialize_database
from agri_gateway.plant_repository import PlantRepository, PlantRepositoryError


class PlantDatabaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.database_path = Path(self.temporary_directory.name) / "greenmind.sqlite3"
        self.summary = initialize_database(self.database_path)
        self.repository = PlantRepository(self.database_path)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def test_seed_counts(self) -> None:
        self.assertEqual(self.summary.plant_count, 6)
        self.assertEqual(self.summary.image_count, 18)
        self.assertEqual(self.summary.device_count, 1)
        self.assertEqual(len(self.repository.list_plants("BUILTIN")), 2)
        self.assertEqual(len(self.repository.list_plants("ONLINE")), 4)

    def test_orchid_profile_and_image(self) -> None:
        orchid = self.repository.get_plant("orchid")
        self.assertIsNotNone(orchid)
        assert orchid is not None
        self.assertEqual(orchid.summary.display_name, "ORCHID")
        self.assertEqual((orchid.temp_min_c, orchid.temp_max_c), (18, 28))
        self.assertEqual((orchid.light_min, orchid.light_max), (30, 60))

        image = self.repository.get_image("orchid", "ATTENTION")
        self.assertIsNotNone(image)
        assert image is not None
        self.assertEqual(image.image_format, "RGB565_BE")
        self.assertEqual((image.width, image.height, image.byte_size), (48, 48, 4608))

    def test_current_plant_can_be_changed(self) -> None:
        self.assertEqual(self.repository.get_current_species_id("GM001"), "pothos")
        self.repository.set_current_plant("GM001", "cactus")
        self.assertEqual(self.repository.get_current_species_id("GM001"), "cactus")

    def test_unknown_plant_does_not_change_device(self) -> None:
        with self.assertRaises(PlantRepositoryError):
            self.repository.set_current_plant("GM001", "missing")
        self.assertEqual(self.repository.get_current_species_id("GM001"), "pothos")

    def test_analysis_log_preserves_species_snapshot(self) -> None:
        analysis_id = self.repository.record_analysis(
            request_id=15,
            device_id="GM001",
            species_id="pothos",
            temperature_c=26,
            humidity_percent=65,
            light_percent=62,
            light_level="NORMAL",
            status="WARN",
            issue="LIGHT_HIGH",
            watering="CHECK_SOIL",
            action_code="MOVE_TO_SHADE",
            suggestion_en="Move the plant away from direct sunlight.",
            provider="mock",
        )
        self.assertGreater(analysis_id, 0)
        with database_session(self.database_path) as connection:
            row = connection.execute(
                "SELECT species_id, request_id FROM ai_analysis_logs WHERE analysis_id = ?",
                (analysis_id,),
            ).fetchone()
        self.assertEqual((row["species_id"], row["request_id"]), ("pothos", 15))

    def test_foreign_keys_are_enabled(self) -> None:
        with database_session(self.database_path) as connection:
            with self.assertRaises(sqlite3.IntegrityError):
                connection.execute(
                    """
                    INSERT INTO plant_images (
                        species_id, state, image_format, width, height,
                        byte_size, png_path, rgb565_path, crc32, sha256,
                        image_version
                    ) VALUES (
                        'missing', 'NORMAL', 'RGB565_BE', 48, 48,
                        4608, 'a.png', 'a.rgb565', '00000000', 'bad', 1
                    )
                    """
                )


if __name__ == "__main__":
    unittest.main()
