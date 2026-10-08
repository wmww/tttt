# Plan 36: parallelism
Written by the planning session of 2026-09-28 (todo entry 4) from two prototypes: parallel loading
(`36_prototype.patch`, env `TTTT_PREPARSE=N`) and K checkers run one after the other (same patch,
`TTTT_CHECKERS=K`). The patch applies to 0746971 (`git apply agents/plans/36_prototype.patch`); it is measurement
code, not a starting point to polish (fixed-size arrays, leaks, timers, a static realpath).

## What the prototypes found (4-core EPYC, best of 5, `-p` corpus configs from `bench.sh`)
**Parsing is thread-clean.** `parse_file` touches only its `SourceFile` and a local error list; the only shared
writes are the atomic `lazy_slot_new` counter and cleat's `_GLOBAL_RETURNED_VALUE_TARGET` (every `return ok;`
writes that global: a TSan race, harmless in practice, fixed by making it `thread_local` in a `thirdparty/*patch.txt`).
A TSan build (`meson setup X --buildtype release -Db_sanitize=thread -Dc_args=-g`, run with `TTTT_NO_REEXEC=1`) on
webpack, nest and the tsc corpus reported nothing else.

**Design that keeps output identical by construction: the pool is a cache warmer.** The sequential loader
(`program_add_source_idempotent`, depth-first, decides file order, `ModuleRef.file`, package-id dedup, error order)
stays exactly as it is and runs on the main thread. A pool of workers parses ahead: a worker takes a queued path,
reads + parses it (errors into a per-file `ErrorCtx`), then *guesses* what the loader will load from it (resolve its
`module_refs`, `/// <reference types/lib>`) and queues unseen paths. The loader, when it reaches a path, takes the
preparsed file (waiting if it's in flight, parsing it itself if it hasn't started) and appends its held errors at
that point. A wrong guess only costs a wasted parse. Measured: every corpus queued exactly the files the loader
used (0 wasted), and `--all-errors` output was byte-identical to the sequential run for 1-4 threads, 3 runs each,
on all 7 corpora.

**Numbers.** Load phase (read + parse + resolve imports), ms, sequential -> pool alone at 2/4 workers:

| corpus | sequential | 2 | 4 | main thread remainder with everything preparsed |
|---|---|---|---|---|
| tsc | 106 | 55 | 54 (checker.ts is the critical path) | 15 |
| webpack | 143 | 85 | 66 | 25 |
| nest | 113 | 95 | 97 (deep import chains, see below) | 37 |
| zod | 110 | 51 | 39 | 19 |

End to end (whole run, ms): tsc 661 -> 631, webpack 564 -> 527, nest 201 -> 180, react 243 -> 228, valibot
622 -> 599, zod 1315 -> 1260, type-fest 327 -> 321. Peak RSS unchanged. **Parallel parsing alone is worth 5-10%**,
because typecheck is 50-90% of every run.

What limits it:
- **Discovery is a critical path**: a file's imports are known only once it is parsed and resolved. nest reaches
  most of its 2394 files through chains of node_modules `.d.ts` imports, so 4 workers are no faster than 2. Fix:
  a cheap lexical import scan (tsc's `preProcessFile`: `import`/`export ... from`, `require(`, `import(`,
  `/// <reference`) run before the full parse so children are queued immediately.
- **Resolution** is 30 ms of nest's 101 (8-20 ms elsewhere): with one lock around the resolver, workers serialize
  on it (25-46 ms under the lock); with a resolver per worker there is no lock but the caches are duplicated (total
  resolution work +30%). Want: one resolver whose caches (package.json by directory, results by (dir, mode, name))
  are safe to share, e.g. sharded mutexes or per-worker caches in front of a shared read-mostly one.
- **The main thread's remainder** (15-37 ms): `program_handle_statement`, resolving every ref again (cached), an
  `fs_get_cwd` syscall and `relative_path_of` per `program_add_source_idempotent` call, the automatic `@types`
  (not queued up front in the prototype, so parsed inline).

**N checkers (`TTTT_CHECKERS=K`, files dealt round robin, checkers run one after the other and kept alive).** The
AST-side walk already goes through the thread-local `current_checker`, and the checker writes almost nothing
outside itself (audit below), so K checkers over one AST worked with no changes at all. Diagnostics: the same codes
at the same positions as K=1; **only union member order in message text differs** (type ids are per checker, unions
print sorted by id): 2-3 messages on zod and webpack, none on the other corpora. (2026-10-05 audit: every corpus at
K=2/3/4/7 and the suite dumps at K=1/2/4 identical modulo quoted text; DefinitelyTyped at K=1 vs 4 differs in one
package, react-router-transition's TS2739 "missing the following properties ...: children, render" vs "render,
children", the same id order leaking out of quotes: `Pick<RouteProps, "children" | ... | "render">` lists the keys in
union order. tsc is creation-order dependent there too; not fixable without checker-independent literal ids.) Typecheck time per checker (sum =
duplicated work, max = ideal parallel wall):

| corpus | K=1 | K=2 sum / max | K=4 sum / max | peak RSS K=1 -> 4 |
|---|---|---|---|---|
| tsc | 464 | 473 / 328 | 505 / 243 | 360 -> 404 MB |
| webpack | 337 | 368 / 188 | 400 / 107 | 382 -> 464 |
| nest | 30 | 36 / 19 | 47 / 16 | 230 -> 249 |
| zod | 1147 | 1213 / 885 | 1266 / 717 | 1334 -> 1566 |
| type-fest | 297 | 301 / 248 | 308 / 230 | 486 -> 501 |
| valibot | 515 | 543 / 330 | 586 / 188 | 465 -> 536 |
| react | 119 | 171 / 93 | 245 / 73 | 235 -> 335 |

Checking in parallel is where the win is (webpack 3.1x, valibot 2.7x at K=4). Duplication costs +4..+19% work
(react +107%: every file touches the React typings). Whole-file partitioning is bounded by the biggest file:
tsc's checker.ts, zod's deep-partial.test.ts, type-fest's one heavy test file. Projected wall at 4 threads with
parallel load and K=4: tsc ~0.40 s (0.66 now, tsgo 1.27), webpack ~0.27 (0.56, tsgo 0.67), zod ~0.85 (1.31).
After that the sequential **resolve names** phase (54-89 ms) is the next largest serial part.

## The decision stage 3 needs: **made** (insights 2026-09-28): option 1
A fixed logical checker count, `--checkers K` defaulting to 4, independent of `--threads`; the test harness runs 1.
Stages 2 and 3 were done before stage 1 (the check phase is the win). The original analysis:
With K checkers, printed union order depends on which checker saw what first. Options:
1. **Fixed logical checker count** (tsgo's approach: `--checkers`, default 4, independent of the thread count),
   files assigned by a rule that depends only on the file list. Output is deterministic for a given K and identical
   for any thread count; it differs from K=1 only in union member order inside messages. The test harness runs K=1
   (tsc's baselines were made by one checker). Recommended.
2. Keep K=1 output everywhere: impossible without making union order independent of type ids, which tsc 5.8's
   baselines depend on. Rejected.
Note tsc itself prints unions in an order that depends on what else is in the program; nobody can rely on it.

## Stages
Each stage: suite unchanged (`scripts/fasttest.sh --summary`), corpus numbers unchanged, `--all-errors` output
byte-identical for threads 1..4 on every corpus (add a script: `scripts/threadcheck.sh` running each corpus with
`--threads 1` and `--threads 4` three times and `cmp`ing), a TSan run on webpack + nest + tsc clean, `scripts/bench.sh`
numbers in `measurements.md`.

### Stage 1: parallel loading: **done** (2026-09-28, todo entry 6)
The cache warmer as prototyped, in `src/load_pool.{h,c}` (documentation.md "Load from the filesystem"): `--threads N`
workers (tttt's default, the harness's 1: `baseline_tests --threads N` uses it), `program_make_source()` split out of
`program_add_source_idempotent()` (reading, flags, file format, parse), `program_load_ahead_from()` guesses the
children, `program_load_ahead()` queues libs, roots and automatic types up front. Beyond the prototype:
- The resolver is shared: its two caches under one mutex held only for lookup/insert (package.json values boxed so
  pointers stay valid; two threads may both resolve a miss). No resolver per worker, no duplicate work.
- **Biggest file first** (a max-heap by `stat` size, `program_set_file_size()`): FIFO started tsc's `checker.ts` late
  and the loader then waited ~100 ms on it; now the load phase is about checker.ts's parse, the bound for whole files.
- **The loader parses queued files while it waits** (and runs `program_parse_ahead()` itself then): measured better
  than idling on every corpus, although it can pick up a big file and delay itself (hacks.md "Load pool").
- `compiler_options()` is thread-local; checker threads set it themselves. `Program.cwd` read once (`program_cwd()`,
  lazily: the harness sets the mock cwd after `program_new`). The harness's allocation counter is atomic.
- Not done: **the lexical import pre-scan** (step 5). Workers were ~90% busy (`TTTT_LOAD_POOL_STATS=1`: busy vs
  lifetime x threads) on every corpus, nest included, so discovery wasn't the limit where this was measured; but the
  machine was at load 8-10 from other jobs all session, which hides a discovery-bound pool. Re-measure nest on a quiet
  machine (4 vs 2 workers, "parsed by the loader" count) before building it.
Validation: suite 10830 unchanged; `baseline_tests --threads 4 --dump` byte-identical to `--threads 1`;
`scripts/threadcheck.sh` identical; TSan clean on all 9 corpora at `--threads 4`; single-threaded instructions unchanged.
Numbers (load phase = libs + files, ms, best of 7 interleaved with the previous binary, 4 threads, machine at load ~8):
tsc 189 -> 100, nest 195 -> 125, webpack 243 -> 153, react 134 -> 78, zod 138 -> 86, xstate 106 -> 58, valibot 102 -> 66,
type-fest 29 -> 22. Walls were too noisy under that load to quote (`measurements.md`); `scripts/bench.sh` on a quiet machine
is owed.

### Stage 2: check-phase readiness: **done** (2026-09-28)
`--checkers K` (tttt and `baseline_tests`; default 1), files dealt round robin by index among the checked files,
checkers run one after the other in `program_typecheck()`. Each checker reports into its own `ErrorCtx`
(`Checker.errs`, parent = the program's for `error_ctx_find()`), merged after the loop; `error_ctx_sort_and_dedupe()`
then does tsc's sortAndDeduplicateDiagnostics (ties ordered by message chain and related information, same head
message deduped), so the order no longer depends on report order (also +2 tests at K=1). Memoised "once per program"
reports: `Error.once_key` (only the emit helper checks needed it). `merged_export_equals` precomputed in
`binder_link`, block-level aliases resolved at the end of name resolution, JSDoc `@link` marking moved to the resolve
phase, `ClassDecl.has_typechecked` became a checker link (`ty_first_check()`), getenv caches read at startup
(`debug_env`), cleat's `return ok;` sink is thread-local (`thirdparty/cleat-failable-patch.txt`), threads linked.
Runtime guards (`log_error`): reports into the frozen program ctx, new binder symbols, first alias resolutions while
checking; none fire on the suite or the corpora.
Validation tools: `scripts/checkerdiff.py [-k K] [corpora]` (K vs 1, modulo quoted text, and K twice byte-identical), and
`baseline_tests --checkers K --dump DIR` against a K=1 dump. State at the end of stage 2: every corpus identical
modulo quoted text at K=2/4 (webpack 2 and zod-root 12 diagnostics differ in union order); the suite's errors at
K=2/4 differ from K=1 in one test (reactDefaultPropsInferenceSuccess, and only inside quotes: a K=1 order dependence
in how TS2769 prints overload signatures, see triage.md). `tttt --time` prints each checker's time: round robin
balances well (webpack 4 x ~100 ms vs 345 at K=1, zod 137-193 ms vs 572), so "by size" partitioning wasn't tried.

### Stage 3: parallel checking: **done** (2026-09-28)
`--threads N` (tttt: default the core count up to 4; `baseline_tests --threads N`, default 1) runs the K checkers on
N threads (`program_set_threads()`; the main thread works too, workers take checker indices from an atomic counter,
worker stacks get the main thread's `RLIMIT_STACK`, at least 8 MB). tttt's `--checkers` now defaults to 4, the
harness's stays 1. Validation: `scripts/threadcheck.sh` (every corpus, `--threads 1` vs 4 three times, `cmp`) identical;
`baseline_tests --checkers 4 --threads 1/4 --dump` identical; a TSan build (`meson setup /tmp/tsan --buildtype
release -Db_sanitize=thread -Dc_args=-g`, `TTTT_NO_REEXEC=1`) reports nothing on all 8 corpora at K=4 on 4 threads.
Nothing had to change in the checker: stage 2's audit was complete for everything the corpora reach.
**This machine has 2 physical cores x 2 SMT threads** (the "4 cores" of `measurements.md`): 4 independent
single-threaded tttt processes each slow down by the same ~55% as 4 checker threads do, so per-checker time at 4
threads (webpack ~100 -> ~155 ms each) is the hardware, not sharing. 4 threads still beats 2 and 3 everywhere measured.
One real sharing cost was found and fixed: tables `calloc`ed fresh from the kernel and probed before written
(intern tables, relation caches, lazy slot pages) got the zero page mapped on read, and the copy-on-write on the first
write flushed every thread's TLB (4% of cycles in IPIs); `zeroed_table()` writes them first.
Numbers: `measurements.md` "Perf". Memory: peak RSS +2..+47% at K=4 (react worst, zod 665 -> 846 MB; see there).

### Later (not planned yet)
- **The import pre-scan** (stage 1 step 5), if a quiet machine shows the pool discovery-bound.
- **Parallel name resolution**: plan 45 (the walk of TypeScript files is parallel since 2026-10-05).
- Better balance than whole files (tsc's checker.ts, zod's deep-partial.test.ts bound K=4 at 1.9x/1.6x): nothing
  cheap; tsgo has the same bound. **Tried and reverted** (stage 1 session): dealing by content size (biggest first to
  the checker with the fewest bytes, deterministic) instead of round robin. Typecheck phase, best of 7, loaded
  machine: tsc 519 -> 481 ms, react 218 -> 146, zod 615 -> 540, type-fest 433 -> 321, but webpack 392 -> 626, xstate
  300 -> 504, valibot 546 -> 648. Bytes are a poor cost proxy (JS vs declarations), and scattering a directory's files
  over the checkers duplicates the types they share; round robin by program order keeps neighbours apart too, but
  evenly. A cost model (per-file check time of a previous run, or type counts) would be the next thing to try.
  **Done differently (2026-10-05)**: `deal_files()` cuts program order into 2 runs of equal bytes per checker, dealt
  round robin: neighbours share a checker (drizzle 1688 -> 1337 MB at K=4) at round robin's walls or better. One run
  each saves more (1277 MB; valibot's total checker time -28%) but balances worse; a cost model is still the next step.
- **Sharing types between checkers: not a lever** (2026-10-08 perf session, vscode). The four checkers make 3.28M types
  against one checker's 2.66M, but at `--threads 1` four checkers take 10.28 s in total against one checker's 10.06 s
  (smaller caches make up for the redone work). Balance is fine too (checkers 3.7-4.2 s at 4 threads). What the 4-thread
  wall (4.2 s) pays for is the hardware: 2 threads take 5.47 s (1.88x of one), the two SMT siblings add only ~1.3x more.
  Single-threaded checker speed is what moves vscode's wall.

## Audit of shared state in the check phase (2026-09-28, subagent)
- Races: the shared `ErrorCtx` (sorted `vec_error_insert_n` in `error_ctx_append_error`, also read by
  `error_ctx_find` in `report_duplicate`, `ty/check.c`, `expr.c`); `compiler_options_private` set/cleared by
  `program_typecheck`; `merged_export_equals`; `Symbol.referenced` via `mark_jsdoc_links`; `CommentDirective.used`;
  `ClassDecl.has_typechecked`; getenv `static int` caches (`flow.c`, `lazy_type.c`, `ty/relate.c` x2);
  `debug_allocation_count` (debug builds only).
- Safe after link: `symbol_resolve_alias`, `module_resolved_exports`, `binder_module_of_ref` (link computes them for
  everything; a check-time assert would keep it so).
- Already per checker or thread-local: arena, type ids, interning, relation caches, `LazySlots`, `global_types`;
  `current_checker`, `current_lazy_slots`, `cycle_trace`, `current_flow`, `display_type_depth`. `lazy_slot_new`
  is atomic.
- Determinism: type ids (union order), errors inside memoised evaluations reported once per checker, the tie order
  of equal diagnostics in `insertion_index_for_span`, per-checker private-member usage (fine while a file's
  `unused_report` runs on its own checker).
