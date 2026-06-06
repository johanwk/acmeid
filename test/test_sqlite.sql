-- test/test_sqlite.sql -- SQL-level smoke tests for the loadable extension.
--
-- The shell wrapper (run_sqlite_tests.sh) loads the extension via the
-- platform-appropriate .load command before piping this file to sqlite3.

.bail on
.headers off
.mode list

-- ------------------------------------------------------------------
--  1. UDF arities
-- ------------------------------------------------------------------
SELECT 'arity1: ' ||
       CASE WHEN acme_verify_id(acme_mint_id('C'))
            THEN 'OK' ELSE 'FAIL' END;

SELECT 'arity2: ' ||
       CASE WHEN acme_verify_id(acme_mint_id('C','ex:'))
            THEN 'OK' ELSE 'FAIL' END;

SELECT 'arity3: ' ||
       CASE WHEN acme_verify_id(acme_mint_id('C','ex:','Pitch 1.5 mm'))
            THEN 'OK' ELSE 'FAIL' END;

SELECT 'arity4: ' ||
       CASE WHEN acme_verify_id(acme_mint_id('C','ex:','Pitch',6))
            THEN 'OK' ELSE 'FAIL' END;

-- ------------------------------------------------------------------
--  2. Determinism trap: mint() must produce one ID per row.
--     SQLite would cache the result if the function were registered
--     with SQLITE_DETERMINISTIC, which would be silently wrong.
-- ------------------------------------------------------------------
WITH rows(n) AS (
    VALUES (1),(2),(3),(4),(5)
)
SELECT 'distinct-per-row: ' ||
       CASE WHEN COUNT(DISTINCT acme_mint_id('C')) = 5
            THEN 'OK' ELSE 'FAIL' END
FROM rows;

-- ------------------------------------------------------------------
--  3. Tampering invalidates.
-- ------------------------------------------------------------------
SELECT 'verify-tampered: ' ||
       CASE WHEN acme_verify_id('ex:C_029BGJH66') = 0
            THEN 'OK' ELSE 'FAIL' END;

-- ------------------------------------------------------------------
--  4. CHECK (acme_verify_id(id)) -- the spine-table pattern.
-- ------------------------------------------------------------------
CREATE TABLE dim_pitch (
    id    TEXT PRIMARY KEY CHECK (acme_verify_id(id)),
    label TEXT NOT NULL UNIQUE
);

WITH v(label) AS (
    VALUES
    ('Pitch 1.0 mm'),
    ('Pitch 1.25 mm'),
    ('Pitch 1.5 mm'),
    ('Pitch 1.75 mm'),
    ('Pitch 2.0 mm')
)
INSERT INTO dim_pitch(id, label)
SELECT acme_mint_id('C','ex:',v.label), v.label
FROM v
WHERE NOT EXISTS (SELECT 1 FROM dim_pitch p WHERE p.label = v.label);

SELECT 'spine-insert: ' ||
       CASE WHEN COUNT(*) = 5 THEN 'OK' ELSE 'FAIL' END
FROM dim_pitch;

-- Re-run: should add zero rows (idempotent).
WITH v(label) AS (
    VALUES
    ('Pitch 1.0 mm'),
    ('Pitch 1.25 mm'),
    ('Pitch 1.5 mm'),
    ('Pitch 1.75 mm'),
    ('Pitch 2.0 mm')
)
INSERT INTO dim_pitch(id, label)
SELECT acme_mint_id('C','ex:',v.label), v.label
FROM v
WHERE NOT EXISTS (SELECT 1 FROM dim_pitch p WHERE p.label = v.label);

SELECT 'spine-idempotent: ' ||
       CASE WHEN COUNT(*) = 5 THEN 'OK' ELSE 'FAIL' END
FROM dim_pitch;

-- The CHECK constraint must reject a tampered ID.  We use INSERT OR
-- IGNORE so SQLite swallows the constraint violation silently (the
-- test is whether the row exists afterwards, not whether sqlite3
-- exits with status 0).
INSERT OR IGNORE INTO dim_pitch(id, label)
VALUES ('ex:C_xxxxx0000ZZZZ0', 'tampered');

SELECT 'check-rejects-bad: ' ||
       CASE WHEN NOT EXISTS (SELECT 1 FROM dim_pitch WHERE label = 'tampered')
            THEN 'OK' ELSE 'FAIL' END;
