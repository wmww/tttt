# Todo
Written by the insights of 2026-09-27 (second of the day, after the window of plans 29-31); `vision.md` "Course
correction" says why. **Ten sessions, then insights.** Delete entries as they complete (leave a one-line "done" note
with the numbers). A plan that overruns its entry takes its extra sessions out of this budget: when 10 sessions have
run (count the commits since this file was written), the next session is insights, whatever is left here. New work
goes into plans or triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus
numbers (`./realworld.py --corpus X` for every pinned corpus) and a commit.

1. ~~Plan 33~~ done: webpack pinned (checkJs, 1238 files), 6146 -> 3981; the rest is JSDoc (typedefs, `@type` on
   assignments/properties, `@template`), ranked into plan 26 stage 3. No sixth corpus pinned (entry 9 has it).
2. ~~Plan 30 stage 3~~ done, plan 30 complete and deleted: generic TS2536, R17 (type-fest 19 -> 11), `infer X
   extends C` names resolved at last, the mapped-key half of conditional flow types; suite 8953 -> 8958.
3. ~~Plan 34 stages 1-2~~ done: `errtextdiff.py`, tsc's incompatible stack and a dozen message rules; the bucket
   432 -> 302, suite 8958 -> 9090. The rest is a ranked queue in plan 34 (no session scheduled for it).
4. ~~Plan 32 planning~~ done: rewritten into three stages; stage A (intersection members, the mixin signature rule)
   executed too, suite 9090 -> 9091. Stages B (outer type parameters: local declarations in generic functions leak `T`)
   and C (mixin classes) are unscheduled: the next insights places them.
5. ~~Plan 26 stage 3~~ done: typedefs/callbacks/imports/enums declare, `@template`, `@type` on assignments and
   members, JSDoc names report TS2304; JS tests 310 -> 375, suite 9091 -> 9156, webpack 3981 -> 515. Its leftovers
   (global expando namespaces, prototype methods' templates: 13 tests lost to the now-visible TS2304/TS2503) are ranked
   in plan 26 "Stage 3 leftovers"; the next insights places them.
6. **Suite checker bugs 3** (`bugs/missing_checks.md`: single-code buckets, target/module-gated diagnostics).
7. **Perf: the parser** (`bugs/perf_leads.md` "Parser": first-char dispatch in `parse_expr`/`parse_type`/keyword
   probes, the `parse_fail` count; nest is 170 of 300 ms parsing and the closest margin to tsgo) plus the
   `is_simple_related` same-enum shortcut. Callgrind before/after on every corpus, `./bench.sh`, `measurements.md`.
8. **Plan 27 stage 2** (permissive grammar; ~190 tests, 476 with a spurious TS1xxx).
9. **Corpus session** (plan 19): the fifth corpus' remaining causes, type-fest's 11 (R20,
   exactOptional; mixins are plan 32 stage C), and if plan 33 pinned no second new corpus, pin an inference-heavy one
   (tRPC / zod v4 / drizzle).
10. **Triage**: drain triage.md; also shrink plan 19 (443 lines, mostly done R-items: keep the live ones and the
    lessons, history is in git) and measurements.md (551 lines: keep the latest of each kind).
11. **Insights.**
