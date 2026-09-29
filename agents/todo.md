# Todo
Written by the insights of 2026-09-29 (after the window of plan 38); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus) and a commit.

1. Done 2026-09-29: drizzle-orm and vue core pinned (both 0 under tsc 5.8); drizzle 693 -> 8, vue 107 -> 0 with four
   Opus sub-agents, suite 12040 -> 12047, other corpora unchanged. Plan 19 has both sections and the leftovers.
2. Done 2026-09-29: perf session. Instructions back under f0dd10eb's everywhere but webpack/valibot/zod (tsc 5.23 ->
   4.61 G, react -29%, vue -17%; missing tsc caches, not reverts), quiet `./bench.sh` run recorded, `./instrcount.sh`.
   Leads in `bugs/perf_leads.md` "Where the time goes now".
3. Done 2026-09-29: plan 39 planned and stage 1 landed (the stack beside the memos, declarations bail at once,
   call and eager-intersection barriers): suite 12047 -> 12052, 0 lost, corpora and zod-root unchanged, +0.1% instr.
4. Done 2026-09-29: plan 39 stage 2 (annotated bindings, pattern names, signature returns, interface bases on the
   stack; TS2502/TS2577/TS2310; defaults_in_flight and jsdoc_values_in_flight gone): suite 12052 -> 12057, 0 lost,
   corpora unchanged, +0.0..0.4% instr. Leftovers in plan 39 "Stage 3" (mostly wait on plan 38 stage 3).
5. Done 2026-09-29: all five plan-sized hacks gone (five Opus sub-agents, one each, no plans needed): `any` to
   `never` (the real bug: declarations without an initializer were checked), instantiation limit (results of an
   exhausted budget memoized like tsc), skipTrue (sharing was already there), `NonNullable<T>` (tsc's
   getNonNullableType), the `optional` marker for chains. Suite 12057 -> 12058, 0 lost, xstate 19 -> 18, drizzle
   8 -> 6, others unchanged, instructions -0.8..+0.2%.
6. Done 2026-09-29: plan 40 planned (three stages: the walk + isolatedDeclarations, accessibility through the
   printer's tracker, TS2742 with plan 41's specifiers). 41 tests; tsc's compiler, valibot, nest and drizzle enable
   declarations and are its false-positive oracles. Execution is for the next insights to schedule.
7. Done 2026-09-29: suite checker bugs 7 (four Opus sub-agents on disjoint test lists: related info/parser spans,
   header printing, elaboration chains, checker underlines, then single-code tests): suite 12058 -> 12316, 0 lost,
   errtextdiff bucket 236 -> 45, xstate 18 -> 14, drizzle instructions -14%, others +-0.8%. Leftovers in triage.md.
8. **Planning: plan 38 stage 3** (resolve once: a call's resolution cached per node, the replay record and
   `LazyMemo.speculative` deleted). Prototype and measure suite, xstate, and the instructions (repeated resolution is
   also a perf cost). May conclude "not worth it"; then say so in plan 38 and stop.
9. **Planning: plan 41** (project references; stub in `plans/41_project_references.md`), including pinning
   TypeScript's own `src/tsconfig.json` (references) as the corpus that validates it.
10. **Triage.**
11. **Insights.**
