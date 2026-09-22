# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

1. **Execution: plan 07** (`plans/07_underlines.md`, types-baseline underlines). Its stage 1 is a measurement:
   do that first and write the numbers into the plan. Budget: 1-2 sessions. If the reuse rule turns out to need
   more than "annotation source text when `ty_lower(annotation)` is the printed type, plus accessible names", land
   the part that covers the bulk (parameter names + annotations) and write the rest into `hacks.md`; don't chase the
   tail of 1445 into `visitExistingNodeTreeSymbolsWorker`.
2. **Execution: plan 08** (`plans/08_corpus_to_zero.md`), first iteration: the `visitEachChildTable` contextual
   typing bug (810 of the corpus' 852 TS7006, one root cause), then the call buckets (TS2345/TS2769/TS2322).
3. **Execution: plan 08 continued**, 1-2 more sessions. Profile and bench when the count is low (the plan says when).
   Spin off `this` types / the iteration protocol as plan 09 if they are what's left, rather than patching.
4. **Planning: plan 09**, `this` types + iteration protocol / well-known symbols (unique symbols as property
   names), unless plan 08 already wrote it. Both show up in the corpus (TS2495, TS7022, `Map.set` chains) and in
   the suite (`triage.md`).
5. **Triage.** Drain `triage.md`; re-measure (`./failstats.py` now that underlines no longer mask the types
   component: its ranking of type-string bugs is new information), and hand over to insights.
