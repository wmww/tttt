# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

1. **Plan 11, session 1** (`plans/11_higher_order_inference.md`): stages 0-2 (context reaching lambda bodies,
   `instantiate_signature_in_context_of` in relations, generic arguments instantiated in context). Bench before/after
   stage 1.
2. **Plan 11, session 2**: stage 3 (higher-order propagation: corpus TS2558 38 -> 0), optional stage 4.
3. **Plan 12, session 1** (`plans/12_fresh_literals.md`): stages 0-2 (fresh twins, fresh-only widening; semver.ts
   -> 0). Stage 2 is one indivisible flip: expect a chase through the suite.
4. **Plan 12, session 2**: stage 3 (per-site rules), stage 4 (object literal freshness + excess property checks).
5. **Plan 08, last session**: the ~34 corpus singles listed in its "Progress", then `./bench.sh` + a callgrind profile
   of the corpus with (close to) zero diagnostics: the first honest perf comparison. Delete plan 08 if the corpus
   is at zero or what's left is written up.
6. **Insights.** Corpus done (or nearly): decide what's next among the ready suite plans 13 (member modifiers,
   ~2 sessions), 14 (syntax gaps, `using` ~330 tests), 15 (checks that don't exist, a loop), and the unplanned
   features (JSX, JS/JSDoc ~900 tests, decorators), and whether perf work comes first.
