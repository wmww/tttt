#!/usr/bin/env bash
# A/B of two tttt binaries at the defaults, runs interleaved (old, new, old, new...) so load drift hits both:
# medians of wall (ms resolution), sys, user, minor faults and peak RSS (wait4's rusage) per corpus, with
# TTTT_SYNC_EXIT=1 (the exit teardown's sys and faults counted in tttt itself rather than in the reaper child).
#
#   scripts/abbench.sh [-n RUNS] OLD NEW CORPUS|PATH... [-- tttt args...]
#
# CORPUS is a name of instrcount.sh's (drizzle, zod, nest, vscode...) or a tsconfig/project path. Copy the binaries
# out first (`cp -L fast/tttt /somewhere/tttt.old`): fast/tttt is a symlink a rebuild replaces. Default 5 runs each.
# Walls on a loaded box are noise: check `uptime` before trusting a few percent.
set -euo pipefail
cd "$( dirname "${BASH_SOURCE[0]}" )/.."
N=5
if test "${1:-}" = -n; then N="$2"; shift 2; fi
test $# -ge 3 || { sed -n '2,10p' "$0" >&2; exit 2; }
OLD="$1"; NEW="$2"; shift 2
PROJECTS=()
while test $# -gt 0 && test "$1" != --; do PROJECTS+=("$1"); shift; done
test "${1:-}" != -- || shift
declare -A C=(
    [tsc]=thirdparty/corpora/TypeScript6/src/compiler
    [type-fest]=thirdparty/corpora/type-fest
    [valibot]=thirdparty/corpora/valibot/library/tsconfig.corpus.json
    [react]=thirdparty/corpora/bulletproof-react/apps/react-vite/tsconfig.corpus.json
    [nest]=thirdparty/corpora/nestjs-boilerplate/tsconfig.corpus.json
    [webpack]=thirdparty/corpora/webpack
    [zod]=thirdparty/corpora/zod/packages/zod/tsconfig.test.json
    [xstate]=thirdparty/corpora/xstate/packages/core/tsconfig.corpus.json
    [drizzle]=thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json
    [vue]=thirdparty/corpora/vue/tsconfig.corpus.json
    [effect]=thirdparty/corpora/effect/packages/effect/tsconfig.corpus.json
    [vscode]=thirdparty/corpora/vscode/src/tsconfig.corpus.json
    [excalidraw]=thirdparty/corpora/excalidraw/tsconfig.corpus.json
    [tanstack-router]=thirdparty/corpora/tanstack-router/tsconfig.corpus.json
)
RUNNER='
import os, sys, time, subprocess, statistics as st
name, n, old, new, path, args = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6:]
env = {**os.environ, "TTTT_SYNC_EXIT": "1"}
d = {"old": [], "new": []}
for _ in range(n):
    for side, b in (("old", old), ("new", new)):
        t0 = time.monotonic()
        pr = subprocess.Popen([b, "--noEmit", "--pretty", "false", *args, "-p", path], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, env=env)
        _, _, ru = os.wait4(pr.pid, 0)
        d[side].append((time.monotonic() - t0, ru.ru_stime, ru.ru_utime, ru.ru_minflt, ru.ru_maxrss / 1024))
m = {k: [st.median(r[i] for r in v) for i in range(5)] for k, v in d.items()}
for k in ("old", "new"):
    w, s, u, f, r = m[k]
    print("%-16s %s wall %6.3f  sys %5.3f  user %6.2f  minflt %8d  maxrss %6d MB" % (name, k, w, s, u, f, r))
o, nw = m["old"], m["new"]
print("%-16s     wall %+5.1f%%  sys %+5.1f%%  minflt %+5.1f%%  maxrss %+5.1f%%" % (name,
      *(100 * (nw[i] - o[i]) / o[i] if o[i] else 0 for i in (0, 1, 3, 4))), flush=True)
'
for p in "${PROJECTS[@]}"; do
    python3 -c "$RUNNER" "$p" "$N" "$OLD" "$NEW" "${C[$p]:-$p}" "$@"
done
