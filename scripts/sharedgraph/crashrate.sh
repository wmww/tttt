#!/usr/bin/env bash
# crashrate.sh N [VAR=VAL...]: how many of N runs of BIN (default release/tttt) on effect at --shared-graph K12 T12
# crash, the environment passed through
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "${BIN:-}")
n=$1; shift; c=0
cd "$OUT" || exit 1
for i in $(seq 1 $n); do timeout 120 env "$@" $BIN --shared-graph --threads 12 --checkers 12 --noEmit --pretty false -p ${P[effect]} > /dev/null 2>&1; s=$?; [ $s -ge 128 ] && c=$((c+1)); done
echo "$* : $c/$n"
