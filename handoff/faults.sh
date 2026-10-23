#!/bin/bash
# faults.sh corpus-config [extra args]: minor faults and sys ms by thread start routine
cd /tmp/wt-ktime2 || exit 1
c=$1; shift
base=$(nm release/tttt | awk '$3=="worker_thread_create"{print $1}')
TTTT_SYNC_EXIT=1 TTTT_FAULTS=1 /usr/bin/time -f "total minflt %R sys %S" ./release/tttt --noEmit --pretty false "$@" -p thirdparty/corpora/$c 2>&1 >/dev/null | grep -E "^FAULTS|^total" > /tmp/wt-ktime2-scratch/f.txt
nm release/tttt | sort > /tmp/wt-ktime2-scratch/nm.txt
python3 - "$base" <<'PY'
import collections, bisect, sys
syms=[(int(l.split()[0],16), l.split()[2]) for l in open('/tmp/wt-ktime2-scratch/nm.txt') if len(l.split())==3]
syms.sort(); addrs=[a for a,_ in syms]
wtc=int(sys.argv[1],16)
lines=open('/tmp/wt-ktime2-scratch/f.txt').read().splitlines()
# find runtime address of worker_thread_create: unknown; use fault_tramp-relative: pointers are PIE, take offset via known symbol? use low 12 bits + page match
agg=collections.defaultdict(lambda:[0,0,0.0])
fns=[l.split() for l in lines if l.startswith('FAULTS 0x')]
for f in fns:
    a=int(f[1],16)
    agg[a][0]+=1; agg[a][1]+=int(f[2]); agg[a][2]+=float(f[3])
# resolve: PIE base = a - symaddr; guess base by trying each symbol for the most common pointer whose page offset matches
names={}
for a in agg:
    cands=[n for s,n in syms if (s & 0xfff)==(a & 0xfff) and n.endswith(('worker','thread','main','run','loop')) ]
    names[a]=','.join(cands[:3]) or hex(a)
for a,v in sorted(agg.items(), key=lambda x:-x[1][1]): print("%-50s threads %3d minflt %7d sys %8.1f ms" % (names[a][:50], *v))
for l in lines:
    if not l.startswith('FAULTS 0x'): print(l)
PY
