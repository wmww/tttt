# Todo
Written by the insights of 2026-10-16 (after the seventeenth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle; it exceeds the 10-minute foreground cap: run it in the background with a bounded wait) after checker
changes, `scripts/instrcount.sh`, an empty `git worktree list` and /tmp scratch deleted (CLAUDE.md), and a commit.
**If TypeScript 7.1.0 stable is on npm when an entry starts (`npm view typescript dist-tags`: on 2026-10-16 `next`
was `7.1.0-dev.20261015.1`), do plan 58 (the re-pin) first, then plan 66 stage 5 (a fan-out on the new failures)**,
and push the rest down (they take two entries' places). `claude-stop` is gitignored: never commit it.

Starting point (c7347260 + this commit): suite 13698 / 13740 (tsgo's 13556 / 13568, dts included), corpora 0 / 0
everywhere but vue 0 / 2, DT sweep 8595 / 8595, `$ExpectType` 68639 / 68640, flagflip 18 / 6 / 3, scenarios 210 / 0
failed, source sweep 3352 / 3363, kdiff 1 / 3363, recycle 4 / 3363, dtsdiff 1077 / 1233 projects, clidiff `--emit`
309 / 396. Perf and memory: plan 67 "Where it stands" (bun table); instructions: measurements.md, todo entry 9 of
the last window.

**This window: the product gaps.** Memory against bun (per-file arenas, then a prototype gate for the compact AST
and the per-checker floor at many cores), the loader's serial work, `.d.ts` for JS, and a plan for the CLI modes
users run tsc in (watch, incremental). Two small debt plans. The suite's last 12 stay off the table.

1. Done 2026-10-16: plan 72 stage 1. vscode load T4 940 -> 805 ms, T1 2525 -> 2310 (the loader takes the workers'
   resolutions, `./` names cached by joined path, path normalization skipped for clean paths, a stat cache); "< 150 ms
   at T1" not met (real resolution left), plan 72 says what a stage 2 would need. Every oracle unchanged.
2. Done 2026-10-16: plan 74 and plan 73. Plan 74: tsc's one-run callers relate once explaining, failed_log deleted, a
   top-level overflow cached (relationComplexityError 2x faster). Plan 73: a class's member symbols are one checker
   table (ty_class_member_symbols()): TS2300, the builder's joins, overload groups and computed accessor pairs (by key
   type: computed_symbol_first deleted). Suite 13699 / 13741 (+1 own test), every oracle and dtsdiff unchanged,
   instructions flat. Leftovers in hacks.md ("Reporting runs", "Member symbols").
3. Done 2026-10-16: real-code fan-out (3 agents, ~55 picks). Source sweep 3352 -> 3356 / 3363, dtsdiff 1077 -> 1116 /
   1233 projects (95611 -> 95824 files), suite 13745 / 13783, every other oracle unchanged; leftovers in
   bugs/sourcesweep_checker.md and bugs/dts_emit.md. Instructions: measurements.md "Real-code fan-out 2".
4. **Plan 64 stage 6** (execution): `.d.ts` for JavaScript sources, first session (77 dtsdiff projects).
5. **Plan 64 stage 6** continued, or its leftovers by fan-out; then plan 64 is closed into bugs/dts_emit.md.
6. **Plan 63 stage 2** (execution): per-file parse arenas (its 2a audit may stop it; say why in the plan).
7. **Plan 75** (planning with a prototype): the per-checker policy first (12-core numbers are in; implement if small),
   then the compact AST prototype and its go/no-go.
8. **Plan 76** (planning): watch / incremental / the refused CLI modes, measured against tsgo's incremental rebuild;
   a recommendation and a question for the human.
9. **Perf + real code**: instrcount against this window's start, perfsweep, scaleprobe, mutate.py for missing checks;
   fix what is cheap, file the rest (bugs/perf_leads.md, bugs/missing_checks.md).
10. **Triage.**
11. **Insights.**

After the insights entry (for the next window): the compact AST if plan 75 says go, what the human decides on plan
76, checker balance (bugs/perf_leads.md), webpack's serial declare (the 12-core bench: declare+link 15% there, 5% on vscode), plan 58 + plan 66
stage 5 if not done.
