#!/usr/bin/env bash
# Instructions (perf stat instructions:u, else callgrind) per pinned corpus, single-threaded: the stable
# perf measure across sessions (agents/measurements.md).
#
#   scripts/instrcount.sh [--bin path/to/tttt] [--save] [corpus...]     (default: `scripts/fastbuild.sh --measured`,
#                                                                       fast/tttt; every corpus but zod-root)
#   scripts/instrcount.sh --vs main [--fast] [corpus...]   this change against REV: release/tttt (--bin to choose)
#                                                         against `scripts/abbuild.sh REV`'s release build, both
#                                                         measured now (REV's counts cached in its abbuild dir);
#                                                         --fast: both through fastbuild.sh's PGO + BOLT pipeline
#
# Each count is printed with its change against test/instrcount.txt (the counts of the last --save, by the same tool),
# and a change over 2% either way is flagged: drizzle's +50% of 2026-10-17 was recorded in measurements.md and went
# unnoticed for two sessions. --save writes the counts there (commit it with the session's measurements)
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
BIN=
SAVE=
VS=
FAST=
while test $# -gt 0; do
    case "$1" in
        --bin) BIN="$2"; shift 2 ;;
        --save) SAVE=1; shift ;;
        --vs) VS="$2"; shift 2 ;;
        --fast) FAST=1; shift ;;
        *) break ;;
    esac
done
REF=test/instrcount.txt
VSDIR=
if test -n "$VS"; then
    test -z "$SAVE" || { echo "instrcount.sh: --save takes no --vs" >&2; exit 2; }
    if test -n "$FAST"; then
        VSBIN=$(scripts/abbuild.sh "$VS" --fast)
        test -n "$BIN" || BIN=$(realpath "$(scripts/fastbuild.sh)")
    else
        VSBIN=$(scripts/abbuild.sh "$VS")
        if test -z "$BIN"; then
            test -e release/build.ninja || meson setup release --buildtype release > /dev/null
            ninja -C release tttt > /dev/null
            BIN=release/tttt
        fi
    fi
    VSDIR=$(dirname "$(dirname "$VSBIN")")
    VSKIND=$(basename "$(dirname "$VSBIN")")
    REF=$(mktemp /tmp/instrcount-ref.XXXXXX)
    trap 'rm -f "$REF"' EXIT
    echo "(against $VS: $VSBIN)"
fi
test -n "$BIN" || BIN=$(scripts/fastbuild.sh --measured)
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
# measure BIN NAME...: "NAME COUNT" lines, and the tool on the last line
measure() {
    local bin="$1" n p i dir; shift
    if perf stat -x, -e instructions:u true 2>&1 | grep -q instructions:u; then
        for n in "$@"; do
            p=$(realpath "${C[$n]}")
            i=$(perf stat -x, -e instructions:u "$bin" "${ARGS[@]}" "$p" \
                2>&1 >/dev/null | grep instructions:u | cut -d, -f1 || true)
            echo "$n ${i:-0}"
        done
        echo perf
    else
        dir=$(mktemp -d /tmp/instrcount.XXXXXX)
        for n in "$@"; do
            valgrind --tool=callgrind --main-stacksize=67108864 --callgrind-out-file="$dir/$n.out" "$bin" "${ARGS[@]}" \
                "$(realpath "${C[$n]}")" > /dev/null 2> "$dir/$n.log" &
        done
        wait
        for n in "$@"; do
            i=$(grep -o 'Collected : [0-9]*' "$dir/$n.log" | grep -o '[0-9]*$' || true)
            echo "$n ${i:-0}"
        done
        rm -rf "$dir"
        echo callgrind
    fi
}
# perf needs kernel.perf_event_paranoid <= 2 (some days the machine has 4): then callgrind counts instead, all corpora
# at once (a few minutes; its counts run ~3% above perf's, so compare only numbers of the same tool). With --vs, REV's
# binary is measured in parallel with ours (counts, not time: load doesn't skew them); its counts are cached by tool,
# corpus and build kind in its abbuild directory (same tree, same binary, same count)
OURS=$(mktemp /tmp/instrcount-ours.XXXXXX)
if test -n "$VS"; then
    MISSING=()
    for n in "${NAMES[@]}"; do
        ls "$VSDIR"/instrcount-"$VSKIND"-"$n".* > /dev/null 2>&1 || MISSING+=("$n")
    done
    if test ${#MISSING[@]} -gt 0; then
        THEIRS=$(mktemp /tmp/instrcount-theirs.XXXXXX)
        measure "$VSBIN" "${MISSING[@]}" > "$THEIRS" &
        measure "$BIN" "${NAMES[@]}" > "$OURS"
        wait
        tool=$(tail -1 "$THEIRS")
        head -n -1 "$THEIRS" | while read -r n i; do echo "$i" > "$VSDIR/instrcount-$VSKIND-$n.$tool"; done
        rm -f "$THEIRS"
    else
        measure "$BIN" "${NAMES[@]}" > "$OURS"
    fi
    tool=$(tail -1 "$OURS")
    test "$tool" = perf || echo "(perf unavailable: callgrind counts)"
    for n in "${NAMES[@]}"; do
        f="$VSDIR/instrcount-$VSKIND-$n.$tool"
        test -s "$f" && echo "$n $(cat "$f") $tool" >> "$REF"
    done
else
    measure "$BIN" "${NAMES[@]}" > "$OURS"
    tool=$(tail -1 "$OURS")
    test "$tool" = perf || echo "(perf unavailable: callgrind counts)"
fi
head -n -1 "$OURS" > "$OURS.n"
while read -r n i; do report "$n" "$i" "$tool"; done < "$OURS.n"
rm -f "$OURS" "$OURS.n"
save "$tool"
