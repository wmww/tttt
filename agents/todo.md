# Todo
Written by the insights of 2026-09-28 (after the window of plans 32 B-36); `vision.md` "Course correction" says
why. **Ten sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the
numbers). A plan that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run
(count the commits since this file was written), the next session is insights, whatever is left here. New work goes
into plans or triage.md, not ahead of the insights entry. Every entry ends with `./fasttest.sh -s`, the corpus
numbers (`./realworld.py --corpus X` for every pinned corpus) and a commit.

1. ~~Plan 36 stage 2~~ done: `--checkers K`, per-checker ErrorCtx + tsc's sortAndDeduplicate (+2 tests),
   `./checkerdiff.py`; suite and corpora identical to K=1 modulo quoted text (plan 36 stage 2 section).
2. ~~Plan 36 stage 3~~ done: `--threads` (default cores up to 4) runs `--checkers` (now default 4) on threads;
   `./threadcheck.sh` identical, TSan clean on all corpora; tsc corpus 0.66 -> 0.48 s, webpack 0.56 -> 0.40, zod
   0.73 -> 0.45, valibot 0.62 -> 0.38 (2 cores x 2 SMT here); RSS +2..+47% (react worst, zod 846 MB).
3. ~~Plan 37 planning~~ done: six candidates prototyped (all 0 lost), ranked in the plan; prototypes in
   `plans/37_prototypes/`. Null widening +48, union origins +37, decorators +14, evolving arrays +8, missing type +3
   (+ zod-root's TS2412), return-only signatures +1; fixing-without-candidates and lazy reverse mapped: not now.
4. ~~Plan 37 A stage 1 + F~~ done: non-widening null/undefined (+48, `Ty.xflags`) and return-only signatures (+1);
   corpora unchanged, perf flat (zod/valibot within 0.1%).
5. ~~Plan 37 B stage 1~~ done: union/keyof origins (+37, 0 components lost, corpora unchanged); valibot +0.7%,
   zod +0.2% instructions after `dedupe_unions` (the prototype's +3% was `U & U` / `keyof (A | B)` cross products).
   E stage 1 too: the missing type (+3, zod-root 82 -> 83 with tsc's TS2412, zod -0.7% instructions).
6. ~~Corpus session~~ done: webpack 107 -> 32 (12 tsc rules, probes in `test/probes/plan19/webpack/`), zod-root = tsc's
   55 (arktype's DSL: interfaces extending type parameters, const type parameters behind conditionals, `infer X
   extends C` constraints; execa's instantiation expression; an Opus sub-agent). Suite +6. Left: plan 19 "webpack",
   the biggest cause being defaulted type parameters leaking into context-sensitive callbacks (bugs/type_core.md).
7. **Suite checker bugs 5, spurious side**: the single-code buckets are now mostly spurious (TS2339 40, TS2322 39,
   TS2304 35, `bugs/missing_checks.md`); sample, group by cause, parallel Opus sub-agents in worktrees like session 4.
8. **Plan 37 C + D**: decorators checked as calls (+14, nest must stay 0) and evolving arrays (+8), both
   stage 1 from their prototypes (replaces the plan 35 queue here: its head items 1-2 were plan 37 A and C).
9. **Perf: memory and types**: `perf_leads.md` "Unreliable variance measurements" first (zod without its `out`
   annotations costs 2x: is tttt marking more measurements unreliable than tsc?), then "Memory" (the `Prop` split, why
   tttt makes more types than tsc), with the K=4 RSS from entry 2 as the baseline (`measurements.md` Perf: zod 846 MB, react 335 vs 228 at K=1); then plan 36 stage 1 (parallel loading) if the session has room.
10. **Triage.**
11. **Insights.**
