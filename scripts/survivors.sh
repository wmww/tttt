#!/bin/bash
# Plan 79's measurement: what a copying checker recycle would keep. At every recycle of one checker (--checkers 1
# --threads 1 at --checker-memory MB), marks the arena conservatively from the declaration-level roots (non-local
# symbols' links, the checker's own fields; identity entries kept while their value is, ty/survivors.c) and prints the
# survivors against the arena: per recycle, then summed by type kind, by key kind and by allocating site.
#   scripts/survivors.sh [-m MB] [--lazy] [--top N] -p thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json
# --lazy: every LazyType memo is a root too (an upper bound: exported variables' types live there).
# Builds a -g release copy with -DTTTT_SURVIVORS in /tmp/tttt-survivors (same code otherwise).
set -e
cd "$(dirname "$0")/.."
TOP=20
MB=64
ARGS=()
while test $# -gt 0; do
    case "$1" in
        --top) TOP=$2; shift 2;;
        -m) MB=$2; shift 2;;
        --lazy) export TTTT_SURVIVORS_LAZY=1; shift;;
        *) ARGS+=("$1"); shift;;
    esac
done
B=/tmp/tttt-survivors
if test ! -e $B; then
    meson setup $B --buildtype release -Dc_args='-g -DTTTT_SURVIVORS' > /dev/null
fi
ninja -C $B tttt > /dev/null
OUT=$($B/tttt --noEmit --keep-going --checkers 1 --threads 1 --checker-memory "$MB" "${ARGS[@]}" 2>&1 > /dev/null || true)
BASE=$(nm $B/tttt | awk '$3 == "arena_alloc" { print $1 }')
echo "$OUT" | BASE=$BASE BIN=$B/tttt TOP=$TOP python3 -c '
import os, re, subprocess, sys
body = open("src/ty/ty_internal.h").read()
body = body[body.index("KEY_LITERAL = 1"):]
keys, i = {254: "(intersection_props)", 255: "(atoms)"}, 0
for line in body[:body.index("}")].split("\n"):
    m = re.match(r"\s*(KEY_\w+)", line)
    if m:
        i = 1 if m.group(1) == "KEY_LITERAL" else i + 1
        keys[i] = m.group(1)
tys = ["intrinsic", "literal", "type-param", "declared", "reference", "anonymous", "union", "intersection", "keyof",
    "indexed", "conditional", "substitution", "template", "string-mapping", "evolving-array"]
M = 1 << 20
rec, ty, key, site = [], {}, {}, {}
for line in sys.stdin:
    f = line.split()
    if not f or not f[0].startswith("survivors-"): continue
    if f[0] == "survivors-total": rec.append([int(f[1]), int(f[4])])
    elif f[0] == "survivors-graph": rec[-1] += [int(f[1]), int(f[3])]
    else:
        d = {"survivors-ty": ty, "survivors-key": key, "survivors-site": site}[f[0]]
        a = d.setdefault(int(f[1]), [0, 0, 0, 0])
        for j in range(len(f) - 2): a[j] += int(f[j + 2])
if not rec: sys.exit("no recycle at this budget")
print("recycle  arena MB  memory MB  survivors MB  %%  with caches MB  %%")
for n, (arena, memory, graph, caches) in enumerate(rec):
    print(f"{n:7}  {arena/M:8.1f}  {memory/M:9.1f}  {graph/M:12.1f}  {100*graph/arena:3.0f}  {caches/M:14.1f}  {100*caches/arena:3.0f}")
arena = sum(r[0] for r in rec); graph = sum(r[2] for r in rec); caches = sum(r[3] for r in rec)
print(f"    all  {arena/M:8.1f}  {sum(r[1] for r in rec)/M:9.1f}  {graph/M:12.1f}  {100*graph/arena:3.0f}  {caches/M:14.1f}  {100*caches/arena:3.0f}")
top = int(os.environ["TOP"])
print("== types (kept MB, dropped MB, kept count, dropped count)")
for k, (kb, db, kc, dc) in sorted(ty.items(), key=lambda x: -x[1][0] - x[1][1]):
    print(f"{kb/M:8.1f} {db/M:8.1f} {kc:9} {dc:9}  {tys[k] if k < len(tys) else k}")
print("== intern blocks by key kind (kept MB, dropped MB, kept entries, dropped entries)")
for k, (kb, db, kc, dc) in sorted(key.items(), key=lambda x: -x[1][0] - x[1][1])[:top]:
    print(f"{kb/M:8.1f} {db/M:8.1f} {kc:9} {dc:9}  {keys.get(k, k)}")
print("== the rest by site (kept MB, dropped MB)")
base = int(os.environ["BASE"], 16)
for s, (kb, db, _, _) in sorted(site.items(), key=lambda x: -x[1][0] - x[1][1])[:top]:
    name = subprocess.run(["addr2line", "-f", "-s", "-i", "-e", os.environ["BIN"], hex(base + s)], capture_output=True, text=True).stdout
    print(f"{kb/M:8.1f} {db/M:8.1f}  " + " ".join(name.split())[:120])
'
