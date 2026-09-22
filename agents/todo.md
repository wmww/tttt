# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

1. **Execution: plan 10 stage 2** (`plans/10_this_types.md`: contextual `this` in object literal methods and function
   expressions, `this` at top level, TS2683/TS2684, signatures printing their `this` parameter), stage 3 if cheap.
   Stage 1 is done.
2. **Triage.** Drain `triage.md` (plan 08 added items: generic class expression bases, enum member aliases, types
   baseline printing gaps, member accessibility/TS2540, substitution types, the ~25 singles in plan 08's Progress;
   plan 09 added: nested lambdas' contextual signatures in generic calls, late bound index signatures, array literal
   destructuring targets as tuples, expando `f[sym] =`);
   re-measure with `./failstats.py` and `./realworld.py`; turn plan 11's sketch into real plan(s) if the corpus
   is down to literals + higher-order inference; hand over to insights.
