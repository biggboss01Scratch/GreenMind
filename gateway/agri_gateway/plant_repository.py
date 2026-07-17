from __future__ import annotations

import json
import re
import sqlite3
from dataclasses import dataclass
from pathlib import Path

from .database import DEFAULT_DATABASE_PATH, database_session


SPECIES_ID_PATTERN = re.compile(r"^[a-z][a-z0-9_]{0,31}$")
IMAGE_STATES = {"NORMAL", "ATTENTION", "DANGER"}


class PlantRepositoryError(RuntimeError):
    pass


@dataclass(frozen=True)
class PlantSummary:
    species_id: str
    name_zh: str
    name_en: str
    display_name: str
    category: str
    difficulty: str
    source_type: str
    data_version: int


@dataclass(frozen=True)
class PlantImage:
    species_id: str
    state: str
    image_format: str
    width: int
    height: int
    byte_size: int
    png_path: str
    rgb565_path: str
    crc32: str
    sha256: str
    image_version: int


@dataclass(frozen=True)
class PlantDetails:
    summary: PlantSummary
    scientific_name: str
    description_zh: str
    temp_min_c: int
    temp_max_c: int
    humidity_min: int
    humidity_max: int
    light_min: int
    light_max: int
    light_preference: str
    watering_style: str
    ventilation_need: str
    care_summary_zh: str
    risk_notes: tuple[str, ...]


def _validate_species_id(species_id: str) -> None:
    if not SPECIES_ID_PATTERN.fullmatch(species_id):
        raise PlantRepositoryError(f"invalid species_id: {species_id!r}")


class PlantRepository:
    def __init__(self, database_path: Path | str = DEFAULT_DATABASE_PATH) -> None:
        self.database_path = Path(database_path)

    def list_plants(self, source_type: str | None = None) -> list[PlantSummary]:
        query = """
            SELECT species_id, name_zh, name_en, display_name, category,
                   difficulty, source_type, data_version
            FROM plant_species
            WHERE enabled = 1
        """
        parameters: tuple[str, ...] = ()
        if source_type is not None:
            source_type = source_type.upper()
            if source_type not in {"BUILTIN", "ONLINE"}:
                raise PlantRepositoryError(f"invalid source_type: {source_type!r}")
            query += " AND source_type = ?"
            parameters = (source_type,)
        query += " ORDER BY CASE source_type WHEN 'BUILTIN' THEN 0 ELSE 1 END, display_name"

        with database_session(self.database_path) as connection:
            rows = connection.execute(query, parameters).fetchall()
        return [
            PlantSummary(
                species_id=row["species_id"],
                name_zh=row["name_zh"],
                name_en=row["name_en"],
                display_name=row["display_name"],
                category=row["category"],
                difficulty=row["difficulty"],
                source_type=row["source_type"],
                data_version=row["data_version"],
            )
            for row in rows
        ]

    def get_plant(self, species_id: str) -> PlantDetails | None:
        _validate_species_id(species_id)
        with database_session(self.database_path) as connection:
            row = connection.execute(
                """
                SELECT s.species_id, s.name_zh, s.name_en, s.display_name,
                       s.scientific_name, s.category, s.description_zh,
                       s.difficulty, s.source_type, s.data_version,
                       r.temp_min_c, r.temp_max_c,
                       r.humidity_min, r.humidity_max,
                       r.light_min, r.light_max, r.light_preference,
                       r.watering_style, r.ventilation_need,
                       r.care_summary_zh, r.risk_notes_json
                FROM plant_species AS s
                JOIN plant_requirements AS r USING (species_id)
                WHERE s.species_id = ? AND s.enabled = 1
                """,
                (species_id,),
            ).fetchone()
        if row is None:
            return None
        summary = PlantSummary(
            species_id=row["species_id"],
            name_zh=row["name_zh"],
            name_en=row["name_en"],
            display_name=row["display_name"],
            category=row["category"],
            difficulty=row["difficulty"],
            source_type=row["source_type"],
            data_version=row["data_version"],
        )
        return PlantDetails(
            summary=summary,
            scientific_name=row["scientific_name"],
            description_zh=row["description_zh"],
            temp_min_c=row["temp_min_c"],
            temp_max_c=row["temp_max_c"],
            humidity_min=row["humidity_min"],
            humidity_max=row["humidity_max"],
            light_min=row["light_min"],
            light_max=row["light_max"],
            light_preference=row["light_preference"],
            watering_style=row["watering_style"],
            ventilation_need=row["ventilation_need"],
            care_summary_zh=row["care_summary_zh"],
            risk_notes=tuple(json.loads(row["risk_notes_json"])),
        )

    def get_image(self, species_id: str, state: str) -> PlantImage | None:
        _validate_species_id(species_id)
        state = state.upper()
        if state not in IMAGE_STATES:
            raise PlantRepositoryError(f"invalid image state: {state!r}")
        with database_session(self.database_path) as connection:
            row = connection.execute(
                """
                SELECT species_id, state, image_format, width, height,
                       byte_size, png_path, rgb565_path, crc32, sha256,
                       image_version
                FROM plant_images
                WHERE species_id = ? AND state = ? AND enabled = 1
                ORDER BY image_version DESC
                LIMIT 1
                """,
                (species_id, state),
            ).fetchone()
        if row is None:
            return None
        return PlantImage(
            species_id=row["species_id"],
            state=row["state"],
            image_format=row["image_format"],
            width=row["width"],
            height=row["height"],
            byte_size=row["byte_size"],
            png_path=row["png_path"],
            rgb565_path=row["rgb565_path"],
            crc32=row["crc32"],
            sha256=row["sha256"],
            image_version=row["image_version"],
        )

    def get_current_species_id(self, device_id: str) -> str | None:
        with database_session(self.database_path) as connection:
            row = connection.execute(
                "SELECT current_species_id FROM devices WHERE device_id = ?",
                (device_id,),
            ).fetchone()
        return None if row is None else str(row["current_species_id"])

    def set_current_plant(self, device_id: str, species_id: str) -> None:
        _validate_species_id(species_id)
        with database_session(self.database_path) as connection:
            plant = connection.execute(
                "SELECT 1 FROM plant_species WHERE species_id = ? AND enabled = 1",
                (species_id,),
            ).fetchone()
            if plant is None:
                raise PlantRepositoryError(f"plant not found: {species_id}")
            cursor = connection.execute(
                """
                UPDATE devices
                SET current_species_id = ?, updated_at = CURRENT_TIMESTAMP
                WHERE device_id = ?
                """,
                (species_id, device_id),
            )
            if cursor.rowcount != 1:
                raise PlantRepositoryError(f"device not found: {device_id}")

    def record_analysis(
        self,
        *,
        request_id: int,
        device_id: str,
        species_id: str,
        temperature_c: int,
        humidity_percent: int,
        light_percent: int,
        light_level: str,
        status: str | None,
        issue: str | None,
        watering: str | None,
        action_code: str | None,
        suggestion_en: str | None,
        provider: str | None,
    ) -> int:
        _validate_species_id(species_id)
        try:
            with database_session(self.database_path) as connection:
                cursor = connection.execute(
                    """
                    INSERT INTO ai_analysis_logs (
                        request_id, device_id, species_id, temperature_c,
                        humidity_percent, light_percent, light_level,
                        status, issue, watering, action_code,
                        suggestion_en, provider
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        request_id,
                        device_id,
                        species_id,
                        temperature_c,
                        humidity_percent,
                        light_percent,
                        light_level,
                        status,
                        issue,
                        watering,
                        action_code,
                        suggestion_en,
                        provider,
                    ),
                )
                return int(cursor.lastrowid)
        except sqlite3.IntegrityError as exc:
            raise PlantRepositoryError(f"cannot record analysis: {exc}") from exc
