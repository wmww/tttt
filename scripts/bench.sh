#!/usr/bin/env bash
# Perf benchmark: tttt vs tsc (JS, thirdparty/tsc) vs tsgo (TypeScript 7 native, whatever `tsc` is on PATH if >= 7).
# Wall time and peak RSS, best and median of RUNS runs, plus tttt's phase split, on the pinned corpora. See
# agents/measurements.md for recorded numbers and caveats.
#
#   scripts/bench.sh [--runs N]
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."

RUNS=5
while test $# -gt 0; do
    case "$1" in
        --runs) RUNS="$2"; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 1 ;;
    esac
done

if test ! -e release; then
    meson setup release --buildtype release
fi
ninja -C release > /dev/null
thirdparty/ensure-tsc.sh

mkdir -p build/bench

# The pinned benchmark: tsc's own compiler sources, the same files every session (scripts/realworld.py is the correctness
# view of the same run). Everyone runs from its tsconfig. tttt exits 1: everything it reports there is a bug
REAL=thirdparty/tsc/src/compiler
# Its tsconfig is composite: without these tsc writes a tsbuildinfo and the next run checks nothing. Its declaration
# and isolatedDeclarations stay on: with noEmit and no errors tsc reports the declaration diagnostics, which tttt
# computes too (plan 40)
REAL_ARGS="--noEmit --composite false --incremental false --emitDeclarationOnly false"
echo
echo "real corpus: $REAL, $(find $REAL -name '*.ts' | wc -l) files, $(find $REAL -name '*.ts' | xargs cat | wc -l) lines, $(find $REAL -name '*.ts' | xargs cat | wc -c) bytes"
measure "tttt" ./release/tttt -q --keep-going -p $REAL
measure "tsc (node)" node $NODE_HEAP thirdparty/tsc/built/local/tsc.js $REAL_ARGS -p $REAL
if command -v tsc > /dev/null && test "$(tsc --version | sed 's/[^0-9]*\([0-9]*\).*/\1/')" -ge 7; then
    measure "tsgo" tsc $REAL_ARGS -p $REAL
    measure "tsgo single-threaded" tsc $REAL_ARGS --singleThreaded -p $REAL
fi
echo "tttt phases (one run):"
./release/tttt -q --time --keep-going -p $REAL 2>&1 > /dev/null | grep -a '^time:' | sed 's/^/  /'

# The modern corpora (plan 17, thirdparty/ensure-corpora.sh): type-level heavy, clean under tsc 4d4f005c as pinned
# (plan 48 stage 5); tttt is clean on them too but for a few (scripts/realworld.py --corpus NAME). tsgo 7 runs on the same
# configs (since the re-pin none sets baseUrl or node10); it isn't clean on webpack (JS: TS 7's reduced JS support),
# vue, drizzle and effect (TS 7 differences, plan 48), so its times there may not be of a full check. zod (plan 19) is
# checked through its tsconfig.test.json, webpack (plan 33, checkJs) and type-fest through their own tsconfig, the rest
# through our tsconfig.corpus.json. VS Code's src (3.41M lines) is the scale benchmark
thirdparty/ensure-corpora.sh
REACT=bulletproof-react/apps/react-vite/tsconfig.corpus.json
NEST=nestjs-boilerplate/tsconfig.corpus.json
for CORPUS in type-fest valibot/library/tsconfig.corpus.json $REACT $NEST webpack zod/packages/zod/tsconfig.test.json xstate/packages/core/tsconfig.corpus.json drizzle/drizzle-orm/tsconfig.corpus.json vue/tsconfig.corpus.json effect/packages/effect/tsconfig.corpus.json vscode/src/tsconfig.corpus.json excalidraw/tsconfig.corpus.json tanstack-router/tsconfig.corpus.json; do
    DIR=thirdparty/corpora/$CORPUS
    TSGO_ARGS="-p $DIR"
    echo
    echo "corpus $CORPUS: $(node thirdparty/tsc/built/local/tsc.js -p $DIR --noEmit --listFilesOnly | grep -c .) files incl. dependencies and libs"
    measure "tttt" ./release/tttt -q --keep-going -p $DIR
    measure "tsc (node)" node $NODE_HEAP thirdparty/tsc/built/local/tsc.js --noEmit -p $DIR
    if command -v tsc > /dev/null && test "$(tsc --version | sed 's/[^0-9]*\([0-9]*\).*/\1/')" -ge 7; then
        measure "tsgo" tsc --noEmit $TSGO_ARGS
        measure "tsgo single-threaded" tsc --noEmit --singleThreaded $TSGO_ARGS
    fi
    echo "tttt phases (one run):"
    (./release/tttt -q --time --keep-going -p $DIR 2>&1 > /dev/null || true) | grep -a '^time:' | sed 's/^/  /'
done

# Project references (plan 41): -b on TypeScript's own src/, 13 projects. tsc -b and tsgo -b emit (a referencing
# project reads the .d.ts files of what it references; noEmit isn't allowed in a referenced project), so they run on a
# /tmp copy, from scratch each time (built/ removed). tttt reads referenced projects from their sources, checks two
# projects at a time with 4 threads, and exits 1 (the 12 diagnostics scripts/realworld.py --corpus tsc-src lists). tsgo
# exits 2 on one TS 7 difference in harness/
BUILD=/tmp/tttt-bench-tsc-b
rm -rf $BUILD && mkdir -p $BUILD
cp -r thirdparty/tsc/src thirdparty/tsc/scripts thirdparty/tsc/package.json $BUILD/
ln -s "$PWD/thirdparty/tsc/node_modules" $BUILD/node_modules
echo
echo "-b thirdparty/tsc/src: $(find thirdparty/tsc/src -name '*.ts' | wc -l) files, $(find thirdparty/tsc/src -name tsconfig.json | wc -l) configs"
measure "tttt" ./release/tttt -q --keep-going -b thirdparty/tsc/src
measure "tsc -b (node)" sh -c "rm -rf $BUILD/built && node thirdparty/tsc/built/local/tsc.js -b $BUILD/src"
if command -v tsc > /dev/null && test "$(tsc --version | sed 's/[^0-9]*\([0-9]*\).*/\1/')" -ge 7; then
    measure "tsgo -b" sh -c "rm -rf $BUILD/built && tsc -b $BUILD/src"
    measure "tsgo -b single-threaded" sh -c "rm -rf $BUILD/built && tsc -b --singleThreaded $BUILD/src"
fi
echo "tttt phases (one run with --builders 1, summed over the projects):"
(./release/tttt -q --time --keep-going --builders 1 -b thirdparty/tsc/src 2>&1 > /dev/null || true) | grep -a '^time:' | grep -v checker |
    awk '{ name = $0; sub(/^time: /, "", name); sub(/ [0-9.]+ ms.*/, "", name); match($0, /[0-9.]+ ms/);
        sum[name] += substr($0, RSTART, RLENGTH - 3) } END { for (n in sum) printf "  %s %.0f ms\n", n, sum[n] }'
rm -rf $BUILD

echo
# The lib files esnext.full.d.ts references, transitively. They're given as root files: --no-lib also ignores
# `/// <reference lib>`, and lib files loaded as libs aren't typechecked
declare -A LIB_SEEN
lib_closure() {
    local f="thirdparty/lib/$1.d.ts"
    [[ -n "${LIB_SEEN[$1]:-}" ]] && return; LIB_SEEN[$1]=1
    for r in $(grep -o '<reference lib="[^"]*"' "$f" | sed 's/.*lib="\(.*\)"/\1/'); do lib_closure "$r"; done
    echo "$f"
}
LIB_FILES=($(lib_closure esnext.full))
echo "stdlib only (parse + resolve + typecheck of esnext.full.d.ts and the ${#LIB_FILES[@]} lib files it references,"
echo "$(cat "${LIB_FILES[@]}" | wc -l) lines):"
./release/tttt -q --time --keep-going --no-lib "${LIB_FILES[@]}" 2>&1 > /dev/null | grep -a '^time:' | sed 's/^/  /'
