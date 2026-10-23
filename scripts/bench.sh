#!/usr/bin/env bash
# Perf benchmark: tttt vs tsgo (the pinned TypeScript 7.1 oracle, thirdparty/typescript/built/tsc), both at their
# defaults (every core; tttt one checker per core for a big enough program, tsgo 4 checkers) and single-threaded.
# Wall time, CPU time (user + sys) and peak RSS, best and median of RUNS runs, plus tttt's phase split, on the pinned corpora. See
# agents/measurements.md for recorded numbers and caveats.
#
#   scripts/bench.sh [--runs N]
#   scripts/bench.sh --checkers "4 8 12 16"   # also tttt at each of these checker counts (plan 62: core scaling)
#   scripts/bench.sh --bun PATH               # also Bun's `bun check` (a Rust port of tsgo 7.0) at every core and at one
#                                             # (`npm pack @oven/bun-linux-x64@1.4.3`, untarred: package/bin/bun)
#   scripts/bench.sh --tscrs PATH             # also tsc-rs (pingdotgg/ts-rust, a Rust port of tsgo at our pin) at its
#                                             # defaults (4 checkers), --fast and single-threaded (`gh release download
#                                             # v0.7.0 -R pingdotgg/ts-rust -p 'tsc-rs-0.7.0-linux-x64.tar.gz'`: its tsc)
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."

RUNS=5
CHECKERS=""
BUN=""
TSCRS=""
while test $# -gt 0; do
    case "$1" in
        --runs) RUNS="$2"; shift 2 ;;
        --checkers) CHECKERS="$2"; shift 2 ;;
        --bun) BUN="$2"; shift 2 ;;
        --tscrs) TSCRS="$2"; shift 2 ;;
        *) echo "unknown argument $1" >&2; exit 1 ;;
    esac
done

# The measured build (scripts/fastbuild.sh --measured: pgo/tttt, LTO + PGO, until the laptop confirms BOLT)
TTTT=$(scripts/fastbuild.sh --measured)
thirdparty/ensure-typescript.sh
thirdparty/ensure-corpora.sh
TSGO=thirdparty/typescript/built/tsc

mkdir -p build/bench

# measure NAME CMD...: runs CMD RUNS times, prints best/median wall seconds, the median run's CPU seconds (user + sys,
# all threads: the cost of the defaults' parallelism, which wall hides) and peak RSS. tttt runs twice each time: its
# wall as it exits (the kernel's address-space teardown left to a detached reaper, main.c exit_detached()), its CPU
# with TTTT_SYNC_EXIT=1 (the teardown's sys counted, as every other tool's is)
measure() {
    local name="$1"; shift
    local runs=() rss=0 status=0
    for _ in $(seq "$RUNS"); do
        /usr/bin/time -f '%e %M %U %S' -o build/bench/time.txt "$@" > build/bench/last_output.txt 2>&1 || status=$?
        read -r wall mem user sys < <(tail -1 build/bench/time.txt)
        if test "$1" = "$TTTT"; then
            TTTT_SYNC_EXIT=1 /usr/bin/time -f '%e %M %U %S' -o build/bench/time.txt "$@" > /dev/null 2>&1 || true
            read -r _ _ user sys < <(tail -1 build/bench/time.txt)
        fi
        runs+=("$wall $(echo "$user + $sys" | bc)")
        if test "$mem" -gt "$rss"; then rss="$mem"; fi
    done
    local sorted=() ; mapfile -t sorted < <(printf '%s\n' "${runs[@]}" | sort -n)
    local median=(${sorted[$((RUNS / 2))]})
    printf '%-24s best %7.3fs  median %7.3fs  cpu %7.2fs  peak RSS %6d MB  exit %d\n' \
        "$name" "${sorted[0]%% *}" "${median[0]}" "${median[1]}" "$((rss / 1024))" "$status"
}

# The pinned benchmark: TypeScript 6's compiler sources (the TypeScript6 corpus), the same files every session
# (scripts/realworld.py is the correctness view of the same run). Everyone runs from its tsconfig
REAL=thirdparty/corpora/TypeScript6/src/compiler
# Its tsconfig is composite: without these tsc writes a tsbuildinfo and the next run checks nothing. Its declaration
# and isolatedDeclarations stay on: with noEmit and no errors tsc reports the declaration diagnostics, which tttt
# computes too (plan 40)
REAL_ARGS="--noEmit --composite false --incremental false --emitDeclarationOnly false"
echo
echo "real corpus: $REAL, $(find $REAL -name '*.ts' | wc -l) files, $(find $REAL -name '*.ts' | xargs cat | wc -l) lines, $(find $REAL -name '*.ts' | xargs cat | wc -c) bytes"
measure "tttt" $TTTT -q --keep-going --noEmit -p $REAL
measure "tttt single-threaded" $TTTT -q --keep-going --singleThreaded --noEmit -p $REAL
for K in $CHECKERS; do measure "tttt --checkers $K" $TTTT -q --keep-going --checkers $K --noEmit -p $REAL; done
measure "tsgo" $TSGO $REAL_ARGS -p $REAL
measure "tsgo single-threaded" $TSGO $REAL_ARGS --singleThreaded -p $REAL
if test -n "$BUN"; then
    measure "bun" "$BUN" check $REAL_ARGS -p $REAL
    measure "bun single-threaded" "$BUN" check $REAL_ARGS --threads=1 -p $REAL
fi
if test -n "$TSCRS"; then
    measure "tsc-rs" "$TSCRS" $REAL_ARGS -p $REAL
    measure "tsc-rs --fast" "$TSCRS" $REAL_ARGS --fast -p $REAL
    measure "tsc-rs single-threaded" "$TSCRS" $REAL_ARGS --singleThreaded -p $REAL
fi
echo "tttt phases (one run):"
$TTTT -q --time --keep-going --noEmit -p $REAL 2>&1 > /dev/null | grep -a '^time:' | sed 's/^/  /'

# The modern corpora (plan 17, thirdparty/ensure-corpora.sh): type-level heavy; tttt matches tsgo on them but for a few
# (scripts/realworld.py --corpus NAME). tsgo isn't clean on webpack (JS: TS 7's reduced JS support). zod (agents/corpora.md) is
# checked through its tsconfig.test.json, webpack (plan 33, checkJs) and type-fest through their own tsconfig, the rest
# through our tsconfig.corpus.json. VS Code's src (3.41M lines) is the scale benchmark
REACT=bulletproof-react/apps/react-vite/tsconfig.corpus.json
NEST=nestjs-boilerplate/tsconfig.corpus.json
for CORPUS in type-fest valibot/library/tsconfig.corpus.json $REACT $NEST webpack zod/packages/zod/tsconfig.test.json xstate/packages/core/tsconfig.corpus.json drizzle/drizzle-orm/tsconfig.corpus.json vue/tsconfig.corpus.json effect/packages/effect/tsconfig.corpus.json vscode/src/tsconfig.corpus.json excalidraw/tsconfig.corpus.json tanstack-router/tsconfig.corpus.json; do
    DIR=thirdparty/corpora/$CORPUS
    TSGO_ARGS="-p $DIR"
    echo
    echo "corpus $CORPUS: $($TSGO -p $DIR --noEmit --listFilesOnly | grep -c .) files incl. dependencies and libs"
    measure "tttt" $TTTT -q --keep-going --noEmit -p $DIR
    measure "tttt single-threaded" $TTTT -q --keep-going --singleThreaded --noEmit -p $DIR
    for K in $CHECKERS; do measure "tttt --checkers $K" $TTTT -q --keep-going --checkers $K --noEmit -p $DIR; done
    measure "tsgo" $TSGO --noEmit $TSGO_ARGS
    measure "tsgo single-threaded" $TSGO --noEmit --singleThreaded $TSGO_ARGS
    if test -n "$BUN"; then
        measure "bun" "$BUN" check --noEmit $TSGO_ARGS
        measure "bun single-threaded" "$BUN" check --noEmit --threads=1 $TSGO_ARGS
    fi
    if test -n "$TSCRS"; then
        measure "tsc-rs" "$TSCRS" --noEmit $TSGO_ARGS
        measure "tsc-rs --fast" "$TSCRS" --noEmit --fast $TSGO_ARGS
        measure "tsc-rs single-threaded" "$TSCRS" --noEmit --singleThreaded $TSGO_ARGS
    fi
    echo "tttt phases (one run):"
    ($TTTT -q --time --keep-going --noEmit -p $DIR 2>&1 > /dev/null || true) | grep -a '^time:' | sed 's/^/  /'
done

# Project references (plan 41): -b on TypeScript 6's src/, 13 projects. tsgo -b emits (a referencing project reads the
# .d.ts files of what it references; noEmit isn't allowed in a referenced project), so it runs on a /tmp copy, from
# scratch each time (built/ removed). tttt reads referenced projects from their sources and checks two projects at a
# time with 4 threads
TS6=thirdparty/corpora/TypeScript6
BUILD=/tmp/tttt-bench-tsc-b
rm -rf $BUILD && mkdir -p $BUILD
cp -r $TS6/src $TS6/scripts $TS6/package.json $BUILD/
ln -s "$PWD/$TS6/node_modules" $BUILD/node_modules
echo
echo "-b $TS6/src: $(find $TS6/src -name '*.ts' | wc -l) files, $(find $TS6/src -name tsconfig.json | wc -l) configs"
measure "tttt" $TTTT -b $TS6/src --no-write -q --keep-going
measure "tsgo -b" sh -c "rm -rf $BUILD/built && $PWD/$TSGO -b $BUILD/src"
measure "tsgo -b single-threaded" sh -c "rm -rf $BUILD/built && $PWD/$TSGO -b --singleThreaded $BUILD/src"
echo "tttt phases (one run with --builders 1, summed over the projects):"
($TTTT -b $TS6/src --no-write -q --time --keep-going --builders 1 2>&1 > /dev/null || true) | grep -a '^time:' | grep -v checker |
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
$TTTT -q --time --keep-going --no-lib "${LIB_FILES[@]}" 2>&1 > /dev/null | grep -a '^time:' | sed 's/^/  /'
