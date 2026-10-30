#!/usr/bin/env bash
# tsan.sh [corpus...]: ThreadSanitizer on the corpora under --shared-graph, the standing race check (plan 89 §3: zero
# data races). Builds a TSan copy of the tree (documentation.md "Typecheck": release, -Db_sanitize=thread, -g) in
# $TSAN_BUILD (default $OUT/btsan-<hash of the tree's path>), then runs every corpus (default: ALL in paths.sh, the -p
# corpora) at each of $THREADS (default "4 12") threads and as many checkers with TTTT_SHARED_GRAPH=1, the log of each
# in $OUT/tsan.X.T.log, and prints one line per run: clean, or N report groups (tsan.py's (access, previous access)
# pairs), plus whether the output matched ref.X (refs.sh; "noref" without one). Then every group over all runs with
# the runs it occurs in (tsan.py over the logs). Exit status 1 if any run had a report or crashed.
# EXTRA= adds tttt arguments, TSAN_ENV= environment (e.g. "TTTT_GRAPH_VARIANCE_LOCK=0"). TSan's memory is ~5-8x a
# plain run's: vscode at T12 peaks around 25 GB (SKIP="vscode" leaves it out on a busy box).
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
cd "$OUT" || exit 1
B=${TSAN_BUILD:-$OUT/btsan-$(echo "$ROOT" | md5sum | cut -c1-8)}
# (On aarch64 GCC's libtsan dies of SIGILL in its first pthread_cond_wait: call_pthread_cancel_with_cleanup branches
# into glibc's BTI-guarded sigsetjmp with a jump BTI rejects (GCC 15.2, glibc 2.43). Clang's runtime works)
if test -z "${CC:-}" && test "$(uname -m)" = aarch64; then export CC=clang; fi
if test ! -e "$B/build.ninja"; then
  meson setup "$B" "$ROOT" --buildtype release -Db_sanitize=thread -Dc_args=-g > "$OUT/tsan-build.log" 2>&1 ||
    { cat "$OUT/tsan-build.log" >&2; exit 1; }
fi
ninja -C "$B" tttt > "$OUT/tsan-build.log" 2>&1 || { cat "$OUT/tsan-build.log" >&2; echo "tsan build failed" >&2; exit 1; }
ulimit -s unlimited 2>/dev/null || ulimit -s 4000000
export TSAN_OPTIONS="halt_on_error=0 report_signal_unsafe=0 history_size=4 ${TSAN_OPTIONS:-}"
status=0; logs=()
for c in "${@:-${ALL[@]}}"; do
  case " $SKIP " in *" $c "*) continue ;; esac
  test -n "${P[$c]}" || { echo "unknown corpus $c" >&2; exit 1; }
  for t in ${THREADS:-4 12}; do
    log=tsan.$c.T$t.log
    timeout 3000 env $TSAN_ENV TTTT_SHARED_GRAPH=1 "$B/tttt" --threads $t --checkers $t $EXTRA --noEmit --pretty false \
      -p "${P[$c]}" > tsan.$c.T$t.o 2> $log; st=$?
    logs+=($log)
    n=$(grep -c 'WARNING: ThreadSanitizer' $log)
    if test "$n" -eq 0; then verdict=clean; else
      verdict="$(python3 "$ROOT/scripts/sharedgraph/tsan.py" --brief $log | grep -c .) groups ($n reports)"; status=1; fi
    if test -e ref.$c; then if cmp -s ref.$c tsan.$c.T$t.o; then out=same; else out=diff; fi; else out=noref; fi
    if test $st -ne 0 && test $st -ne 66 && test $st -ne 1 && test $st -ne 2; then out="$out, exit $st"; status=1; fi
    echo "$c T$t: $verdict, output $out"
  done
done
echo "--- groups over all runs"
python3 "$ROOT/scripts/sharedgraph/tsan.py" "${logs[@]}"
exit $status
