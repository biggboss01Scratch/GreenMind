PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS schema_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS plant_species (
    species_id TEXT PRIMARY KEY,
    name_zh TEXT NOT NULL,
    name_en TEXT NOT NULL,
    display_name TEXT NOT NULL,
    scientific_name TEXT NOT NULL,
    category TEXT NOT NULL,
    description_zh TEXT NOT NULL,
    difficulty TEXT NOT NULL
        CHECK (difficulty IN ('EASY', 'MEDIUM', 'ADVANCED')),
    source_type TEXT NOT NULL
        CHECK (source_type IN ('BUILTIN', 'ONLINE')),
    enabled INTEGER NOT NULL DEFAULT 1
        CHECK (enabled IN (0, 1)),
    data_version INTEGER NOT NULL DEFAULT 1
        CHECK (data_version > 0),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS plant_requirements (
    species_id TEXT PRIMARY KEY
        REFERENCES plant_species(species_id) ON DELETE CASCADE,
    temp_min_c INTEGER NOT NULL,
    temp_max_c INTEGER NOT NULL,
    humidity_min INTEGER NOT NULL,
    humidity_max INTEGER NOT NULL,
    light_min INTEGER NOT NULL,
    light_max INTEGER NOT NULL,
    light_preference TEXT NOT NULL,
    watering_style TEXT NOT NULL,
    ventilation_need TEXT NOT NULL,
    care_summary_zh TEXT NOT NULL,
    risk_notes_json TEXT NOT NULL,
    CHECK (temp_min_c <= temp_max_c),
    CHECK (humidity_min BETWEEN 0 AND 100),
    CHECK (humidity_max BETWEEN 0 AND 100),
    CHECK (humidity_min <= humidity_max),
    CHECK (light_min BETWEEN 0 AND 100),
    CHECK (light_max BETWEEN 0 AND 100),
    CHECK (light_min <= light_max)
);

CREATE TABLE IF NOT EXISTS plant_images (
    image_id INTEGER PRIMARY KEY AUTOINCREMENT,
    species_id TEXT NOT NULL
        REFERENCES plant_species(species_id) ON DELETE CASCADE,
    state TEXT NOT NULL
        CHECK (state IN ('NORMAL', 'ATTENTION', 'DANGER')),
    image_type TEXT NOT NULL DEFAULT 'PIXEL'
        CHECK (image_type = 'PIXEL'),
    image_format TEXT NOT NULL DEFAULT 'RGB565_BE'
        CHECK (image_format = 'RGB565_BE'),
    width INTEGER NOT NULL CHECK (width > 0),
    height INTEGER NOT NULL CHECK (height > 0),
    byte_size INTEGER NOT NULL CHECK (byte_size > 0),
    png_path TEXT NOT NULL,
    rgb565_path TEXT NOT NULL,
    crc32 TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    image_version INTEGER NOT NULL DEFAULT 1
        CHECK (image_version > 0),
    enabled INTEGER NOT NULL DEFAULT 1
        CHECK (enabled IN (0, 1)),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (species_id, state, image_version)
);

CREATE TABLE IF NOT EXISTS devices (
    device_id TEXT PRIMARY KEY,
    device_name TEXT NOT NULL,
    current_species_id TEXT NOT NULL DEFAULT 'pothos'
        REFERENCES plant_species(species_id),
    last_seen_at TEXT,
    firmware_version TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS ai_analysis_logs (
    analysis_id INTEGER PRIMARY KEY AUTOINCREMENT,
    request_id INTEGER NOT NULL CHECK (request_id BETWEEN 0 AND 65535),
    device_id TEXT NOT NULL REFERENCES devices(device_id),
    species_id TEXT NOT NULL REFERENCES plant_species(species_id),
    temperature_c INTEGER NOT NULL,
    humidity_percent INTEGER NOT NULL CHECK (humidity_percent BETWEEN 0 AND 100),
    light_percent INTEGER NOT NULL CHECK (light_percent BETWEEN 0 AND 100),
    light_level TEXT NOT NULL
        CHECK (light_level IN ('DARK', 'NORMAL', 'STRONG')),
    status TEXT,
    issue TEXT,
    watering TEXT,
    action_code TEXT,
    suggestion_en TEXT,
    dialog_zh TEXT,
    provider TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_plant_species_enabled
    ON plant_species(enabled, source_type, display_name);

CREATE INDEX IF NOT EXISTS idx_plant_images_lookup
    ON plant_images(species_id, state, enabled, image_version);

CREATE INDEX IF NOT EXISTS idx_ai_logs_device_time
    ON ai_analysis_logs(device_id, created_at);

CREATE INDEX IF NOT EXISTS idx_ai_logs_species_time
    ON ai_analysis_logs(species_id, created_at);
