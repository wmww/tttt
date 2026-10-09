# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-08 (triage session, todo entry 10 of the twelfth window): scripts moved to `scripts/` (the
human's request); the human's TypeScript 7 request researched and written up as plan 53 (+ 53_findings.md); plans
36-42, 44, 45, 48, 49, 51 consolidated into bugs/, documentation.md and hacks.md and deleted; Opus sub-agents fixed the
vue order dependence (alias variances), discord.js's global-block merge order, unknown compiler options
(TS5023/5025/5093/6369/6114/6258), the root-file "in the program because" chain and TS6305 for `/// <reference
path>`, the lone-`\r` harness split, the mikro-orm spread constraint and several source-sweep keys, then second
tasks (a mutation seed, the kysely perf lead, svelte/react-navigation). The commit message has the numbers;
leftovers are in bugs/include_order.md, harness.md, sourcesweep_checker.md, missing_checks.md, perf_leads.md.

## From plan 53 stage 1 (CLI parity, 2026-10-08)
- `scripts/clidiff.py` (tttt's stdout against `tsc -p --noEmit --pretty false`, byte for byte) finds message text the
  head-message oracles never compared, all checker side, none CLI:
  - **TS6059/TS6307 lack the "The file is in the program because:" chain** under them: every inclusion reason of the
    file (`Imported via "../x" from file '/abs/a.ts'`, `with packageId '...'`), in tsc's order of discovery
    (socket.io/examples/basic-crud-application/server, nest/integration/file-upload). tttt only keeps
    `SourceFile.included_at`; the root-file chain (`program_explain_root()`) exists already.
- `--showConfig`, `--explainFiles`, `--watch`, `--init` are refused (exit 1). `--listFiles` prints tttt's own lib paths
  (thirdparty/lib/X.d.ts, not lib.X.d.ts) in tttt's file order: not compared against tsc.
- TS7016 at a package's import of its own name (`@vitest/browser/internal/vendor-types` from inside
  vitest/packages/browser, resolved through its own exports) lacks tsc's TS7058 chain line ("If the '@vitest/browser'
  package actually exposes this module, try adding..."): the self-name resolution leaves `ModuleRef.package_name`
  empty, so `program.c`'s package hint pass skips it (vitest/test/e2e/dts/browser-playwright).
- `function h(v: 0|1|2) { v++; v += 1; }`: tsc reports nothing, tttt TS2322 on `v += 1` (the compound assignment's
  result checked against the declared literal union; tsc assigns `number` to a narrowed-by-assignment reference?).
  Found by the plan 54 stage 1 crash fixes, reachable code too.
- failstats.py counts "pass: all" over its own notion of ran tests (12128 / 13835 on the native suite at plan 54 stage
  1) while the harness says 11860 / 13571: the errors-only tests without any baseline and skipped configurations are
  counted differently. Make failstats read the harness's saved results for its totals.

## From plan 54 stage 2 (oracle tools on tsgo, 2026-10-09)
- tsgo's CLI names a default lib file `bundled:///libs/lib.es5.d.ts` (not relative to the cwd) and sorts it after the
  project's files (`a.ts` before `bundled:...`); tttt prints `../home/.../thirdparty/lib/es5.d.ts` and sorts it first.
  A clidiff mismatch whenever a diagnostic lands in a lib (`type Partial<T> = T;` in a project). `sweep.lib_key`
  accepts both spellings.
- `scripts/tscinstcount.js` (tsc's per-node TS2589 instantiation counter, `VARIANCES=1` per variance measurement) was
  deleted with tsc 6 (plan 54 stage 3). If a TS2589 runaway needs tsc's budget again, port it as a `test/oracle`
  overlay: a patched copy of `internal/checker/checker.go` logging `c.instantiationCount` where it's reset (in a scratch
  branch, never committed; README's "instrument the oracle").
- tsgo 7.1's `-b` exit codes weren't compared (stage 2 moved the non-build CLI to 7.1's: 2 with diagnostics, 1 only
  when noEmitOnError skips the emit). `build()` in main.c still returns 1 for any project with errors.

## From plan 54 stage 4 (removed options, 2026-10-09)
- **TS2880 (`assert` import attributes) is a parse error in tsgo** (parser.go: import declarations, import types and
  `export ... from`), tttt reports it from the checker (`port_statement.c`, `several_types.c`; the dynamic `import(m,
  { assert })` one is still the checker's in tsgo too). As a syntactic diagnostic it gates semantic checking on the
  CLI. failstats: missing TS2880 in 8 tests, spurious in 5. Plan 54 "Stage 5+" (7.1's new diagnostics) territory.
- source sweep `+TS6059` (react-router/packages/create-react-router, 11 projects in 2 repos): a file outside rootDir
  brought in by an import (`tsdown.config.ts` imports `../../build.utils.ts`) is TS6059 at the import in tttt, nothing
  in tsgo; a root file outside rootDir is TS6059 in both (`/tmp/rd`-style probe matches). tsgo adds it as a
  processing diagnostic from CommonSourceDirectory(), maybe only for files known at that point.
- `EMIT_HELPER_EXTENDS/ASSIGN/GENERATOR/VALUES/READ/SPREAD_ARRAY/MAKE_TEMPLATE_OBJECT` are never requested any more
  (their checks were ES5 / downlevelIteration only); `helpers.c`'s table is indexed by bit, so they stay in the enum.
- `tsgo_skips_configuration()` (harness) still reads module/moduleResolution as written to skip tsgo's removed
  configurations; `@module: none` no longer parses (TS6046), so such configurations run with the default module unless
  they have no baselines (all 7 `@module: none` tests have none).

## From the TS 7.1 delta fan-out, agent A (relation elaboration, 2026-10-09)
- Duplicate declarations across merges: tsgo's addDuplicateDeclarationError makes a fresh diagnostic per merge (so
  each starts with "'x' was also declared here.", TS6203) and compactAndMergeRelatedInfos joins them; tttt's scope.c
  counts the related information already on one diagnostic and says "and here." (TS6204): duplicateIdentifierRelatedSpans1,
  exportAsNamespace_augment. The merge itself is ported (error.c merge_related_info()).
- Global diagnostics order: tsgo prints TS5053 before TS2318s (arrayIterationLibES5TargetDifferent(nolib=true,...)),
  and a file's errors before tsconfig.json's when its path sorts first (pathsValidation5); tttt sorts globals by code
  and puts tsconfig first. Check what Loc/path tsgo gives option diagnostics.
- Relation paths that differ from tsgo's (not elaboration rules): conditionalTypes2 (`Extract<Extract<T, Foo>, Bar>`
  explained through `Extract<T, Bar>`), mappedTypeConstraints2 (tttt simplifies `M<K>[keyof M<K>]` first),
  keyofAndIndexedAccessErrors, complicatedIndexedAccessKeyofReliesOnKeyofNeverUpperBound, complexRecursiveCollections
  (another failing member first), reactReduxLikeDeferredInferenceAllowsAssignment and
  circularlyConstrainedMappedTypeContainingConditionalNoInfiniteInstantiationDepth (tsgo elaborates 9 levels),
  mergedDeclarations7 (tsgo: a namespace import of `export =` gives `PassportStatic` not assignable to `Passport`
  through `use(): this`), inferFromGenericFunctionReturnTypes3 (spurious errors), conditionalTypes1's
  `Partial<T>[keyof T] & {}` target display.
- Circularity results: blockScopedBindingUsedBeforeDef (tsgo `{}` where tttt says `any`), YieldExpression6_es6
  (`() => Generator<any, void, unknown>` for a generator referencing itself).
## From the TS 7.1 types-printer fan-out (agent C, 2026-10-09)
- **tsgo's pseudochecker is only partly emulated.** serializeTypeForDeclaration/serializeReturnTypeForSignature print a
  declaration's type from its syntax (pseudochecker, `internal/pseudochecker`) when equivalent to the checker's type,
  wherever there's an enclosing declaration. tttt has slices (`a: x as T`, `return x as T`, single-quoted literals under
  `as const`, `p? = 0`, mapped `a?: T`). Missing: a variable's whole initializer (`const a = { get foo() {...}, set
  foo(v) {...} }` prints `{ get foo(): string; set foo(value: string); }` in `a`'s line only; `{ r: ['w'] } as const`
  prints `readonly ['w']` there, `["w"]` in the expression's line), and its isolatedDeclarations errors (ErrorNodes).
  Plan-sized: a port of pseudochecker + pseudoTypeEquivalentToType over tttt's AST.
- **Anonymous types inside generic declarations are instantiated even when they reference no outer type parameter**
  (tsc filters getOuterTypeParameters by isTypeParameterPossiblyReferenced): `Promise<number>.finally`'s parameter type
  is a new union, so its annotation `(() => void) | undefined | null` isn't reused and prints `| null | undefined`
  (es2018ObjectAssign, usePromiseFinally). Also an identity/perf question. Reusing parameter annotations with the
  signature's mapper (tsgo does) was tried: 94 regressions (`this` must map, `then` isn't reused by tsgo).
- Function types of an object literal's properties: tsgo prints `{ foo: () => { foo: () => any; } }` where tttt prints
  `{ foo: () => { foo: any; } }` (genericCallOnMemberReturningClosedOverObject, thisInPropertyBoundDeclarations,
  newTarget, noImplicitThisBigThis): the widened literal's property function type seems a distinct type in tsgo.
- Nested instantiations of one mapped type share tttt's type parameter (tsgo clones it): `Compute<A>` prints `[K in ...]`
  inside `[K in ...]`, tsgo `K_1` (computedTypesKeyofNoIndexSignatureType, declarationsWithRecursiveInternalTypes...).
- 7.1's import attributes on module symbols: `typeof import("./0", { with: { type: "json" } })`
  (getTypeOfModuleImportAttributes, importAttributes2/3, importAssertion2/3, typeOnlyESMImportFromCJS).
- A rest parameter expanded from a tuple: tsgo's fake scope names the *original* parameters by index (enterNewScope),
  so `typeof a` of the rest `a` is reused (declarationEmitTypeofRest); tttt's name_accessible() has TS 6.0's rule.
- Accessor bodies in type literals/interfaces are kept by tsgo's parser and typed (accessorBodyInTypeContext:
  `get foo() { return 0 }` is number); tttt drops them.
- `implements`/interface `extends` qualifiers are `any` in tsgo's types baseline (ported as observed in
  baseline_test_traversal.c); the checker path that makes them any wasn't found.
