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

echo
echo "stdlib only (parse + resolve + typecheck of thirdparty/lib/esnext.full.d.ts and everything it references;"
echo "tttt reports name errors there, so --keep-going. $(cat thirdparty/lib/*.d.ts | wc -l) lines):"
./release/tttt -q --time --keep-going --no-lib thirdparty/lib/esnext.full.d.ts 2>&1 > /dev/null | grep -a '^time:' | sed 's/^/  /'
