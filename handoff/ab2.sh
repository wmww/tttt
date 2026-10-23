#!/bin/bash
# ab2.sh N config [args]: tttt.old vs tttt.new interleaved, TTTT_SYNC_EXIT=1, medians of wall/sys/user/minflt/maxrss
cd /tmp/wt-ktime2 || exit 1
n=$1; c=$2; shift 2
for i in $(seq $n); do for b in old new; do
  TTTT_SYNC_EXIT=1 /usr/bin/time -f "$b %e %S %U %R %M" -o /tmp/wt-ktime2-scratch/t.txt /tmp/wt-ktime2-scratch/tttt.$b --noEmit --pretty false "$@" -p thirdparty/corpora/$c >/dev/null 2>&1; grep "^$b " /tmp/wt-ktime2-scratch/t.txt
done; done | python3 -c '
import sys,statistics as st
d={}
for l in sys.stdin:
    f=l.split(); d.setdefault(f[0],[]).append([float(x) for x in f[1:]])
for k in ("old","new"):
    v=d[k]; m=[st.median(x[i] for x in v) for i in range(5)]
    print(k,"wall %.3f sys %.3f user %.2f minflt %d maxrss %d MB" % (m[0],m[1],m[2],m[3],m[4]/1024))'
