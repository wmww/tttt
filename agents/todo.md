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
7. ~~Suite checker bugs 5~~ done: 10312 -> 10477 (+165, 0 lost); four Opus sub-agents on the spurious buckets
   (narrowing +15, relations +16, JS +19, inference +18), the main tree did missing-check families (TS2708/2709/2689,
   TS2497 + reportNonExportedMember, TS2721-2723, TS6200 amalgamation, `--pretty` output, triple-slash
   resolution-mode...). Single-code spurious buckets drained (TS2339 38 -> 10, TS2322 36 -> 5, TS2304 35 -> 7); webpack
   30; instructions +2% on tsc/valibot/zod (measurements.md: a lead for entry 9).
8. ~~Plan 37 C + D~~ done: decorators as calls (+15 with TS2318 for missing lib types, nest 0, +0.4% instructions)
   and evolving arrays (+21: +7 from the port, +14 from making Array at checker start; an Opus sub-agent), 10477 ->
   10513, 0 lost, corpora byte-identical; tsc/webpack +0.3% instructions. Found: webpack gives other diagnostics
   with `--checkers 1` than with 4 (triage.md, already at 8eab3851).
9. ~~Perf: memory and types~~ done: variance measurements report unreliability like tsc (zod with its `in`/`out`
   stripped 7.8 -> 4.65 G and now reports tsc's 26 errors; +3 tests incl. an indexed-access relation fix), the
   `Prop`/`PropDecl` split (arena -20..-31%, zod K=4 RSS 846 -> 675 MB), `./arenasites.sh`. Type counts are at or
   under tsc's: that lead is closed. Plan 36 stage 1 not started (walls unmeasurable that day: machine at load 10).
10. ~~Triage~~ done: 10516 -> 10543 (+27, 0 lost; four Opus sub-agents + the main tree), webpack no longer depends on
   the checker count (30 at K=1 and 4), corpora otherwise unchanged. Found: tsc's leaf-wise contextual instantiation
   is the common root of three type_core.md inference bullets (plan-sized; next to plan 37's fixing without candidates).
11. **Insights.**
