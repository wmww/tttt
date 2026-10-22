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
- Tools: `tttt --time` (phases with the main thread's CPU), `TTTT_LOAD_POOL_STATS=1` (the loader's own work, waits,
  inline parses), `TTTT_LOAD_POOL_INLINE=1` (why each inline parse happened), `TTTT_RESOLVE_TIMES=1` (declare, link's
  steps, walk), `TTTT_FILE_TIMES=1` (per-file check timeline), `TTTT_DEFER_STATS=1` (materializations over 10 ms),
  and the main-thread wall sampler `bench/sampler/` (LD_PRELOAD, `timer_create`, `report.py --under/--exclude/
  --callers`), which stands in for perf on the shared box.

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
- **Loader leftovers** at T1 (2026-10-16): real resolution (11k misses, ~170 M instructions), the resolver's `join()`
  of `../` names (137 M), clones of cached results (100 M: a refcounted `ResolvedModule` would remove them).

## The parser
~12% of vscode's K1 instructions with no hot site (74 M child `Parser` creations for 138 MB of source, the top site
10%); ~0.9 s single-threaded on vscode, ~150 MB/s. Structural: a `Parser` copy per speculative attempt, identifiers
re-scanned by each failing alternative, a malloc per node (arenas per file exist since 2026-10-16). The lever is a
real token per position and node arenas: a rewrite of src/parser.c with the whole suite (and `scripts/syntaxstats.py`)
as its oracle. It parallelizes already, so it is CPU (and 4-core wall) more than many-core wall: show parse on the
critical path at the defaults before starting it. Error recovery stays as it is (one list/commit-point framework,
documentation.md "Error recovery").
