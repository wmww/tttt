#!/bin/bash
# Where the checker's arena memory goes: bytes by allocating call site (with inlined callers), and types (136 bytes
# each, also counted in the first list under ty_alloc) by the code that makes them.
#   scripts/arenasites.sh -p thirdparty/corpora/zod/packages/zod/tsconfig.test.json [--top N]
# Builds a -g release copy with -DTTTT_ARENA_SITES in build-sites/ (this tree's: a shared /tmp directory was
# configured from whichever tree ran first; same code otherwise) and runs it with one
# checker on one thread.
set -e
cd "$(dirname "$0")/.."
TOP=25
ARGS=()
while test $# -gt 0; do
    if test "$1" = --top; then TOP=$2; shift 2; else ARGS+=("$1"); shift; fi
done
B=build-sites
if test ! -e $B; then
    meson setup $B --buildtype release -Dc_args='-g -DTTTT_ARENA_SITES' > /dev/null
fi
ninja -C $B tttt > /dev/null
OUT=$($B/tttt -q --keep-going --checkers 1 --threads 1 --time --no-write "${ARGS[@]}" 2>&1 > /dev/null || true)
echo "$OUT" | grep "checker 0"
BASE=$(nm $B/tttt | awk '$3 == "arena_alloc" { print $1 }')
for kind in arena-site type-site; do
    echo "== ${kind/-site/} (MB, site)"
    echo "$OUT" | awk -v k=$kind '$1 == k { print $3, $2 }' | sort -n -r | head -$TOP | while read bytes off; do
        printf "%7.1f  " "$(echo "$bytes / 1048576" | bc -l)"
        addr2line -f -s -i -e $B/tttt "$(printf '0x%x' $((0x$BASE + off)))" | paste -s -d' ' | cut -c1-140
    done
done
echo "== interned keys (MB, entries, average words, kind)"
echo "$OUT" | awk '$1 == "key-kind" { print $2, $3, $4 }' | python3 -c '
import re, sys
body = open("src/ty/ty_internal.h").read()
body = body[body.index("KEY_LITERAL = 1"):]
names, i = {}, 0
for line in body[:body.index("}")].split("\n"):
    m = re.match(r"\s*(KEY_\w+)", line)
    if m:
        i = 1 if m.group(1) == "KEY_LITERAL" else i + 1
        names[i] = m.group(1)
rows = sorted((int(b), int(c), int(k)) for k, b, c in (l.split() for l in sys.stdin))[::-1]
for b, c, k in rows[:'$TOP']:
    print(f"{b / 1048576:7.1f}  {c:9}  {b / c / 8 - 1:5.1f}  {names.get(k, k)}")
'
