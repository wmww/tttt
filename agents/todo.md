# Todo
Written by the insights of 2026-09-29 (after the window of plan 38); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus) and a commit.

1. **Corpora: pin two new ones** (the pinned ones are out of signal: 0 everywhere but webpack 6, xstate 19). Pick
   for features the current set doesn't exercise, each clean under tsc 5.8 with its own tsconfig: (a) a type-level
   heavy library of a kind not yet pinned (drizzle-orm, @tanstack/router, effect, tRPC: SQL/route builders, deep
   template literal and conditional chains), (b) a large application-style package with lots of classes and
   overloads (vue core's packages, or TypeScript's own `src/services` through an overlay tsconfig: it is already
   vendored). Add to `thirdparty/ensure-corpora.sh` like xstate, then work the new diagnostics down by root cause with
   Opus sub-agents in worktrees (plan 19 gets a section per corpus). Record instructions and `./bench.sh` numbers for
   them in measurements.md.
2. **Perf session.** Instructions have crept +8% on tsc's corpus since 2026-09-28 (4.82 -> 5.21 G, similar
   elsewhere) over ~20 sessions of fixes with no profile. Callgrind the corpora against f0dd10eb's numbers, bisect
   the biggest steps, fix or gate. Run `./bench.sh` on a quiet machine (owed since the load pool; check `uptime`
   first). hacks.md "Generic source constraints push both nesting stacks" (+14% on react). `bugs/perf_leads.md`.
3. **Planning: plan 39** (one resolution stack for circularity; stub in `plans/39_resolution_stack.md`). Prototype
   on its tests, measure the per-LazyType cost, decide.
4. **Plan 39 execution** (or, if the planning session said no, a suite checker bugs session like entry 7).
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
