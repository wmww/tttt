#!/usr/bin/env bash
# Regenerates the expected baselines of tttt's own regression tests (test/cases/tttt*.ts, tsc-suite style: `// @option`
# and `// @filename` headers) from tsgo 7.1, the oracle: test/baselines/NAME.{errors.txt,symbols,types} (.errors.txt
# only when there are errors, none of .symbols/.types with @noTypesAndSymbols, a NAME(option=value) set per
# configuration). baseline_tests runs these tests with the suite (`--own-tests`, on by default).
# Usage: scripts/owntests.sh [test/cases/ttttFoo.ts...] (default: all of them; old baselines of those tests are replaced)
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
TEST_DIR=$(mktemp -d /tmp/owntests.XXXXXX)
trap 'rm -rf "$TEST_DIR"' EXIT
mkdir -p "$TEST_DIR/tests/cases/conformance" test/baselines
if test $# -eq 0; then set -- test/cases/*.ts test/cases/*.tsx; fi
for f in "$@"; do
    test -e "$f" || continue
    name=$(basename "$f"); name=${name%.*}
    case "$name" in tttt*) ;; *) echo "$f: tttt's own tests must be named tttt*" >&2; exit 1;; esac
    cp "$f" "$TEST_DIR/tests/cases/conformance/"
    rm -f test/baselines/"$name".* test/baselines/"$name"\(*
done
scripts/tsgoprobe.sh "$TEST_DIR" >/dev/null
for f in "$TEST_DIR"/baselines/local/conformance/*; do
    # (Their header names the file where it is in this repo, like the suite's do)
    case "$f" in *.errors.txt|*.symbols|*.types)
        sed '1s|^//// \[tests/cases/conformance/|//// [test/cases/|' "$f" >test/baselines/"$(basename "$f")";; esac
done
ls "$TEST_DIR"/baselines/local/conformance | grep -E '\.(errors\.txt|symbols|types)$' | wc -l | xargs echo "baselines written:"
