# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

1. **Execution: plan 09** (`plans/09_iteration_protocol.md`), stages 1-2: unique symbols + symbol-keyed members, then
   iteration types and their consumers. Biggest corpus bucket left (~100 of 203). Probes in `test/probes/plan09/`.
2. **Execution: plan 09 stage 3** (generators, async iteration if there's room), or finish stages 1-2 if they ran
   long. Then `./bench.sh` + a callgrind profile of the corpus (plan 08's Validation) if the corpus count is low.
3. **Execution: plan 10** (`plans/10_this_types.md`) stage 1: the `this` type parameter and this-arguments. Biggest
   suite bucket (~242 types baselines). Stage 2 (contextual `this`) in the same or the next session.
4. **Triage.** Drain `triage.md` (plan 08 added items: generic class expression bases, enum member aliases, types
   baseline printing gaps, member accessibility/TS2540, substitution types, the ~25 singles in plan 08's Progress);
   re-measure with `./failstats.py` and `./realworld.py`; turn plan 11's sketch into real plan(s) if the corpus
   is down to literals + higher-order inference; hand over to insights.
