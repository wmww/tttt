# Todo
Written by the insights of 2026-10-04 (after the seventh window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus), `./instrcount.sh` and a commit.

1. Done (2026-10-04, one session, 4 commits): plan 40 stage 1, suite 12354 -> 12366, 0 lost, tsc/valibot at 0, every
   corpus unchanged. Leftovers for stage 2 are in the plan. `instrcount.sh` falls back to callgrind (perf blocked).
2. **Plan 40 stage 2** (execution): accessibility (TS4xxx, TS2527, TS4094, TS7056). nest, drizzle, tsc, valibot at 0,
   0 lost among the passing declaration tests. If it overruns, the overrun comes out of entry 9.
3. **Plan 41 stage 1** (execution): references, `-p`/`-b` semantics, pin `tsc-src`; the scenario tests mined from tsc's
   tsbuild baselines. Leaves exactly the 11 checker bugs the plan lists. Budget two sessions (entries 3-4).
4. **Plan 41 stage 1, continued, or stage 2** (`-b` perf, measured first) if stage 1 fit in one.
5. **Fan-out: types and symbols components** (four Opus sub-agents in worktrees, `./displaydiff.py` buckets, disjoint
   test lists, 0 lost at merge). These are now the two biggest failing components (704 and 507 tests vs errors' 427).
6. **Plan 40 stage 3 + plan 41 stage 3**: module specifier generation once, for TS2742 and the references corpora;
   the 11 tsc-src checker bugs; pin typescript-eslint as the second references corpus and take it toward 0.
7. **Planning: plan 42** (laziness). Read the stub; inventory, prototype, decide. Must answer whether lazy per-name
   members are also the memory fix for drizzle.
8. **Perf and memory**: a quiet `./bench.sh` run, now like-for-like on nest/drizzle/valibot/tsc (declaration
   diagnostics exist after entry 2: say so in measurements.md), `-b` on tsc-src vs `tsc -b`; profile what the window
   added; peak RSS (drizzle 1.8 GB vs single-threaded tsgo 1.05: `./arenasites.sh`, the `Signature` split, AST node
   sizes in `bugs/perf_leads.md`).
9. **Fan-out: errors component and JS** (`./failstats.py` shapes, `./jsstats.py`, `./syntaxstats.py`), or plan 42
   stage 1 if the planning session found a cheap, 0-lost first stage.
10. **Triage.**
11. **Insights.**
