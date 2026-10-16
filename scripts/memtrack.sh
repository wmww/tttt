#!/bin/bash
# Where the malloc heap goes (the AST, kept in malloc by TTTT_AST_MALLOC=1 rather than its arenas; source text, checker
# tables; the binder and checker arenas are mmapped: scripts/arenasites.sh for those): live
# blocks at the end of each phase by allocating function, and how much of them is all-zero words and Spans.
#   scripts/memtrack.sh -p thirdparty/corpora/webpack [--phase resolve_names|load_parse_files|typecheck] [--top N] [--by f0|f01|size|sizef]
# Builds a -g release copy in /tmp/tttt-memtrack (same code otherwise) and runs it with one checker on one thread under
# bench/memtrack/memtrack.so (LD_PRELOAD, ~10x slower). Dumps stay in $MEMTRACK_DIR (default /tmp/tttt-memtrack/out):
# rerun bench/memtrack/{an,scan,gcd}.py on them with MEMTRACK_BIN=/tmp/tttt-memtrack/tttt
# MEMTRACK_STOP=<phase> (load_parse_files, resolve_names) exits after that phase's dump: vscode in ~50 s instead of the
# whole run. Each phase also prints malloc churn (realloc'd bytes counted apart) and mallinfo on stderr
set -e
cd "$(dirname "$0")/.."
TOP=30
PHASE=resolve_names
BY=f0
ARGS=()
while test $# -gt 0; do
    case "$1" in
        --top) TOP=$2; shift 2;;
        --phase) PHASE=$2; shift 2;;
        --by) BY=$2; shift 2;;
        *) ARGS+=("$1"); shift;;
    esac
done
B=/tmp/tttt-memtrack
if test ! -e $B/build.ninja; then
    meson setup $B --buildtype release -Dc_args='-g' > /dev/null
fi
ninja -C $B tttt > /dev/null
gcc -O2 -shared -fPIC bench/memtrack/memtrack.c -o $B/memtrack.so
export MEMTRACK_DIR=${MEMTRACK_DIR:-$B/out}
mkdir -p "$MEMTRACK_DIR"
SF=$(gdb -batch -ex 'print sizeof(struct SourceFile)' $B/tttt 2>/dev/null | awk '{ print $3 }')
MEMTRACK=1 MEMTRACK_SOURCE_FILE_SIZE=${SF:-440} TTTT_AST_MALLOC=1 LD_PRELOAD=$B/memtrack.so \
    $B/tttt --checkers 1 --threads 1 -q --keep-going --no-write "${ARGS[@]}" 2>&1 > /dev/null | grep -E "^[a-z_]+: (cumulative|mallinfo)" || true
export MEMTRACK_BIN=$B/tttt
echo "== $PHASE: live blocks by $BY"
python3 bench/memtrack/an.py "$MEMTRACK_DIR/memtrack-dump-$PHASE.txt" "$BY" "$TOP"
echo "== $PHASE: Spans and zero words by allocating function"
python3 bench/memtrack/scan.py "$MEMTRACK_DIR/memtrack-scan-$PHASE.txt" | head -$((TOP + 1))
