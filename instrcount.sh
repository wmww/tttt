#!/usr/bin/env bash
# Instructions (perf stat instructions:u) per pinned corpus, single-threaded, no re-exec: the stable perf measure
# across sessions (agents/measurements.md).
#
#   ./instrcount.sh [--bin path/to/tttt] [corpus...]     (default: ./release/tttt, every corpus but zod-root)
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )"
BIN=./release/tttt
if test "${1:-}" = --bin; then BIN="$2"; shift 2; else ninja -C release > /dev/null; fi
declare -A C=(
    [tsc]=thirdparty/tsc/src/compiler
    [type-fest]=thirdparty/corpora/type-fest
    [valibot]=thirdparty/corpora/valibot/library
    [react]=thirdparty/corpora/bulletproof-react/apps/react-vite
    [nest]=thirdparty/corpora/nestjs-boilerplate
    [webpack]=thirdparty/corpora/webpack
    [zod]=thirdparty/corpora/zod/packages/zod/tsconfig.test.json
    [xstate]=thirdparty/corpora/xstate/packages/core/tsconfig.corpus.json
    [drizzle]=thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json
    [vue]=thirdparty/corpora/vue/tsconfig.corpus.json
)
NAMES=("$@")
test ${#NAMES[@]} -gt 0 || NAMES=(tsc type-fest valibot react nest webpack zod xstate drizzle vue)
for n in "${NAMES[@]}"; do
    p=$(realpath "${C[$n]}")
    i=$(TTTT_NO_REEXEC=1 perf stat -x, -e instructions:u "$BIN" --checkers 1 --threads 1 -q --keep-going -p "$p" \
        2>&1 >/dev/null | grep instructions:u | cut -d, -f1 || true)
    printf '%-10s %8.3f G\n' "$n" "$(echo "$i / 1000000000" | bc -l)"
done
