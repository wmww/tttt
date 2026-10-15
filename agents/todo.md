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
4. Done (2026-10-14): plan 62 stages 2 and 3. kdiff K=16 4 -> 1 / 3363 (inbox-zero, tsgo's too), DT 0, corpora and
   suite dumps byte-identical; `--checkers` defaults to one per thread, at most one per 64 KB of checked source. The
   12-core bench is a human item (triage.md); stage 4 (what the duplication is) folds into entry 5.
5. Done (2026-10-14): plan 67 written. Bun bounds its checker's memory (flat RSS while checking); tttt's checker
   grows linearly, and **recycling checkers at a memory budget** (prototype, byte-identical output on every corpus
   even with a fresh checker per file) takes vscode 4.26 -> 2.51 GB at K1, 4.90 -> 3.03 GB at the defaults, ~+3%
   wall. Plan 62 stage 4 answered (duplication +19%, no shared lib layer); plan 63 folded in (1b = plan 67 stage 2);
   the compact AST costed (~-450 MB, deferred to insights).
6. Done (2026-10-14): plan 64 stage 1 (dts component: 1408 TS tests, `scripts/dtsstats.py`; `scripts/dtsdiff.py`:
   1233 projects / 76294 files, agent) and most of stage 2: the writer (`ty/declaration_emit.c`), dts 1192 / 1408,
   dtsdiff 400 / 1233 projects, 48974 / 76294 files. Plan 64 "Status" lists what's left.
7. Done (2026-10-15): plan 64 stages 2-3. dts 1192 -> 1345 / 1408 (95.5%), dtsdiff 400 -> 922 / 1233 projects,
   48974 -> 74279 / 76294 files; comments (`ty/decl_comments.c`), expandos as tsgo 7.1 builds them, late bound index
   members, export default of function/class expressions, accessors as get/set... The dts component stays out of the
   headline count until stage 4 (23 of its 63 failures are output paths and declaration maps). What's left:
   bugs/dts_emit.md, plan 64 "Status".
8. **Plan 66 stage 3**: the harness emits declarations before collecting diagnostics like tsgo's
   `harnessutil.CompileFiles`; re-bucket plan 66 bucket 2. Then plan 64 stage 3's long tail if it overran.
9. **Plan 66 stage 4: second fan-out** on what's left of the suite, every remaining test fixed, planned or argued in
   `bugs/tsgo_divergences.md` (plan 66's rule). One agent on the real-code residue instead: the source sweep tail,
   effect's 3 spurious, vue's 3 / webpack's 1 missing, and plan 65 (TS5115) if its zod side can be solved.
10. **Triage.**
11. **Insights.**

After the insights entry (for the next window): **plan 67 stage 1 first** (checker recycling: one session, the
biggest memory win on the table, -40% of vscode's peak), then plan 67 stage 2; plan 64 stages 3-5 (comments,
CLI/`-b`, declarationMap); plan 58 if not done; plan 65 if not done.
