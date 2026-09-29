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
8. ~~Parser session~~ done (four Opus sub-agents by syntaxstats cluster): parser-divergent tests 197 -> 34, spurious
   TS1xxx 196 -> 47 tests, suite 11274 -> 11472 (0 lost), corpora unchanged. Instructions +1-2.5% (nest), partly
   clawed back; measurements.md for the perf session. Plan 27 is essentially done: its "Leftovers" are per-test items
   for triage/checker sessions, no further parser session.
9. ~~Types component~~ done (four Opus sub-agents by first differing line kind): suite 11472 -> 11962 (0 lost), types
   component failures 1368 -> 807, JS 885 -> 892, corpora unchanged, instructions +0.3-1.2%. Plan 35 "Queue (after
   stage 3)" has the leftovers; no second types session is needed before the insights.
10. ~~Triage~~ done (three Opus sub-agents + a hacks.md one): suite 11962 -> 12040 (0 lost), JS 892 -> 898,
    log_error output empty, hacks.md 985 -> 523 lines, corpora unchanged (webpack 6, xstate 19, the rest 0),
    instructions +0.6% on tsc's compiler after gating two regressions. New: plan 39 (resolution stack, a planning
    stub); triage.md "For the insights session" lists the plan-sized hacks.
11. **Insights.**
