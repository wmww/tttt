#!/usr/bin/env bash
# tsgo's suite baselines for arbitrary test files (plan 54, test/oracle/probe_test.go): scripts/tsgoprobe.sh DIR runs
# tsgo's compiler test runner on every DIR/tests/cases/{compiler,conformance}/*.ts(x) and writes what the native
# suite's reference baselines would be into DIR/baselines/local/{compiler,conformance} (.errors.txt only when there
# are errors, .types/.symbols unless @noTypesAndSymbols, .js...). DIR/tests/lib (the suite's /.lib) is linked in if
# missing. The runner's chatter goes to DIR/probe.log. `--rerun`: run again after each line of stdin (test/diffapp).
#
# Loud, never silent: a test configuration running over TTTT_PROBE_TIMEOUT seconds (default 120) is reported on
# stderr ("tsgoprobe: TIMEOUT FILE"), listed in DIR/timedout.txt, set aside and the others rerun (the runner can't
# kill one test); the exit status is then 3 and that test has no baselines, which callers must not read as "tsgo
# reports nothing". A progress line every TTTT_PROBE_PROGRESS (default 500) finished configurations. Exit 4 when the
# runner died otherwise (a panic outside a test: see DIR/probe.log). TTTT_PROBE_BIN: another probe binary.
set -euo pipefail
ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
test -n "${TTTT_PROBE_BIN:-}" || "$ROOT/thirdparty/ensure-typescript.sh"
PROBE=${TTTT_PROBE_BIN:-$ROOT/thirdparty/typescript/built/tttt-probe.test}
DIR="$(realpath "$1")"
test -e "$DIR/tests/lib" || ln -s "$ROOT/thirdparty/typescript/tsc/testdata/tests/lib" "$DIR/tests/lib"
if test "${2:-}" = --rerun; then export TTTT_PROBE_RERUN=1; fi
rm -f "$DIR/timedout.txt"
ASIDE="$DIR/timedout"
# (Background jobs get /dev/null as stdin: --rerun reads the caller's)
exec 3<&0
while true; do
    # (Exit status 1 is every baseline being "new"; a panic in a test is reported in the log as a failure too)
    set +e
    TTTT_TESTDATA="$DIR" "$PROBE" -test.run '^TestTtttProbe$' -test.timeout 0 <&3 > "$DIR/probe.log" 2>&1 &
    pid=$!
    tail -n +1 --pid=$pid -f "$DIR/probe.log" 2>/dev/null | grep --line-buffered -F '[tttt runner]'
    wait $pid
    status=$?
    set -e
    late=$(grep -oE '^\[tttt runner\] TIMEOUT \S+' "$DIR/probe.log" | awk '{print $4}' | sort -u || true)
    if test -z "$late"; then
        break
    fi
    mkdir -p "$ASIDE"
    for f in $late; do
        name=$(basename "$f")
        src=$(find "$DIR/tests/cases" -name "$name" -print -quit)
        echo "tsgoprobe: TIMEOUT $name (over ${TTTT_PROBE_TIMEOUT:-120} s): no baselines for it; rerunning the others" >&2
        echo "$name" >> "$DIR/timedout.txt"
        if test -n "$src"; then
            mkdir -p "$ASIDE/$(dirname "${src#$DIR/}")"
            mv "$src" "$ASIDE/${src#$DIR/}"
        fi
    done
    test -z "${TTTT_PROBE_RERUN:-}" || break
done
# (The timed-out tests back where they were)
if test -d "$ASIDE"; then
    ( cd "$ASIDE" && find . -type f ) | while read -r f; do mv "$ASIDE/$f" "$DIR/$f"; done
    rm -r "$ASIDE"
fi
if test -s "$DIR/timedout.txt"; then exit 3; fi
if test "$status" -gt 1 || ! grep -qF '[tttt runner] Done running tests' "$DIR/probe.log"; then
    echo "tsgoprobe: the runner died (exit $status), see $DIR/probe.log:" >&2
    tail -5 "$DIR/probe.log" >&2
    exit 4
fi
