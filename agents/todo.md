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

1. Done (2026-10-08): plan 51 planned from three prototypes, and its stage 1 landed (general ContextChecked + six
   latent fixes, dead widening/return-mapper channels deleted; every oracle unchanged). Leaf-wise instantiation: no port.
2. Done (2026-10-08): plan 52 planned from a 22-repo prototype (655 projects: 344 match tsc, 45 s tttt, 9 min tsc,
   all installed with node 24 + corepack, pruned node_modules leave tsc's output identical). Two bugs fixed on the way
   (no-target default lib, sweep.py's file-less diagnostics).
3. Done (2026-10-08): plan 50, in place rather than clone + redirect (getCommonJsExportEquals merged into the
   export= target at link time; four clone special cases deleted). Suite 14832 (+3), every other oracle flat;
   stages 3-5 left as single-test quirks in bugs/binder_modules.md.
4. Done (2026-10-08): plan 51 stage 2, context anchors (the ember regression was already gone after stage 1). Every
   oracle 0 lost, `$ExpectType` +3; late/sig-context/overload-failure channels deleted; instructions +0.1-0.5%,
   xstate +0.7% (anchor bookkeeping).
5. Done (2026-10-08): plan 52 stage 1. 174 repos / 3359 projects, tsc cached (~1 h), `./sourcesweep.py` (~5 min,
   `--sample 4` in oracles.py): **2241 match (746 clean under tsc), 47 / 174 repos**, 0 crash after fixing 10; nine
   mechanism fixes on the way, every other oracle flat. Most of the missing direction is one config cascade (plan 52).
6. Done (2026-10-08): perf session. Checker sharing isn't a lever (plan 36: 4 checkers cost ~2% more CPU than one;
   the vscode wall is SMT-bound). Found pathologies through the source sweep instead (karakeep 55 s -> 0.6 s,
   headlessui 16 -> 1.2 s, mcp-sdk 5.5x tsc's types -> below tsc's); instructions vscode -1.8%, vue -4.5%, tsc -2.1%. Two open leads
   in bugs/perf_leads.md (docmost's function types keyed by the whole mapper, immutable-js). `TTTT_FILE_TIMES`.
7. **Hacks audit** (fan-out, one sub-agent per hacks.md section group): for every entry, try deleting the hack
   (many predate plans 38, 39, 42, 48); keep deletions that are 0 lost on every oracle, rewrite stale entries,
   mark the permanent ones with their reason. Goal: hacks.md shorter, not longer.
8. **Execution: plan 52 drain** (fan-out on the source sweep's root causes; the references-under-`-p` cascade first,
   plan 52 says why).
9. **Execution: plan 51 stage 3** (widening where the return type is computed, NONE/FIRST_ABSENT through anchors,
   KEY_LITERAL_CONTEXT as a stack).
10. **Triage**: drain triage.md; also consolidate finished plans (36-42, 44, 45, 48: move live leftovers to bugs/ or
    the active plans, delete the rest, including their prototype dirs).
11. **Insights.**
