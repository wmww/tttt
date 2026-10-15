# Leftovers of the tsc 6 era in the corpora
tsgo 7.1 is the oracle since plan 54 (the corpora compare against its own cached diagnostics, `test/corpora.tsc.jsonl`).
What the old tsc 6 oracle forced on the corpora and could now be undone:

- **An Effect 4 corpus** (effect `main`): tsgo-clean where tsc 6 wasn't, heavy (tsgo 8.6 s on its src): a perf corpus
  candidate next to vscode.
- **zod-root**: tsgo 7.0.2 ran away on it (18 GB); the 7.1 cache exists, measurements.md has tttt's count. Which
  package blew 7.0.2 up was never bisected: a perf hazard worth a look if tttt slows down there.

# Suite tests whose expected output depends on tsgo's JS emit (plan 66 bucket 2, 2026-10-15)
tsgo's harness (`harnessutil.CompileFiles`) emits before it collects the diagnostics it baselines, and its emit is the
JS emitter's and the declaration transformer's, file by file. Their resolver queries (a property access's constant
value, alias references for import elision, the types the declaration transformer prints) resolve types before the
checker walks the files, which moves where a circularity is first found. Tried 2026-10-15 (todo entry 8): running
tttt's declaration walk on every file before checking any (`program_set_emit_first()`, a harness-only switch, not
kept) fixed none of them and broke mappedTypeWithAsClauseAndLateBoundProperty2. Argued out of reach unless JS emit
ever exists (the human: out of scope, vision.md):
- incorrectRecursiveMappedTypeConstraint, typeParameterWithInvalidConstraintType (no `@declaration`): TS2313's related
  TS2751 location. Evidence: tsgo's CLI (`built/tsc --noEmit`, check order) prints exactly tttt's diagnostics for both;
  only the harness's emit-first run differs.
Not in this class after all (tsgo's CLI differs from tttt too: checker or parser bugs, plan 66's second fan-out):
recursiveMappedTypes (TS2589/TS2502/TS2615 placement).
circularBaseTypes (a missed TS2313 for `X<T>`'s `K`), recursiveMappedTypes (TS2589/TS2502/TS2615 placement),
manyCompilerErrorsInTheTwoFiles (two missing TS1134 parse errors).

# Suite tests whose expected output is a tsgo instantiation-cache collision (plan 66 second fan-out, 2026-10-15)
- genericCallInferenceUsingThisTypeNoInvalidCacheReuseAfterMappedTypeApplication1: tsgo's `outer.log(100)` is
  `Effect<100, never, never>` (the test's own comment, from Strada, says `Effect<100, never, XXX>`, which tttt prints).
  Evidence: delete the `export const a = inner.log(100);` line and tsgo prints `Effect<100, never, XXX>` for `b` too;
  an instrumented tsgo (getObjectTypeInstantiation) shows the `log` function type is a SingleSignatureType (the
  higher-order call result) whose instantiation for `outer` (this -> `XXX & { _type: XXX }`) hits the cache entry made
  for `inner` (this -> XXX): getTypeInstantiationKey keys it by the outer type parameters' arguments plus `!`, not by
  the mapper `m` it then combines in (`newMapper = combineTypeMappers(newMapper, m)`), so the second `this` reuses the
  first's result. Reproducing it means a deliberately colliding cache key (and a SingleSignatureType notion tttt
  doesn't have) whose result depends on which `this` asked first: the bug the test was written against, regressed in
  tsgo. Not reproduced; re-check when the oracle is re-pinned (plan 58).
- recursiveMappedTypes (2026-10-15, agent B): the baseline's TS2615 at (79,1) (`x.type`) is found by the JS emit's
  import elision: an instrumented tsgo's stack at that error is emitJSFile -> ImportElisionTransformer ->
  EmitResolver.MarkLinkedReferencesRecursively -> markPropertyAliasReferenced -> checkExpressionCached(x) ->
  ListChild -> Child<ListWidget> -> getTypeOfMappedSymbol, before any checking; tsgo's CLI reports it at (74,13) (the
  `Child<ListWidget>` of `"each"`, checkTypeReferenceNode during the interface's check) together with the TS2502 on
  `"each"`. tttt's TS2615 lands on (79,1) only because it misses the cycle at the interface (below), so fixing that
  moves it to the CLI's place. The CLI-order bugs left are filed in missing_checks.md ("Circularity").
