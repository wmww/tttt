# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

1. **Execution: plan 10 stage 3** (`plans/10_this_types.md`: class/interface `extends`/`implements` checks with
   this-arguments, `this` in static members, TS2331/TS2332/TS2526), then delete the plan (stages 1-2 are done; move
   leftovers to hacks.md/triage.md).
2. **Triage.** Drain `triage.md` (plan 08 added items: generic class expression bases, enum member aliases, types
   baseline printing gaps, member accessibility/TS2540, substitution types, the ~25 singles in plan 08's Progress;
   plan 09 added: nested lambdas' contextual signatures in generic calls, late bound index signatures, array literal
   destructuring targets as tuples, expando `f[sym] =`);
   re-measure with `./failstats.py` and `./realworld.py`; turn plan 11's sketch into real plan(s) if the corpus
   is down to literals + higher-order inference; hand over to insights.
