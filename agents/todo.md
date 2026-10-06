# Todo
Written by the insights of 2026-10-05 (after the ninth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus), `./sweep.py` and `./flagflip.py` after checker changes, `./instrcount.sh` and a
commit.

1. ~~Planning: plan 46 stage 1~~ done 2026-10-06: `./flagflip.py` built (156 cells, 47k tsc diagnostics) and drained
   in the same session by four sub-agents' prototypes: 603 / 1460 / 1029 (missing / spurious / other code) -> 103 / 68
   / 17; suite 13099 flat, sweep 8580, instructions +0.3%. Stage 2 (mutations) prototyped and judged worth a tool.
2. **Execution: plan 46 stage 2, the mutation tool** (`./mutate.py` from `plans/46_prototypes/`, spec in plan 46),
   one ~2000-mutant run root-caused into the plan, then a fan-out (Opus agents in worktrees, disjoint causes, 0 lost at
   merge) on its top causes plus flagflip's open causes 2-3 (union index infos, `typesDefinitelyUnrelated`: both were
   tried and lost a corpus or DT package, read the plan first). Every merge: `./flagflip.py` must not regress.
3. **Execution: plan 44 stage 3 item 2, `$ExpectType`** (tool: tttt prints the types on the asserted lines, compared
   with tsc 5.8's `typeToString`, cached; ranked with displaydiff's kinds). Fix the top kinds if time is left.
4. **Execution: fan-out on the `$ExpectType` ranking** plus the plan 35 queue where they overlap.
5. **Planning: plan 47** (speculative checks). It may say no; if yes, entry 9 executes stage 1.
6. **Execution: drain the mutation ranking and flagflip's queue** (plan 46 "Open causes": TS2589 fidelity is the
   biggest; plan it if it's plan-sized). If both are thin, the nuia sweep run (plan 46 "Not done").
7. **Corpora: pin two new ones** chosen for what the current ones don't exercise: a JSX-heavy application (eg
   excalidraw) and an inference-heavy router/RPC library (eg TanStack Router or tRPC); clean under tsc 5.8, drained to
   0 the same session like effect/vscode (plan 19 has the loop); `./flagflip.py --tsc -c NAME` gives them flag-flip cells.
8. **Perf session**: instruction counts since the start of this window, bisect anything above +1%; then the cheapest of
   plan 45 stage 2 (JS resolve), the sweep's per-package startup (lib loading, plan 44 "Perf side-product") or
   perf_leads.md's top item, by measurement.
9. **Execution: plan 47 stage 1** if planned; otherwise a suite-tail fan-out (missing_checks.md buckets, plan 35 queue,
   plan 19 "effect"'s last 7).
10. **Triage.**
11. **Insights.**
