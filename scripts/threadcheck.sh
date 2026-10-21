#!/usr/bin/env bash
# Plan 36: tttt's output (pretty: with related information) must not depend on the thread count. Runs every pinned corpus (or the ones
# named) with --threads 1 once and --threads N (default 4) three times, all with the same --checkers K (default 4),
# and cmps the outputs.
#   scripts/threadcheck.sh                 # every corpus
#   scripts/threadcheck.sh webpack zod     # some
#   THREADS=2 CHECKERS=8 scripts/threadcheck.sh
# thirdparty/ensure-corpora.sh runs only when a corpus it checks is missing; TTTT_NO_ENSURE=1 errors out then instead
# (worktrees). The environment goes through to tttt (TTTT_SHARED_GRAPH=1...)
set -uo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
ninja -C release tttt > /dev/null || exit 1
THREADS=${THREADS:-4}
CHECKERS=${CHECKERS:-4}
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
corpora=$(PYTHONPATH=scripts python3 -c 'import realworld, os, sys
for name, path in realworld.CORPORA.items():
    flag = "-b" if name in realworld.BUILD_CORPORA else "-p"
    if len(sys.argv) < 2 or name in sys.argv[1:]: print(name, flag, os.path.relpath(os.path.realpath(path)))' "$@")
missing=$(while read -r name flag path; do test -e "$path" || echo "$name"; done <<< "$corpora")
if test -n "$missing"; then
    if test -n "${TTTT_NO_ENSURE:-}"; then echo "missing corpora (TTTT_NO_ENSURE set):" $missing >&2; exit 1; fi
    thirdparty/ensure-corpora.sh > /dev/null || exit 1
    corpora=$(PYTHONPATH=scripts python3 -c 'import realworld, os, sys
for name, path in realworld.CORPORA.items():
    flag = "-b" if name in realworld.BUILD_CORPORA else "-p"
    if len(sys.argv) < 2 or name in sys.argv[1:]: print(name, flag, os.path.relpath(os.path.realpath(path)))' "$@")
fi
failed=0
while read -r name flag path; do
    run() { ./release/tttt $flag "$path" --no-write --pretty --keep-going --checkers "$CHECKERS" --threads "$1" > "$out/$name.$2" 2>&1; }
    run 1 base
    status=identical
    for i in 1 2 3; do
        run "$THREADS" "$i"
        cmp -s "$out/$name.base" "$out/$name.$i" || { status="DIFFERENT (run $i)"; failed=1; }
    done
    echo "$name: $(grep -c ' TS[0-9]*: ' "$out/$name.base") diagnostics, threads 1 vs $THREADS x3: $status"
done <<< "$corpora"
exit $failed
