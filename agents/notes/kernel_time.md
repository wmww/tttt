# Kernel time at the defaults: TLB shootdowns and page faults (2026-10-23, worker ktime)

The laptop (measurements.md "The 12-core box at 48cd9835", part A) measured kernel cycles at 15-24% of user cycles at
`--threads 12 --checkers 12`, and perf c2c put 77-90% of all HITMs on the kernel's TLB-shootdown lines. This note says
who issued the shootdowns, what changed, and what the laptop should confirm.

## How it was measured (this box: a loaded 16-vCPU VM, no perf, no bpftrace)
- `strace -ff -k -e trace=munmap,madvise,mprotect,mmap,mremap` (one file per thread: `-f` alone interleaves
  `<unfinished>` lines and loses the stacks), aggregated by syscall, advice, the innermost tttt frames and the
  thread's start routine (= the phase). Shootdown-causing: munmap and madvise(DONTNEED/FREE) of touched memory;
  mprotect here only adds permissions; MADV_POPULATE_WRITE/HUGEPAGE flush nothing; munmap of never-touched memory
  (glibc's new-heap alignment trims, the AST slabs' segment alignment) clears no PTE and flushes nothing.
- `/proc/interrupts`' TLB line is useless here: the other workers make 1.4-4.4k shootdowns a second (a drizzle run
  reads ~1.4k either way). On the laptop, idle otherwise, it is the direct number.
- Faults, sys, peak RSS: `/usr/bin/time`, configurations interleaved, medians of 7 runs (vscode 3). Walls are noise
  here. vscode's fault count swings 450k-850k between runs with the box's free 2 MB blocks (huge_pages_take()'s
  budget), so compare only within one interleaved batch.
- Fault-induced shootdowns (copy-on-write of the zero page, THP splits) can't be seen by strace: prelude.h's
  zeroed_table() comment is one such case already fixed.

## Attribution at main (0a1f67836-era, the defaults: 8 checkers on these, 16 threads)
Shootdown-causing calls per run, by site and phase:

| site (thread, when) | drizzle | zod | vue | nest | vscode |
|---|---|---|---|---|---|
| glibc free() past the mmap threshold + heap trims, checker threads, **during checking** (intern/relation/link table growth) | 136 | 97 | 110 | 21 | **1180** |
| glibc trims on the release thread and materialize helpers (**during checking**), incl. source_texts_release() | 8 | 10 | 8 | 15 | 1 |
| thread-exit stack madvise (start_thread; every phase, checkers' exits during checking) | 68 | 66 | 64 | 72 | 78 |
| pthread_join stack munmaps (64 MB stacks > glibc's 40 MB stack cache; main thread at phase ends) | ~73 | ~64 | ~63 | ~57 | ~100 |
| malloc_trim(0) before checking (main thread, few other threads alive) | 47 | 32 | 36 | 42 | 160 |
| minor faults / sys s | 117k / 0.18 | 84k / 0.12 | 65k / 0.09 | 34k / 0.07 | 645k / 1.07 |

## What changed (branch wt-ktime, four commits)
1. **Stack pool** (b821cfbf5): worker_thread_create()/worker_thread_join() (load_pool.c) run every worker thread on a
   stack from a pool kept until exit; glibc neither madvises nor unmaps a caller's stack. Exit madvises 68 -> 0, join
   munmaps ~73 -> 0. RSS within noise.
2. **mallopt** (42bec5fe0): M_MMAP_THRESHOLD 32 MB (glibc's max), M_TRIM_THRESHOLD 1 GB. Calls on checker threads:
   drizzle 136 -> 1, zod 88 -> 2, vue 110 -> 0, vscode 1180 -> 7-11 (blocks past 32 MB). Faults -8 to -11% (vscode
   -30%), peak RSS +2 to +6% (drizzle 841 -> 892 MB, vscode 5058 -> 5250). 4 MB and 8 MB thresholds measured next to
   32: same RSS within noise (drizzle 921 / 889 / 895 MB), more checking-time munmaps on vscode (37 / 12 / 7-11),
   faults no better: 32 MB stays.
3. **Texts kept** (6cb0be95c): the after-names source_texts_release() is gone (it ran on the release thread as the
   checkers started; file pages the page cache keeps anyway; vscode peak RSS within noise). Releasing on the main
   thread instead cost 5 ms on vscode's critical path. Recycle-time releases stay.
4. **Big tables** (708a71cbc): intern, link and relation tables of 2 MB or more get their own 2 MB-aligned pages
   (THP within huge_pages_take()'s budget), populated in one call, and a free keeps them by power-of-two class for the
   next table (prelude.c big_table()). Faults drizzle -27%, zod -18%, effect -20%, vue/react -3/-4%; RSS within noise.

After all four, the shootdown-causing calls left per run are malloc_trim's 32-160 madvises before the checkers
start (main thread, workers parked or gone) and vscode's 7-11 frees of tables past 32 MB. Instructions flat
(callgrind K1 within +0.2%; kernel work isn't in callgrind).

## Measured and not changed
- **Huge first chunk on checker type arenas** (Arena.huge_first): right at the defaults. Without it faults +12-22%
  (nest 31.6k -> 38.8k, react 39.8k -> 53.3k, zod 75k -> 87k, drizzle 106k -> 124k) and sys up; it costs nest ~7 MB.
- **Arena populate-ahead waste** (populated and huge-page-rounded minus used, all live arena chunks at exit): drizzle
  680 vs 661 MB used (3%), zod 460 vs 445, react 159 vs 143, nest 72 vs 52 (20 MB: 8 checkers' first huge chunks).
  A few ms of zeroing spread over threads: not worth a change.
- **glibc.malloc.hugetlb=1** (GLIBC_TUNABLES, i.e. glibc madvising its own heaps for THP): faults -23 to -35% on
  every corpus, sys -20-30%, but peak RSS +30 MB on nest (+19%) and react (+12%), it bypasses huge_pages_take()'s
  fragmentation budget (the compaction-stall risk prelude.h describes), and it needs a re-exec to set. Not taken; the
  big tables got about two thirds of its fault cut on drizzle for no RSS. The rest is other malloc (AST helpers,
  scopes, flow), the next lever if faults still matter.

## Confirmed on the laptop (2026-10-22, measurements.md "The 12-core box at 3a77c827: kernel time")
At the defaults, f903d2e0e against 3a77c827: kernel cycles 21-28% of user -> 6-9% (drizzle, zod, effect, vue, vscode),
TLB shootdowns per run -95 to -99% (drizzle 428k -> 8.8k), faults -79 to -96%, walls -13 to -23%; perf c2c on drizzle:
the shootdown HITMs 217 -> 2 in 10 runs, no hot user line. **Peak RSS rose more than expected: +4 to +15%** (nest +44 MB,
drizzle +116, vscode +190), not split by commit yet.
