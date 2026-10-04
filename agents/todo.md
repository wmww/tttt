# Todo
Written by the insights of 2026-10-04 (after the seventh window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus), `./instrcount.sh` and a commit.

1. Done (2026-10-04, one session, 4 commits): plan 40 stage 1, suite 12354 -> 12366, 0 lost, tsc/valibot at 0, every
   corpus unchanged. Leftovers for stage 2 are in the plan. `instrcount.sh` falls back to callgrind (perf blocked).
2. Done (2026-10-04, one session, 7 commits): plan 40 stage 2, suite 12366 -> 12376, 0 lost, every corpus unchanged,
   callgrind +0.6% nest / +1.0% drizzle. Left: deep-recursion TS7056, TS5088 (plan 40). bench.sh's compiler corpus is
   now like-for-like for entry 8.
3. Done (2026-10-04, one session, 3 commits): plan 41 stage 1. `-b thirdparty/tsc/src` (corpus tsc-src) leaves the 11
   checker bugs + one 4-checker-only (triage), 3.65 s vs tsc -b's 15.6; `./scenarios.py` 199 pass / 19 fail (all in
   triage); suite 12376 -> 12377, 0 lost, corpora and callgrind unchanged.
4. Done (2026-10-04, one session, 3 commits): plan 41 stage 2. `-b` on tsc-src 3.65 s -> 1.32 s (tsgo -b 2.34 s),
   785 MB; reference sources' typed bodies skipped, two projects at once, parallel teardown. Suite, corpora and
   callgrind unchanged. Next lever for `-b` is parallel name resolution (plan 41 Status).
5. Done (2026-10-04, one session, ~110 picked commits): types/symbols fan-out, suite 12377 -> 12712, 0 lost; types
   component 703 -> 560 failing, symbols 506 -> 217; xstate 12 -> 8, other corpora unchanged; callgrind within +0.5%
   (webpack), vue/drizzle down. Leftovers in plan 35's queue and bugs/display.md.
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
