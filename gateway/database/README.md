# GreenMind SQLite database

## Source of truth

- `schema.sql`: versioned SQLite schema;
- `seed_plants.json`: six initial plant profiles and one demo device;
- `../assets/plants/manifest.json`: generated image metadata and checksums;
- `../tools/init_database.py`: validation and idempotent database initialization.

The generated `../data/greenmind.sqlite3` is intentionally ignored by Git.

## Tables

| Table | Purpose |
|---|---|
| `schema_meta` | schema version and calibration note |
| `plant_species` | stable ID and basic display information |
| `plant_requirements` | temperature, air humidity and relative-light preferences |
| `plant_images` | PNG/RGB565 paths, state, size, version and checksums |
| `devices` | device identity and current plant |
| `ai_analysis_logs` | immutable plant identity and readings used for each AI request |

`devices.last_seen_at` is stored, but online/offline should be calculated from its age.
It must not be treated as a permanently stored Boolean.

## Initialize

Run from the `gateway` directory:

```powershell
python .\tools\generate_plant_assets.py
python .\tools\init_database.py --reset
```

Initialization fails if an asset is missing, is not 48×48 RGB565, has an unexpected
byte size, or does not match its CRC32/SHA-256.

The initial environmental ranges are course-project defaults. Light values use the
board sensor's relative 0–100 scale and are not lux. They should be calibrated before
being presented as plant-care reference values.
