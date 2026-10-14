# Todo
Written by the insights of 2026-10-14 (after the fifteenth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle) after checker changes, `scripts/instrcount.sh`, an empty `git worktree list` and /tmp scratch deleted
(CLAUDE.md, the disk filled once), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry starts, do plan
58 (the re-pin) first** and push the rest down (it takes one entry's place; on 2026-10-14 `next` was still a dev build).

Starting point (d072fbbd + this commit): suite **13520 / 13633** (tsgo's 13455 / 13568 + 65 own tests, all passing),
corpora 0 spurious (missing: webpack 1, vue 3; effect 3 spurious), DT sweep 8594 / 8595, `$ExpectType` 68637 / 68640,
flagflip 42 / 6 / 3, scenarios 210 / 0 failed, source sweep 3332 / 3363 (sample 811 / 822), kdiff 4 / 3363,
perfsweep check median 0.17x tsgo, worst types ratio 1.62. Instructions: measurements.md "Perf session (2026-10-14)".

**This window: the suite to 100% (the human's request, plan 66), `.d.ts` emit to a working writer (plan 64), and the
checker count following the cores (plan 62).** Memory against bun gets a planning session, not more struct diets.

1. Done (2026-10-14): plan 66 stage 1 fan-out, suite 13520 -> 13604 (agent C's plan 69 stage 1 was found uncommitted
   in its worktree by the next session and merged then; stale worktrees removed).
2. Done (2026-10-14, one session for entries 2 and 3): content mappers, `contentMapper*` 15 / 15, suite 13615 / 13660,
   both content-mapper scenarios pass; plan 56 deleted (documentation.md "Content mappers").
3. (Merged into 2.)
4. **Plan 62 stages 2 and 3.** Display independent of K (`order.c` id ties, symbol ids in names), then `--checkers`
   follows the cores with a size cap; the human item asking for `scripts/bench.sh` at K 4/8/12/16 on the 12-core box.
   If stage 2 overruns, stage 3 goes to the next window (don't take a third session).
5. **Planning: plan 67, memory against bun.** vscode peaks at 4.2 GB single-threaded (bun 2.1 GB at defaults), and plan
   63's struct diet missed its targets. Measure where the peak is (AST, binder, checker arenas, per-checker
   duplication: plan 62 stage 4's question) and cost the structural options: 32-bit node references in per-file
   arenas (plan 63 stage 2 is their prerequisite), a shared frozen lib type layer, freeing the AST's
   checker-irrelevant parts. Prototype the most promising far enough to measure; the plan may say "stay incremental"
   (then plan 63 stage 1b/2 are the path) with numbers. Fold plan 63 into it or keep it, as the evidence says.
6. **Plan 64 stage 1 + start of stage 2**: the suite `dts` component, `scripts/dtsdiff.py`, then the writer.
7. **Plan 64 stage 2**: the writer on declaration_diagnostics.c's walk, TS sources, no comments.
8. **Plan 64 stage 2 finished + plan 66 stage 3**: the harness emits declarations before collecting diagnostics like
   tsgo's `harnessutil.CompileFiles`; re-bucket plan 66 bucket 2.
9. **Plan 66 stage 4: second fan-out** on what's left of the suite, every remaining test fixed, planned or argued in
   `bugs/tsgo_divergences.md` (plan 66's rule). One agent on the real-code residue instead: the source sweep tail,
   effect's 3 spurious, vue's 3 / webpack's 1 missing, and plan 65 (TS5115) if its zod side can be solved.
10. **Triage.**
11. **Insights.**

After the insights entry (for the next window): plan 64 stages 3-5 (comments, CLI/`-b`, declarationMap); plan 67's
first stage (or plan 63 stage 1b/2); plan 58 if not done; plan 65 if not done.
