#!/usr/bin/env bash
# refs.sh [BIN] [corpus...]: the K1 reference outputs ref.X in OUT (one checker, no recycling, --pretty false), which
# every other script compares a --shared-graph run against. Default: release/tttt, the six CORE corpora in paths.sh
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "${1:-}"); shift || true
cd "$OUT" || exit 1
for c in "${@:-${CORE[@]}}"; do
  "$BIN" --threads 1 --checkers 1 --checker-memory off --noEmit --pretty false -p "${P[$c]}" > ref.$c 2>/dev/null
  echo "ref.$c: $(wc -l < ref.$c) lines"
done
