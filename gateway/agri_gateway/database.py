from __future__ import annotations

import hashlib
import json
import sqlite3
import zlib
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterator


GATEWAY_ROOT = Path(__file__).resolve().parents[1]
DATABASE_DIR = GATEWAY_ROOT / "database"
ASSET_ROOT = (GATEWAY_ROOT / "assets").resolve()
DEFAULT_DATABASE_PATH = GATEWAY_ROOT / "data" / "greenmind.sqlite3"
DEFAULT_SCHEMA_PATH = DATABASE_DIR / "schema.sql"
DEFAULT_SEED_PATH = DATABASE_DIR / "seed_plants.json"
DEFAULT_MANIFEST_PATH = GATEWAY_ROOT / "assets" / "plants" / "manifest.json"


class DatabaseInitializationError(RuntimeError):
    pass


@dataclass(frozen=True)
class DatabaseSummary:
    database_path: Path
    schema_version: int
    plant_count: int
    image_count: int
    device_count: int


def connect_database(database_path: Path | str = DEFAULT_DATABASE_PATH) -> sqlite3.Connection:
    connection = sqlite3.connect(Path(database_path))
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA foreign_keys = ON")
    return connection


@contextmanager
def database_session(
    database_path: Path | str = DEFAULT_DATABASE_PATH,
) -> Iterator[sqlite3.Connection]:
    connection = connect_database(database_path)
    try:
        yield connection
        connection.commit()
    except Exception:
        connection.rollback()
        raise
    finally:
        connection.close()


def _load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise DatabaseInitializationError(f"cannot load {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise DatabaseInitializationError(f"{path} must contain one JSON object")
    return value


def _resolve_asset(relative_path: str) -> Path:
    candidate = (GATEWAY_ROOT / relative_path).resolve()
    if candidate != ASSET_ROOT and ASSET_ROOT not in candidate.parents:
        raise DatabaseInitializationError(f"asset path escapes asset root: {relative_path}")
    return candidate


def _validate_manifest(
    seed: dict[str, Any], manifest: dict[str, Any]
) -> list[dict[str, Any]]:
    plant_ids = {
        plant["species_id"]
        for plant in seed.get("plants", [])
        if isinstance(plant, dict) and isinstance(plant.get("species_id"), str)
    }
    expected_states = {"NORMAL", "ATTENTION", "DANGER"}
    entries = manifest.get("entries")
    if not isinstance(entries, list):
        raise DatabaseInitializationError("asset manifest has no entries list")

    seen: set[tuple[str, str]] = set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise DatabaseInitializationError("asset manifest entry must be an object")
        species_id = entry.get("species_id")
        state = entry.get("state")
        key = (species_id, state)
        if species_id not in plant_ids:
            raise DatabaseInitializationError(f"asset references unknown plant: {species_id}")
        if state not in expected_states:
            raise DatabaseInitializationError(f"asset has invalid state: {state}")
        if key in seen:
            raise DatabaseInitializationError(f"duplicate asset state: {species_id}/{state}")
        seen.add(key)

        if entry.get("width") != 48 or entry.get("height") != 48:
            raise DatabaseInitializationError(f"asset must be 48x48: {species_id}/{state}")
        if entry.get("image_format") != "RGB565_BE":
            raise DatabaseInitializationError(f"asset format must be RGB565_BE: {species_id}/{state}")
        raw_path = _resolve_asset(str(entry.get("rgb565_path", "")))
        png_path = _resolve_asset(str(entry.get("png_path", "")))
        if not raw_path.is_file() or not png_path.is_file():
            raise DatabaseInitializationError(f"asset file is missing: {species_id}/{state}")
        raw = raw_path.read_bytes()
        if len(raw) != 48 * 48 * 2 or len(raw) != entry.get("byte_size"):
            raise DatabaseInitializationError(f"asset byte size mismatch: {species_id}/{state}")
        crc32 = f"{zlib.crc32(raw) & 0xFFFFFFFF:08X}"
        sha256 = hashlib.sha256(raw).hexdigest()
        if crc32 != entry.get("crc32") or sha256 != entry.get("sha256"):
            raise DatabaseInitializationError(f"asset checksum mismatch: {species_id}/{state}")

    expected = {(species_id, state) for species_id in plant_ids for state in expected_states}
    missing = sorted(expected - seen)
    if missing:
        raise DatabaseInitializationError(f"asset states missing: {missing}")
    return entries


def _upsert_plant(connection: sqlite3.Connection, plant: dict[str, Any]) -> None:
    requirements = plant["requirements"]
    connection.execute(
        """
        INSERT INTO plant_species (
            species_id, name_zh, name_en, display_name, scientific_name,
            category, description_zh, difficulty, source_type, enabled, data_version
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?)
        ON CONFLICT(species_id) DO UPDATE SET
            name_zh = excluded.name_zh,
            name_en = excluded.name_en,
            display_name = excluded.display_name,
            scientific_name = excluded.scientific_name,
            category = excluded.category,
            description_zh = excluded.description_zh,
            difficulty = excluded.difficulty,
            source_type = excluded.source_type,
            enabled = 1,
            data_version = excluded.data_version,
            updated_at = CURRENT_TIMESTAMP
        """,
        (
            plant["species_id"],
            plant["name_zh"],
            plant["name_en"],
            plant["display_name"],
            plant["scientific_name"],
            plant["category"],
            plant["description_zh"],
            plant["difficulty"],
            plant["source_type"],
            plant["data_version"],
        ),
    )
    connection.execute(
        """
        INSERT INTO plant_requirements (
            species_id, temp_min_c, temp_max_c, humidity_min, humidity_max,
            light_min, light_max, light_preference, watering_style,
            ventilation_need, care_summary_zh, risk_notes_json
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(species_id) DO UPDATE SET
            temp_min_c = excluded.temp_min_c,
            temp_max_c = excluded.temp_max_c,
            humidity_min = excluded.humidity_min,
            humidity_max = excluded.humidity_max,
            light_min = excluded.light_min,
            light_max = excluded.light_max,
            light_preference = excluded.light_preference,
            watering_style = excluded.watering_style,
            ventilation_need = excluded.ventilation_need,
            care_summary_zh = excluded.care_summary_zh,
            risk_notes_json = excluded.risk_notes_json
        """,
        (
            plant["species_id"],
            requirements["temp_min_c"],
            requirements["temp_max_c"],
            requirements["humidity_min"],
            requirements["humidity_max"],
            requirements["light_min"],
            requirements["light_max"],
            requirements["light_preference"],
            requirements["watering_style"],
            requirements["ventilation_need"],
            requirements["care_summary_zh"],
            json.dumps(requirements["risk_notes"], ensure_ascii=False),
        ),
    )


def _upsert_image(connection: sqlite3.Connection, entry: dict[str, Any]) -> None:
    connection.execute(
        """
        UPDATE plant_images
        SET enabled = 0, updated_at = CURRENT_TIMESTAMP
        WHERE species_id = ? AND state = ? AND image_version <> ?
        """,
        (entry["species_id"], entry["state"], entry["image_version"]),
    )
    connection.execute(
        """
        INSERT INTO plant_images (
            species_id, state, image_type, image_format, width, height,
            byte_size, png_path, rgb565_path, crc32, sha256,
            image_version, enabled
        ) VALUES (?, ?, 'PIXEL', ?, ?, ?, ?, ?, ?, ?, ?, ?, 1)
        ON CONFLICT(species_id, state, image_version) DO UPDATE SET
            image_format = excluded.image_format,
            width = excluded.width,
            height = excluded.height,
            byte_size = excluded.byte_size,
            png_path = excluded.png_path,
            rgb565_path = excluded.rgb565_path,
            crc32 = excluded.crc32,
            sha256 = excluded.sha256,
            enabled = 1,
            updated_at = CURRENT_TIMESTAMP
        """,
        (
            entry["species_id"],
            entry["state"],
            entry["image_format"],
            entry["width"],
            entry["height"],
            entry["byte_size"],
            entry["png_path"],
            entry["rgb565_path"],
            entry["crc32"],
            entry["sha256"],
            entry["image_version"],
        ),
    )


def initialize_database(
    database_path: Path | str = DEFAULT_DATABASE_PATH,
    *,
    reset: bool = False,
    schema_path: Path = DEFAULT_SCHEMA_PATH,
    seed_path: Path = DEFAULT_SEED_PATH,
    manifest_path: Path = DEFAULT_MANIFEST_PATH,
) -> DatabaseSummary:
    database_path = Path(database_path).resolve()
    database_path.parent.mkdir(parents=True, exist_ok=True)
    if reset and database_path.exists():
        database_path.unlink()

    try:
        schema = schema_path.read_text(encoding="utf-8")
    except OSError as exc:
        raise DatabaseInitializationError(f"cannot load schema: {exc}") from exc
    seed = _load_json(seed_path)
    manifest = _load_json(manifest_path)
    entries = _validate_manifest(seed, manifest)
    plants = seed.get("plants")
    if not isinstance(plants, list) or not plants:
        raise DatabaseInitializationError("seed file contains no plants")

    with database_session(database_path) as connection:
        connection.executescript(schema)
        log_columns = {
            str(row["name"])
            for row in connection.execute("PRAGMA table_info(ai_analysis_logs)")
        }
        if "dialog_zh" not in log_columns:
            connection.execute(
                "ALTER TABLE ai_analysis_logs ADD COLUMN dialog_zh TEXT"
            )
        connection.execute(
            """
            INSERT INTO schema_meta(key, value) VALUES ('schema_version', ?)
            ON CONFLICT(key) DO UPDATE SET value = excluded.value
            """,
            (str(seed["schema_version"]),),
        )
        connection.execute(
            """
            INSERT INTO schema_meta(key, value) VALUES ('calibration_note', ?)
            ON CONFLICT(key) DO UPDATE SET value = excluded.value
            """,
            (seed["calibration_note"],),
        )
        for plant in plants:
            _upsert_plant(connection, plant)
        for entry in entries:
            _upsert_image(connection, entry)

        device = seed["default_device"]
        connection.execute(
            """
            INSERT INTO devices(device_id, device_name, current_species_id)
            VALUES (?, ?, ?)
            ON CONFLICT(device_id) DO UPDATE SET
                device_name = excluded.device_name,
                updated_at = CURRENT_TIMESTAMP
            """,
            (device["device_id"], device["device_name"], device["current_species_id"]),
        )
        plant_count = connection.execute(
            "SELECT COUNT(*) FROM plant_species WHERE enabled = 1"
        ).fetchone()[0]
        image_count = connection.execute(
            "SELECT COUNT(*) FROM plant_images WHERE enabled = 1"
        ).fetchone()[0]
        device_count = connection.execute("SELECT COUNT(*) FROM devices").fetchone()[0]

    return DatabaseSummary(
        database_path=database_path,
        schema_version=int(seed["schema_version"]),
        plant_count=plant_count,
        image_count=image_count,
        device_count=device_count,
    )
