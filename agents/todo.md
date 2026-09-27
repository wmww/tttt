# Todo
Written by the insights of 2026-09-27 (after plans 26-30 were written); `vision.md` "Course correction (insights of
2026-09-27)" says why. **Ten sessions, then insights.** Delete entries as they complete (leave a one-line "done"
note with the numbers). A plan that overruns its entry takes its extra sessions out of this budget: when 10 sessions
have run (count the commits since this file was written), the next session is insights, whatever is left here. New
work goes into plans or triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the
corpus numbers (`./realworld.py --corpus X` for all four once plan 31 is in) and a commit.

1. Done: plan 29 stages 1+2 (suite 8599, type-fest 46, valibot 54, react 1, tsc 0).
2. **Plan 29 stage 3** (diagnostics at tsc's sites: TS2589, TS2456, TS4109, TS2615), stage 4 only if its measurement
   says so. Then re-sample what's left of type-fest/valibot and update plan 19's ranking.
3. **Plan 30 stage Q + stage 1** (`plans/30_substitution_types.md`): write constraint, restrictive "definitely true",
   tsc's indexed-access simplification and constraints.
4. **Plan 30 stage 2**: substitution types (coordinate with plan 29's lazy lowering, which is in by now). Stage 3
   goes after the insights unless stage 2 finishes early.
5. **Plan 31: the fourth corpus** (`plans/31_fourth_corpus.md`): a Node/server application, decorators if possible;
   first run, ranked root causes into plan 19, cheap fixes the same day.
6. **Plan 26 stage 2**: expandos and JS classes (the binder's assignment declarations). `./jsstats.py` before/after.
7. **Corpus session (plan 19)**: the top root causes from entries 2 and 5, across all four corpora.
8. **Suite checker bugs 2**: failstats' single-code buckets (triage.md "Single-code error buckets") and the
   module/target-gated diagnostics (triage.md "Target-gated diagnostics"; missing TS1202/TS1203/TS2441/TS2725/TS1262
   in tests where tttt reports nothing). Like entry 8 of the last window: many small missing checks, commit each.
9. **Perf: plan 28** (resolve calls once), then a check-up with callgrind counts on the tsc corpus (the stable
   measure) and `./bench.sh` including the fourth corpus; update `measurements.md`.
10. **Triage.** Drain triage.md; also prune hacks.md (914 lines: drop what plans 26-30 fixed, consolidate).
11. **Insights.** Waiting to be scheduled: plan 27 stage 2 (permissive grammar), plan 30 stage 3, plan 26 stages 3-4,
    decorators (if the fourth corpus asks for them).
