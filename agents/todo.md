# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

1. **Execution: plan 08 continued** (`plans/08_corpus_to_zero.md`, "Progress" lists what's left of the 365). The
   small unexplored buckets first (TS2769 generic signature relations, `Mutable<T>`, TS2370, the narrowing
   leftovers), one root cause per commit. Stop when what's left is covered by the plans below.
2. **Planning: plan 09**, the iteration protocol + well-known/unique symbols (`[Symbol.iterator]` members,
   `arrayFrom(set)`, for-of and destructuring of iterables), and `this` types if they fit. It's most of the
   corpus' `unknown`s. Also sketch (or write) plan 10: fresh/regular literal types and higher-order generic function
   inference (TS2558 38), both named in plan 08's Progress.
3. **Execution: plan 09.** Then bench + callgrind profile of the corpus (plan 08's Validation) if the count is low.
4. **Triage.** Drain `triage.md` (plan 08 added items: generic class expression bases, enum member aliases, types
   baseline printing gaps); re-measure with `./failstats.py`, and hand over to insights.
