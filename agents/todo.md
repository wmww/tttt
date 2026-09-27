# Todo
Written by the insights of 2026-09-27 (third of the day, after the window of plans 32-34); `vision.md` "Course
correction" says why. **Ten sessions, then insights.** Delete entries as they complete (leave a one-line "done" note
with the numbers). A plan that overruns its entry takes its extra sessions out of this budget: when 10 sessions have
run (count the commits since this file was written), the next session is insights, whatever is left here. New work
goes into plans or triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus
numbers (`./realworld.py --corpus X` for every pinned corpus) and a commit.

1. **Plan 32 stage B** (execution): outer type parameters, local declarations in generic functions capture `T`.
   Type-core work: check zod/valibot instruction counts before and after.
2. **Plan 35 stage 1** (tool + first fixes): split displaydiff's `other`/`we-any` by mechanism, write the queue into
   plan 35, fix from the top.
3. **Perf**: webpack first (the one corpus tsgo wins, even single-threaded: profile the JSDoc/JS checking path), then
   `DEBUG_ALLOCATOR` in release (`src/prelude.h`: measure with it off), then zod's `infer_from_returns` (tsc infers
   between references to the same generic class by type arguments; `perf_leads.md`). Re-measure callgrind on every
   corpus and replace the Instructions section of `measurements.md`.
4. **Plan 36 planning**: parallelism readiness. Prototype parallel parsing, measure, audit bind/check shared state,
   write the staged plan. No production parallelism in this session unless the prototype is clean and cheap.
5. **Plan 26**: stage 3 leftovers (global expando namespaces, prototype methods' templates) then stage 4 as far as
   it goes; webpack 135 is the corpus check.
6. **Plan 27 stage 3**: lexical diagnostics (`syntaxstats.py` lexical clusters; tsc 5.5's regex literal checks,
   TS1497-TS1535, are unimplemented entirely: `./unimplemented_codes.sh`).
7. **Plan 35 stage 2**: the next items of its queue.
8. **Corpus session**: plan 32 stage C (mixin classes, type-fest 4 -> 0), then zod's 15 (plan 19).
9. **Suite checker bugs 4**: `bugs/missing_checks.md` buckets; 656 tests where tsc reports errors and tttt none.
   If time is left, plan 34's queue (error text, 311 tests).
10. **Triage**: drain triage.md, prune hacks.md (677 lines) of entries that are fixed, refresh measurements.md.
11. **Insights.**
