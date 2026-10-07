# Todo
Written by the insights of 2026-10-07 (after the tenth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus), `./sweep.py` and `./flagflip.py` after checker changes, `./instrcount.sh` and a
commit.

**This window retargets tttt from TypeScript 5.8 to 6.0/7.0 (plan 48).** Entry 1 rewrites plan 48 and **may
rewrite entries 2-9** to fit what it measures, keeping the count. Until the switch, keep 5.8 work to what's needed.

1. **Planning: plan 48** (retarget). Answer its questions with prototypes: the oracle (tsc 6.0.3 vs tsgo 7.0.2
   baselines, type ordering in particular), the checker-visible changes with test counts, the staging (prototype the
   switch in a worktree and measure the suite under 6.0), the other oracles' order. Rewrite plan 48 and entries 2-9.
2. **Execution: plan 48 stage 1**, the switch: `thirdparty/tsc` to the new version, option defaults and deprecation
   diagnostics, libs, harness; saved results regenerated; per-test "lost" defined against the old run.
3. **Execution: fan-out on the switch's regressions** (four sub-agents, failstats buckets, disjoint test lists).
4. **Execution: second fan-out**, or plan 48's next stage if the suite is back.
5. **Execution: plan 48 stage 2**, the tools: sweep / `$ExpectType` / flagflip / mutation / scenarios caches and
   `tscbaseline.sh` on the new tsc, DefinitelyTyped at a current commit; drain what's cheap.
6. **Corpora: re-pin** every corpus at a current upstream commit (TypeScript-6-era), drop the 5.9 lib shims, record
   tsc's counts; drain with sub-agents.
7. **Corpora / sweep drain** continued.
8. **Perf session**: new instruction-count baselines on the re-pinned corpora, `./bench.sh` against tsgo 7.0.2 (which
   now accepts every config), look at anything that moved.
9. **Execution**: 5.9/6.0 language features plan 48 listed that the switch didn't need (or plan 48's leftovers).
10. **Triage.**
11. **Insights.**
