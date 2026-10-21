#!/usr/bin/env bash
# rep3.sh BIN N "VAR=VAL ..." corpus...: rep.sh with an environment, TTTT_GRAPH_STATS kept per run (stats.TAG.X.I,
# its last line same/diff; summ.sh TAG tabulates them), differing outputs as diff3.TAG.X.I
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "$1"); N=$2; ENVS=$3; shift 3
cd "$OUT" || exit 1
TAG=$(echo "$ENVS" | tr -c 'a-zA-Z0-9\n' '_' | cut -c1-90)
for c in "$@"; do same=0; diff=0; crash=0
for i in $(seq 1 $N); do
  timeout 600 env $ENVS TTTT_GRAPH_STATS=1 $BIN --shared-graph --threads 12 --checkers 12 --noEmit --pretty false -p ${P[$c]} > rep3.$c.o 2> stats.$TAG.$c.$i; st=$?
  if [ $st -ge 128 ] || [ $st -eq 124 ]; then crash=$((crash+1)); mv stats.$TAG.$c.$i crash3.$TAG.$c.$i; continue; fi
  if cmp -s ref.$c rep3.$c.o; then same=$((same+1)); echo same >> stats.$TAG.$c.$i; else diff=$((diff+1)); echo diff >> stats.$TAG.$c.$i; cp rep3.$c.o diff3.$TAG.$c.$i; fi
done
echo "$ENVS $c: same $same diff $diff crash $crash"
done
