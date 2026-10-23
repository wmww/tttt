# A deterministic deal: the checker a file is checked in, and what it checked before, fixed by (program, K)
corH, 2026-10-23. Status: measured 2026-10-23, neither variant is cheap enough (below); the deal stays dynamic.

## Why
tttt promises output independent of a checker's history (`kdiff -k 1 -m 0`), and keeps it where it can, but tsgo's own
semantics aren't history-free: a circularity is reported where its cycle is first entered (bun 92-R16b's TS2615, TS5115
and TS2589 at the instantiation limits, sequelize's TS7022, bugs/tsgo_divergences.md). tsgo deals files to checkers
statically per K (compiler/checkerpool.go createCheckers: weights and imports), so its output is reproducible at a K.
tttt's deal is dynamic in two places, which turns every such case into a run-to-run flake:
1. **Stealing** (`steal_file()`): a checker done with its share takes the last unclaimed files of the share with the most
   source left, so which checker checks a file, and after what, depends on timing. `TTTT_NO_STEAL=1` makes 92-R16b
   stable (200/200 runs at K2 and K4), but costs 7-20% wall at the defaults (drizzle +7%, zod +21%, effect +19%,
   excalidraw +13%, abbench n=7 on a loaded box): byte-sized shares are a poor cost estimate and the end of the
   check waits for the slowest share.
2. **The memory pool's recycles** (`pool_recycle_due()`, the CLI's default): a checker recycles when the process's
   resident set is over the pool and it holds at least its share of the checkers' sum, both read while the others
   run. Under memory pressure (openclaw, opencode) where a checker starts afresh depends on timing too. (The
   per-checker budget, `--checker-memory MB`, is deterministic: a checker's own `checker_memory()` after a file.)
More checkers than threads with no stealing (K16, K32 at 16 threads) is no better: about the same wall, +15-40% RSS.

## The design
A file's output is a function of the files its checker checked before it, in order. Make that sequence a function of
(program, K):
- **Heads and tail blocks.** `deal_files()` keeps today's K shares (two contiguous runs of program order each, by
  bytes, the costliest file first), but only the first part of each share, its head (half its bytes), stays with the
  share's checker. The rest of every share is cut into blocks of contiguous files (about 1/(8K) of the source each,
  never splitting below one file) that are shares of their own: each is checked in a fresh checker, start to finish,
  by whichever thread takes it. Threads take heads first, then blocks in a fixed order (by share, then position), from
  the existing `check_shares_run()` counter. A thread's choice decides only when a block runs, not what it sees.
- **No stealing.** Blocks replace it: the end of the check is balanced by block size instead of by stealing files.
- **Deterministic recycles.** The memory pool still bounds the process, but as a per-checker budget fixed before
  checking: the pool divided by the checkers that can be live at once (the threads), recycling on the checker's own
  `checker_memory()` like `--checker-memory`. Recycle points then depend on the checker's own sequence only.
- A block's checker is dropped after its after-check pass (unused and declaration diagnostics): kept, as
  `program_checker_of()` wants for `--expect-types` and the harness, only at `--checker-memory` unlimited (the
  library default), where blocks don't apply (one share per checker, as today).
K-dependence stays what it is today, a function of K (the blocks are a function of K and the program), and K1 is
unchanged: one share, no blocks, no stealing. The thread count no longer changes the output at a fixed K.

## Variant 2: fixed sequences, no fresh checkers
K checkers as today, each with a fixed sequence of blocks: the program's files cut into many small blocks of
contiguous files, dealt round-robin by cumulative bytes (so every checker's bytes are balanced by granularity, not by
stealing), each checker taking its blocks in order. What a checker saw before a file is a function of (program, K);
warm-up is paid K times as today; the question is only the idle tail and the locality lost by finer interleaving
(neighbours that share types end up in different checkers: more types made in total). Today's deal is this with two
runs per checker plus stealing, so the prototype is a parameter: the runs per checker, and no stealing.

## Costs to measure
Each block repeats a checker's warm-up (the library types it touches): 8K blocks are 8K warm-ups on top of K. abbench at
the defaults on vscode, effect, drizzle, excalidraw and zod and the perfsweep `--defaults` sample against main's
fast/tttt (wall, user, peak RSS), instrcount (K1 unchanged). Over ~2% wall on vscode at the defaults is for the human.

## Measured (2026-10-23, 16-core box at load 5-13, abbench n=7 at the defaults, K8, release builds)
Wall against today's stealing deal (user time about the same in every row: the loss is the idle tail):
| | drizzle | zod | effect | excalidraw |
|---|---|---|---|---|
| TTTT_NO_STEAL=1 (today's 2 runs per checker) | +7% | +21% | +19% | +13% |
| variant 2, 8 runs per checker, no stealing | +15% | +24% | +20% | +19% |
| variant 2, 64 runs | +34% | +48% | +22% | +36% |
| variant 2, 256 runs | +35% | +46% | +34% | +24% |
| variant 1, 256 KB blocks | (crash) | (crash) | (crash) | +26%, RSS +25% |
Finer round robin balances bytes but not cost (zod at 64 runs: checkers finish between 72 and 222 ms against 132-159
with stealing; test files' cost per byte varies by 3x and more), and it costs locality (drizzle RSS +19-25%). Variant 1
(prototype kept as a patch, not in the tree) crashed with heap corruption under threads at 64 KB and 8 KB blocks on
drizzle and effect (not at --threads 1, not under valgrind or ASAN: unexplained; a block's checker starting mid-share is
a new situation for the deferred bodies and the after-check pass), and where it ran its fresh checkers cost +26% wall.
The knob for variant 2 stays: `TTTT_DEAL_RUNS=N` (runs per checker in deal_files()), with `TTTT_NO_STEAL=1`.
What would make a static deal competitive is a cost estimate better than bytes (bun's identifier count, a declaration
count, the import fan-in), which is a scheduling study of its own. Until then the run-to-run flakes are the K>1 face of
tsgo's own order dependences, and the oracles and the human measure at K1, where tttt matches.
