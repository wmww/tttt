#!/usr/bin/env bash
# The standard quick gate in one command, one PASS/FAIL line per piece and exit 1 when any fails:
#   suite        release/baseline_tests --summary (fasttest.sh's): 0 newly failing tests against the saved results
#   tsc zod drizzle   scripts/realworld.py on the corpora: 0 spurious / 0 missing against tsc's cache, exit 0-2
#   instrcount   scripts/instrcount.sh drizzle zod: no rise over 2% against test/instrcount.txt (a drop is noted)
#   -p REGEX     also scripts/sourcesweep.py -f REGEX (every matching project matches tsc) and
#                scripts/kdiff.py -k 16 -f REGEX (no project differs between 1 and 16 checkers): the touched project
#   -k           also scripts/threadcheck.sh and scripts/checkerdiff.py on drizzle zod
#
#   scripts/quickgate.sh                   # ~2.5 min on a busy box, ~5 when instrcount rebuilds fast/tttt (PGO)
#   scripts/quickgate.sh -p '^trpc/' -k
#
# The pieces run in parallel (one instrcount: perf counts instructions, not time, so load doesn't skew it); each one's
# output is in /tmp/quickgate-XXXXXX/NAME.txt (printed at the end; delete it when done). It doesn't save the suite's
# results (test/baseline_results.ts: the coordinator saves those per batch, workers don't commit them), so the
# suite's "newly failing" is against what main last saved. A -p project that already mismatched on main fails too:
# compare with main's output when it does.
set -uo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
PROJECT=
KCHECK=
while getopts "p:kh" opt; do
    case "$opt" in
        p) PROJECT="$OPTARG" ;;
        k) KCHECK=1 ;;
        *) sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
    esac
done
LOG=$(mktemp -d /tmp/quickgate-XXXXXX)
if ! ninja -C release > "$LOG/build.txt" 2>&1; then
    tail -20 "$LOG/build.txt"
    echo "FAIL  build (log: $LOG/build.txt)"
    exit 1
fi
thirdparty/ensure-typescript.sh > /dev/null || { echo "FAIL  thirdparty/ensure-typescript.sh"; exit 1; }

NAMES=()
# start NAME COMMAND...: runs it in the background, its output in $LOG/NAME.txt, exit status and seconds in NAME.rc
start() {
    local name="$1"; shift
    NAMES+=("$name")
    ( s=$SECONDS; "$@" > "$LOG/$name.txt" 2>&1; echo "$? $((SECONDS - s))" > "$LOG/$name.rc" ) &
    eval "PID_${name//-/_}=$!"
}
start suite ./release/baseline_tests --summary
start tsc scripts/realworld.py --no-build
start zod scripts/realworld.py --no-build --corpus zod
start drizzle scripts/realworld.py --no-build --corpus drizzle
start instrcount scripts/instrcount.sh drizzle zod
if test -n "$PROJECT"; then
    start sourcesweep scripts/sourcesweep.py --no-build -f "$PROJECT"
    start kdiff scripts/kdiff.py -k 16 -f "$PROJECT"
fi
if test -n "$KCHECK"; then
    start threadcheck env TTTT_NO_ENSURE=1 scripts/threadcheck.sh drizzle zod
    start checkerdiff env TTTT_NO_ENSURE=1 scripts/checkerdiff.py drizzle zod
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
for n in "${NAMES[@]}"; do
    read -r rc secs < "$LOG/$n.rc"
    case "$n" in
        suite)
            d=$(plain suite | grep -oE '[0-9]+ new passed, [0-9]+ new failed' | tail -1)
            lost=$(echo "$d" | grep -oE '[0-9]+ new failed' | grep -oE '^[0-9]+')
            if test "$lost" = 0; then verdict suite PASS "$d"
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
        threadcheck|checkerdiff)
            d=$(plain "$n" | tail -1)
            if test "$rc" = 0; then verdict "$n" PASS "$d"; else verdict "$n" FAIL "$d"; fi ;;
    esac
done
echo "logs: $LOG (rm -r $LOG when done)"
if test "$FAILED" = 0; then echo "quickgate: PASS"; else echo "quickgate: FAIL"; fi
exit "$FAILED"
