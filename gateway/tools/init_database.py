from __future__ import annotations

import argparse
import sys
from pathlib import Path


GATEWAY_ROOT = Path(__file__).resolve().parents[1]
if str(GATEWAY_ROOT) not in sys.path:
    sys.path.insert(0, str(GATEWAY_ROOT))

from agri_gateway.database import DEFAULT_DATABASE_PATH, initialize_database
from agri_gateway.plant_repository import PlantRepository


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Create or update the local GreenMind SQLite database."
    )
    parser.add_argument(
        "--db",
        type=Path,
        default=DEFAULT_DATABASE_PATH,
        help="SQLite database path.",
    )
    parser.add_argument(
        "--reset",
        action="store_true",
        help="Delete the selected database file before initialization.",
    )
    args = parser.parse_args()

    summary = initialize_database(args.db, reset=args.reset)
    repository = PlantRepository(summary.database_path)
    print(f"database: {summary.database_path}")
    print(
        f"schema={summary.schema_version} plants={summary.plant_count} "
        f"images={summary.image_count} devices={summary.device_count}"
    )
    for plant in repository.list_plants():
        details = repository.get_plant(plant.species_id)
        assert details is not None
        print(
            f"{plant.species_id:10} {plant.display_name:10} {plant.source_type:7} "
            f"T={details.temp_min_c}-{details.temp_max_c} "
            f"H={details.humidity_min}-{details.humidity_max} "
            f"L={details.light_min}-{details.light_max}"
        )


if __name__ == "__main__":
    main()
