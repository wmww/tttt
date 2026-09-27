# Todo
Written by the insights of 2026-09-27 (after plans 26-30 were written); `vision.md` "Course correction (insights of
2026-09-27)" says why. **Ten sessions, then insights.** Delete entries as they complete (leave a one-line "done"
note with the numbers). A plan that overruns its entry takes its extra sessions out of this budget: when 10 sessions
have run (count the commits since this file was written), the next session is insights, whatever is left here. New
work goes into plans or triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the
corpus numbers (`./realworld.py --corpus X` for all four once plan 31 is in) and a commit.

1. Done: plan 29 stages 1+2 (suite 8599, type-fest 46, valibot 54, react 1, tsc 0).
2. Done: plan 29 stage 3 (suite 8603, type-fest 48 with different content, valibot 54, react 1, tsc 0; plan 19 R21
   rewritten with what the moved TS2589s uncovered). Stage 4 not done (optional; plan 29). The type-fest/valibot
   re-sample for plan 19's ranking wasn't done: fold it into entry 7.
3. Done: plan 30 stages Q + 1 (suite 8613, valibot 50, type-fest 48, react 1, tsc 0; callgrind +0.5%).
4. Done: plan 30 stage 2 (suite 8616, type-fest 48, valibot 50, react 1, tsc 0; callgrind +0.2%). Stage 3 waits
   for the insights.
5. Done: plan 31 (nestjs-boilerplate pinned as `--corpus nest`: 134 -> 0 over five commits, all tsc rules; suite
   8631, type-fest 47, valibot 49, react 1, tsc 0). Decorators showed nothing: a clean corpus only finds spurious
   diagnostics (plan 19's NestJS section). Plan file deleted.
6. Done: plan 26 stage 2 (JS tests 260 -> 299, suite 8686, corpora unchanged). The JS TS2339 gate stays (hacks.md
   "Assignment declarations"; triage.md "From plan 26 stage 2").
7. Done: plan 19 corpus session (valibot 49 -> **0**, react 1 -> **0**, type-fest 47 -> 19, nest 0, tsc 0; suite
   8686 -> 8703). Three worktree sub-agents plus the session; expect-type mismatches are visible now (arity of
   generic rest types). Left: R17 (plan 30 stage 3), R20, mixins (new plan 32, draft), exactOptional missing type.
8. Done: suite checker bugs 2 (suite 8703 -> **8948**, corpora unchanged: type-fest 19, others 0). ~45 commits of
   small missing checks, two thirds by two worktree sub-agents (module/export codes; declaration/scoping codes).
   Leftovers in triage.md "Single-code error buckets". Also: unresolved names and missing properties are error-typed
   now (TS2347), empty statements exist in the AST.
9. Done: perf (plan 28: calls resolve once, tsc corpus callgrind 6.17 G -> 5.79 G; valibot's plan 31 regression
   was eager instantiation of mapped instances, fixed: 8.15 G -> 5.64 G, 0.81 s wall). Suite 8948, corpora unchanged.
10. Done: triage (suite 8948 -> 8952, corpora unchanged: type-fest 19, others 0). triage.md drained into new
    `bugs/*.md` files (missing_checks, type_core, display, harness, perf_leads) and plans 26/27; hacks.md 1025 -> 603
    lines. Fixed on the way: distributed conditionals no longer keep their alias (`Awaited<X | Y>`), homomorphic
    `Prettify` instances print their structure, generic spreads are `{ a } & T & { b }` in tsc's order.
11. **Insights.** Waiting to be scheduled: plan 27 stage 2 (permissive grammar), plan 30 stage 3, plan 26 stages 3-4,
    plan 32 (mixin classes, a draft needing a planning session), decorators (if the fourth corpus asks for them).
