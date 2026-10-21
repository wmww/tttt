#!/usr/bin/env bash
# rep.sh BIN N corpus...: N runs at --shared-graph K12 T12 against ref.X: same/diff/crash; differing outputs kept as
# diffout.X.I, crashes' stderr as crashlog.X.I (EXTRA=... adds arguments)
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "$1"); N=$2; shift 2
cd "$OUT" || exit 1
for c in "$@"; do same=0; diff=0; crash=0
for i in $(seq 1 $N); do
  timeout 600 $BIN --shared-graph --threads 12 --checkers 12 $EXTRA --noEmit --pretty false -p ${P[$c]} > rep.$c.o 2>rep.$c.e; st=$?
  if [ $st -ge 128 ] || [ $st -eq 124 ]; then crash=$((crash+1)); cp rep.$c.e crashlog.$c.$i; continue; fi
  if cmp -s ref.$c rep.$c.o; then same=$((same+1)); else diff=$((diff+1)); cp rep.$c.o diffout.$c.$i; fi
done
echo "$c: same $same diff $diff crash $crash"
done
