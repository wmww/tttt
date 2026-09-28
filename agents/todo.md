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
4. ~~**Plan 36 planning**~~ done: prototypes of parallel loading (cache-warmer pool: byte-identical output, whole
   runs 5-10% faster on 4 cores) and K checkers (sequential; webpack's check 3.1x at K=4 ideal, +4..19% duplicated
   work, only union member order differs). Plan 36 rewritten with 3 stages; stage 3 needs a decision (fixed
   logical checker count, recommended). Nothing in `src/` changed.
5. ~~**Plan 26**~~ done: stage 3 leftovers, the JS TS2339 gate removed, TS8xxx syntax errors, `@implements`,
   JSDoc modifier tags, typed property declarations, CommonJS aliases. JS tests 408 -> 470, suite 9667 -> 9729,
   webpack 118 -> 116, other corpora unchanged. Left: plan 26 Status and hacks.md "JavaScript".
6. ~~**Plan 27 stage 3**~~ done: numbers, templates, unicode identifiers and UTF-16 columns, file decoding (UTF-16,
   binary), escapes, conflict markers, regex validation (`regex_check.c`, a sub-agent's port), and an `other:M` sweep.
   Parser diffs 402 -> 263 tests, suite 9729 -> 9833, corpora unchanged, tsc-corpus parse 104 -> 109 ms. Left:
   escaped identifier names (`bugs/escaped_identifiers.md`); tagged templates as calls went to triage.
7. ~~**Plan 35 stage 2**~~ done: stage 1's queue 1-9 (constructor-typed fields, tagged templates as calls, ...)
   plus heritage names, contextually typed field initializers (a spurious TS7006), non-strict null subtyping. Suite
   9833 -> 9951, corpora unchanged. The new queue (non-widening null/undefined first) is in plan 35.
8. ~~**Corpus session**~~ done: plan 32 stage C (mixin classes; the plan is finished and deleted), then all of zod.
   Suite 9951 -> 9978, type-fest 4 -> 0, zod 13 -> 0, webpack 116 -> 115. valibot +2.5% instructions (triage.md).
9. ~~**Suite checker bugs 4**~~ done: ~35 small checks in the main tree (output paths TS5055, module-kind/option
   checks, TS1259, TS2353 for binding patterns, TS2699, TS18050 on null objects, TS7010 from widening, import()
   grammar, TS2350, TS7029, ...) plus four Opus sub-agents in worktrees (missing TS2339, missing TS2322, TS2303/TS2209,
   two corpus regressions: isGenericType and variance annotations). Suite 9978 -> 10188, tsc-errors-tttt-none 541 ->
   421, corpora unchanged (webpack 115). The single-code buckets are now mostly spurious errors (missing_checks.md).
10. **Triage**: drain triage.md, prune hacks.md (677 lines) of entries that are fixed, refresh measurements.md.
11. **Insights.**

After insights (for it to order): plan 36 stage 1 (parallel loading, one session), stage 2 (check-phase readiness,
single-threaded `--checkers K`), then stage 3 (parallel checking) once the union-order decision is made.
