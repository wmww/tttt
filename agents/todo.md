# Todo
Written by the insights of 2026-10-05 (after the ninth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus), `./sweep.py` after checker changes, `./instrcount.sh` and a
commit.

1. **Planning: plan 46 stage 1** (flag flips). Build the tool with tsc's output cached, measure the flag matrix on
   every corpus (non-strict modes included), rank the differences by root cause, write the ranking into the plan. Fix
   the cheapest causes if time is left (the missing TS2412/TS2375 of exactOptionalPropertyTypes is a candidate).
2. **Execution: drain plan 46's ranking** (fan-out: four Opus agents in worktrees, disjoint root causes, 0 lost at
   merge, the sweep and corpora unchanged at default flags).
3. **Execution: plan 44 stage 3 item 2, `$ExpectType`** (tool: tttt prints the types on the asserted lines, compared
   with tsc 5.8's `typeToString`, cached; ranked with displaydiff's kinds). Fix the top kinds if time is left.
4. **Execution: fan-out on the `$ExpectType` ranking** plus the plan 35 queue where they overlap.
5. **Planning: plan 47** (speculative checks). It may say no; if yes, entry 9 executes stage 1.
6. **Planning + execution: plan 46 stage 2** (mutation testing) if entry 1 judged it worth a tool: prototype ~100
   mutations, build it if the yield is there, drain the first causes. Otherwise a second flag-flip drain.
7. **Corpora: pin two new ones** chosen for what the current ones don't exercise: a JSX-heavy application (eg
   excalidraw) and an inference-heavy router/RPC library (eg TanStack Router or tRPC); clean under tsc 5.8, drained to
   0 the same session like effect/vscode (plan 19 has the loop).
8. **Perf session**: instruction counts since the start of this window, bisect anything above +1%; then the cheapest of
   plan 45 stage 2 (JS resolve), the sweep's per-package startup (lib loading, plan 44 "Perf side-product") or
   perf_leads.md's top item, by measurement.
9. **Execution: plan 47 stage 1** if planned; otherwise a suite-tail fan-out (missing_checks.md buckets, plan 35 queue,
   plan 19 "effect"'s last 7).
10. **Triage.**
11. **Insights.**
