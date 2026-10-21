#!/usr/bin/env bash
# bis.sh BIN N "VAR=VAL ..." corpus: N runs at --shared-graph K12 T12 with an environment, how many differ from ref.X
# and each differing run's top codes (for bisecting a knob)
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "$1"); N=$2; E=$3; c=$4; d=0
cd "$OUT" || exit 1
for i in $(seq 1 $N); do env $E $BIN --shared-graph --threads 12 --checkers 12 --noEmit --pretty false -p ${P[$c]} > b.$c.o 2>/dev/null; cmp -s b.$c.o ref.$c || { d=$((d+1)); diff ref.$c b.$c.o | grep "^[<>]" | grep -o "^. [^(]*([0-9,]*): error TS[0-9]*" | sed "s|.*/corpora/||" | sort | uniq -c | head -3 | tr "\n" " "; echo; }; done
echo "$E: $d / $N differ"
