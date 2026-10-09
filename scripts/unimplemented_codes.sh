#!/usr/bin/env bash
# Missing diagnostic codes (per the last scripts/failstats.py run) that src/ never mentions: checks tttt doesn't have at all.
# Output: missing-count tests code message. Usage: scripts/unimplemented_codes.sh [rows=150]
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
grep -rhoE "TS[0-9]{4,5}|\b[0-9]{4,5}\b" src | sed 's/^TS//' | sort -u > /tmp/src_codes.txt
scripts/failstats.py --no-run -n "${1:-150}" 2>&1 | sed -n '/missing diagnostics/,/spurious diagnostics/p' \
    | grep -oP '^\s+\d+\s+in\s+\d+ tests\s+TS\d+' | while read -r n _ t _ c; do
    if ! grep -qx "${c#TS}" /tmp/src_codes.txt; then echo "$n $t $c"; fi
done | python3 -c '
import json, sys
d = json.load(open("thirdparty/typescript/tsc/internal/diagnostics/diagnosticMessages.json"))
m = {v["code"]: k for k, v in d.items()}
for line in sys.stdin:
    n, t, c = line.split()
    print(f"{n:>5} {t:>4}  {c:<8} {m.get(int(c[2:]), chr(63))[:90]}")
'
