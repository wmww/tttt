# Shared-graph stress tooling (plan 86 stage 4d/4e)

Scripts for `--shared-graph` (`TTTT_SHARED_GRAPH=1`) runs at many checkers, compared byte for byte against one
checker's output. Every script sources `paths.sh` and works in `$SG_OUT` (default `/tmp/tttt-sharedgraph`): the
reference outputs, every run's output and the kept diffs/crash logs go there, never into the repo. tttt prints paths
relative to its cwd, so always make the references and the runs through these scripts (they `cd "$SG_OUT"`). A `BIN`
argument may be empty (`""`) for `release/tttt`; other builds (`-g`, TSan, `-DTTTT_GRAPH_FINGERPRINT`) are your own, kept in
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
| `fingerprint.sh N corpus...` | the fingerprint-at-publish oracle (src/ty/graph_debug.c, a `-DTTTT_GRAPH_FINGERPRINT` build in `$FP_BUILD`, default `$OUT/bfp-<hash>`): each run first checks everything with one private checker in the same process, recording every result's structural fingerprint by key (members, prop types, signature returns, constraints, declared types, inferred returns, variances, declaration memos, a dozen intern memo kinds), then the K12 workers compare each one they compute and publish. One line per run, then the differences over all runs grouped by (kind, file, subject, result) with a symbolized backtrace. `FP_MODE=1` aborts at the first; `FP_ENV="TTTT_GRAPH_FINGERPRINT_TRACE=1"` prints both values' fields. ~3x a K12 run |
| `seeds.sh BIN corpus N [FIRST]` | seeded deterministic schedules (`TTTT_GRAPH_SEED=S`, src/ty/graph_sched.c): one worker runs at a time and hands a token on in a seeded order at the scheduling points (claims, identity publication, memo flushes, file starts), so a run is a function of its seed and a failing seed replays (the command is in the script's header). N seeds from FIRST, `J` at a time (default 8), K12 T12 `--checker-memory off`, against `ref.X`; failing seeds listed with their first differing line, outputs kept as `seed.X.S.o`. `SEED_ENV="TTTT_GRAPH_SEED_RATE=4"`: a handoff one point in 4 (default 16) |
| `symbolize.py BIN [LOG]` | a log's glibc backtrace frames (the fingerprint oracle's `BIN(+0x...)`) replaced by `function@file:line` (addr2line, inlined frames joined) |
| (fingerprint + seed) | a named leaking key for a failing seed: `TTTT_GRAPH_FINGERPRINT=1 TTTT_GRAPH_FINGERPRINT_STACKS=1 TTTT_GRAPH_FINGERPRINT_TRACE=K TTTT_GRAPH_FINGERPRINT_ONLY=path-part TTTT_GRAPH_SEED=S TTTT_SHARED_GRAPH=1 bfp/tttt --threads 12 --checkers 12 --checker-memory off ...`, stderr through symbolize.py: the first differing publication, its key, both values' fields, the publisher's stack and K1's. The seed replays under the fingerprint build too (the record runs before the workers) |
| `summ.sh TAG` | tabulates rep3.sh's stats files: same/diff and the shadowed workers retired per corpus |
| `quick.sh BIN N corpus...` | rep.sh with the last run's graph totals printed |
| `t3loop.sh BIN N [VAR=VAL...]` | the stress loop's measuring side: drizzle runs (K, T from the environment, default 12), verdict, shadowed workers retired, limit hits, first differing codes. Run an effect K12 loop beside it for load (`while :; do release/tttt --shared-graph --threads 12 --checkers 12 --noEmit -p ...effect...; done`) |
| `bis.sh BIN N "VAR=VAL ..." corpus` | how many of N runs differ under an environment, with each one's top codes (bisecting a knob) |
| `crashrate.sh N [VAR=VAL...]` | crash count of N effect K12 runs (`BIN=` env to pick the build) |
| `cgtc.sh BIN corpus K [args]` | callgrind instructions inside `program_typecheck` and the worker threads, output checked against `ref.X` |
| `hunt.sh BIN corpus N` | N gdb runs of a `-g` build, each crash's backtrace and registers kept as `crash.X.I` (`HUNT_ENV=`) |
| `hang.sh BIN corpus N SECS` | gdb runs; one past SECS is interrupted and all threads' stacks kept as `hang.X.I` |
| `g2bench.sh BIN [RUNS]` | gate G2's table: best wall, CPU and peak RSS per `-p` corpus at K1, defaults, K4, shared K4, K12, shared K12 (`ONLY=` restricts) |
| `tsan.sh [corpus...]` | the standing race check: a TSan build of the tree (`$TSAN_BUILD`, default `$OUT/btsan-<hash>`), every `-p` corpus (default `ALL`) at `THREADS` (default "4 12") with `TTTT_SHARED_GRAPH=1`; one line per run (clean / N report groups, output against `ref.X`), then every group over all runs; exit 1 if any. ~25 min for all, vscode T12 ~25 GB (`SKIP=vscode`); `TSAN_ENV=`, `EXTRA=` |
| `tsan.py LOG... [--brief]` | groups ThreadSanitizer reports by the unordered pair (access, previous access), innermost non-TSan frames, with the runs each group occurs in and both stacks of its first report |
| `rootcensus.py run [-f REGEX] [--out F]` / `report` | the root census (agents/notes/threading.md "The census"): a `-DTTTT_ROOT_CENSUS` build in /tmp/tttt-rootcensus run with `TTTT_DUP_CENSUS=1` at one thread, K1, the default K and K8 round robin: each type's root declaration under four rules and which checkers touched it; plain and flagged typecheck CPU next to it; the report models "share on second touch" (routed/plain) per project and at breadth |

**Fingerprint coverage** (graph_debug.c's header has the details). Fingerprinted, clean results only (the taint counter
around each computation; `TTTT_GRAPH_FINGERPRINT_TAINTED=1` compares tainted ones too, counted apart): members and
declared raw members, property types, signature returns, signature resolution (the SigResolved record), type
parameter constraints, defaults and base constraints, symbols' declared types, inferred return types, variances,
declaration memos, conditional branches, mapped templates, deferred references' arguments, implied constraints' check
types, class base type variables, equivalent bases (a shared word: SW_FP() in overlay.h, the point every converted site
should call), whether an intersection reduces to never, the relation results the workers share, and the intern memo
kinds fingerprint.inc's fp_key() knows (regular unions, widened objects, conditional instances, distributive
constraints, union pairs, keyof properties, normalized intersections, reduced unions, indexed, simplified,
definitely-related). Not fingerprinted, and why: identities (the value is the key), scratch kinds and expression memos
(never published), the other memo kinds (KEY_ALIAS_INSTANCE, KEY_UNION_NODE, KEY_BASE_SIGNATURE, KEY_ITERATION_TYPES,
KEY_REVERSE_MAPPED, KEY_INTERSECTION_PROP_CONTEXT, intersection properties...: fp_key() doesn't know their key words;
each needs a case there), TK_FREE_PARAMS (a node_links set of type parameters, no fingerprint written), a signature's
predicate (published by CAS beside the return, no frame), the lib singletons (made once before the workers). A kind
converted to shared words later needs its SW_FP() at the sw_finish() call.
