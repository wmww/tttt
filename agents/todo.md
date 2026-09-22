# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

1. **Execution: plan 08** (`plans/08_corpus_to_zero.md`), first iteration: the `visitEachChildTable` contextual
   typing bug (810 of the corpus' 852 TS7006, one root cause), then the call buckets (TS2345/TS2769/TS2322).
2. **Execution: plan 08 continued**, 1-2 more sessions. Profile and bench when the count is low (the plan says when).
   Spin off `this` types / the iteration protocol as plan 09 if they are what's left, rather than patching.
3. **Planning: plan 09**, `this` types + iteration protocol / well-known symbols (unique symbols as property
   names), unless plan 08 already wrote it. Both show up in the corpus (TS2495, TS7022, `Map.set` chains) and in
   the suite (`triage.md`).
4. **Triage.** Drain `triage.md`; re-measure (`./failstats.py`: since plan 07 underlines no longer mask the types
   component, so its ranking of type-string bugs is new information), and hand over to insights.
