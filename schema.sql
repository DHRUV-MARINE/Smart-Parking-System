-- ============================================================================
--  SmartParking - SQLite schema + sample data
--  Loaded automatically by the C++ backend the first time it starts.
--  (You can also run it by hand:  sqlite3 data/smartparking.db < schema.sql)
-- ============================================================================

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- ---------------------------------------------------------------------------
-- Parking locations.  One row per parking lot.
-- `node_id` is the vertex of this lot inside the road graph (graph_nodes).
-- `occupancy_seed` is only used the first time slots are generated: it decides
-- how many slots start out occupied so the demo has a realistic mix of
-- Available / Limited / Full lots.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS parking_locations (
    id             INTEGER PRIMARY KEY,
    name           TEXT    NOT NULL,
    address        TEXT    NOT NULL,
    area           TEXT    NOT NULL,
    lat            REAL    NOT NULL,
    lng            REAL    NOT NULL,
    total_slots    INTEGER NOT NULL,
    occupancy_seed INTEGER NOT NULL DEFAULT 0,   -- percentage occupied at seed time
    price_per_hour REAL    NOT NULL,
    price_3_hours  REAL    NOT NULL,
    price_per_day  REAL    NOT NULL,
    type           TEXT    NOT NULL CHECK (type IN ('Public', 'Private')),
    node_id        INTEGER NOT NULL,
    rating         REAL    NOT NULL DEFAULT 4.0,
    open_hours     TEXT    NOT NULL DEFAULT '24x7'
);

-- ---------------------------------------------------------------------------
-- Road graph.  `kind` = 'landmark' (a place a user can start from) or
-- 'parking' (the vertex that represents a parking lot).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS graph_nodes (
    id   INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    lat  REAL NOT NULL,
    lng  REAL NOT NULL,
    kind TEXT NOT NULL CHECK (kind IN ('landmark', 'parking'))
);

-- Weighted, undirected edges (distance in kilometres).  Both directions are
-- stored so the C++ adjacency list can be built with a single pass.
CREATE TABLE IF NOT EXISTS graph_edges (
    from_node   INTEGER NOT NULL,
    to_node     INTEGER NOT NULL,
    distance_km REAL    NOT NULL,
    PRIMARY KEY (from_node, to_node)
);

-- ---------------------------------------------------------------------------
-- Parking slots (managed in memory by a linked list per lot).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS slots (
    id          INTEGER PRIMARY KEY,
    location_id INTEGER NOT NULL REFERENCES parking_locations(id),
    label       TEXT    NOT NULL,
    occupied    INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_slots_location ON slots(location_id);

-- ---------------------------------------------------------------------------
-- Bookings (managed in memory by a singly linked list).
-- status: 'active' | 'cancelled' | 'completed'
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS bookings (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    location_id INTEGER NOT NULL REFERENCES parking_locations(id),
    slot_id     INTEGER NOT NULL REFERENCES slots(id),
    user_name   TEXT    NOT NULL,
    hours       INTEGER NOT NULL,
    amount      REAL    NOT NULL,
    status      TEXT    NOT NULL DEFAULT 'active',
    created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
);

CREATE INDEX IF NOT EXISTS idx_bookings_user ON bookings(user_name);
CREATE INDEX IF NOT EXISTS idx_bookings_status ON bookings(status);

-- ---------------------------------------------------------------------------
-- Waiting list for full lots (a FIFO queue per location in memory).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS waiting_list (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    location_id INTEGER NOT NULL REFERENCES parking_locations(id),
    user_name   TEXT    NOT NULL,
    created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
);

-- ===========================================================================
--  SAMPLE DATA - 10 parking locations around Shimla and nearby towns
-- ===========================================================================
INSERT OR IGNORE INTO parking_locations
    (id, name, address, area, lat, lng, total_slots, occupancy_seed,
     price_per_hour, price_3_hours, price_per_day, type, node_id, rating, open_hours)
VALUES
    (1,  'The Ridge Parking',         'The Ridge, Mall Road, Shimla 171001',            'The Ridge',      31.1041, 77.1739,  45, 55, 40, 110, 320, 'Public',  101, 4.5, '6 AM - 11 PM'),
    (2,  'Lakkar Bazaar Parking',     'Lakkar Bazaar Road, Shimla 171001',              'Lakkar Bazaar',  31.1047, 77.1828,  40, 70, 35,  95, 280, 'Public',  102, 4.2, '7 AM - 10 PM'),
    (3,  'Sanjauli Parking',          'Sanjauli Chowk, Shimla 171006',                  'Sanjauli',       31.1006, 77.1929,  50, 45, 30,  80, 240, 'Public',  103, 4.0, '24x7'),
    (4,  'ISBT Tutikandi Parking',    'ISBT Complex, Tutikandi, Shimla 171004',         'Tutikandi',      31.0903, 77.1447,  80, 100, 25, 65, 200, 'Public',  104, 4.1, '5 AM - 12 AM'),
    (5,  'Victory Tunnel Parking',    'Circular Road, Victory Tunnel, Shimla 171003',   'Circular Road',  31.1043, 77.1655,  30, 35, 32,  85, 250, 'Public',  105, 3.9, '24x7'),
    (6,  'Jakhoo Temple Parking',     'Jakhoo Road, Shimla 171001',                     'Jakhoo',         31.1003, 77.1855,  25, 95, 45, 120, 360, 'Private', 106, 4.4, '6 AM - 9 PM'),
    (7,  'Chhota Shimla Parking',     'Chhota Shimla, Shimla 171002',                   'Chhota Shimla',  31.0900, 77.1620,  40, 75, 28,  75, 220, 'Private', 107, 4.0, '24x7'),
    (8,  'New Shimla Parking',        'New Shimla Sector 1, Shimla 171009',             'New Shimla',     31.0794, 77.1620,  60, 20, 22,  60, 180, 'Private', 108, 4.2, '24x7'),
    (9,  'Summer Hill Parking',       'Summer Hill, Shimla 171005',                     'Summer Hill',    31.1023, 77.1390,  35, 100, 20, 55, 170, 'Public',  109, 3.8, '24x7'),
    (10, 'Kufri Parking',             'Kufri Main Road, Shimla 171012',                 'Kufri',          31.0979, 77.2675,  90, 30, 18,  48, 150, 'Private', 110, 4.6, '9 AM - 8 PM');

-- Landmarks (where a user can start their search) + intermediate junctions.
INSERT OR IGNORE INTO graph_nodes (id, name, lat, lng, kind) VALUES
    (1,  'The Ridge',                31.1041, 77.1737, 'landmark'),
    (2,  'Mall Road',                31.1043, 77.1745, 'landmark'),
    (3,  'ISBT Tutikandi',           31.0903, 77.1447, 'landmark'),
    (4,  'Sanjauli Chowk',           31.1006, 77.1930, 'landmark'),
    (5,  'Victory Tunnel',           31.1043, 77.1655, 'landmark'),
    (6,  'Chhota Shimla',            31.0900, 77.1620, 'landmark'),
    (7,  'New Shimla',               31.0794, 77.1620, 'landmark'),
    (8,  'Summer Hill',              31.1023, 77.1390, 'landmark'),
    (9,  'Kufri',                    31.0979, 77.2680, 'landmark'),
    (10, 'Jakhoo Temple',            31.1003, 77.1855, 'landmark'),
    (11, 'Shimla Railway Station',   31.1036, 77.1590, 'landmark'),
    (12, 'Dhalli Junction',          31.0870, 77.1200, 'landmark'),
    (101, 'The Ridge Parking',       31.1041, 77.1739, 'parking'),
    (102, 'Lakkar Bazaar Parking',   31.1047, 77.1828, 'parking'),
    (103, 'Sanjauli Parking',        31.1006, 77.1929, 'parking'),
    (104, 'ISBT Tutikandi Parking',  31.0903, 77.1447, 'parking'),
    (105, 'Victory Tunnel Parking',  31.1043, 77.1655, 'parking'),
    (106, 'Jakhoo Temple Parking',   31.1003, 77.1855, 'parking'),
    (107, 'Chhota Shimla Parking',   31.0900, 77.1620, 'parking'),
    (108, 'New Shimla Parking',      31.0794, 77.1620, 'parking'),
    (109, 'Summer Hill Parking',     31.1023, 77.1390, 'parking'),
    (110, 'Kufri Parking',           31.0979, 77.2675, 'parking');

-- Road connections (stored in both directions).  Distances are road kilometres.
INSERT OR IGNORE INTO graph_edges (from_node, to_node, distance_km) VALUES
    -- landmark <-> landmark (Mall Road, Cart Road, NH-5 ...)
    (1, 2, 0.3),    (2, 1, 0.3),
    (1, 10, 3.0),   (10, 1, 3.0),
    (1, 11, 2.0),   (11, 1, 2.0),
    (1, 4, 4.0),    (4, 1, 4.0),
    (2, 11, 1.8),   (11, 2, 1.8),
    (2, 9, 14.0),   (9, 2, 14.0),
    (11, 5, 0.9),   (5, 11, 0.9),
    (11, 8, 4.0),   (8, 11, 4.0),
    (5, 6, 3.5),    (6, 5, 3.5),
    (5, 4, 3.5),    (4, 5, 3.5),
    (6, 7, 3.0),    (7, 6, 3.0),
    (6, 3, 2.5),    (3, 6, 2.5),
    (6, 4, 4.5),    (4, 6, 4.5),
    (3, 12, 4.5),   (12, 3, 4.5),
    (12, 7, 2.5),   (7, 12, 2.5),
    (12, 8, 6.0),   (8, 12, 6.0),
    (7, 8, 5.5),    (8, 7, 5.5),
    (9, 12, 11.0),  (12, 9, 11.0),
    (9, 1, 13.5),   (1, 9, 13.5),
    -- landmark <-> parking entrance
    (1, 101, 0.15), (101, 1, 0.15),
    (2, 102, 0.8),  (102, 2, 0.8),
    (4, 103, 0.3),  (103, 4, 0.3),
    (3, 104, 0.1),  (104, 3, 0.1),
    (5, 105, 0.2),  (105, 5, 0.2),
    (10, 106, 0.4), (106, 10, 0.4),
    (6, 107, 0.3),  (107, 6, 0.3),
    (7, 108, 0.4),  (108, 7, 0.4),
    (8, 109, 0.2),  (109, 8, 0.2),
    (9, 110, 0.3),  (110, 9, 0.3),
    -- short cuts between car parks
    (101, 102, 0.9), (102, 101, 0.9),
    (101, 105, 0.8), (105, 101, 0.8),
    (103, 107, 4.0), (107, 103, 4.0),
    (108, 109, 5.0), (109, 108, 5.0);
