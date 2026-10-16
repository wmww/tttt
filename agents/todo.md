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
4. Done 2026-10-16: plan 64 stage 6, first session. JavaScript declaration emit (ES module JS, JSDoc declarations,
   `this.x` members, class expandos, `@overload`s, CommonJS exports, `require` imports): suite JS dts 0 -> 320 / 346
   (apart), headline 13746 / 13787 (+1 own test), dtsdiff 1116 -> 1166 / 1233 projects (96584 / 96726 files), clidiff
   `--emit` 309 -> 369 / 396, every other oracle unchanged. Left: plan 64's status (JS declaration diagnostics, the
   last 26 JS tests, folding them into the headline).
5. Done 2026-10-16: plan 64 stage 6 finished and plan 64 closed into bugs/dts_emit.md (main + one agent).
   JavaScript declaration diagnostics reported for real (also under --noEmit), the suite's JS dts folded into the
   headline (351 / 352), suite 13756 / 13798; dtsdiff 1166 -> 1193 / 1233 projects (96627 / 96726 files, no missing file left), every other oracle unchanged; instructions flat (callgrind, vscode 95.80 G).
6. Done 2026-10-16: plan 63 stage 2, plan 63 closed (leftovers in bugs/perf_leads.md "Memory"). The AST in per-file
   arenas (`ast_alloc.h`), the glibc huge-page re-exec deleted (it cost vscode 10% today: compaction stalls), arenas
   prefaulted and madvised within a `/proc/buddyinfo` budget. vscode 8.14 -> 5.91 s default, K1 parse 3030 -> 1603 ms,
   peak -52 MB; mid-size corpora 0-6% faster; instructions -1.2..-1.6%; every oracle unchanged.
7. Done 2026-10-16: plan 75, closed. The CLI's default caps the checkers' recycle budget at 1.5 GB together (64 MB
   each at K24: vscode K24 on 4 threads 4.90 -> 3.56 GB for +1% wall; unchanged at K <= 12). The compact AST: no-go
   (~165 MB of vscode's 557 MB of AST zero words is union padding; the Expr 72 -> 40 option is in bugs/perf_leads.md).
8. Done 2026-10-16: plan 76 planned (measured, recommendation and the human's question at its end). tttt's full check
   beats tsgo's incremental rebuild after an export edit almost everywhere, loses after a body edit on 12 of 19
   projects (1.2-2x, up to 7x); tsgo's incremental rebuild runs away on webpack (28 GB). Found on the way: dokploy at
   the default checkers 55 -> 3 s (recycle thrash, `check_share()`), `--listFilesOnly`'s library order.
9. Done 2026-10-16: perf + real code. Four perf fixes (keyof of an object remembered, index atoms, typed member sort,
   intersection dedup): type-fest -38% instructions, every corpus -1..-4% (measurements.md "Perf + real code"), folo's
   worst file 11 -> 0.6 s; scaleprobe all linear, perfsweep median 0.21. Mutation seed 81 (one agent): 16 -> 7, suite
   13764 / 13803 (+8 own tests); leftovers in bugs/missing_checks.md, perf leftovers in bugs/perf_leads.md.
10. Done 2026-10-16: triage. Computed accessor pairs get TS2676/TS2808, late-bound method overloads group by symbol
   (suite 13766 / 13805, +2 own tests; every oracle unchanged, instructions flat). `scripts/perfsweep.py --defaults`
   times tttt at its default checkers: dokploy gains nothing at K4 for 2x memory (bugs/perf_leads.md). One agent pruned
   ~50 fixed entries from bugs/missing_checks.md and bugs/sourcesweep_checker.md (some were tsc 5 behaviour, not tsgo's).
11. **Insights.** report.md asks the human plan 76's question (local loop: none / watch + `-b` buildinfo / LSP).

After the insights entry (for the next window): what the human decides on plan
76, checker balance (bugs/perf_leads.md), webpack's serial declare (the 12-core bench: declare+link 15% there, 5% on vscode), plan 58 + plan 66
stage 5 if not done.
