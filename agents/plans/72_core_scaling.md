# Plan 72: core scaling
Planned 2026-10-15 (todo entry 8). The answer: **parallel declare/link is a "not now"**. The biggest serial part is
the loader thread's own work during loading, so stage 1 cut that (done 2026-10-16, below; the table is from before it). Checker imbalance is the biggest non-serial loss and
stays a lead without a stage. Numbers are from this 4-vCPU box (2 cores x SMT), release build, best of a few runs, load
< 1. `TTTT_RESOLVE_TIMES=1` (added by this session) prints name resolution's steps.

## Where the time goes (vscode, 138 MB checked source; webpack; effect)
| step (ms) | vscode T1 | vscode T4 | webpack T1 / T4 | effect T1 / T4 | parallel? |
|---|---|---|---|---|---|
| load+parse libs | 12 | 28 | 11 / 20 | 12 / 38 | serial |
| load+parse files | 1806 | 957 | 258 / 100 | 165 / 76 | pool, see below |
| of it read+parse (any thread) | 1455 | | 227 | 153 | yes |
| of it the loader's own non-parse work | **~350** | ~570 (SMT-slowed) | ~30 | ~11 | **serial** |
| declare | 80 | 85 | 42 / 44 | 11 / 11 | serial |
| link | 62 | 67 | 6 / 6 | 3.5 / 4 | serial |
| resolve walk | 1133 | 433 | 130 / 69 | 111 / 57 | yes |
| shard merge + serial files | 7 | 7 | 7 / 7 | 8 / 8 | serial |
| typecheck | 10190 | 4240 | 883 / 542 | 1239 / 701 | yes, see balance |
| startup, config, exit | ~50 | ~116 | | ~15 | serial |

- **Loader**: at T4 the vscode loader thread spent 957 ms in the phase, 387 of them parsing files itself, and waited 1
  ms (`TTTT_LOAD_POOL_STATS=1`: workers 89% busy). It is never starved: it is the critical path, and its non-parse
  work (re-resolving every import the workers already resolved, through the resolutions cache; adding sources; path
  normalization `path_next_segment`/`push_segments` 0.5 G instructions; the config's `glob_matches` 0.12 G) is
  ~350 ms at full speed. Callgrind of `--listFilesOnly` at T1: parse 81% of 16.9 G instructions,
  `module_resolver_resolve` 5.8%. strace: **194k `newfstatat` for 34k distinct paths** (`vs/base/common` alone 10867
  times, each `node_modules` up the tree ~2000 times), 14k opens: there is no file/directory-exists cache (tsc's
  compiler host caches them).
- **Declare + link**: 150 ms on vscode, 48 on webpack, 14 on effect.
- **Checker duplication is small**: total check work at `--threads 1`, K = 1 / 4 / 12 checkers: vscode 10.6 / 11.0 /
  12.5 s, effect 1.22 / 1.29 / 1.32, webpack 0.90 / 0.95 / 1.01. +8-17% at 12.
- **Checker imbalance is large**: K = 12, each checker's time (`--threads 1`) against the mean: vscode max 1.3x,
  effect 1.4x, webpack 1.5x, type-fest **3.1x** (one checker 388 ms against a 123 ms mean: a few heavy files).
  Over-decomposition with the dynamic pickup `check_shares_run()` already does (K = 24/36 on 12 workers, simulated
  from per-checker times) doesn't help in index order and helps only effect and zod (151 -> 126, 115 -> 99 ms) with
  LPT order, which would need a cost known before checking. Bytes were already rejected as a proxy (perf_leads.md).

**At 12 cores**, extrapolated (parallel parts / 12, check = max checker): vscode about 100 startup + 30 libs + 350
loader + 150 declare/link + 100 walk + ~1100-1300 check, ~1.9 s. Serial ~630 ms (33%): the loader 350 (~18%),
declare/link 150 (~8%). webpack ~300 ms: declare/link 48 (~16%), loader 30, libs 20. effect ~230 ms: declare/link
14 (6%). The 2026-10-11 run on the 12-core machine measured vscode 3.08 s with 12 threads (4.43 with 4, a 1.44x gain
where 3x was ideal). That run was before checker recycling. The ~1.2 s between it and this estimate is contention
this box can't show (memory bandwidth, page faults on 7.6 GB, allocator). **That is the open question for the
human's bench.**

## Stage 1: the loader's serial work (done 2026-10-16, todo entry 1)
vscode `load+parse files`, HEAD f760ecdc against stage 1, 3 runs each (`--listFilesOnly`): **T1 2525 -> 2310 ms, T4
940 -> 805 ms (-15%)**; effect T4 76 -> 45, webpack T4 100 -> 95. Output unchanged (every oracle, threadcheck,
scenarios, full source sweep). Peak RSS +8 MB at T1, +24 MB at T4 on vscode (+0.9%: the probe cache, the joined-path
keys, resolutions held until the loader takes them).
- The split is in `TTTT_LOAD_POOL_STATS=1` now: "the loader parsed X ms itself; its own work Y" (pool lifetime minus
  waits and its own parses), and without a pool "the loader read and parsed for X ms". vscode T4: own work 630 -> ~385
  ms; T1: non-parse ~415 -> ~225 ms. Instructions at T1 (callgrind `--listFilesOnly`): 16.88 -> 15.68 G, the non-parse
  load work outside the config's globbing 2.46 -> 1.30 G.
- What did it, by size: (a) the loader takes the worker's resolutions (`SourceFile.preresolved`) instead of asking the
  shared resolver again: the T4 win, mostly the resolver lock's contention with the workers, not the lookups; (b) a
  `./`/`../` name is also cached by the path it joins to (`resolves_by_joined_path()`): resolution misses 62k -> 11k
  on vscode; (c) `program_canonical_view()`: cleat's `relative_path_of()` (1.1 G instructions, two allocations and a
  segment walk) skipped for normalized paths in the working directory, likewise the resolver's `absolute_path_fast()`/
  `join()`, its cache key built in one allocation, tsconfig's `path_join()`; (d) `CachedFs` (`src/cached_fs.c`):
  194k `newfstatat` -> 43k. The stat cache barely moved the wall (a stat is ~1 us): the plan's guess that stats were
  the cost was wrong, path strings were.
- Not met: "< 150 ms at T1". What's left at T1 is real resolution (11k misses, ~170 M instructions), the resolver's
  `join()` of `../` names (slow path, 137 M), clones of cached results (100 M) and parse-side work counted as loading
  (`source_file_make`, reading). At T4 on this box the phase is parse-throughput bound (workers ~94% busy, the loader
  waits ~15 ms), so the loader's remaining ~385 ms only shows at more cores: the 12-core `TTTT_LOAD_POOL_STATS` line
  (below) says whether a stage 2 is worth it. Candidates if so: the inline-parsed files' resolutions (the loader
  parses ~1/3 of vscode's files itself at T4, and resolves their imports itself), fewer `ResolvedModule` clones (a
  refcounted result), the config's glob walk (0.25 G instructions, before loading: `glob_matches`, `fs_is_directory`
  per directory entry where `d_type` would do).

## Not now: parallel declare/link
150 ms on vscode (~8% of a 12-core run), 48 on webpack (~16%), < 15 on everything mid-size. The way to do it is
unchanged (bugs/perf_leads.md: per-file tables merged in program order, merged type parameters made in declare). Redo
it after stage 1, and only if the human's 12-core `--time` shows declare+link above 10% of the wall on vscode or
webpack. Webpack's declare is high for its size (42 ms against a 130 ms walk), probably its big `types.d.ts` and
`@types/node`. Look at that first: a serial fix there may be cheaper than parallelizing.

## Not a stage: load discovery (the lexical import pre-scan)
At T4 the loader waited 1 ms on vscode and 26 on webpack: discovery isn't starving the pool. The loader's own work is
the critical path (stage 1). Redo the measurement at 12 threads on the human's machine (`TTTT_LOAD_POOL_STATS=1`:
"the loader waited"). Only a wait of more than ~10% of the load phase would justify the pre-scan.

## Lead: checker balance
This is the biggest non-serial loss: 30-40% over the ideal on most corpora at 12, 3x on type-fest. A cost known
before checking is the missing piece. Ideas, none measured: per-file node counts weighted by kind (type-level-heavy
`.ts` against plain code); splitting a heavy checker's leftover files once it finishes last (work stealing at file
granularity; the output must stay K-independent, `scripts/kdiff.py`). bugs/perf_leads.md "Checker balance" keeps it.

## The 12-core bench (done 2026-10-16, measurements.md "The 12-core machine")
vscode at 24 threads: load phase 389 ms (the loader waited 6 ms, workers 59% busy: the loader thread is still the
critical path, stage 1 stands), declare+link 130 ms = 5% of the 2.5 s wall (under the 10% bar: parallel declare/link
stays "not now" for vscode), check 1424 ms with max/mean 1.3x across 24 checkers, ~250 ms startup + exit. webpack:
declare+link 45 ms = 15% of 0.30 s, over the bar: look at its serial `declare` first (above). The pre-scan stays
unjustified (6-8 ms waited). The 1.2 s gap the estimate above asked about is gone: vscode is 2.5 s at 24 threads.
