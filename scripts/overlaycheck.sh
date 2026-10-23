#!/usr/bin/env bash
# Do tttt's Go overlays on tsgo still apply and compile? Run it at every re-pin of thirdparty/typescript (and after
# editing an overlay), so a broken patch is seen then, not the day a worker needs the tool (reentrytrace's overlay was
# broken by the 7.1 beta for days). One OK/BROKEN line each, exit 1 when any is broken; nothing is kept and the shared
# checkout is never written:
#   oracle           tttt-oracle, tttt-probe.test and tttt-test-mapper (ensure-typescript.sh's OVERLAY list,
#                    this tree's test/oracle), compiled to /dev/null
#   reentrytrace     scripts/reentrytrace.py --check (patched checker.go, relater.go)
#   variancetrace    scripts/variancetrace.py --check (patched relater.go)
#   tsgotrace        scripts/tsgotrace.py --check (patched ast/diagnostic.go, the hooks)
#
#   scripts/overlaycheck.sh          # ~2-4 min cold (each overlay recompiles internal/checker), logs in /tmp/overlaycheck-*
set -uo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
thirdparty/ensure-typescript.sh > /dev/null || { echo "BROKEN ensure-typescript.sh"; exit 1; }
LOG=$(mktemp -d /tmp/overlaycheck-XXXXXX)
TSC=$(realpath thirdparty/typescript/tsc)
ENV=$(thirdparty/ensure-typescript.sh --env)
# (test/oracle's overlay map, written here from this tree's files rather than reusing built/overlay.json, which
# points at whichever tree last rebuilt the tools)
python3 - "$TSC" "$PWD/test/oracle" > "$LOG/oracle-overlay.json" <<'PY'
import json, os, re, sys
tsc, oracle = sys.argv[1:]
body = re.search(r'\nOVERLAY=\((.*?)\n\)', open('thirdparty/ensure-typescript.sh').read(), re.S).group(1).split()
print(json.dumps({'Replace': {os.path.join(tsc, body[i + 1]): os.path.realpath(os.path.join(oracle, body[i]))
                              for i in range(0, len(body), 2)}}))
PY
FAILED=0
one() {  # NAME COMMAND...
    local name="$1"; shift
    if "$@" > "$LOG/$name.txt" 2>&1; then echo "OK      $name"
    else FAILED=1; echo "BROKEN  $name (log: $LOG/$name.txt)"; tail -5 "$LOG/$name.txt" | sed 's/^/        /'; fi
}
one oracle bash -c "$ENV
cd '$TSC' && go build -overlay '$LOG/oracle-overlay.json' -o /dev/null ./cmd/tttt-oracle ./cmd/tttt-test-mapper &&
    go test -overlay '$LOG/oracle-overlay.json' -c -o /dev/null ./internal/testrunner"
one reentrytrace scripts/reentrytrace.py --check
one variancetrace scripts/variancetrace.py --check
one tsgotrace scripts/tsgotrace.py --check
if test "$FAILED" = 0; then rm -rf "$LOG"; echo "overlaycheck: OK"; else echo "overlaycheck: BROKEN (logs: $LOG)"; fi
exit "$FAILED"
