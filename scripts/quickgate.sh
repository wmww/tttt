#!/usr/bin/env bash
# The standard quick gate in one command, one PASS/FAIL line per piece and exit 1 when any fails:
#   suite        release/baseline_tests --summary (fasttest.sh's): 0 newly failing tests against the saved results
#   tsc zod drizzle   scripts/realworld.py on the corpora: 0 spurious / 0 missing against tsc's cache, exit 0-2
#   instrcount   scripts/instrcount.sh --vs BASE drizzle zod: release/tttt against BASE's release build
#                (scripts/abbuild.sh), no rise over 2% (a drop is noted): this change's cost, not the drift of main's
#                ~100 fixes since test/instrcount.txt's last save, and no PGO + BOLT retraining per fix. BASE is
#                `git merge-base HEAD main` (the main this branch started from); BASE's counts are cached
#   --no-instr   skip it ("instrcount: skipped (batch gate)": the coordinator's batch gate measures)
#   --vs-main    the suite against BASE's own results (abbuild.sh --suite-results, run once per BASE) rather than
#                test/baseline_results.ts: "new passed/failed" are then this change's, not main's unsaved drift
#   -p REGEX     also scripts/sourcesweep.py -f REGEX (every matching project matches tsc) and
#                scripts/kdiff.py -k 16 -f REGEX (no project differs between 1 and 16 checkers): the touched project
#   -k           also scripts/threadcheck.sh and scripts/checkerdiff.py on drizzle zod, and scripts/sgcheck.py (the shared
#                graph, the default mode at K > 1, against one plain checker) at K4 and K16, both deals, one run, on
#                zod, drizzle and three hubs (openbot/packages/i18n, dokploy/apps/dokploy, karakeep/packages/trpc)
#
#   scripts/quickgate.sh                   # ~3 min on a busy box (callgrind of two binaries; ~1 min with --no-instr)
#   scripts/quickgate.sh -p '^trpc/' -k --vs-main
#
# The pieces run in parallel (one instrcount: perf counts instructions, not time, so load doesn't skew it); each one's
# output is in /tmp/quickgate-XXXXXX/NAME.txt (printed at the end; delete it when done). It doesn't save the suite's
# results (test/baseline_results.ts: the coordinator saves those per batch, workers don't commit them), so the
# suite's "newly failing" is against what main last saved (--vs-main: against BASE's run). A -p project that already
# mismatched on main fails too: compare with main's output when it does. The oracles, sweeps and callgrind run under
# nice -n 10 (the box is shared; the suite is the one piece a worker waits on first).
set -uo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
PROJECT=
KCHECK=
NOINSTR=
VSMAIN=
usage() { sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
while test $# -gt 0; do
    case "$1" in
        -p) PROJECT="$2"; shift 2 ;;
        -k) KCHECK=1; shift ;;
        --no-instr) NOINSTR=1; shift ;;
        --vs-main) VSMAIN=1; shift ;;
        *) usage ;;
    esac
done
BASE=$(git merge-base HEAD main)
LOG=$(mktemp -d /tmp/quickgate-XXXXXX)
if ! ninja -C release > "$LOG/build.txt" 2>&1; then
    tail -20 "$LOG/build.txt"
    echo "FAIL  build (log: $LOG/build.txt)"
    exit 1
fi
thirdparty/ensure-typescript.sh > /dev/null || { echo "FAIL  thirdparty/ensure-typescript.sh"; exit 1; }
SUITE_ARGS=(--summary)
if test -n "$VSMAIN"; then
    # (Builds BASE's tree out of tree if needed, ~40 s, then its suite once, ~20 s; cached)
    if ! RES=$(scripts/abbuild.sh "$BASE" --suite-results 2> "$LOG/abbuild.txt"); then
        tail -5 "$LOG/abbuild.txt"; echo "FAIL  abbuild.sh $BASE --suite-results (log: $LOG/abbuild.txt)"; exit 1
    fi
    SUITE_ARGS+=(--results-path "$RES")
fi
N=(nice -n 10)

NAMES=()
# start NAME COMMAND...: runs it in the background, its output in $LOG/NAME.txt, exit status and seconds in NAME.rc
start() {
    local name="$1"; shift
    NAMES+=("$name")
    ( s=$SECONDS; "$@" > "$LOG/$name.txt" 2>&1; echo "$? $((SECONDS - s))" > "$LOG/$name.rc" ) &
    eval "PID_${name//-/_}=$!"
}
start suite ./release/baseline_tests "${SUITE_ARGS[@]}"
start tsc "${N[@]}" scripts/realworld.py --no-build
start zod "${N[@]}" scripts/realworld.py --no-build --corpus zod
start drizzle "${N[@]}" scripts/realworld.py --no-build --corpus drizzle
test -n "$NOINSTR" || start instrcount "${N[@]}" scripts/instrcount.sh --bin release/tttt --vs "$BASE" drizzle zod
if test -n "$PROJECT"; then
    start sourcesweep "${N[@]}" scripts/sourcesweep.py --no-build -f "$PROJECT"
    start kdiff "${N[@]}" scripts/kdiff.py -k 16 -f "$PROJECT"
fi
if test -n "$KCHECK"; then
    start threadcheck "${N[@]}" env TTTT_NO_ENSURE=1 scripts/threadcheck.sh drizzle zod
    start checkerdiff "${N[@]}" env TTTT_NO_ENSURE=1 scripts/checkerdiff.py drizzle zod
    start sgcheck "${N[@]}" scripts/sgcheck.py -k 4,16 -n 1 -j 2 \
        -f '^(zod|drizzle|openbot/packages/i18n|dokploy/apps/dokploy|karakeep/packages/trpc)$'
fi
for n in "${NAMES[@]}"; do
    pid_var="PID_${n//-/_}"
    wait "${!pid_var}"
done

FAILED=0
plain() { sed 's/\x1b\[[0-9;]*m//g' "$LOG/$1.txt"; }
verdict() {  # NAME PASS|FAIL DETAIL
    local rc secs
    read -r rc secs < "$LOG/$1.rc"
    test "$2" = PASS || FAILED=1
    printf '%-5s %-12s %5ss  %s\n' "$2" "$1" "$secs" "$3"
}
echo
test -z "$VSMAIN" || echo "(suite against $(git log -1 --format='%h %s' "$BASE" | cut -c1-80))"
test -z "$NOINSTR" || echo "      instrcount: skipped (batch gate)"
for n in "${NAMES[@]}"; do
    read -r rc secs < "$LOG/$n.rc"
    case "$n" in
        suite)
            d=$(plain suite | grep -oE '[0-9]+ new passed, [0-9]+ new failed' | tail -1)
            lost=$(echo "$d" | grep -oE '[0-9]+ new failed' | grep -oE '^[0-9]+')
            if test -z "$d" && test "$rc" = 0 && plain suite | grep -qE '^ *[0-9]+ passed, [0-9]+ failed, [0-9]+ total'; then
                verdict suite PASS "no test changed"
            elif test "$lost" = 0; then verdict suite PASS "$d"
            else verdict suite FAIL "${d:-no summary (exit $rc)}"; fi ;;
        tsc|zod|drizzle)
            d=$(plain "$n" | grep -oE 'against tsc: [0-9]+ spurious, [0-9]+ missing' | head -1)
            if echo "$d" | grep -q ': 0 spurious, 0 missing' && plain "$n" | grep -qE 'exit [012],'; then
                verdict "$n" PASS "$d"
            else verdict "$n" FAIL "${d:-no comparison} $(plain "$n" | grep -oE '(exit [0-9]+|KILLED BY SIGNAL [0-9]+),' | head -1)"; fi ;;
        instrcount)
            rise=$(plain instrcount | grep -E '\+[0-9.]+%  <--' || true)
            counts=$(plain instrcount | grep -E '^(drizzle|zod) ' | awk '{printf "%s %s %s  ", $1, $2 $3, $4}')
            if test "$rc" != 0 || test -z "$counts"; then verdict instrcount FAIL "exit $rc"
            elif test -n "$rise"; then verdict instrcount FAIL "rise over 2%: $counts"
            else verdict instrcount PASS "$counts"; fi ;;
        sourcesweep)
            d=$(plain sourcesweep | grep -oE '[0-9]+ / [0-9]+ projects match' | head -1)
            if test -n "$d" && test "$(echo "$d" | awk '{print $1}')" = "$(echo "$d" | awk '{print $3}')" && test "$(echo "$d" | awk '{print $3}')" != 0; then
                verdict sourcesweep PASS "$d"
            else verdict sourcesweep FAIL "${d:-no summary (exit $rc)} (-f '$PROJECT')"; fi ;;
        kdiff)
            d=$(plain kdiff | tail -1)
            if test "$rc" = 0; then verdict kdiff PASS "$d"; else verdict kdiff FAIL "$d"; fi ;;
        threadcheck|checkerdiff|sgcheck)
            d=$(plain "$n" | tail -1)
            if test "$rc" = 0; then verdict "$n" PASS "$d"; else verdict "$n" FAIL "$d"; fi ;;
    esac
done
echo "logs: $LOG (rm -r $LOG when done)"
if test "$FAILED" = 0; then echo "quickgate: PASS"; else echo "quickgate: FAIL"; fi
exit "$FAILED"
