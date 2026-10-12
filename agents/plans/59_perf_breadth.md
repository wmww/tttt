# Plan 59: perf at breadth (tttt against tsgo 7.1 on every source sweep project)
Written by the insights of 2026-10-11. Our perf claims (3-14x faster than tsgo) come from 14 curated corpora that
perf sessions have tuned for months. The source sweep's 3363 real projects had never been looked at for perf against
tsgo, and a quick look found two kinds of waste the corpora can't show:

- **Checking after the gate.** tsc skips semantic checking when there are options or syntactic diagnostics
  (`emitFilesAndReportErrors`). tttt prints the same output but some options diagnostics (TS5090 `paths` without
  `./`, found by `program_resolve_names()`'s option validation) only appear *after* `main.c` has decided to type-check.
  So it type-checks the whole program and then drops the result: tiptap takes 3.1 s against tsgo's 0.7 s (tsgo:
  `Types: 89`). Up to ~800 projects only have config or option codes under tsc (a rough filter, codes 5000-6999;
  TS6133 is in that range too, so measure it properly), and they account for about a quarter of tttt's sweep time.
- **Type-count blowups.** rxjs/packages/rxjs (both sides fully check, same 22030 diagnostics): tttt 3.0 s / 1.83M
  types / 1.9 GB against tsgo `--singleThreaded` 2.2 s / 0.70M types / 0.8 GB. On type-fest the counts agree (tttt
  4.78M, tsgo 5.10M), so **the type count is a machine-independent signal of a missing cache or a runaway
  instantiation**. Wall times on this shared box are noisy (the sweep's "tttt 2801 s vs tsc 2811 s" line was measured
  while 4 agents ran; idle, a 1/8 sample is 84 s vs 214 s), but type counts aren't.

## Stage 1 (one session): the gate, and the tool. **Done 2026-10-11**
Gate: `program_report_global_diagnostics()` (TS2318) and the options validation run before `main.c` decides to check;
either gates (tiptap 3.9 -> 0.2 s; source sweep 3119 -> 3125, clidiff sample 779 -> 783). `scripts/perfsweep.py` exists,
one line in `scripts/oracles.py`, the baseline is in measurements.md "Perf at breadth" (median check ratio 0.21, types
0.76; 1779 projects). Lesson: tsc `--noEmit` reads and writes an incremental project's .tsbuildinfo and skips checking
when it says nothing changed, so tsgo's side is measured with a throwaway `--tsBuildInfoFile` (the first pass, without,
hid ~300 projects and ranked mcp-sdk as a 98x outlier that was really 0.86x). The rxjs 2.6x stands; the new worst are
below.

1. **The gate**: validate options (whatever `program_resolve_names()` reports as DIAGNOSTIC_OPTIONS, and
   anything else tsc counts as options/global diagnostics) *before* `main.c` decides `typecheck`, so a gated program
   does no checking. Keep the declaration-diagnostics exception (`emit_declarations`): tsgo with
   declaration/composite and noEmit=false still reports them. Check this against tsgo on tiptap and a composite
   project with a TS5090. `scripts/clidiff.py --sample 4` and the source sweep must not change verdicts.
2. **`scripts/perfsweep.py`**: for every source sweep project (and the corpora, `--corpora`) where *tsgo actually
   checks* (its `Types` > ~1000), tttt `--checkers 1 --threads 1 --time` (check ms, types, arena MB, peak RSS from
   `/usr/bin/time` or getrusage) against `tsc -p P --noEmit --singleThreaded --extendedDiagnostics` (Check time,
   Types, Instantiations, Memory used). Cache tsgo's side in `test/perfsweep/` (types/instantiations are stable,
   times are per machine: store them with the machine name and remeasure tsgo when the machine changes). Output:
   the distribution of tttt/tsgo check-time ratios (median, p90, worst), the total, and projects ranked by **types
   ratio** (tttt types / tsgo types) and by time ratio, with `-f regex`, `--last`, `--sample N`. Run serially or with
   `-j` and a warning that wall times are inflated under load. One line in `scripts/oracles.py` (median ratio, worst
   types ratio). Add it to CLAUDE.md's script list.
3. Record the baseline in measurements.md ("Perf at breadth"), with the 20 worst projects by types ratio and by time.

## Stage 2 (a fan-out session): drain the outliers
Stage 1's ranking by types ratio (measurements.md): date-fns/pkgs/docs 16.4, feathers/packages/typebox 7.0, nitro 5.2,
rxjs/packages/rxjs 2.6, nuxt 2.6, lit/packages/labs/ssr-react 2.4, dokploy (3 projects) 2.3, mcp-sdk/packages/codemod
2.2, quill 1.8, immutable-js 1.8; by time, linkwarden/apps/mobile (1.6x tsgo's check time at 0.87x its types). Group
them by root cause before dealing them out (nitro/nuxt likely share one, as do the dokploy three).
Four Opus sub-agents, one disjoint set of outlier projects each (worst types ratio first: rxjs, then whatever stage 1
ranks), each finds the root cause (`TTTT_FILE_TIMES=1` for the file, then which type kinds grow: `scripts/arenasites.sh`;
the tsgo comparison is its Types/Instantiations; an instrumented tsgo through the `test/oracle` overlay if needed)
and fixes it the way tsc does it (a cache tsc has, a laziness tsc has), never with a project-specific shortcut.
Validate each with the whole oracle set (suite 0 lost, `scripts/oracles.py`, `scripts/instrcount.sh` on the
corpora not up), and rerun perfsweep at the end. Leftovers go into bugs/perf_leads.md.

## Validation
- tiptap (and the other gated projects) do no type checking: tttt's time there is load + parse + resolve.
- perfsweep exists, its baseline and post-drain numbers are in measurements.md, and the worst types ratio is
  understood for each of the top 10 (fixed, or written up with a reason in bugs/perf_leads.md).
