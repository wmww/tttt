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
4. **Plan 39 stage 2** (`plans/39_resolution_stack.md`: annotated declarations and signature returns push, TS2577,
   base types/constraints, which function of a nested cycle reports). Log tsc's stack before guessing.
5. **Plan-sized hacks** (Opus sub-agents in worktrees, one each; they are listed in hacks.md): instantiation-limit
   poisoning (signatures/members/variances memoized past `ty_instantiation_limit_reached`), `check_assignable`
   letting `any` through to `never` (25 tests depend on flow typing yielding `any` where tsc has `never`), conditional
   skipTrue (sharing of conditional instantiations; zod perf, `first_unfit_argument`), `ty_non_nullable` ->
   `ty_non_nullish_part` with `NonNullable<T>` printing, `chain_object_type` (TS18048 on `a?.b.c`: an `optional`
   marker type). Each is a tsc mechanism port: land it only at 0 lost and corpora unchanged; a sub-agent that finds
   it is bigger writes a plan instead.
6. **Planning: plan 40** (declaration-emit diagnostics without emit: TS2742, TS4xxx, isolatedDeclarations; 45
   failing tests, 41 fail only on these). Stub in `plans/40_declaration_diagnostics.md`.
7. **Suite checker bugs 7** (four Opus sub-agents by failstats bucket, as entries 5 of the last window): the 237
   tests with the right codes at the right positions but other text first (`./errtextdiff.py`, plan 34), then
   single-code tests (`bugs/missing_checks.md`).
8. **Planning: plan 38 stage 3** (resolve once: a call's resolution cached per node, the replay record and
   `LazyMemo.speculative` deleted). Prototype and measure suite, xstate, and the instructions (repeated resolution is
   also a perf cost). May conclude "not worth it"; then say so in plan 38 and stop.
9. **Planning: plan 41** (project references; stub in `plans/41_project_references.md`), including pinning
   TypeScript's own `src/tsconfig.json` (references) as the corpus that validates it.
10. **Triage.**
11. **Insights.**
