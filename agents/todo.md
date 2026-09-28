# Todo
Written by the insights of 2026-09-27 (third of the day, after the window of plans 32-34); `vision.md` "Course
correction" says why. **Ten sessions, then insights.** Delete entries as they complete (leave a one-line "done" note
with the numbers). A plan that overruns its entry takes its extra sessions out of this budget: when 10 sessions have
run (count the commits since this file was written), the next session is insights, whatever is left here. New work
goes into plans or triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus
numbers (`./realworld.py --corpus X` for every pinned corpus) and a commit.

1. ~~**Plan 32 stage B**~~ done: outer type parameters. Suite 9409 -> 9427, nothing lost, corpora unchanged,
   callgrind neutral (zod 10.885 -> 10.858 G, valibot 5.247 -> 5.247, tsc 5.062 -> 5.063, type-fest 3.334 -> 3.341).
2. ~~**Plan 35 stage 1**~~ done: `displaydiff.py --fine` (first-differing-line ranking), queue written into plan 35.
   Suite 9427 -> 9666 (error-recovery types, type-writer rules, IIFE parameters), webpack 135 -> 130, other corpora
   unchanged.
3. ~~**Perf**~~ done: tttt beats single-threaded tsgo on every corpus (webpack 0.69 -> 0.56 s vs 0.60): huge pages
   (re-exec with glibc's tunable, madvised arena), parser lists shrink to fit (RSS -20%), TS2454's check no longer
   re-walks flow, inference's deep-nesting limit is tsc's 2 (+ circularity, typesDefinitelyUnrelated: zod's TS2589s
   gone), relation budget (TS2859; suite 50 -> 23 s). Suite 9666 -> 9667, webpack 130 -> 118, zod 15 -> 13.
   Memory is the next perf front (`bugs/perf_leads.md`: `Prop` split, 3.5x tsc's type count, AST node arenas).
4. **Plan 36 planning**: parallelism readiness. Prototype parallel parsing, measure, audit bind/check shared state,
   write the staged plan. No production parallelism in this session unless the prototype is clean and cheap.
5. **Plan 26**: stage 3 leftovers (global expando namespaces, prototype methods' templates) then stage 4 as far as
   it goes; webpack 118 is the corpus check.
6. **Plan 27 stage 3**: lexical diagnostics (`syntaxstats.py` lexical clusters; tsc 5.5's regex literal checks,
   TS1497-TS1535, are unimplemented entirely: `./unimplemented_codes.sh`).
7. **Plan 35 stage 2**: the next items of its queue.
8. **Corpus session**: plan 32 stage C (mixin classes, type-fest 4 -> 0), then zod's 15 (plan 19).
9. **Suite checker bugs 4**: `bugs/missing_checks.md` buckets; 656 tests where tsc reports errors and tttt none.
   If time is left, plan 34's queue (error text, 311 tests).
10. **Triage**: drain triage.md, prune hacks.md (677 lines) of entries that are fixed, refresh measurements.md.
11. **Insights.**
