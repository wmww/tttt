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
6. Done (2026-10-04, one session + two sub-agents, ~25 commits): plan 40 stage 3 (`module_specifiers.c`, TS2742,
   alternative containing modules) and plan 41 stage 3 (tsc-src 12 -> 0, typescript-eslint pinned, 66 -> 0). Suite
   12712 -> 12728, 0 lost; scenarios 199 -> 208. Callgrind react +3.7%, vue +3.4% (both from correct checker fixes:
   leads for entry 8 in measurements.md / perf_leads.md), nest +0.7%.
7. Done (2026-10-05, one session + three prototype sub-agents): plan 42 written. Go, as four independent ~1-session
   stages (prototypes in `plans/42_prototypes/`, each 0 lost): reverse mapped types +1, reduced intersections +2,
   mapped info / re-entry +2ish, optional barrier stage. tsc resolves re-entered calls again (plan 38's premise was
   wrong). Not the memory fix: drizzle is 896 MB at one checker (tsgo 1 thread 1054), 1.7 GB at four (tsgo 1.87).
8. Done (2026-10-05, one session + one sub-agent, 7 commits): base_constraint() memo (react 1.695 -> 1.687 G),
   vue's regression explained (tsc-inherent overload rounds) and a quarter back (8.105 -> 8.027), bench like-for-like
   (measurements.md), checkers get contiguous runs of files (drizzle K=4 1688 -> 1253 MB), Prop 56 / Signature 176 /
   Ty 136 bytes, packed interned keys. Below tsgo's peak RSS everywhere except nest/webpack (the AST: perf_leads.md
   "Memory", needs a plan). Suite and corpora unchanged.
9. Done (2026-10-05, one session, 2 commits): plan 42 stages 1 and 2, suite 12728 -> 12731, 0 lost, every corpus
   byte-identical; callgrind up to +1.0% (vue; drizzle/xstate +0.7%) from stage 2's reductions in is_related_to.
   Stage 2's `reduction_forces_members` HACK turned out unneeded and is gone. Stages 3-4 remain in the plan.
10. Done (2026-10-05, one session + five sub-agents, ~22 commits): triage drained (triage.md header). Suite 12731 ->
   12753, 0 lost; scenarios 208 -> 215; corpora unchanged; 1-vs-4-checker suite dumps byte-identical; checkReturn
   port landed with deferred function bodies; plan 43 (AST memory, ~-100 MB on nest/webpack estimated) written.
11. **Insights.**
