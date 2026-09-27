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
6. ~~Suite checker bugs 3~~ done: await grammar (top-level await/for await, TS1308/TS1103 with TS1356), import.meta
   module gating, TS1216, TS2816, eager TS2318, TS2839, TS2842, enum member value checks, TS7005 ambients, `any` keys
   and index signatures; node module kinds imply their target; suite 9156 -> 9210, corpora unchanged. TS2705 (lazy in
   tsc) tried and reverted, notes in missing_checks.md.
7. ~~Perf: the parser~~ done: first-char dispatch, keyword probes in place, whitespace fast path, tsconfig pruning,
   resolution cache; callgrind tsc corpus 5.84 -> 5.05 G, nest -28%, webpack -19%. Next levers in `perf_leads.md`.
8. ~~Plan 27 stage 2~~ done: modifiers (a checkGrammarModifiers port), heritage clauses, index signatures, `with`,
   yield/await, private names; parser-diff tests 544 -> 402, suite 9210 -> 9363. Leftovers in plan 27.
9. ~~Corpus session~~ done: zod v4.6.5 pinned (inference-heavy, exactOptionalPropertyTypes; 87 -> 15 the same day,
   leftovers ranked in plan 19 "zod corpus": the missing type, ProtoOf inference, keyof deferral), type-fest 11 -> 4
   (mixins only), webpack 515 -> 135 (sub-agent; plan 26 "Stage 3 leftovers"); suite 9363 -> 9395. zod is the
   heaviest corpus (11.7 G instructions, profile in perf_leads.md).
10. ~~Triage~~ done: triage.md drained (fixes + routing), plan 19 524 -> ~100 lines (live items and lessons only),
    measurements.md 623 -> ~70 (latest of each kind, fresh failstats/bench); suite 9395 -> 9409, corpora unchanged.
    Bench: webpack is the one corpus where tsgo single-threaded beats tttt (0.60 vs 0.69 s).
11. **Insights.**
