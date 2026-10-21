#!/usr/bin/env bash
# g2bench.sh BIN [RUNS]: per -p corpus and configuration (K1, defaults, K4, shared K4, K12, shared K12), the best wall
# of RUNS (s), its CPU (user+sys) and peak RSS (MB), and the load average (ONLY="corpus/tsconfig ..." restricts)
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "$1"); RUNS=${2:-3}
CORPORA="${ONLY:-webpack zod/packages/zod/tsconfig.test.json xstate/packages/core/tsconfig.corpus.json drizzle/drizzle-orm/tsconfig.corpus.json vue/tsconfig.corpus.json effect/packages/effect/tsconfig.corpus.json excalidraw/tsconfig.corpus.json tanstack-router/tsconfig.corpus.json vscode/src/tsconfig.corpus.json}"
for corpus in $CORPORA; do
  for cfg in "K1:--threads 1 --checkers 1" "def:" "def4:--threads 4 --checkers 4" "sh4:--shared-graph --threads 4 --checkers 4" "def12:--threads 12 --checkers 12" "sh12:--shared-graph --threads 12 --checkers 12"; do
    name=${cfg%%:*}; args=${cfg#*:}
    best=""
    for i in $(seq 1 $RUNS); do
      /usr/bin/time -f "%e %U %S %M" -o "$OUT/g2.t" $BIN -q --keep-going --noEmit $args -p $C/$corpus >/dev/null 2>&1
      read e u s m < <(tail -1 "$OUT/g2.t")
      if [ -z "$best" ] || awk -v a=$e -v b=${best%% *} 'BEGIN{exit !(a<b)}'; then best="$e $(echo "$u + $s" | bc) $((m/1024))"; fi
    done
    echo "$corpus $name $best $(cut -d' ' -f1 /proc/loadavg)"
  done
done
