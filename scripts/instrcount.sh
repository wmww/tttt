#!/usr/bin/env bash
# Instructions (perf stat instructions:u, else callgrind) per pinned corpus, single-threaded: the stable
# perf measure across sessions (agents/measurements.md).
#
#   scripts/instrcount.sh [--bin path/to/tttt] [corpus...]     (default: ./release/tttt, every corpus but zod-root)
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
BIN=./release/tttt
if test "${1:-}" = --bin; then BIN="$2"; shift 2; else ninja -C release > /dev/null; fi
declare -A C=(
    [tsc]=thirdparty/corpora/TypeScript6/src/compiler
    [type-fest]=thirdparty/corpora/type-fest
    [valibot]=thirdparty/corpora/valibot/library/tsconfig.corpus.json
    [react]=thirdparty/corpora/bulletproof-react/apps/react-vite/tsconfig.corpus.json
    [nest]=thirdparty/corpora/nestjs-boilerplate/tsconfig.corpus.json
    [webpack]=thirdparty/corpora/webpack
    [zod]=thirdparty/corpora/zod/packages/zod/tsconfig.test.json
    [xstate]=thirdparty/corpora/xstate/packages/core/tsconfig.corpus.json
    [drizzle]=thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json
    [vue]=thirdparty/corpora/vue/tsconfig.corpus.json
    [effect]=thirdparty/corpora/effect/packages/effect/tsconfig.corpus.json
    [vscode]=thirdparty/corpora/vscode/src/tsconfig.corpus.json
    [excalidraw]=thirdparty/corpora/excalidraw/tsconfig.corpus.json
    [tanstack-router]=thirdparty/corpora/tanstack-router/tsconfig.corpus.json
)
NAMES=("$@")
test ${#NAMES[@]} -gt 0 || NAMES=(tsc type-fest valibot react nest webpack zod xstate drizzle vue effect vscode excalidraw tanstack-router)
ARGS=(--checkers 1 --threads 1 -q --keep-going --noEmit -p)
# perf needs kernel.perf_event_paranoid <= 2 (some days the machine has 4): then callgrind counts instead, all corpora
# at once (a few minutes; its counts run ~3% above perf's, so compare only numbers of the same tool)
if perf stat -x, -e instructions:u true 2>&1 | grep -q instructions:u; then
    for n in "${NAMES[@]}"; do
        p=$(realpath "${C[$n]}")
        i=$(perf stat -x, -e instructions:u "$BIN" "${ARGS[@]}" "$p" \
            2>&1 >/dev/null | grep instructions:u | cut -d, -f1 || true)
        printf '%-10s %8.3f G\n' "$n" "$(echo "$i / 1000000000" | bc -l)"
    done
else
    echo "(perf unavailable: callgrind counts)"
    DIR=$(mktemp -d /tmp/instrcount.XXXXXX)
    for n in "${NAMES[@]}"; do
        valgrind --tool=callgrind --main-stacksize=67108864 --callgrind-out-file="$DIR/$n.out" "$BIN" "${ARGS[@]}" \
            "$(realpath "${C[$n]}")" > /dev/null 2> "$DIR/$n.log" &
    done
    wait
    for n in "${NAMES[@]}"; do
        i=$(grep -o 'Collected : [0-9]*' "$DIR/$n.log" | grep -o '[0-9]*$' || true)
        printf '%-10s %8.3f G\n' "$n" "$(echo "${i:-0} / 1000000000" | bc -l)"
    done
    rm -rf "$DIR"
fi
