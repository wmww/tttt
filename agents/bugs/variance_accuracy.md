# Measured variances that differ from tsgo's

`scripts/variancetrace.py PROJECT` (2026-10-12) builds an instrumented tsgo (patched relater.go through
`go build -overlay`, into /tmp/tsgo-vartrace) and compares every stored variance measurement with tttt's
(`TTTT_DEBUG_VARIANCE=1`), both single-threaded; `--block NAME` extracts NAME's measurement (every structural
comparison, property, cache and maybe-stack hit inside it) from both logs for a diff.

Fixed with it (2026-10-12): feathers/packages/knex (62 of 62 agree, was 27 of 62; types 18287 -> 4832, tsgo 7324).
Three tsgo behaviours tttt lacked, all in relate.c:
- `generic_relation_key()`: tsgo's getRelationKey writes unconstrained type parameters of references with generic
  arguments by position, so `WhereIn<sub-T, TResult>` with QueryBuilder's TResult shares the cache entry WhereIn's
  own measurement made with its own TResult (and the maybe stack, plus the ignoreConstraints "broadest" key check). [fixed 2026-10-12]
- `check_related()`'s top-level cache lookup for two object types returns the cached answer without its
  unreliable/unmeasurable marks (tsgo's isTypeRelatedTo, unlike recursiveTypeRelatedTo). Without it QueryBuilder's
  TRecord came out `in out (unreliable)`, and every failed QueryBuilder comparison fell back to structure. [fixed 2026-10-12]
- A reference whose variances are in flight relates as Unknown, not Maybe: a Maybe at depth 0 is cached as success,
  which the generic key then hands to the measurement itself (vue's DirectiveBinding went independent). [fixed 2026-10-12]

Left (vue's and type-fest's sets were the same before the fix; xstate and effect weren't compared before it):
- xstate/packages/core: 137 of 162 agree. Many of the 25 are tsgo `independent` (4) where tttt measures
  `contra|unreliable` (18) or `co|unreliable` (17) for TContext/TEvent of action/delay aliases (DelayExpr
  `4 4 2 4` vs `18 2 2 17`, InitialTransitionConfig, RaiseActionOptions, DelayedTransitions...). Guess, not checked: tsgo
  got Unknown (another measurement in flight) where tttt compared. Start with `--block DelayExpr`. [open]
- effect/packages/effect: 447 of 459 (Fixture, FixtureFn, Fixtures, Class, ExtendedAPI, Unify, brand, RequiredKeys). [open]
- vue: 138 of 145 (CreateComponentPublicInstanceWithMixins, DefineComponent, DefineProps, ExtractPropTypes, Mock,
  Server, ToResolvedProps). [open]
- type-fest: 53 of 55 (`_ReadonlyObjectDeep`/`_WritableObjectDeep`: tsgo `out (unreliable)` 17, tttt 16). [open]
None of these shows up as a diagnostic difference on the corpora today; they matter for types counts (the knex case
was a 4x) and for which comparisons fall back to structure.
