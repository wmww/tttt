# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

1. **Planning: plan 09**, the iteration protocol + well-known/unique symbols (`[Symbol.iterator]` members,
   `arrayFrom(set)`, for-of and destructuring of iterables), and `this` types (measured: ~242 types baselines fail
   only on `this`, TS7022 13 in the corpus; give them their own plan if they don't fit in 09). It's most of the
   corpus' `unknown`s. Also sketch (or write) plan 10: fresh/regular literal types and higher-order generic function
   inference (TS2558 38), both named in plan 08's Progress.
2. **Execution: plan 09.** Then bench + callgrind profile of the corpus (plan 08's Validation) if the count is low.
3. **Triage.** Drain `triage.md` (plan 08 added items: generic class expression bases, enum member aliases, types
   baseline printing gaps, member accessibility/TS2540, substitution types, the ~25 singles in plan 08's Progress); re-measure with `./failstats.py`, and hand over to insights.
