# Shared-graph stress tooling (plan 86 stage 4d/4e)

Scripts for `--shared-graph` (`TTTT_SHARED_GRAPH=1`) runs at many checkers, compared byte for byte against one
checker's output. Every script sources `paths.sh` and works in `$SG_OUT` (default `/tmp/tttt-sharedgraph`): the
reference outputs, every run's output and the kept diffs/crash logs go there, never into the repo. tttt prints paths
relative to its cwd, so always make the references and the runs through these scripts (they `cd "$SG_OUT"`). A `BIN`
argument may be empty (`""`) for `release/tttt`; other builds (`-g`, TSan, `-DTTTT_GRAPH_TAINT`) are your own, kept in
`$SG_OUT` or another scratch dir.

**References first:** `refs.sh [BIN] [corpus...]` writes `ref.X` for each corpus named, by default the six `CORE`
ones in `paths.sh` (drizzle, zod, excalidraw, effect, nest, vscode; `ALL` is every `-p` corpus): one K1 run, `--threads 1 --checkers 1 --checker-memory off --noEmit --pretty false
-p TSCONFIG`. Regenerate them after any change to the default path's output (they're never committed).

| script | what it does |
|---|---|
| `paths.sh` | sourced: `ROOT`, `OUT`, `P[corpus]` (corpus -> tsconfig), `sg_bin` |
| `refs.sh [BIN] [corpus...]` | the K1 references `ref.X` |
| `rep.sh BIN N corpus...` | N runs at K12 T12 against `ref.X`: same / diff / crash; `diffout.X.I`, `crashlog.X.I` kept (`EXTRA=` adds arguments) |
| `rep3.sh BIN N "VAR=VAL ..." corpus...` | rep.sh with an environment and `TTTT_GRAPH_STATS=1` kept per run (`stats.TAG.X.I`): the 20-run tables |
| (rep3.sh with `TTTT_GRAPH_RECHECK=1`) | the recheck gate: after the workers join, every file checked again by one fresh checker in the finished graph (`=private`: without the graph, a K1 check in the same process) and each file's errors compared with the workers' (program.c graph_recheck()); stderr says `recheck: N of M files differ` and shows the first differing file with both lists (`TTTT_GRAPH_RECHECK_ALL=1`: every one); rep3.sh counts the runs where a file differs and names its first file. Doubles the check's work. Found plan 86 4e item 1's redo and relation-cache bugs |
| `summ.sh TAG` | tabulates rep3.sh's stats files: same/diff and the broken wait cycles (fallbacks) per corpus |
| `quick.sh BIN N corpus...` | rep.sh with the last run's graph totals printed |
| `t3loop.sh BIN N [VAR=VAL...]` | the stress loop's measuring side: drizzle runs (K, T from the environment, default 12), verdict, redos, redone files, limit hits, first differing codes. Run an effect K12 loop beside it for load (`while :; do release/tttt --shared-graph --threads 12 --checkers 12 --noEmit -p ...effect...; done`) |
| `bis.sh BIN N "VAR=VAL ..." corpus` | how many of N runs differ under an environment, with each one's top codes (bisecting a knob) |
| `crashrate.sh N [VAR=VAL...]` | crash count of N effect K12 runs (`BIN=` env to pick the build) |
| `cgtc.sh BIN corpus K [args]` | callgrind instructions inside `program_typecheck` and the worker threads, output checked against `ref.X` |
| `hunt.sh BIN corpus N` | N gdb runs of a `-g` build, each crash's backtrace and registers kept as `crash.X.I` (`HUNT_ENV=`) |
| `hang.sh BIN corpus N SECS` | gdb runs; one past SECS is interrupted and all threads' stacks kept as `hang.X.I` |
| `g2bench.sh BIN [RUNS]` | gate G2's table: best wall, CPU and peak RSS per `-p` corpus at K1, defaults, K4, shared K4, K12, shared K12 (`ONLY=` restricts) |
| `fb.gdb` | `gdb -x fb.gdb --args BIN ...`: a backtrace at every `graph_fallback_hook` (broken wait cycle) |
| `tsan.sh [corpus...]` | the standing race check: a TSan build of the tree (`$TSAN_BUILD`, default `$OUT/btsan-<hash>`), every `-p` corpus (default `ALL`) at `THREADS` (default "4 12") with `TTTT_SHARED_GRAPH=1`; one line per run (clean / N report groups, output against `ref.X`), then every group over all runs; exit 1 if any. ~25 min for all, vscode T12 ~25 GB (`SKIP=vscode`); `TSAN_ENV=`, `EXTRA=` |
| `tsan.py LOG... [--brief]` | groups ThreadSanitizer reports by the unordered pair (access, previous access), innermost non-TSan frames, with the runs each group occurs in and both stacks of its first report |
