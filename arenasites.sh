#!/bin/bash
# Where the checker's arena memory goes: bytes by allocating call site (with inlined callers), and types (152 bytes
# each, also counted in the first list under ty_alloc) by the code that makes them.
#   ./arenasites.sh -p thirdparty/corpora/zod/packages/zod/tsconfig.test.json [--top N]
# Builds a -g release copy with -DTTTT_ARENA_SITES in /tmp/tttt-arenasites (same code otherwise) and runs it with one
# checker on one thread.
set -e
cd "$(dirname "$0")"
TOP=25
ARGS=()
while test $# -gt 0; do
    if test "$1" = --top; then TOP=$2; shift 2; else ARGS+=("$1"); shift; fi
done
B=/tmp/tttt-arenasites
if test ! -e $B; then
    meson setup $B --buildtype release -Dc_args='-g -DTTTT_ARENA_SITES' > /dev/null
fi
ninja -C $B tttt > /dev/null
OUT=$(TTTT_NO_REEXEC=1 $B/tttt -q --keep-going --checkers 1 --threads 1 --time "${ARGS[@]}" 2>&1 > /dev/null || true)
echo "$OUT" | grep "checker 0"
BASE=$(nm $B/tttt | awk '$3 == "arena_alloc" { print $1 }')
for kind in arena-site type-site; do
    echo "== ${kind/-site/} (MB, site)"
    echo "$OUT" | awk -v k=$kind '$1 == k { print $3, $2 }' | sort -n -r | head -$TOP | while read bytes off; do
        printf "%7.1f  " "$(echo "$bytes / 1048576" | bc -l)"
        addr2line -f -s -i -e $B/tttt "$(printf '0x%x' $((0x$BASE + off)))" | paste -s -d' ' | cut -c1-140
    done
done
