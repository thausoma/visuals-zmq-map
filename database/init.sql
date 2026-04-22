CREATE TABLE IF NOT EXISTS measurements (
    id SERIAL PRIMARY KEY,
    latitude DOUBLE PRECISION,
    longitude DOUBLE PRECISION,
    altitude DOUBLE PRECISION,
    accuracy REAL,
    net_type VARCHAR(10),
    rsrp_global INTEGER,
    current_time_ms BIGINT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS cell_data (
    id SERIAL PRIMARY KEY,
    measurement_id INTEGER REFERENCES measurements(id) ON DELETE CASCADE,
    cell_type VARCHAR(10),
    band VARCHAR(10),
    cell_identity BIGINT,
    earfcn INTEGER,
    mcc VARCHAR(5),
    mnc VARCHAR(5),
    pci INTEGER,
    tac INTEGER,
    asu_level INTEGER,
    cqi INTEGER,
    rsrp INTEGER,
    rsrq INTEGER,
    rssi INTEGER,
    rssnr INTEGER,
    timing_advance INTEGER
);

CREATE INDEX idx_measurements_time ON measurements(current_time_ms);