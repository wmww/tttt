#!/usr/bin/env bash
# seeds.sh BIN corpus N [FIRST]: N seeded schedules (TTTT_GRAPH_SEED=FIRST..FIRST+N-1, FIRST default 1) of a
# --shared-graph K12 T12 run (K=, T= override; --checker-memory off) against ref.X, J at a time (default 8: each run
# keeps about one core busy, the token lets one worker run). A seed is a schedule: one that differs (or crashes, or
# hangs past 600 s) replays with `TTTT_GRAPH_SEED=S TTTT_SHARED_GRAPH=1 BIN --threads 12 --checkers 12
# --checker-memory off --noEmit --pretty false -p TSCONFIG` (from $OUT: the paths are relative to it). Prints the
# failing seeds with their first differing line, and a summary line. Differing outputs: $OUT/seed.X.S.o; SEED_ENV= adds
# environment (TTTT_GRAPH_SEED_RATE=N: a handoff one point in N, default 16; TTTT_GRAPH_RECHECK=1)
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
BIN=$(sg_bin "$1"); c=$2; N=$3; FIRST=${4:-1}
cd "$OUT" || exit 1
test -e ref.$c || { echo "no ref.$c: refs.sh first" >&2; exit 1; }
export BIN c SEED_ENV K=${K:-12} T=${T:-12} TSCONFIG=${P[$c]}
one() {
  s=$1
  timeout 600 env $SEED_ENV TTTT_GRAPH_SEED=$s TTTT_SHARED_GRAPH=1 "$BIN" --threads $T --checkers $K --checker-memory off \
    --noEmit --pretty false -p "$TSCONFIG" > seed.$c.$s.o 2> seed.$c.$s.e; st=$?
  if [ $st -ge 124 ]; then echo "seed $s: crash/hang (exit $st): $(tail -1 seed.$c.$s.e)"; return; fi
  rc=$(grep -m1 '^recheck: [1-9][0-9]* of' seed.$c.$s.e)
  if cmp -s ref.$c seed.$c.$s.o && test -z "$rc"; then rm -f seed.$c.$s.o seed.$c.$s.e; echo "seed $s: same"; return; fi
  echo "seed $s: differs: $(diff ref.$c seed.$c.$s.o | grep -m1 '^[<>]' | cut -c1-200) $rc"
}
export -f one
seq $FIRST $((FIRST + N - 1)) | xargs -P ${J:-8} -I{} bash -c 'one {}' > seeds.$c.log
grep -v ': same$' seeds.$c.log | sort -t' ' -k2 -n
echo "$c: $(grep -c ': same$' seeds.$c.log) of $N seeds K1's output (seeds $FIRST..$((FIRST + N - 1)))"
