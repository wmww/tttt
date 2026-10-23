#!/usr/bin/env python3
# Analysis of TTTT_FILE_CENSUS output (scripts/filecensus.sh): usage filecensus_an.py CENSUS.txt BIN [TOP]
import re, subprocess, sys, collections
# usage: an.py census.txt BIN [top]
path, binp = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 30
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
memotags = {1: "expr", 2: "local binding", 3: "decl binding", 4: "loop var", 5: "class field/accessor", 6: "other"}
M = 1 << 20
base = int(subprocess.run(["nm", binp], capture_output=True, text=True).stdout.split("arena_alloc\n")[0].split("\n")[-1].split()[0], 16)
tot = collections.Counter(); st = collections.defaultdict(lambda: [0]*4); cat = collections.defaultdict(lambda: [0]*4)
tyk = collections.defaultdict(lambda: [0]*4); site = collections.defaultdict(lambda: [0]*4)
memo = collections.defaultdict(lambda: [0]*3); link = collections.defaultdict(lambda: [0]*2)
misc = collections.defaultdict(lambda: [0]*8)
ncheckers = set(); totals = []
for line in open(path):
    f = line.split()
    if not f or not f[0].startswith("fcensus-"): continue
    k = f[0][8:]
    if k == "total":
        ncheckers.add(f[1]); totals.append(line.strip())
        tot["arena"] += int(f[4]); tot["memory"] += int(f[8]); tot["files"] += int(f[10])
    elif k == "state":
        st[int(f[2])][0] += int(f[3])
    elif k == "cat":
        for j in range(4): cat[f[2]][j] += int(f[3 + j])
    elif k == "tykind":
        for j in range(4): tyk[int(f[2])][j] += int(f[3 + j])
    elif k == "site":
        for j in range(4): site[int(f[2])][j] += int(f[3 + j])
    elif k == "memo":
        for j in range(3): memo[int(f[2])][j] += int(f[3 + j])
    elif k == "link":
        for j in range(2): link[int(f[2])][j] += int(f[3 + j])
    elif k == "tables":
        a = list(map(int, f[3:5] + f[6:8] + f[9:11]))
        for j in range(6): misc["tables"][j] += a[j]
    elif k == "nodelinks":
        a = [int(f[2]), int(f[3]), int(f[5]), int(f[7])]
        for j in range(4): misc["nodelinks"][j] += a[j]
    elif k == "relations":
        a = [int(f[2]), int(f[3]), int(f[5]), int(f[7])]
        for j in range(4): misc["relations"][j] += a[j]
arena = tot["arena"]
print(f"checker censuses: {len(totals)} ({len(ncheckers)} checkers), arena {arena/M:.1f} MB, checker_memory {tot['memory']/M:.1f} MB, files {tot['files']}")
names = ["warm", "kept (file-made, needed later)", "droppable (referenced)", "droppable (unreferenced)"]
for s_ in range(4): print(f"  {names[s_]:34} {st[s_][0]/M:9.1f} MB {100*st[s_][0]/arena:5.1f}%")
print("== by category (MB): warm kept drop garbage | droppable %")
for c, v in sorted(cat.items(), key=lambda x: -sum(x[1])):
    print(f"  {c:14} {v[0]/M:8.1f} {v[1]/M:8.1f} {v[2]/M:8.1f} {v[3]/M:8.1f} | {100*(v[2]+v[3])/max(1,sum(v)):5.1f}%")
print("== types by kind (MB): warm kept drop garbage")
for k, v in sorted(tyk.items(), key=lambda x: -sum(x[1])):
    print(f"  {tys[k] if k < len(tys) else k:14} {v[0]/M:8.1f} {v[1]/M:8.1f} {v[2]/M:8.1f} {v[3]/M:8.1f}")
print("== lazy memos by tag: warm, file-made, evaluated")
for t, v in sorted(memo.items()): print(f"  {memotags.get(t, t):22} {v[0]:9} {v[1]:9} {v[2]:9}")
t = misc["tables"]; nl = misc["nodelinks"]; r = misc["relations"]
print(f"== tables: types {t[0]}/{t[1]} ({t[1]*16/M:.1f} MB), links {t[2]}/{t[3]} ({t[3]*16/M:.1f} MB), iprops {t[4]}/{t[5]}")
print(f"   node links {nl[0]}/{nl[1]} ({nl[1]*16/M:.1f} MB): kept {nl[2]}, droppable {nl[3]}")
print(f"   relations {r[0]} entries ({r[1]/M:.1f} MB): naming a droppable type {r[2]}, unknown {r[3]}")
print("== node links by kind: kept droppable")
for k, v in sorted(link.items(), key=lambda x: -sum(x[1]))[:top]:
    print(f"  {keys.get(k, k):34} {v[0]:9} {v[1]:9}")
print("== other allocations by site (MB): warm kept drop garbage")
rows = sorted(site.items(), key=lambda x: -sum(x[1]))[:top]
addrs = [hex(base + s) for s, _ in rows]
out = subprocess.run(["addr2line", "-f", "-s", "-i", "-e", binp] + addrs, capture_output=True, text=True).stdout
# addr2line -i prints variable lines per address; rerun individually for robustness
for (s_, v) in rows:
    name = subprocess.run(["addr2line", "-f", "-s", "-i", "-e", binp, hex(base + s_)], capture_output=True, text=True).stdout
    name = " ".join(name.split())[:110]
    print(f"  {v[0]/M:6.1f} {v[1]/M:7.1f} {v[2]/M:7.1f} {v[3]/M:7.1f}  {name}")
looks = collections.Counter(); reuse = collections.Counter()
for line in open(path):
    f = line.split()
    if f and f[0] == "fcensus-looks": looks[f[2]] += int(f[3])
    if f and f[0] == "fcensus-reuse": reuse[f[2]] += int(f[4])
print(f"== later files' looks (distinct per file): intern {looks['0']}, node links {looks['1']}, memos {looks['2']}; "
    f"at droppable entries (re-derived if dropped): intern {reuse['0']}, node links {reuse['1']}, memos {reuse['2']}")
