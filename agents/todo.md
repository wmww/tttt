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
2. **Plan 38 planning** (`plans/38_inference_flow.md`, a brief): prototype tsc's inference flow in a worktree
   (anyFunctionType first pass, leaf-wise instantiateContextualType, lazy fixing), measure against the suite, webpack,
   zod and entry 1's corpus, write the stages. If the memoization question has no good answer, say so and stop:
   that is a valid outcome.
3. **Plan 38 stage 1** (execution).
4. **Plan 38 stage 2** (execution; or the next plan 38 stage, or suite checker bugs 6 if planning found one stage is
   enough).
5. **Suite checker bugs 6** (four Opus sub-agents in worktrees + the main tree, like entry 7 of the last window, +165):
   buckets from `./failstats.py`: the single-code tests (missing TS2322 12, TS2742 10, TS2538 8, TS7053 8; spurious
   TS2339 10), `./unimplemented_codes.sh`'s families (TS2774/TS2845 condition checks, TS17012, TS1147, TS2564...),
   the 365 tests where tsc reports errors and tttt nothing.
6. **Perf: plan 36 stage 1** (parallel loading; `36_prototype.patch`). Load+parse is 117 ms of the tsc corpus's
   0.53 s, 145 ms of webpack's. Also look at checker balance: on the tsc corpus one checker takes 292 ms and the
   others 90-166 (round-robin dealing; `checker.ts` is one file), so the check phase is bounded by one file. Update
   measurements.md (instructions per corpus, walls, RSS).
7. **JS session** (plan 26, sub-agents): `./jsstats.py`: 560 / 1049 pass; only-CommonJS 65, only-JSDoc 60, other 54,
   only-expandos 37, expandos+JSDoc 34.
8. **Parser session** (plan 27 stage 1 leftovers and `./syntaxstats.py` clusters): 252 tests have a spurious TS1xxx;
   many spurious TS2304/TS1005/TS1128 are downstream of recovery (constructorWithIncompleteTypeAnnotation alone: 22
   TS2304).
9. **Types component** (plan 35's queue, sub-agents): `./displaydiff.py` "other" alone fixes 333 TS + 54 JS tests,
   underlines/order 113 + 24, we-any 59 + 36.
10. **Triage**, including a pass over hacks.md (811 lines: merge, delete what plans 37/38 made stale) and the two
    log_error tests (`Error added with invalid span`, TS2503/TS2304 on `_`).
11. **Insights.**
