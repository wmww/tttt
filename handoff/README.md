# ktime2 hand-off (unfinished deliverable 2: the remaining faults)

- `arena-huge-align.patch` (src/ty/checker.c arena_alloc_chunk()): a huge-page arena chunk is mapped as whole 2 MB
  pages and 2 MB aligned (8 MB including the header, mapped +2 MB and the untouched ends cut off, as big_table()
  does). The current unaligned 8 MB + 4 KB chunk gets THP only on the 3 aligned blocks inside it; its ends (~2 MB a
  chunk) are zeroed and mapped 4 KB at a time through MADV_POPULATE_WRITE (which counts as minor faults).
  Measured (A/B interleaved, TTTT_SYNC_EXIT=1, medians; this box THP=madvise): drizzle minflt 73k -> 44k, sys
  0.13 -> 0.11 s, maxrss 885 -> 892 MB (+0.8%; an earlier variant with 10 MB chunks +1.5%); zod 62k -> 41k, sys
  0.10 -> 0.08; vscode 302k -> 187k, sys 0.71 -> 0.64, maxrss +0.9%; nest flat faults, maxrss 172 -> 177 MB (+3%:
  8 checkers' first chunk populated in 2 MB pages). NOT GATED: needs suite --summary, threadcheck, checkerdiff,
  instrcount (should be flat: no user-side change), and a laptop check (THP always: the ends could already be huge
  there only if the kernel aligned the mapping, which it does not for a length that isn't a 2 MB multiple).
- `fault-instrumentation.patch`: throwaway debug code (TTTT_FAULTS=1: per worker thread minflt and sys at its exit,
  by start routine; TTTT_SMAPS_TMP=path: copies /proc/self/smaps at exit). Used by `faults.sh` and `smaps.py`.
- `tail.py` (last output byte -> exit timing), `ab2.sh` (old/new binary A/B), `faults.sh`, `smaps.py`: the tools.
