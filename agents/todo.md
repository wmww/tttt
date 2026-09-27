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
6. **Plan 26 stage 2**: expandos and JS classes (the binder's assignment declarations). `./jsstats.py` before/after.
7. **Corpus session (plan 19)**: the top root causes from entry 2 (type-fest/valibot re-sample; nest is at 0 and
   react at 1), across all four corpora.
8. **Suite checker bugs 2**: failstats' single-code buckets (triage.md "Single-code error buckets") and the
   module/target-gated diagnostics (triage.md "Target-gated diagnostics"; missing TS1202/TS1203/TS2441/TS2725/TS1262
   in tests where tttt reports nothing). Like entry 8 of the last window: many small missing checks, commit each.
9. **Perf: plan 28** (resolve calls once), then a check-up with callgrind counts on the tsc corpus (the stable
   measure) and `./bench.sh` including the fourth corpus; update `measurements.md`. First: valibot is still ~19%
   slower than before plan 31 part 3 (measurements.md "plan 31": callgrind 5.41 G -> 8.19 G, instantiation);
   find out if that's legitimate distribution work or waste.
10. **Triage.** Drain triage.md; also prune hacks.md (914 lines: drop what plans 26-30 fixed, consolidate).
11. **Insights.** Waiting to be scheduled: plan 27 stage 2 (permissive grammar), plan 30 stage 3, plan 26 stages 3-4,
    decorators (if the fourth corpus asks for them).
