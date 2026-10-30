# What still depends on the box (2026-10-28)
An audit for the human's question "anything that looks like a dependence on specific hardware or configuration?".
Nothing is x86-specific in the instruction-set sense: no `-march`/`-mtune`, the PGO and BOLT profiles are instruction
counts of the training corpora (input-dependent), `rdtsc` appears only in the parked shared graph's debug code and
the taint prototype. The dependences are Linux kernel configuration and box state:

- **The default checker count reads CPU topology from sysfs** (`physical_cores()`, `default_checker_cap()` in
  main.c: one checker per physical core, every hardware thread up to 4; no cap when sysfs is missing). A VM that
  exposes each vCPU as its own core gets twice the checkers of a bare-metal box with SMT. Its removal is built
  (ad0215612, in history) and waits for the duplication fix that makes it moot: on this box drizzle K8 -> K16 costs
  +50% user CPU and +22% RSS because every checker rebuilds the declared world (todo.md "Now").
- **The memory pool reads MemAvailable and the cgroup limit at startup** (`memory_available_bytes()` in prelude.c,
  80% of it; `TTTT_MEM_AVAILABLE_MB` overrides). Output is unaffected (the one-checker rerun's rule 4 covers
  recycles); wall is: a box low on free memory recycles checkers more. Kept on purpose (the alternative is an OOM
  kill or an arbitrary cap).
- **Kernel features used opportunistically, each with a silent fallback**: MADV_POPULATE_WRITE (5.14) pre-faults
  arenas, PR_SET_VMA_ANON_NAME (5.17) names mappings. An older kernel faults lazily, nothing else.
- **Address-space reservations**: the AST reserves 256 MB segments (MAP_NORESERVE; `ast_owns()` is one load, slabs
  2 MB aligned) and every thread its arenas and stack, so a small `ulimit -v` fails (`scripts/tttt_run.py` sets
  RLIMIT_AS to mem_gb + 0.5 GB per thread for that reason). The checker arenas' two mmaps in `arena_alloc_chunk()`
  (checker.c) lack MAP_NORESERVE, so `vm.overcommit_memory=2` could refuse them: untested, a one-line fix each.
- **Removed 2026-10-28**: the huge text page (THP-dependent, laptop-only win) and the opt-in `--huge-pages` path with
  its /proc/buddyinfo budget (measurements.md "The 12-core box at df95ec2f": faults -3.6x, wall within noise).

macOS arm64 (2026-10-29): the Linux-only interfaces behind #ifdefs and what they became there: agents/notes/macos.md.

arm64 Linux (2026-10-29): the address width, the runtime page size, a GCC 15 miscompile of the PGO build, and how a
fresh box is set up: agents/notes/arm64.md.
