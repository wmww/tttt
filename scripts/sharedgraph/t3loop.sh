#!/usr/bin/env bash
# t3loop.sh BIN N [VAR=VAL...]: the stress loop's measuring side: N drizzle runs at --shared-graph (K and T from the
# environment, default 12), each run's verdict against ref.drizzle, redos, redone files, limit hits and the first
# differing codes (t3diff.I.o/.e kept). Run an effect K12 loop beside it for load
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "$1"); N=$2; shift 2
cd "$OUT" || exit 1
for i in $(seq 1 $N); do
  env "$@" TTTT_GRAPH_STATS=1 $BIN --shared-graph --threads ${T:-12} --checkers ${K:-12} --noEmit --pretty false -p ${P[drizzle]} > t3.o 2> t3.e
  v=same; cmp -s t3.o ref.drizzle || { v=DIFF; cp t3.o t3diff.$i.o; cp t3.e t3diff.$i.e; }
  echo "$i $v redos=$(grep -o '^graph: [0-9]* files checked again' t3.e | awk '{print $2}') redofiles=$(grep "redo of" t3.e | sed "s|.*drizzle-orm/src/||; s| (worker.*||" | tr "\n" ","); limits=$(grep "limit hits" t3.e | awk "{s+=\$NF} END {print s+0}") $( [ $v = DIFF ] && diff ref.drizzle t3.o | grep -o '^. [^(]*([0-9,]*): error TS[0-9]*' | sed 's|.*/drizzle-orm/||' | head -3 | tr '\n' ' ')"
done
