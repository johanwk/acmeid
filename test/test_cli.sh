#!/bin/sh
# test/test_cli.sh -- Black-box tests for the acmeid CLI.
#
# Assumes ./acmeid (or ./acmeid.exe) is built in the project root.

set -eu

PASS=0
FAIL=0

case "$(uname -s 2>/dev/null || echo unknown)" in
    MINGW*|MSYS*|CYGWIN*) ACMEID=./acmeid.exe ;;
    *)                    ACMEID=./acmeid     ;;
esac

if [ ! -x "$ACMEID" ]; then
    echo "test_cli: $ACMEID not found; run 'make cli' first" >&2
    exit 2
fi

check() {                                   # check "label" actual expected
    if [ "$2" = "$3" ]; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
        printf 'FAIL [%s]\n  got: %s\n  want: %s\n' "$1" "$2" "$3" >&2
    fi
}

check_zero() {                              # check_zero "label" exit-code
    if [ "$2" = 0 ]; then PASS=$((PASS + 1))
    else FAIL=$((FAIL + 1)); echo "FAIL [$1] exit=$2 (wanted 0)" >&2
    fi
}

check_nonzero() {
    if [ "$2" != 0 ]; then PASS=$((PASS + 1))
    else FAIL=$((FAIL + 1)); echo "FAIL [$1] exit=0 (wanted non-zero)" >&2
    fi
}

# -- mint + verify round-trips ---------------------------------------

ID=$("$ACMEID" mint -t C)
"$ACMEID" verify "$ID"
check_zero "mint -t C | verify"             $?

ID=$("$ACMEID" mint -t C -p ex:)
case "$ID" in ex:*) PASS=$((PASS + 1));;
              *) FAIL=$((FAIL + 1)); echo "FAIL prefix not retained: $ID" >&2;;
esac
"$ACMEID" verify "$ID"; check_zero "verify with prefix" $?

ID=$("$ACMEID" mint -t C -p ex: -l "Pitch 1.5 mm")
case "$ID" in ex:C_pitch*) PASS=$((PASS + 1));;
              *) FAIL=$((FAIL + 1)); echo "FAIL slug not retained: $ID" >&2;;
esac

ID=$("$ACMEID" mint -t C -p ex: -l Pitch -n 6)
LEN=${#ID}
# Expected length: 6 (prefix) + 2 + 5 + 4 + 6 + 1 = 24
check "len with -n 6" "$LEN" 24
"$ACMEID" verify "$ID"; check_zero "verify -n 6" $?

# Silent clamp: -n 0 and -n 99 should both succeed and verify.
ID=$("$ACMEID" mint -t C -n 0);  "$ACMEID" verify "$ID"; check_zero "-n 0 verify"  $?
ID=$("$ACMEID" mint -t C -n 99); "$ACMEID" verify "$ID"; check_zero "-n 99 verify" $?

# -- pipe round-trip --------------------------------------------------

"$ACMEID" mint -t C | "$ACMEID" verify
check_zero "mint | verify (pipe)" $?

# -- error paths ------------------------------------------------------

set +e
"$ACMEID" mint            >/dev/null 2>&1; check_nonzero "mint without -t"        $?
"$ACMEID" mint -t 1       >/dev/null 2>&1; check_nonzero "mint with bad type '1'" $?
"$ACMEID" verify nope     >/dev/null 2>&1; check_nonzero "verify gibberish"       $?
"$ACMEID" frob            >/dev/null 2>&1; check_nonzero "unknown subcommand"     $?
set -e

# -- batch ------------------------------------------------------------

FIXTURE=test/fixtures/labels.txt
mkdir -p test/fixtures
{
  echo "Pitch 1.5 mm"
  echo "Diameter 10 mm"
  echo ""
  echo "ISO 50001"
} > "$FIXTURE"

OUT=$("$ACMEID" batch -t C -p ex: < "$FIXTURE")
NLINES=$(printf '%s\n' "$OUT" | wc -l | tr -d ' ')
check "batch line count" "$NLINES" 4

# Every emitted ID (last tab-separated field) must verify.
echo "$OUT" | while IFS=$(printf '\t') read -r _ ID; do
    "$ACMEID" verify "$ID" || { echo "FAIL batch produced invalid id: $ID" >&2; exit 1; }
done

echo "test_cli: $PASS passed, $FAIL failed"
test "$FAIL" -eq 0
