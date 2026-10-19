#!/usr/bin/env bash
# Instructions (perf stat instructions:u, else callgrind) per pinned corpus, single-threaded: the stable
# perf measure across sessions (agents/measurements.md).
#
#   scripts/instrcount.sh [--bin path/to/tttt] [--save] [corpus...]     (default: the shipped LTO + PGO build,
#                                                                       scripts/pgobuild.sh; every corpus but zod-root)
#
# Each count is printed with its change against test/instrcount.txt (the counts of the last --save, by the same tool),
# and a change over 2% either way is flagged: drizzle's +50% of 2026-10-17 was recorded in measurements.md and went
# unnoticed for two sessions. --save writes the counts there (commit it with the session's measurements)
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
BIN=
SAVE=
while test $# -gt 0; do
    case "$1" in
        --bin) BIN="$2"; shift 2 ;;
        --save) SAVE=1; shift ;;
        *) break ;;
    esac
done
test -n "$BIN" || BIN=$(scripts/pgobuild.sh)
REF=test/instrcount.txt
declare -A RESULT
# The count of $1 (instructions) with its change against the reference of tool $2
report() {
    local n="$1" i="$2" tool="$3" g ref note=""
    RESULT[$n]="$i"
    g=$(echo "$i / 1000000000" | bc -l)
    ref=$(awk -v n="$n" -v t="$tool" '$1 == n && $3 == t { print $2 }' "$REF" 2>/dev/null || true)
    if test -n "$ref" && test "$ref" != 0; then
        note=$(awk -v a="$i" -v b="$ref" 'BEGIN { d = (a - b) * 100 / b; printf "%+6.2f%%%s", d, (d > 2 || d < -2) ? "  <-- against " sprintf("%.3f", b / 1e9) " G" : "" }')
    fi
    printf '%-16s %8.3f G  %s\n' "$n" "$g" "$note"
}
save() {
    test -n "$SAVE" || return 0
    local tool="$1" tmp
    tmp=$(mktemp)
    # (Only the corpora measured are replaced)
    { test -f "$REF" && awk -v t="$tool" -v names=" ${!RESULT[*]} " '$3 != t || index(names, " " $1 " ") == 0' "$REF"; true; } > "$tmp"
    for n in "${!RESULT[@]}"; do echo "$n ${RESULT[$n]} $tool"; done >> "$tmp"
    sort "$tmp" > "$REF"
    rm -f "$tmp"
    echo "saved to $REF"
}
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
        report "$n" "$i" perf
    done
    save perf
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
        report "$n" "${i:-0}" callgrind
    done
    rm -rf "$DIR"
    save callgrind
fi
