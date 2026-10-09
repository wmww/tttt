#!/usr/bin/env bash
# tsgo's suite baselines for arbitrary test files (plan 54, test/oracle/probe_test.go): scripts/tsgoprobe.sh DIR runs
# tsgo's compiler test runner on every DIR/tests/cases/{compiler,conformance}/*.ts(x) and writes what the native
# suite's reference baselines would be into DIR/baselines/local/{compiler,conformance} (.errors.txt only when there
# are errors, .types/.symbols unless @noTypesAndSymbols, .js...). DIR/tests/lib (the suite's /.lib) is linked in if
# missing. The runner's chatter goes to DIR/probe.log. `--rerun`: run again after each line of stdin (test/diffapp).
set -euo pipefail
ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
"$ROOT/thirdparty/ensure-typescript.sh"
DIR="$(realpath "$1")"
test -e "$DIR/tests/lib" || ln -s "$ROOT/thirdparty/typescript/tsc/testdata/tests/lib" "$DIR/tests/lib"
if test "${2:-}" = --rerun; then export TTTT_PROBE_RERUN=1; fi
# (Exit status 1 is every baseline being "new"; a panic in tsgo is reported in the log as a failure too)
TTTT_TESTDATA="$DIR" "$ROOT/thirdparty/typescript/built/tttt-probe.test" -test.run '^TestTtttProbe$' -test.timeout 0 \
    | tee "$DIR/probe.log" | grep --line-buffered -F '[tttt runner]' || true
