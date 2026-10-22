# The front end's critical path (load, parse, declare, link, walk)
What was measured and decided about the serial part of a run before checking. History: `git show
af936072c:agents/plans/72_core_scaling.md` and the "Stage 3" sections of `git show
af936072c:agents/plans/86_multiples.md`. Numbers by date are in measurements.md ("Costliest file first and the front
end's split", "The 12-core box at efb91998", "The config glob next to the load pool").

## Where the time goes
- vscode on the 12-core laptop at the defaults (2026-10-20, efb91998): libs 40 ms, load+parse 259 ms (the loader parsed
  744 of 11035 files itself, 83 ms; waited 14 ms), resolve names 203 ms (declare 71, link 51, walk 44), typecheck 1852
  ms: **~0.5 s of a 2.5 s wall**. At 1.93 s (f2ad73c9) the front end is the same ~0.5 s, so it now caps every
  many-core gain (Amdahl). On a small project it is most of the wall (nest loses to bun 0.08 vs 0.06 s).
- The 16-vCPU box, vscode T16 (2026-10-21): config + libs + load+parse 189 ms best, link ~50 ms (aliases 43 serial),
  config glob 29 ms alone (250 M instructions: glob_matches(), path strings), 55-80 ms next to the parsing pool (SMT
  and all-core clocks, not locks: measured with the wall sampler).
- **The timeline, vscode T16 on the 16-vCPU box (8 cores, SMT2), quiet, 2026-10-22 at 08a80d88** (`bench/timeline/`):
  352 ms to the end of resolve. The main thread is the critical path through config+load: busy 60-100% from 0 to 176
  ms (glob 56 ms CPU, then the loader's own 85 ms), while the pool, at ~10-12 busy, finishes parsing at 148 ms and
  idles for the loader's last 28 ms. Then 70 ms with the pool idle (declare merge, module refs, link).

  | ms | phase | wall | main CPU | main thread | pool |
  |---|---|---|---|---|---|
  | 0-64 | config | 64 | 56 | the glob: glob_matches 18%, list_directory 25% | parsing, ~10.5 busy |
  | 64-176 | libs + load+parse files | 122 | 89 (+19 blocked) | loader: load_pool_take's lock ~20%, add_made_source, include reasons, hashing | ~10-12 busy to 148 ms, then idle |
  | 176-188 | collect files, pool end, file scopes | 12 | 12 | emit_source_may_be_emitted, load_pool_destroy's frees | idle |
  | 188-208 | declare in isolation | 20 | 17 | | bursts, ~5.5x |
  | 208-278 | declare merge 8, between 2, module refs 9, link 56 | 75 | 75 | resolve_alias_target_in 32%, symbol_table_get_own + memcmp ~30% | idle |
  | 278-328 | parallel walk | 48 | 44 | | ~11x |
  | 328-335 | shard merge 5, remaining aliases 3 | 8 | 8 | | idle |

  The workers are off a CPU for ~35% of their parse jobs even quiet (1.5 of 2.3 s): mmap of each file 19% of their
  blocked samples, the resolver's rwlock and package_json_of 7-8%, glibc's heap-growth mprotect ~10% (21k calls),
  the pool's mutex 3%; parse is <36% of their samples.
- **After 2026-10-22's cuts (loaded box, load 15-65 all session: walls are relative only, the laptop must retake them)**:
  the walk no longer precedes the loader (config phase, now until the first root, 92-261 -> 6-14 ms; main thread CPU
  in config+load 176-203 -> 132-166 ms); link's alias loop lower in 5/5 pairs with prefetching; module refs on 16
  threads; options' per-file paths 41 -> 10 M instructions. Not yet retaken quietly: the T16 table above is before
  them, and T4 (laptop-class) and nest have no quiet timeline at all. Link at T4 read 82-96 ms against 52-56 at T1
  and T16 in every run (unexplained: check on the laptop).
- **Retaken on the 12-core laptop at 2989c0f0 (2026-10-22, measurements.md "The 12-core box at 2989c0f0")**: vscode's
  front end 251-261 ms at the defaults (efb91998: ~500), 440-453 at T4; link 39-43 ms at T24 and **40-51 at T4** (the
  16-vCPU box's 82-96 was that box's); nest 51-58 ms of its ~80 ms wall. At T24 load+parse the main thread is the
  critical path (82 ms on a CPU, 27 blocked, 15 queued). Perf counters: K1 is 53% frontend-bound (the code footprint:
  23% icache fetch misses, op cache 63% hit), the hash tables and allocation are not where the stalls land.
- Tools: `tttt --time` (phases with the main thread's CPU), `TTTT_LOAD_POOL_STATS=1` (the loader's own work, waits,
  inline parses), `TTTT_LOAD_POOL_INLINE=1` (why each inline parse happened), `TTTT_RESOLVE_TIMES=1` (declare, link's
  steps, walk), `TTTT_FILE_TIMES=1` (per-file check timeline), `TTTT_DEFER_STATS=1` (materializations over 10 ms),
  the main-thread wall sampler `bench/sampler/` (LD_PRELOAD, `timer_create`, `report.py --under/--exclude/
  --callers`), and the whole-process timeline `bench/timeline/` (every thread's schedstat and stacks per phase,
  `timeline.py OUT --skip @libc`), which stand in for perf on the shared box.

## Done
- The loader's serial work (2026-10-16): the loader takes the workers' resolutions (`SourceFile.preresolved`),
  `./`/`../` names cached by joined path (misses 62k -> 11k on vscode), `program_canonical_view()` and fast absolute
  paths, `CachedFs` (194k `newfstatat` -> 43k: stats were not the cost, path strings were). vscode load T4 940 -> 805
  ms.
- Resolver caches sharded 64 ways, shared boxed resolutions, sizes stat()ed by workers, `d_type` in the glob; workers
  take files by rank (the loader's preorder), files >= 128 KB first (inline parses ~1340 -> 350-500 at T12).
- The pool starts on libs and on each file the include glob finds before the glob ends
  (`project_set_root_files_streamed()`, `program_load_ahead_libs()`/`_roots()`), inline parses 400 -> 156; the end of
  resolve's frees on a thread (`program_release_run()`).
- Parallel declare (`program_declare_files()`, `declares_in_isolation()`: 10604 of vscode's 11015 files); module refs
  on threads unless a pattern has augmentations; webpack's CommonJS export walk moved onto the load pool (declare
  40 -> 18 ms).
- 2026-10-22: a 16 MB malloc top pad through the front end (main(): glibc's mprotect per 128 KB of heap growth took
  the mmap lock for writing 21k times on vscode; reset before checking, peak RSS within 1%). The include walk on its
  own thread, streaming final roots to the loader in root order (main.c's RootStream, tsconfig.c's take_found()/
  emit_ready(): see the commit for the invariant). Link's alias loop prefetched four stages ahead
  (link_prefetch_aliases()). Module refs on threads with pattern augmentations (their finders deferred to program
  order). program_verify_project_files() through absolute_path_fast().
- Checker balance: work stealing (`steal_file()`), costliest file first (`share_first`: -6% on TypeScript6 at 4
  cores, a wash on 12).

## Measured and not built
- **Import pre-scan** (tsc's preProcessFile, so workers queue a file's children before its full parse): ~35% of the
  loader's ~400 inline parses at T16 were found just in time, bounding the gain at ~15 ms of ~160 ms of load. The
  rest of the loader's CPU (~80 ms: include reasons, path hashing, malloc, misses on the workers' ASTs) is diffuse.
- **Two-phase link** (phase A on threads computes each alias's first step read-only; phase B, serial in
  `Binder.symbols` order, replays today's recursion with it, so TS2303 cycles, placeholders and report order are
  unchanged): best case 43 -> ~25 ms, for the riskiest ordering code in the binder (resolve_alias_target()'s first
  step is 250 lines of interop cases). Fully parallel alias resolution would change TS2303 positions. Revisit only if
  link becomes the floor.
- **The costliest file's bodies on several threads**: TypeScript6's checker.ts materializes in ~70 ms at the head of
  the critical checker, but it is one 2.9 MB body (createTypeChecker); a 7-thread chunked parse took 35 -> 31-33 ms.
  Only deferring nested bodies inside a deferred one would split it.
- **A cost model for the deal**: the biggest file bounds type-fest and TypeScript6; size predicts nothing
  (union-to-tuple.ts is 1.4 KB and 180 ms), bun's identifier-count model wouldn't see type-level evaluation either.
- **An adaptive load pool mutex** (PTHREAD_MUTEX_ADAPTIVE_NP, and the condvar signals after the unlock), for the
  loader's ~20% of load-phase samples in load_pool_take()'s lock at T16: worse in 3/3 pairs at load 60 (spinning while
  the holder is preempted), unmeasurable quiet here. Try it on the laptop only if a quiet timeline shows lock waits.
- **Interning names** (built and declined, 2026-10-22: branch wt-fe2, deleted; measurements.md "The 12-core box at
  8e3f4fb0: BOLT and interning A/B"). The design: one atom table for the process (the checkers' `AtomTable` made
  process-wide: the suite's programs overlay a cached lib program's globals, and ~97 call sites read symbol tables by
  text without a program at hand), a 32-bit name hash stored in `Atom`, `SymbolEntry` as `{Atom const*, Symbol*}` (24
  -> 16 bytes) compared by pointer, names interned at insertion on the declare threads, lookups by text through a
  find-only probe of the atom table, and the names link looks up (an import specifier's imported and local names,
  `default`, `export=`) interned while declaring so the serial alias loop touched no text. Here: instructions +0.1 to
  +0.7% per corpus, link's main-thread samples -20%. On the laptop: link -22% as predicted (41 -> 32 ms), but declare
  +4 ms, the walk +4 ms, the checkers +2% typecheck wall / +1 s CPU: vscode +1.8% wall and +2.8% cycles, nest +2-5%
  wall, TypeScript6 a tie on wall and +1% cycles; dTLB misses +3.3%, L1d misses +1.8%. Cycles rose 5x more than
  instructions, on the data side. Why, unmeasured: every lookup by text now probes the shared atom table (one big
  table every thread reads, its lines spread over 24 cores' caches) before the symbol table, where it used to confirm a
  hit with one memcmp against text the walk had just read; and an entry's name is one more indirection. Interning at
  the parser's identifier nodes wouldn't help: on TypeScript6 each of 190k identifier nodes is looked up by text once
  (by the walk), so the probe would only move. What would still pay: atoms only where a name is looked up many times
  from a place that holds it already (link's alias loop), without the table keying on them.
- **Loader leftovers** at T1 (2026-10-16): real resolution (11k misses, ~170 M instructions), the resolver's `join()`
  of `../` names (137 M), clones of cached results (100 M: a refcounted `ResolvedModule` would remove them).

## The parser
Measured 2026-10-22 (worker "parser", branch wt-parser; callgrind on the pgo build): the parser was 17.4% of vscode's
K1 instructions at the defaults (eager parse 4.44 G with the body skipper's 0.87 G, deferred bodies 7.49 G parsed by the
checkers: 7.4% of checker-thread CPU samples at the defaults, 8.2% at --threads 4) and 16.3% with --no-defer-bodies;
the laptop's counters put it at 9.8% of cycles (a higher IPC than the checker's). tsgo's own parser takes 1.5x tttt's
instructions on TypeScript6's compiler (1.01 G against 0.665 G), so a tsc-shaped rewrite was declined; so was a token
cache (re-scans were only ~30% of identifier and trivia work, a sixth of the parse). What cost was failed attempts
(~32% of parse cycles, 44 M of 73 M child parsers failed): a statement's first word now picks the branches
(`statement_word()`), and a dozen parse functions fail on a character none of their branches start with before making a
child parser (`GATE_FAIL`). Parse instructions on vscode 11.04 -> 9.50 G (-14%), K1 -2.6%; child parsers 73 M -> 41 M.

What's left in the profile (vscode, -O3): grammar code and dispatch ~35%, allocation ~15% (`reuse_or_bump()` scanning
the free-class masks, list growth in parse_append_statement_sequence(), whose elements are pointed at while built),
trivia ~13% (the per-character loop of hop_whitespace_slow(), not re-scanning), identifiers ~10%, Parser
create/merge/fail ~8% (a smaller Parser or cheaper parse_fail(): 1-2% of the parse, not done), literals ~5%, spans 4%,
token lookahead 3.4%. The skipper (skip_balanced_braces_at()) is table-driven already; what would halve it is a scan that
stops only at `{}()[]'"`/<` and decides a `/` by looking back at the previous token: ~0.4 G on vscode, not done.

Not built: a chunked parse of a big declaration file (lib.dom.d.ts, 2.4 MB, ~75 M instructions, ~11 ms on a laptop core,
the bound of small projects' libs phase). Sketch: a depth-0 scan for top-level statement starts (strings, comments,
template literal types and braces only: no regex or JSX in a declaration file; ~5-6 instructions a byte, ~14 M, ~19% of
the parse and serial), then the chunks parsed on the load pool's threads, each with a ParserShared of its own, their
statements, errors (in order; the same-position dedup across a boundary aside) and module refs (`/// <reference>`,
imports: source order matters) appended in order, the file's AstArena taking ranges from several threads (a lock in
add_range()). ~6-7 ms off lib.dom's critical path at 8 threads (11 -> 4-5 ms); 1-2 days. The statement dispatch doesn't
make the scan cheaper (it's character-level, like the skipper).

Tools from that round (in its commit messages): a build that runs every branch a quick test skipped and panics if one
would have succeeded (the statement dispatch was verified so, then removed), and `-DVERIFY_GATES`, which keeps doing that
for the GATE_FAIL tests (and checks tok_at()'s and starts_with_followed_by_word_break()'s fast paths against their slow
ones): run its baseline_tests (with and without --no-defer-bodies) and `tttt --listFilesOnly` over the corpora and the
source sweep's tsconfigs after touching a test. Per-function attempt/failure counters were a temporary patch (Parser
creation per `__func__`, rdtsc of maximal failed subtrees).
