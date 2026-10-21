#!/usr/bin/env bash
# fingerprint.sh N corpus...: the fingerprint-at-publish oracle (src/ty/graph_debug.c). Builds a -DTTTT_GRAPH_FINGERPRINT
# copy of the tree in $FP_BUILD (default $OUT/bfp-<hash of the tree's path>), then runs each corpus N times with
# TTTT_SHARED_GRAPH=1 at K12 T12 (K=, T= override) in count mode (TTTT_GRAPH_FINGERPRINT=${FP_MODE:-print}): each run
# first checks everything with one private checker, recording every result's fingerprint by key, then the workers
# compare what they compute and publish. One line per run (differences, the output against ref.X), then the
# differences over all runs grouped by (kind, file, subject, result) with one symbolized backtrace each. Logs:
# $OUT/fp.X.I.log, differing outputs fpdiff.X.I. FP_ENV= adds environment (TTTT_GRAPH_FINGERPRINT_TRACE=1: both values' fields)
source "$(dirname "${BASH_SOURCE[0]}")/paths.sh"
N=$1; shift
cd "$OUT" || exit 1
B=${FP_BUILD:-$OUT/bfp-$(echo "$ROOT" | md5sum | cut -c1-8)}
if test ! -e "$B/build.ninja"; then
  meson setup "$B" "$ROOT" --buildtype release -Dc_args='-g -DTTTT_GRAPH_FINGERPRINT' > "$OUT/fp-build.log" 2>&1 ||
    { cat "$OUT/fp-build.log" >&2; exit 1; }
fi
ninja -C "$B" tttt > "$OUT/fp-build.log" 2>&1 || { cat "$OUT/fp-build.log" >&2; exit 1; }
K=${K:-12}; T=${T:-12}
logs=()
for c in "$@"; do
  for i in $(seq 1 $N); do
    log=fp.$c.$i.log
    timeout 1200 env $FP_ENV TTTT_GRAPH_FINGERPRINT=${FP_MODE:-print} TTTT_SHARED_GRAPH=1 "$B/tttt" --threads $T --checkers $K \
      --noEmit --pretty false -p "${P[$c]}" > fp.$c.o 2> $log; st=$?
    logs+=($log)
    if test -e ref.$c; then if cmp -s ref.$c fp.$c.o; then out=same; else out=diff; cp fp.$c.o fpdiff.$c.$i; fi; else out=noref; fi
    n=$(grep -o '^fingerprint: [0-9]* differences' $log | grep -o '[0-9]*')
    echo "$c $i: ${n:-?} differences, output $out${st:+, exit $st}"
  done
done
python3 - "$B/tttt" "${logs[@]}" <<'PY'
import re, subprocess, sys
binary, logs = sys.argv[1], sys.argv[2:]
groups = {}
for path in logs:
    lines = open(path, errors='replace').read().split('\n')
    for i, l in enumerate(lines):
        m = re.match(r'fingerprint: differs from K1: (.*?) key \d+ K1 \d+ now \d+ \(worker \d+, checking (.*)\)$', l)
        if not m: continue
        desc, frames = [], []
        for x in lines[i + 1:]:
            if x.startswith('fingerprint:   '): desc.append(x[15:][:200])
            elif re.search(r'\(\+0x[0-9a-f]+\)', x): frames.append(re.search(r'\(\+(0x[0-9a-f]+)\)', x).group(1))
            else: break
        k = (m.group(1), m.group(2).split('/')[-1], ' | '.join(d for d in desc if not d.startswith(('K1  fields', 'now fields'))))
        g = groups.setdefault(k, [0, set(), frames])
        g[0] += 1
        g[1].add(path)
def symbolize(frames):
    if not frames: return []
    out = subprocess.run(['addr2line', '-f', '-s', '-e', binary, *frames], capture_output=True, text=True).stdout.split('\n')
    return [f'{out[j]}@{out[j + 1]}' for j in range(0, len(out) - 1, 2)]
print(f'--- {len(groups)} distinct differences over {len(logs)} runs')
for (kind, file, desc), (n, runs, frames) in sorted(groups.items(), key=lambda x: -x[1][0]):
    print(f'{n} in {len(runs)} runs: {kind}, checking {file}: {desc}')
    print('    ' + ' < '.join(f for f in symbolize(frames)[2:12] if not f.startswith('??')))
PY
