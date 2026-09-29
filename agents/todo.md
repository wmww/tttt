# Todo
Written by the insights of 2026-09-28 (after the window of plans 36-37); `vision.md` "Course correction" says
why. **Ten sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the
numbers). A plan that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run
(count the commits since this file was written), the next session is insights, whatever is left here. New work goes
into plans or triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus
numbers (`./realworld.py --corpus X` for every pinned corpus) and a commit.

1. ~~Corpus: pin an inference-heavy corpus~~ done: xstate core (394 -> 61, three sub-agents), webpack 30 -> 17,
   suite 10543 -> 10554. Plan 38 has seven xstate/webpack repros in `plans/38_prototypes/`; plan 19 "xstate" lists
   the rest by cause.
2. ~~Plan 38 planning~~ and 3. ~~Plan 38 stage 1~~ done in one session (the planning session died before writing
   up; the next recovered its worktrees, fixed the last losses and landed it): suite 10554 -> 10565, 0 lost, xstate
   61 -> 43, webpack 17 -> 13, others 0. Counts as two sessions.
4. ~~Plan 38 stage 2~~ done (four sub-agents + main tree): xstate 43 -> 19, webpack 13 -> 9, suite 10565 -> 10567, 0
   lost. What is left of plan 38 is "Stage 3" (resolve once), which needs a planning session: after the insights.
5. ~~Suite checker bugs 6~~ done (four Opus sub-agents + main tree): suite 10567 -> 10830 (+263, 0 lost), JS 560 ->
   579, corpora unchanged (webpack 9, xstate 19, the rest 0). Instructions +0-5% (webpack +5.2%, valibot +3.5%;
   measurements.md). Main tree: tsconfig option diagnostics at the option (the harness and CLI register the config),
   resolveName details, TS2818/TS2686/TS7036/TS2376. Buckets left: `bugs/missing_checks.md`.
6. ~~Perf: plan 36 stage 1~~ done: the load pool (plan 36 "Stage 1"), load phase -35..-47% on every corpus (measured
   on a machine at load 8-10: `./bench.sh` on a quiet one is owed, and the import pre-scan was left for then). Suite
   and corpora unchanged. Size-based checker dealing tried and reverted (mixed; plan 36 "Later"). Not looked at: the
   2-5% instruction creep of the checker bugs sessions and hacks.md's "Generic source constraints push both nesting
   stacks" (+14% on react): the next perf session.
7. ~~JS session~~ done (four Opus sub-agents by bucket: CommonJS, expandos, JSDoc, syntax/other): JS 579 -> 885 /
   1049, suite 10830 -> 11274 (0 lost), webpack 9 -> 6, the rest unchanged. Instructions +0.2-1.6%, nest +4.6%
   (module resolution), webpack +5.1% (checks more): measurements.md, for the next perf session. Leftovers: plan 26
   "Leftovers".
8. **Parser session** (plan 27 stage 1 leftovers and `./syntaxstats.py` clusters): 225 tests have a spurious TS1xxx;
   many spurious TS2304/TS1005/TS1128 are downstream of recovery (constructorWithIncompleteTypeAnnotation alone: 22
   TS2304).
9. **Types component** (plan 35's queue, sub-agents): `./displaydiff.py` "other" alone fixes 333 TS + 54 JS tests,
   underlines/order 113 + 24, we-any 59 + 36.
10. **Triage**, including a pass over hacks.md (811 lines: merge, delete what plans 37/38 made stale) and the two
    log_error tests (`Error added with invalid span`, TS2503/TS2304 on `_`).
11. **Insights.**
