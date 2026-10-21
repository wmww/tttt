#!/usr/bin/env bash
# cgtc.sh BIN CORPUS K [extra args...]: callgrind instructions inside program_typecheck and the worker threads
# (--shared-graph, K checkers on K threads, no recycling), and whether the output is ref.X's
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "$1"); c=$2; K=$3; shift 3
cd "$OUT" || exit 1
out=cg/cg.$c.$K.$$
valgrind --tool=callgrind --callgrind-out-file=$out --toggle-collect=program_typecheck --toggle-collect=check_shares_thread --toggle-collect=materialize_ahead_thread --collect-atstart=no \
  $BIN --shared-graph --threads $K --checkers $K --checker-memory off --noEmit --pretty false -p ${P[$c]} "$@" > cg/out.$c.$K.$$ 2>/dev/null
ir=$(grep -h '^summary:\|^totals:' $out | head -1 | awk '{print $2}')
cmp -s cg/out.$c.$K.$$ ref.$c && s=same || s=DIFF
echo "$c K$K $ir $s"
rm -f "${OUT:?}"/cg/cg.*.$$ "${OUT:?}"/cg/out.*.$$
