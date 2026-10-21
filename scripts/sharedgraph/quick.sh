#!/usr/bin/env bash
# quick.sh BIN N corpus...: N runs at K12 T12 --shared-graph against ref.X with TTTT_GRAPH_STATS, the last run's totals
# printed (qdiff.X.I / qcrash.X.I kept)
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "$1"); N=$2; shift 2
cd "$OUT" || exit 1
for c in "$@"; do same=0; diff=0; crash=0
for i in $(seq 1 $N); do
  timeout 600 env TTTT_GRAPH_STATS=1 $BIN --shared-graph --threads 12 --checkers 12 --noEmit --pretty false -p ${P[$c]} > q.$c.o 2> q.$c.e; st=$?
  if [ $st -ge 128 ] || [ $st -eq 124 ]; then crash=$((crash+1)); cp q.$c.e qcrash.$c.$i; continue; fi
  if cmp -s ref.$c q.$c.o; then same=$((same+1)); else diff=$((diff+1)); cp q.$c.o qdiff.$c.$i; fi
done
echo "$c: same $same diff $diff crash $crash; last: $(grep 'graph: total\|checked again' q.$c.e | tr '\n' ' ')"
done
