# Todo
Written by the insights of 2026-10-05 (after the eighth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus), `./instrcount.sh` and a commit.

1. **Execution: plan 42 stage 3** (mapped info read through expansion / calls re-entered in flight). Delete the
   hacks it makes unneeded. At the end, decide stage 4 (the barrier): do it in the same session if it's small, otherwise write
   down why it waits.
2. **Corpus: pin Effect** (the `effect` core package of Effect-TS, with its type tests if it has them: HKT encodings,
   generators as do-notation, `pipe` chains with dozens of overloads, variance annotations, branded types). It must be
   clean under tsc 5.8 at a pinned commit (pick the last commit before any move to TS 6/7, like webpack/xstate). If
   Effect doesn't fit, pick another library that's heavy on generics and that none of the current corpora
   resembles, and say why. Rank the false errors by root cause, fix the top ones, and leave the rest in plan 19 with
   numbers.
3. **Planning: plan 44 (breadth sweep)**. Prototype the tool and the first sample, then write the real plan. If the
   tool is done in the session, record the first numbers and file the top root causes.
4. **Execution: plan 43** (AST and binder memory; nest <= 200 MB, webpack <= 300 MB at one checker). As many stages
   as fit in one session, and the rest stays in the plan.
5. **Corpus: pin a large application for scale** (a real app of several hundred thousand lines or more, e.g. VS
   Code's `src` if its types install reproducibly, or a big monorepo app). It's the perf showcase against tsgo and
   finds a different kind of false error than the libraries. Add it to bench.sh. Same treatment as entry 2.
6. **Fan-out: the errors tail** (four Opus sub-agents, disjoint failstats buckets: the 66 "tttt reports nothing"
   tests, the 117 missing+spurious ones, the spurious TS1xxx/syntaxstats, and missing_checks.md). 0 lost at merge.
7. **Planning + execution: parallel name resolution** (plan 36's next step, and plan 41's `-b` lever). Write plan 45
   from what the binder still shares (hacks.md "Parallelism contract audit"), then execute stage 1 if it fits. Threads
   1/4 must stay byte-identical (`./threadcheck.sh`, `./checkerdiff.py`).
8. **Corpus: whatever entries 2, 3 and 5 ranked first** (sweep and corpus root causes, ahead of suite plans), toward 0.
9. **Fan-out: types/symbols tail** (plan 35's queue, `./displaydiff.py --fine`, bugs/display.md), four sub-agents.
10. **Triage.** Also: the deferred checks tsc does that tttt doesn't (triage.md), hacks.md pruning (725 lines: merge
   or delete entries whose code is gone).
11. **Insights.**
