#!/usr/bin/env bash
# Regenerates the expected baselines of tttt's own regression tests (test/cases/tttt*.ts, tsc-suite style: `// @option`
# and `// @filename` headers) from tsgo 7.1, the oracle: test/baselines/NAME.{errors.txt,symbols,types} (.errors.txt
# only when there are errors, none of .symbols/.types with @noTypesAndSymbols, NAME.js (the dts component) with
# @declaration or @composite, a NAME(option=value) set per
# configuration; an empty NAME.noerrors when tsgo reports no errors). baseline_tests runs these tests with the suite (`--own-tests`, on by default).
# Usage: scripts/owntests.sh [test/cases/ttttFoo.ts...] (default: all of them; old baselines of those tests are replaced)
#
# A test tsgo didn't finish (scripts/tsgoprobe.sh's timeout) or produced no baseline for (and no @noTypesAndSymbols
# to explain it) FAILS: it is printed, its old baselines are kept, nothing is written for it (never a .noerrors: a
# hang once starved 8 tests that were then recorded as "tsgo reports nothing"), and the exit status is 1.
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
TEST_DIR=$(mktemp -d /tmp/owntests.XXXXXX)
trap 'rm -rf "$TEST_DIR"' EXIT
mkdir -p "$TEST_DIR/tests/cases/conformance" test/baselines
if test $# -eq 0; then set -- test/cases/*.ts test/cases/*.tsx; fi
NAMES=()
for f in "$@"; do
    test -e "$f" || continue
    name=$(basename "$f"); name=${name%.*}
    case "$name" in tttt*|bun*|npm_*) ;; *) echo "$f: own tests must be named tttt* (or bun*: bun's differential cases, npm_*: the npm sample's repros)" >&2; exit 1;; esac
    cp "$f" "$TEST_DIR/tests/cases/conformance/"
    NAMES+=("$name")
done
set +e
scripts/tsgoprobe.sh "$TEST_DIR" >/dev/null
probe=$?
set -e
if test "$probe" != 0 && test "$probe" != 3; then
    echo "owntests.sh: tsgoprobe.sh failed (exit $probe): no baselines written" >&2
    exit 1
fi
LOCAL="$TEST_DIR/baselines/local/conformance"
mkdir -p "$LOCAL"
# The tests whose baselines can be trusted: finished, and with a baseline unless @noTypesAndSymbols explains none
declare -A FAILED=()
for f in "$@"; do
    test -e "$f" || continue
    name=$(basename "$f"); name=${name%.*}
    if grep -qxF "$(basename "$f")" "$TEST_DIR/timedout.txt" 2>/dev/null; then
        FAILED[$name]="tsgo didn't finish it (scripts/tsgoprobe.sh's timeout, TTTT_PROBE_TIMEOUT)"
    elif ! ls "$LOCAL" | grep -qE "^$name(\(.*\))?\.(types|symbols|errors\.txt)\$" \
            && ! grep -qiE '@noTypesAndSymbols: *true' "$f"; then
        FAILED[$name]="tsgo wrote no baseline for it (see the runner's log: a panic? a skipped configuration?)"
    fi
done
for name in "${NAMES[@]}"; do
    test -z "${FAILED[$name]:-}" || continue
    rm -f test/baselines/"$name".* test/baselines/"$name"\(*
done
ok() {  # BASELINE FILE: of a test that didn't fail
    local base=$(basename "$1"); base=${base%%(*}; base=${base%%.*}
    test -z "${FAILED[$base]:-}"
}
for f in "$LOCAL"/*; do
    test -e "$f" || continue
    ok "$f" || continue
    # (Their header names the file where it is in this repo, like the suite's do)
    case "$f" in *.errors.txt|*.symbols|*.types)
        sed '1s|^//// \[tests/cases/conformance/|//// [test/cases/|' "$f" >test/baselines/"$(basename "$f")";;
    # (Declaration maps, in the dts component too: of tests with declarationMap)
    *.js.map)
        base=$(basename "$f" .js.map); base=${base%%(*}
        if grep -qiE '@declarationMap: *true' test/cases/"$base".ts* 2>/dev/null; then
            cp "$f" test/baselines/"$(basename "$f")"
        fi;;
    # (The emit, for the dts component: only of tests that emit declarations)
    *.js)
        base=$(basename "$f" .js); base=${base%%(*}
        if grep -qiE '@(declaration|composite): *true' test/cases/"$base".ts* 2>/dev/null; then
            sed '1s|^//// \[tests/cases/conformance/|//// [test/cases/|' "$f" >test/baselines/"$(basename "$f")"
        fi;; esac
done
# tsgo reported nothing: say so explicitly (baseline_tests fails an own test with neither file)
for f in "$@"; do
    test -e "$f" || continue
    name=$(basename "$f"); name=${name%.*}
    test -z "${FAILED[$name]:-}" || continue
    configs=$(ls "$LOCAL" | grep -E "^$name(\(.*\))?\.(types|errors\.txt)\$" | sed -E 's/\.(types|errors\.txt)$//' | sort -u) || true
    # (No baseline at all, @noTypesAndSymbols and clean: the marker is the plain name's, wrong for a test with several
    # configurations, whose names only the harness knows)
    if test -z "$configs"; then configs=$name; fi
    for c in $configs; do
        test -e test/baselines/"$c".errors.txt || : >test/baselines/"$c".noerrors
    done
done
ls "$LOCAL" | grep -E '\.(errors\.txt|symbols|types)$' | wc -l | xargs echo "baselines written:"
if test ${#FAILED[@]} -gt 0; then
    for name in "${!FAILED[@]}"; do echo "owntests.sh: FAILED $name: ${FAILED[$name]}; its old baselines are kept" >&2; done
    exit 1
fi
