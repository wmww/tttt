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
5. Done: VS Code's `src` pinned (1.39M lines, tsc-clean; tttt 2.5 s / 2.1 GB vs tsgo 6.75 s / 3.7 GB, in bench.sh),
   129 -> 0 the same session with four sub-agents, who then took effect 61 -> 31, xstate 7 -> 2, drizzle 4 -> 0,
   webpack 6 -> 0 and the sweep 7971 -> 8016 match (react-relay crash fixed). Suite 12768, 0 lost; callgrind within
   +1.2% (measurements.md "VS Code corpus pinned").
6. **Fan-out: the errors tail** (four Opus sub-agents, disjoint failstats buckets: the 66 "tttt reports nothing"
   tests, the 117 missing+spurious ones, the spurious TS1xxx/syntaxstats, and missing_checks.md). 0 lost at merge.
7. **Planning + execution: parallel name resolution** (plan 36's next step, and plan 41's `-b` lever). Write plan 45
   from what the binder still shares (hacks.md "Parallelism contract audit"), then execute stage 1 if it fits. Threads
   1/4 must stay byte-identical (`./threadcheck.sh`, `./checkerdiff.py`).
8. **Corpus: the sweep and effect toward 0** (entry 5's agents already took every other corpus to 0 but xstate 2).
   The sweep's ranking (`./sweep.py`, plan 44 "Open root causes": the circular/unresolved `export =` TS2497/TS2305 in
   ~65 webpack-plugin packages first, carbon-components-react's 25 s), effect's 31 (plan 19 "effect": the overload
   reset typing callbacks before a generic-call argument (`tap(acquireRelease(...))`), blocking the `dual<A, B>()`
   predicate gate), xstate's 2 (plan 19). Sweep and corpus root causes go ahead of suite plans.
9. **Fan-out: types/symbols tail** (plan 35's queue, `./displaydiff.py --fine`, bugs/display.md), four sub-agents.
10. **Triage.** Also: the deferred checks tsc does that tttt doesn't (triage.md), hacks.md pruning (725 lines: merge
   or delete entries whose code is gone).
11. **Insights.**
