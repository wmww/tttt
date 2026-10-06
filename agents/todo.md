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
4. ~~Execution: fan-out on the `$ExpectType` ranking + plan 35 queue~~ done 2026-10-06: five sub-agents (A union
   order, B singles/sweep then plan 35 item 10, C types semantics then the errors tail, D display then symbols, E
   symbols), ~150 picks, 0 lost: suite 13104 -> 13227, `$ExpectType` 68416 -> 68610 (the oracle now checks before
   reading types), sweep 8580 -> 8584, flagflip 81 / 63 -> 79 / 63. New `./expectprobe.py` (one cut-down DT project,
   reducer). Instructions: measurements.md.
5. ~~Planning: plan 47 (speculative checks)~~ done 2026-10-06: **no mechanism needed**, executed in the same session.
   Lifting `first_unfit_skipped_literal`'s restrictions (generic overloads, parameters with type variables, single
   candidates) left nothing harmful behind once it did tsc's two other skip-round rules (object literals regularised:
   no excess properties; nested generic calls skipped, else they're resolved once under the trial type: effect +6).
   Suite 13227 -> 13228 (1 types component lost to a latent alias-argument bug, bugs/display.md "Aliases"), sweep
   8584 -> 8588, flagflip missing 78 -> 65, corpora / `$ExpectType` flat; instructions vue +1.8%, xstate +1.2%.
6. ~~Execution: flagflip's queue and the mutation leftovers~~ done 2026-10-06: `./sweep.py --flags nuia|eopt` built,
   nuia 8585 -> 8588, eopt 8588 at once (both now equal the default sweep but react-relay); `./mutate.py --tests
   multi,js` built (~7% yield, 3x single-file's), drained by three sub-agents: seeds 1/2 92 / 177 -> 12 / 40, fresh seed 3
   2.0%; one hang fixed. Suite 13228 -> 13229 (+1 errors component), flagflip 65 / 63 / 12 flat. Instructions:
   measurements.md.
7. ~~Corpora: pin two new ones~~ done 2026-10-06: **excalidraw** (JSX app, 149k lines, TS 5.9 lib shim) 9 -> 0 and
   **tanstack-router** (react-router + type tests against router-core's built .d.ts, new postinstall hook) 4 -> 0, by
   two sub-agents; their flag-flip cells added (26, 5757 tsc diagnostics). Suite 13229 -> 13232, sweep/`$ExpectType`
   flat, instructions flat (a +25% drizzle mid-session caught and reverted: triage "Distributed conditionals").
8. ~~Perf session~~ done 2026-10-06: no commit of the window above +0.6%; the biggest steps (conditional instances
   named by their root's alias) led to tsc's way of computing their alias arguments, and relation cache sizes
   compared with tsc's to three missing tsc shortcuts (`tttt --time` prints them now): drizzle 13.09 -> 10.80 G
   (-17.5%), vue 8.07 -> 6.98 (-13.5%), react -3.6%, xstate -2.8%, zod +2.5% (variances now in tsc's order). The
   distributed members' root alias is back (+1% drizzle instead of +25%). Plan 45 stage 2 by a sub-agent: webpack
   resolve 63 -> 37 ms. bugs/display.md's alias-argument bug fixed at no cost now (tsc's rule, +1 types
   component). Suite/corpora/sweep/`$ExpectType`/flagflip flat. Instructions: measurements.md.
9. **Execution: suite-tail fan-out** (missing_checks.md buckets, plan 35 queue, plan 19 "effect"'s last 7).
10. **Triage.**
11. **Insights.**
