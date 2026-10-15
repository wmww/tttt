# Plan 72: core scaling
Planned 2026-10-15 (todo entry 8). The answer: **parallel declare/link is a "not now"**. The biggest serial part is
the loader thread's own work during loading, so stage 1 cuts that. Checker imbalance is the biggest non-serial loss and
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

## Stage 1: the loader's serial work (one session)
Goal: vscode's loader non-parse time at `--threads 1` from ~350 to < 150 ms, with output unchanged.
1. Measure it first. Add a loader-thread split to `TTTT_LOAD_POOL_STATS` (time in `program_make_source` on the
   loader thread against the phase; a thread-local accumulator, which this session prototyped and reverted). Profile
   with callgrind on `--listFilesOnly --threads 1` and `--toggle-collect=program_load*`.
2. A filesystem probe cache in the resolver's `Fs`: file-exists and directory-exists results by path, shared under
   the resolver's lock or kept per thread. This is tsc's cached compiler host. The suite's `mock_fs` must keep working.
   Expect most of the 160k redundant stats to go.
3. Don't resolve on the loader what a worker resolved. `program_load_ahead_from()` resolves a parsed file's imports on
   the worker; the loader then does it again through the cache (key building, lock). Hand over the worker's
   `ResolvedModule`s with the file, keeping the loader's program-order decisions where they need it
   (`program_load_module_ref()`).
4. Path normalization (`push_segments`, `path_next_segment`): find who normalizes the same path again and again.
Validate: `scripts/threadcheck.sh`, `scripts/oracles.py --quick`, suite, `scripts/scenarios.py`; record the vscode
loader split at T1/T4 and the stat count (`strace -f -c`) in measurements.md.

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

## For the human's 12-core bench (triage.md)
Besides `scripts/bench.sh --checkers "4 8 12 16"`, one line per corpus decides which of the above matters there:
`TTTT_RESOLVE_TIMES=1 TTTT_LOAD_POOL_STATS=1 ./release/tttt -q --keep-going --noEmit --time -p <corpus>` for vscode,
webpack and effect at the default thread count.
