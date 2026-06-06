#!/bin/sh
# test/run_sqlite_tests.sh -- run test/test_sqlite.sql against an
# in-memory sqlite3, with the freshly-built extension loaded.
#
# Requires sqlite3 in PATH.  Builds the extension first.

set -eu

case "$(uname -s 2>/dev/null || echo unknown)" in
    MINGW*|MSYS*|CYGWIN*) EXT=./acmeid ;;     # SQLite appends .dll
    Darwin*)              EXT=./acmeid ;;     # SQLite appends .dylib
    *)                    EXT=./acmeid ;;     # SQLite appends .so
esac

if ! command -v sqlite3 >/dev/null 2>&1; then
    echo "test_sqlite: sqlite3 not on PATH; skipping" >&2
    exit 0
fi

OUT=$(printf '.load %s\n' "$EXT" | cat - test/test_sqlite.sql | sqlite3 :memory:)

echo "$OUT"

if echo "$OUT" | grep -q FAIL; then
    echo "test_sqlite: at least one assertion FAILED" >&2
    exit 1
fi

NPASS=$(echo "$OUT" | grep -c ': OK' || true)
echo "test_sqlite: $NPASS passed, 0 failed"
