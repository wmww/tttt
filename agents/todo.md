# Todo
Written by the insights of 2026-10-05 (after the eighth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus), `./instrcount.sh` and a commit.

1. Done: plan 42 stages 3 and 4 (suite 12753 -> 12758, 0 lost, every corpus byte-identical, callgrind within
   +0.3%). Plan 42 is finished; the resolve_call HACK is gone.
2. Done: Effect v3.22.2 pinned (core package src + test + dtslint, 299k lines, tsc-clean), 6300 -> 63 the same
   session (suite 12759, 0 lost; xstate +3.9% instructions, see measurements.md). The 63 by cause in plan 19 "effect".
3. Done: plan 44 stage 1, the breadth sweep (`./sweep.py`: all 8208 DefinitelyTyped packages in 2 minutes against
   tsc 5.8's cached diagnostics). 7969 match exactly, 1 crash, 2 packages slower than tsc; a symlink resolution bug
   fixed on the way (drizzle 6 -> 4). Root causes with repros in plan 44 "Open root causes".
4. Done: plan 43 (AST and binder memory) in one session with four sub-agents: K1 peak RSS nest 243 -> 142 MB,
   webpack 394 -> 253 (targets 200/300), every corpus -20 to -42% and below tsgo; instructions lower everywhere (nest
   -3.4%); outputs byte-identical. Leftover levers in perf_leads.md "Memory"; `./memtrack.sh` is the tool.
5. **Corpus: pin a large application for scale** (a real app of several hundred thousand lines or more, e.g. VS
   Code's `src` if its types install reproducibly, or a big monorepo app). It's the perf showcase against tsgo and
   finds a different kind of false error than the libraries. Add it to bench.sh. Same treatment as entry 2.
6. **Fan-out: the errors tail** (four Opus sub-agents, disjoint failstats buckets: the 66 "tttt reports nothing"
   tests, the 117 missing+spurious ones, the spurious TS1xxx/syntaxstats, and missing_checks.md). 0 lost at merge.
7. **Planning + execution: parallel name resolution** (plan 36's next step, and plan 41's `-b` lever). Write plan 45
   from what the binder still shares (hacks.md "Parallelism contract audit"), then execute stage 1 if it fits. Threads
   1/4 must stay byte-identical (`./threadcheck.sh`, `./checkerdiff.py`).
8. **Corpus: whatever entries 2, 3 and 5 ranked first** (the sweep's: plan 44 "Open root causes", react-relay's crash
   and carbon's 25 s first) (effect's top: return-type unions leaking a signature's own
   type parameters, nested generic calls without contextual inference, plan 19) (sweep and corpus root causes, ahead of suite plans), toward 0.
9. **Fan-out: types/symbols tail** (plan 35's queue, `./displaydiff.py --fine`, bugs/display.md), four sub-agents.
10. **Triage.** Also: the deferred checks tsc does that tttt doesn't (triage.md), hacks.md pruning (725 lines: merge
   or delete entries whose code is gone).
11. **Insights.**
