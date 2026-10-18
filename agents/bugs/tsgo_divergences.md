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
- mutuallyRecursiveInference: the eager-base runaway's TS5114 (2026-10-15). tsgo's CLI reports it at `class X` (9,7, the
  class check's typeWithThis-to-base relation), with or without emit, and so does tttt; the suite baseline has it at
  `this.a` (12,9), which the harness's JS emit resolves first (a property access's constant value). Same for tttt's own
  ttttEagerBaseRunaway* tests, which needed `@noEmit` to agree with the CLI.
- numericExportNameDeclaration (types only, 2026-10-15): `module.exports[1]` is `error` in the suite's types baseline,
  `any` in tttt. Not emit order but the same kind of artifact: the declaration transformer (which tsgo runs for
  declaration diagnostics too: `@noEmit` with `@declaration` gives `error` as well) resolves export "1"'s type first,
  through `Object.defineProperty(exports, 1, {})` (an empty descriptor: errorType, emitted as `declare const _exported:
  any`), and the types writer later reads that cached type for the element access. Without `@declaration` tsgo's
  types baseline says `any` (probes n5-n9: dropping the defineProperty line gives `2` with declarations, dropping
  `@declaration` gives `any`), so the printed type depends only on which entry point resolved the symbol first.
  Nothing a user sees differs: tsgo's CLI reports no diagnostics either way, like tttt, and the types aren't printed.
  **Accepted** (insights 2026-10-15): revisit only if plan 64 stage 6 (JS `.d.ts`) makes tttt's declaration walk
  resolve it first anyway.
Not in this class after all (tsgo's CLI differs from tttt too): circularBaseTypes and manyCompilerErrorsInTheTwoFiles
(fixed 2026-10-15); recursiveMappedTypes is partly (below), its CLI-visible part is in missing_checks.md.

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

# Suite tests whose expected output is a tsgo type-identity artifact (plan 71 item 1, 2026-10-15)
Hacks that reproduced one test's tsgo artifact (union order by creation, a type's identity) were removed when the
oracles (corpora, DT sweep, source sweep, `$ExpectType`, flagflip) didn't move; the tests they were written for are
argued here instead. Nothing a user sees differs in any of them.
- noImplicitAnyUnionNormalizedObjectLiteral1 (types only): `bar` prints `{ c: string; p: string; s: string; } | { c?:
  undefined; p: any; s: any; }` where tsgo prints the other order. A union's members are ordered by type id, and
  under strictNullChecks false tsgo's getWidenedTypeWithContext remakes every object literal member of a widening
  union (getWidenedTypeOfObjectLiteral makes a new type even when nothing in it widens), so the order is that of
  their re-creation. tttt's structural intern hands the unchanged member back (its id stays older than the widened
  one's). Reproducing it took a flag that made a structurally identical copy of each member (`OBJECT_WIDENED_COPY`,
  removed 2026-10-15): an extra type per member for an order only the types baseline shows.
- declarationEmitAugmentationUsesCorrectSourceFile (symbols and types only; its errors and `.d.ts` match): the export
  specifier of `declare namespace knex { export { Knex } }`, whose target namespace `Knex` another file augments, is
  `Symbol(knex.Knex)` and `any` to tsgo, `Symbol(Knex)` and `typeof Knex` to tttt. tsgo's mergeSymbol resolves the
  alias before merging (resolveSymbol caches the unmerged namespace, with no value meaning, as its aliasTarget), then
  merges into a clone of the target that replaces the alias in knex's exports: the alias is left in no table (named
  through its parent) and its type is errorType. Only the specifier's own symbol shows it: `knex.Knex` as a name or
  value reaches the merged clone in both. Reproduced by `SymbolRare.merge_detached` (b3ad2000, removed 2026-10-15),
  which marked such aliases in the binder and special-cased them in getTypeOfAlias and the symbol chain.

# Outputs that depend on tsgo's own checker count (triage 2026-10-18)
- **TS5115 / TS2589** (instantiation depth): each checker reports the first place it hits the limit, then caches the
  error type (`instantiateTypeWithAlias`), so the count depends on how many checkers see the circular type. tsgo on
  xstate's core with the `nosnc` flags: 1 at `--singleThreaded`, 3 at `--checkers 4` and 8. tttt does the same per
  checker, and a recycled checker counts as a fresh one (flagflip's xstate sloppy/nosnc cells: 1 at K1, 2-3 at the
  defaults, 5 at `--checker-memory 0`; since work stealing, which checker gets there first depends on timing, so the
  flagflip spurious count flickers 8 / 9 between identical runs). The oracles run tsgo single-threaded and tttt's
  kdiff/recycle oracles use default flags, which never hit the limit. Accepted: a program-wide "reported" set would
  still depend on which checker gets there first. Read flagflip's TS5115/TS2589 deltas as noise.
- sequelize/packages/core's TS7022 differs with the checker count in tsgo too (kdiff, plan 62).
- rxjs/packages/rxjs's TS2322 elaborations (2026-10-18, since b3c2fbfa normalized deferred references in the
  relation): tsgo `--singleThreaded` (the cached oracle) says "types returned by '[bufferTime](...)[bufferTime](...)
  [groupBy]'" with `number[][]` below, tsgo `--checkers 16` "'[bufferTime](...)[groupBy]'" with `number[]`: which
  nested pairs a checker related before decides where the chain fails. tttt prints the same two texts at K1 and K16
  (kdiff and the recycle oracle list it): matching tsgo, not a tttt order bug.
