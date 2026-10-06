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
2. ~~Execution: plan 46 stage 2~~ done 2026-10-06: `./mutate.py` built, seeds 2-5 (32k mutants) drained by five
   sub-agents: seed 2 94 -> 6 mismatches, the fresh seeds' yield now ~1%; flagflip 103 / 68 / 17 -> 81 / 63 / 12 (open
   causes 2, 3, 5 done, 1 judged not worth porting, 4/6/8 partly); suite 13099 -> 13103, sweep 8580 flat, two parser
   crashes fixed. Instructions: plan 46 / measurements.md.
3. ~~Execution: plan 44 stage 3 item 2, `$ExpectType`~~ done 2026-10-06: `./expecttypes.py` built (69k assertions,
   ~30 s); 99.5% -> 99.7% match after six fixes (suite 13103 -> 13104, sweep/corpora/flagflip flat). The ranking for
   entry 4 is in plan 44 "`$ExpectType` ranking": union order (135, chrome 42), then singles.
4. **Execution: fan-out on the `$ExpectType` ranking** plus the plan 35 queue where they overlap. The ranking is thin
   (224 mismatches, mostly creation-order union order): one or two agents on it, the rest on the plan 35 queue /
   displaydiff's kinds; `./expecttypes.py` next to the sweep as a gate.
5. **Planning: plan 47** (speculative checks). It may say no; if yes, entry 9 executes stage 1.
6. **Execution: flagflip's queue and the mutation leftovers** (plan 46 "Open causes" 4, 6, 8; triage.md's seed 3-5
   notes, which a triage session may consolidate first). Both are thin now: start with the nuia sweep run (plan 46
   "Not done"), then a mutation widening (multi-file or JS tests, plan 46 stage 2 "Next use").
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
