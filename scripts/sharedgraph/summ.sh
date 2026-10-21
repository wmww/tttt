#!/usr/bin/env bash
# summ.sh TAG: per corpus, rep3.sh's runs same/diff and their shadowed-worker retirements, from its stats files
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
cd "$OUT" || exit 1
for c in excalidraw drizzle effect zod nest vscode; do
  ls stats.$1.$c.* >/dev/null 2>&1 || continue
  for f in stats.$1.$c.*; do echo "$(tail -1 $f) $(grep -c 'shadowed worker' $f)"; done |
    awk -v c=$c '{n[$1]++; fb+=$2; if ($2>0) wfb[$1]++} END {printf "%s: same %d diff %d, shadow retirements %d total, runs with any: same %d diff %d\n", c, n["same"], n["diff"], fb, wfb["same"], wfb["diff"]}'
done
