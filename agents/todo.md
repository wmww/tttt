# Todo
Written by the insights of 2026-10-07 (after the eleventh window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus numbers
(`./realworld.py --corpus X` for every pinned corpus), `./sweep.py`, `./expecttypes.py` and `./flagflip.py` after
checker changes, `./instrcount.sh` and a commit.

Starting point: suite **14829 / 14969**, every corpus 0 but effect 3, sweep 8594 / 8595, `$ExpectType` 68628 / 68640,
flagflip 18 / 10 / 4. **No per-test suite drains this window** (vision.md): a fix that needs a new special case goes
to bugs/, not into the code.

1. **Planning: plan 51** (contextual-check state). Prototype deleting the KEY_CONTEXT_* side channels in favour of
   tsc's ContextChecked / leaf-wise contextual instantiation / resolved-signature lookup; write the stages for
   entries 4 and 9, or say no with numbers.
2. **Planning: plan 52** (source sweep). Prototype on ~20 real repositories (selection, dependency install, tsc
   cache, runtime), write the plan for entries 5 and 8.
3. **Execution: plan 50** (tsc's export= / CommonJS symbol clones). The sweep is the real oracle here.
4. **Execution: plan 51 stage 1.**
5. **Execution: plan 52 stage 1**: build `./sourcesweep.py`, cache tsc, first numbers; fix crashes.
6. **Perf session**: vscode (tttt's weakest ratio, 3.0x tsgo). The check phase (four checkers redo ~24% of the type
   work: measure sharing vs file-to-checker affinity vs load balance, plan 36's open question) and AST bytes per line
   (~600). Pick by measurement; write plan 53 if it's bigger than a session.
7. **Hacks audit** (fan-out, one sub-agent per hacks.md section group): for every entry, try deleting the hack
   (many predate plans 38, 39, 42, 48); keep deletions that are 0 lost on every oracle, rewrite stale entries,
   mark the permanent ones with their reason. Goal: hacks.md shorter, not longer.
8. **Execution: plan 52 drain** (fan-out on the source sweep's root causes).
9. **Execution: plan 51 stage 2** (or whatever plan 51 says).
10. **Triage**: drain triage.md; also consolidate finished plans (36-42, 44, 45, 48: move live leftovers to bugs/ or
    the active plans, delete the rest, including their prototype dirs).
11. **Insights.**
