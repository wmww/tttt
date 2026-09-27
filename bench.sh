#!/usr/bin/env bash
# Perf benchmark: tttt vs tsc (JS, thirdparty/tsc) vs tsgo (TypeScript 7 native, whatever `tsc` is on PATH if >= 7).
# Wall time and peak RSS, best and median of RUNS runs, plus tttt's phase split. See bench/make_corpus.py for what is
# being checked and agents/measurements.md for recorded numbers and caveats.
#
#   ./bench.sh [--copies N] [--runs N]
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )"

COPIES=200
RUNS=5
while test $# -gt 0; do
    case "$1" in
        --copies) COPIES="$2"; shift 2 ;;
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
bench/make_corpus.py "$COPIES"

# The same libs tttt's main.c loads, so everyone checks the same declarations
LIBS=es5,dom,webworker.importscripts,scripthost,es2015.collection,es2015.iterable,es2015.generator
TSC_ARGS="--noEmit --strict --target esnext --lib $LIBS"

# measure NAME CMD...: runs CMD RUNS times, prints best/median wall seconds and peak RSS
measure() {
    local name="$1"; shift
    local walls=() rss=0 status=0
    for _ in $(seq "$RUNS"); do
        /usr/bin/time -f '%e %M' -o build/bench/time.txt "$@" > build/bench/last_output.txt 2>&1 || status=$?
        read -r wall mem < <(tail -1 build/bench/time.txt)
        walls+=("$wall")
        if test "$mem" -gt "$rss"; then rss="$mem"; fi
    done
    local sorted=($(printf '%s\n' "${walls[@]}" | sort -n))
    printf '%-22s best %7.3fs  median %7.3fs  peak RSS %6d MB  exit %d\n' \
        "$name" "${sorted[0]}" "${sorted[$((RUNS / 2))]}" "$((rss / 1024))" "$status"
}

FILES=(build/bench/corpus/*.ts)
echo
echo "corpus: ${#FILES[@]} files, $(cat "${FILES[@]}" | wc -l) lines, $(cat "${FILES[@]}" | wc -c) bytes; $RUNS runs each"
measure "tttt" ./release/tttt -q "${FILES[@]}"
measure "tsc $(node thirdparty/tsc/lib/tsc.js --version | cut -d' ' -f2) (node)" node thirdparty/tsc/lib/tsc.js $TSC_ARGS "${FILES[@]}"
if command -v tsc > /dev/null && test "$(tsc --version | sed 's/[^0-9]*\([0-9]*\).*/\1/')" -ge 7; then
    measure "tsgo $(tsc --version | cut -d' ' -f2)" tsc $TSC_ARGS "${FILES[@]}"
    measure "tsgo single-threaded" tsc $TSC_ARGS --singleThreaded "${FILES[@]}"
else
    echo "tsgo: no TypeScript >= 7 on PATH, skipped"
fi

echo
echo "tttt phases (one run):"
./release/tttt -q --time "${FILES[@]}" 2>&1 > /dev/null | grep -a '^time:' | sed 's/^/  /'

# The pinned benchmark: tsc's own compiler sources, the same files every session (./realworld.py is the correctness
# view of the same run). Everyone runs from its tsconfig. tttt exits 1: everything it reports there is a bug
REAL=thirdparty/tsc/src/compiler
# Its tsconfig is composite: without these tsc writes a tsbuildinfo and the next run checks nothing
REAL_ARGS="--noEmit --composite false --incremental false --declaration false --declarationMap false --emitDeclarationOnly false --isolatedDeclarations false"
echo
echo "real corpus: $REAL, $(find $REAL -name '*.ts' | wc -l) files, $(find $REAL -name '*.ts' | xargs cat | wc -l) lines, $(find $REAL -name '*.ts' | xargs cat | wc -c) bytes"
measure "tttt" ./release/tttt -q --keep-going -p $REAL
measure "tsc (node)" node thirdparty/tsc/lib/tsc.js $REAL_ARGS -p $REAL
if command -v tsc > /dev/null && test "$(tsc --version | sed 's/[^0-9]*\([0-9]*\).*/\1/')" -ge 7; then
    measure "tsgo" tsc $REAL_ARGS -p $REAL
    measure "tsgo single-threaded" tsc $REAL_ARGS --singleThreaded -p $REAL
fi
echo "tttt phases (one run):"
./release/tttt -q --time --keep-going -p $REAL 2>&1 > /dev/null | grep -a '^time:' | sed 's/^/  /'

# The modern corpora (plan 17, thirdparty/ensure-corpora.sh): type-level heavy, clean under tsc 5.8. tttt still reports
# plenty there (./realworld.py --corpus NAME), so these times are of an incomplete check. tsgo 7 dropped
# moduleResolution node10, so it gets bundler/esnext instead (valibot: 2 TS2554 of its own, a TS 7 change), and
# removed baseUrl, so the React app (plan 25) gets a generated tsconfig.tsgo.json that unsets it (its paths are
# relative to the tsconfig either way). The NestJS app (plan 31) is `incremental`: without --incremental false tsc
# writes a tsbuildinfo and later runs check nothing; tsgo 7 also defaults to strict, which its tsconfig doesn't unset.
# webpack (plan 33) is checkJs JavaScript; tsgo 7 rejects 3 of its JSDoc comments (TS1003), so its time may not be of
# a full check
thirdparty/ensure-corpora.sh
REACT=bulletproof-react/apps/react-vite
NEST=nestjs-boilerplate
echo '{"extends": "./tsconfig.json", "compilerOptions": {"baseUrl": null}}' > thirdparty/corpora/$REACT/tsconfig.tsgo.json
for CORPUS in type-fest valibot/library $REACT $NEST webpack; do
    DIR=thirdparty/corpora/$CORPUS
    TSGO_ARGS="--moduleResolution bundler --module esnext -p $DIR"
    TSC_EXTRA=""
    if test $CORPUS = $REACT; then TSGO_ARGS="-p $DIR/tsconfig.tsgo.json"; fi
    if test $CORPUS = webpack; then TSGO_ARGS="-p $DIR"; fi
    if test $CORPUS = $NEST; then TSC_EXTRA="--incremental false"; TSGO_ARGS="$TSGO_ARGS --incremental false --strict false"; fi
    echo
    echo "corpus $CORPUS: $(node thirdparty/tsc/lib/tsc.js -p $DIR --noEmit $TSC_EXTRA --listFilesOnly | grep -c .) files incl. dependencies and libs"
    measure "tttt" ./release/tttt -q --keep-going -p $DIR
    measure "tsc (node)" node thirdparty/tsc/lib/tsc.js --noEmit $TSC_EXTRA -p $DIR
    if command -v tsc > /dev/null && test "$(tsc --version | sed 's/[^0-9]*\([0-9]*\).*/\1/')" -ge 7; then
        measure "tsgo" tsc --noEmit $TSGO_ARGS
        measure "tsgo single-threaded" tsc --noEmit --singleThreaded $TSGO_ARGS
    fi
    echo "tttt phases (one run):"
    (./release/tttt -q --time --keep-going -p $DIR 2>&1 > /dev/null || true) | grep -a '^time:' | sed 's/^/  /'
done

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
