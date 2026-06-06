#!/bin/sh
# test/run_sqlite_tests.sh -- run test/test_sqlite.sql against an
# in-memory sqlite3, with the freshly-built extension loaded.
#
# Requires sqlite3 in PATH.  Builds the extension first.

set -eu

# Use the full filename rather than relying on SQLite to append the
# platform suffix.  On macOS the CLI binary `./acmeid` sits next to
# `./acmeid.dylib`, and `dlopen("./acmeid")` will happily open the
# PIE executable -- SQLite then looks for `sqlite3_acmeid_init` in
# the CLI and reports "symbol not found".
case "$(uname -s 2>/dev/null || echo unknown)" in
    MINGW*|MSYS*|CYGWIN*) EXT=./acmeid.dll ;;
    Darwin*)              EXT=./acmeid.dylib ;;
    *)                    EXT=./acmeid.so ;;
esac

if ! command -v sqlite3 >/dev/null 2>&1; then
    echo "test_sqlite: sqlite3 not on PATH; skipping" >&2
    exit 0
fi

# Detect a sqlite3 binary built with -DSQLITE_OMIT_LOAD_EXTENSION
# (Apple ships such a build in /usr/bin/sqlite3).  Probing with a
# bogus path is enough: a loader-enabled build reports a file-open
# error, a loader-disabled build reports "unknown command".
PROBE=$(printf '.load /nonexistent/path/acmeid_probe\n.quit\n' \
        | sqlite3 :memory: 2>&1 || true)
case "$PROBE" in
    *"unknown command"*|*"not authorized"*|*"extension loading"*[Dd]isabled*)
        echo "test_sqlite: sqlite3 in PATH has extension loading disabled; skipping" >&2
        echo "  (hint: on macOS, 'brew install sqlite' and prepend its bin to PATH)" >&2
        exit 0
        ;;
esac

OUT=$(printf '.load %s\n' "$EXT" | cat - test/test_sqlite.sql | sqlite3 :memory:)

echo "$OUT"

if echo "$OUT" | grep -q FAIL; then
    echo "test_sqlite: at least one assertion FAILED" >&2
    exit 1
fi

NPASS=$(echo "$OUT" | grep -c ': OK' || true)
echo "test_sqlite: $NPASS passed, 0 failed"
