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
- Accessor bodies in type literals/interfaces are kept by tsgo's parser and typed (accessorBodyInTypeContext:
  `get foo() { return 0 }` is number); tttt drops them.
- `implements`/interface `extends` qualifiers are `any` in tsgo's types baseline (ported as observed in
  baseline_test_traversal.c); the checker path that makes them any wasn't found.

## From the TS 7.1 delta fan-out, agent D (symbols, alias naming, union order, 2026-10-09)
- TS expandos follow tsgo's binder now (no host namespace decls, no Method flag, `name.x` only, no member over a
  file's own non-expando declaration, `F.prototype = {}` an expando) but the binder still adds 6.0's
  SYMBOL_DECL_EXPANDO to the host (filtered in symbol_declaration_spans(), ignored by is_nested_expando_function()):
  the checker's expando rules want that namespace meaning. A real port of bindDeferredExpandoAssignment would drop it.
  JavaScript keeps all of 6.0's rules (todo entry 8). Class statics aren't in host->exports at bind time, so
  `Point.origin = v` over a `declare class Point { static origin }` still declares (targetTypeTest1 symbols).
- Index symbols (`__index`) of an object rest (`const { ...t2 } = strMap; t2.z`) are named after the rest's binding
  (`t2.__index`: tsgo's getRestType gives the rest type the binding element's symbol); tttt's rest type has none
  (noUncheckedIndexedAccessDestructuring).
- Union order left: tuples of different lengths each have an Array base of their own in tsgo (sliceResultCast:
  `x.slice` is a union of two identical-looking signatures), comparisonReverseMappedTypes `sameSource` (tttt's
  homomorphic `identity(nested)` isn't a reverse mapped type?), noImplicitAnyUnionNormalizedObjectLiteral1
  (normalized spread literal ordered first in tsgo), instantiationExpressions (instantiation expression types).
- checkerInitializationCrash: a global namespace's `export import VNode = react.ReactNode` merged with another file's
  `type VNode` lists one declaration in tsgo and doesn't name `react.ReactNode` (tttt merges them into an alias+type).
- expandoFunctionContextualTypesNoValue: `import Foo` + `export function Foo` (TS2440): tsgo's lookupName finds the
  function for `Foo.bar = v`, tttt the import.
- **Generic methods of a class or non-thisless interface get fresh type parameters in tsgo when accessed through the
  type** (resolveObjectTypeMembers instantiates every member that isn't syntactically thisless with the `this` mapper;
  instantiateSignature clones type parameters), so an annotation mentioning them isn't reused: `Iterator.from` (lib
  `IteratorConstructor extends typeof Iterator`) prints its `Iterator<T, ...> | Iterable<T, ...>` sorted
  (builtinIterator, awaitUsingDeclarationsWithIteratorObject, usingDeclarationsWithIteratorObject). Verified with an
  instrumented tsgo (pseudoTypeEquivalentToType false under the signature's mapper). tttt's ty_instantiate() returns
  the method's type unchanged for a `this`-only mapper that it doesn't mention, so ty_display can't tell; a display-only
  emulation needs the access path. Making the instantiation real is a checker identity change (perf, caches).

## From the TS 7.1 delta fan-out, agent B (diagnostic codes, 2026-10-09)
  - Object literal method `this` (thisInObjectLiterals, accessorInferredReturnTypeErrorInReturnStatement,
    checkingObjectWithThisInNamePositionNoCrash): 6.0's TS7023 where tsgo has none. tsgo seems to know a literal's
    member names without resolving a method's return type (`this.spaaace` is TS2339): lazy member types, plan-sized.
  - A merged property+accessor (now one symbol, TS 7 excludes) still has 6.0's type: objectLiteralErrors,
    callSignaturesWithParameterInitializers2 types; duplicateClassElements, propertyAndAccessorMerging symbols (an
    interface property joining a class/interface accessor across declarations); symbolProperty44 (late-bound name
    vs method).
  - TS5114/TS5115: mutuallyRecursiveInference, keyofGenericExtendingClassDoubleLayer never hit the instantiation depth
    limit in tttt; recursiveMappedTypes hits it elsewhere (TS2589 vs TS2502).
  - exportAsNamespaceConflict: the `export =` side of an `export as namespace` cycle doesn't go through
    symbol_resolve_alias's stack (one TS2303 of two).
  - augmentExportEquals2: tsgo lets `declare module "./file1"` augment an `export = foo` function (no TS2671/TS2503).
  - mappedTypeProperties: `[P in T]: any` in an interface or class declaration is only TS7061 in tsgo (no TS2304/TS2693
    for the key), but the same in a class expression is checked fully; why the class expression differs is unclear.
  - shebangError: `#!` not at the file's start is one Unknown token of two characters in tsgo's scanner (tttt scans
    the `!`: TS2872, TS2362 one column early).
  - arrayFakeFlatNoCrashInferenceDeclarations: TS5088 from a union's circularity elision
    (typeToTypeNodeOrCircularityElision), not ported.

## Source sweep after the TS 7.1 fan-out (agent A, 2026-10-09: 3049 / 3363)
- `type CP<T extends unknown[]> = { [K in keyof T]: Conv<T[K]> }`: `CP<Parameters<T>>` against `unknown[]` (a type
  argument constraint) is assignable in tsgo, TS2740 in tttt (probe: vitest's browser-playwright
  `BrowserCommand<ConvertUserEventParameters<Parameters<T>>>`; tsgo reports TS2344 there for other reasons, without a
  chain). Port getResolvedApparentTypeOfMappedType / tsgo's homomorphic-mapped-over-array relation.
- `Partial<{ a: number } & (object extends R ? { c?: F } : { c: F })>` has no `c` in tttt (TS2339; trpc's
  `Partial<CreateHTTPHandlerOptions<TRouter>>.createContext`, 8 projects): expand_generic_intersection_homomorphic()
  skips a part whose apparent type is a union (a deferred conditional's constraint); listing the union's common
  properties there was not enough, the property lookup goes another way.
- mikro-orm (120+ diagnostics): `NativeInsertUpdateOptions<T> & UpsertOptions<T, never>` not assignable to
  `UpsertOptions<T, never>` for `getOnConflictReturningFields(meta, data, uniqueFields, options, platform)`: the
  inferred T prints as T but isn't the caller's (TS2719-style "two different types" deep in the chain). Inference.
- tanstack-table examples (`spans.reduce((total, span) => ...)` with `spans: unknown` from an unresolved Playwright
  call): tsgo's untyped call gives the callbacks contextual any, tttt TS7006 (a plain `unknown.reduce(...)` probe is
  fine: the difference is in how `spans` becomes unknown).

## JS fan-out, agent D (2026-10-09)
- Returned object literals aren't widened in tsgo: `function f() { return { x: [] } }` is `() => { x: undefined[] }`
  with strict off (tttt: any[]), but `{ x: [], y: null }` widens both and `const o = { x: [] }` is any[]
  (argumentsReferenceInObjectLiteral_Js and probes). Some object flag (ContainsObjectOrArrayLiteral /
  RequiresWidening) propagates differently in tsgo; find which and port it.
- importTag13: `@import x = require("m")` after 'from' expected, tsgo parses the module specifier as an expression and
  says TS1141 over `= require("types")`; tttt's JSDoc @import parser stops.

## JS fan-out, agent B: CommonJS (2026-10-09)
- A JSDoc `@typedef T` and a same-named class/variable in a JS module (ES or CommonJS) is TS2300 in tsgo (the typedef
  gets a local twin, declareModuleMember), none in tttt (typedefCrossModule2's Foo, probe `/** @typedef {number} Foo */
  class Foo {}`). Also tsgo binds top-level typedefs after every other statement: declaration lists put them last
  (jsTypedefMergedWithModuleExportProperty's symbols). C's typedef binding.
- `Object.defineProperty(o, "name", ...)` on a non-exports object names the member as written, `"name"` with quotes
  (checkObjectDefineProperty); exports already do (add_define_property_export()). A's expando code: needs a display
  span separate from the lookup name in add_expando_member().
- `exports.fn2 = Math.min`: tsgo's symbols baseline names `Math.min` `Symbol(fn2, Decl(lib.es5.d.ts...))` (an alias
  declaration's entity name shows the alias's name with the target's declarations: moduleExportsAliasLoop1/2). Quirk.
- JS declaration emit diagnostics: only TS6424 is ported (`js_declaration_diagnostics_check()`); TS4023 for JS files
  (jsDeclarationsTypeReassignmentFromDeclaration2, cjsObjectDefinePropertyPrivateModuleDeclarationEmit) needs the
  declaration walk to run on JS, which it skips entirely (`declaration_diagnostics_wanted()`).
- A module augmentation's module in the types baseline (`declare module "./test"` of a JS module): tsgo prints
  `typeof import("./test")` from the augmenting file, tttt `typeof import(".")` (jsExportMemberMergedWithModuleAugmentation2);
  naming it from outside the augmentation in the traversal lost two TS tests.
- Valgrind reports the thread-local `alias_stack` (scope.c symbol_resolve_alias) as possibly lost in every test that
  resolves an alias (`scripts/test.sh -f importNonExportedMember11`): noise that stops `test.sh` at the first test.
- jsdocTypedefBeforeParenthesizedExpression: `@param a {NotADuplicateIdentifier}` with the typedef in a comment before
  a parenthesized expression statement prints `number` in tsgo, the alias name in tttt. (Agent C)
- (agent D, JSDoc second pass) typedefTagWrapping: a `@typedef` whose type fails to parse (`{function(string): boolean}`)
  has a missing name in tsgo, declared in the global scope as '(Missing)' at the '}' expected position: three such
  script files report TS2300 "Duplicate identifier '(Missing)'". callbackTag4: tsgo's symbols baseline names a
  `@this` tag's synthetic `this` parameter by the tag's whole text (`Symbol(@this {{ a: string, b: number }}\n, ...)`).
  Both are tsgo quirks; port only if cheap.
## JS fan-out, agent A (2026-10-09)
- `obj3.y = Object.values(obj3)` (expandoObjectIndexSignatures, expandoNoInferredIndex): tsgo reports TS7022 for the
  cycle through the container's type inside the call's inference; tttt's guarded_assignment_symbol_type() doesn't
  see it past the call's resolving barrier (Checker.resolving_start).
- typeFromContextualThisType symbols: `this` in a method of a literal typed by `@type {{...}}` on `const o1` is
  `Symbol(o1, Decl(type literal))` in tsgo (the reparsed type literal is named by the variable): C's reparse naming.

- flagflip after B's third task (2026-10-09, every cell but vscode): webpack's cells are down to its default-flag misses
  (20; 15 under sloppy/nosnc) but for one: webpack/nuia UmdLibraryPlugin.js:233 wants TS2322 inside
  `f([...(Array.isArray(r) ? r : [r])])` where tttt says TS2345 on the argument. tsgo's checkArrayLiteral passes
  ForceTuple through the conditional to the `[r]` branch, so the spread operand is `string[] | [string | R]`, a
  union isTupleLikeType accepts (a union's "0" from the array's index signature); `ty_array_literal_as_tuple()`
  only forces a directly spread array literal and turns a non-array union into an array. Other corpora: only
  their default-flag misses, plus xstate/sloppy and xstate/nosnc spurious TS2589/TS5114/TS5115 (State.ts:395,
  graph/utils.ts:91: instantiation depth on `Values` without strictNullChecks), xstate/eopt one spurious TS2322
  (input.test.ts:272, a spawn signature), vue/nuia 2 spurious + 2 other-code: TypeScript checker cases, not JS.
- (agent D, JS mutants 2026-10-09, seeds 1-3 of `--tests js -n 3000`: 83/28/31 differing before, ~19/26/29 after)
  What's left, by owner:
  - TS (anyone): `const t: never = f4([[1, "x"], [2, "y"]])` with `<const T>(x: [T, T]) => T` prints `[1, "x"]` where
    tsgo has `readonly [1, "x"]`: a contextual return type (never) seems to replace T as the array literals' contextual
    type, so they lose the const context (jsdocTemplateTag6 mutants, also reproducible in TS).
  - TS: `interface I { readonly [symb]: boolean }` with `symb: never` is a number index signature in tsgo
    (expandoFunctionSymbolPropertyJs mutants).
  - Suggestions: tttt never reports unused declarations as suggestions (captureSuggestions with noUnused* off):
    unused_enabled() is false then; tsgo reports TS6133 suggestions, in plain JS too (noFlakyDiagnosticsUntypedModule*).
  - C: `/** @type {Foo} */ export default { ... }` elaboration position (checkJsdocTypeTagOnExportAssignment5/6);
    an earlier comment's `@extends` still counts for TS8023 (checkJSDocAugmentsTagMatchesExtends reads every
    comment); jsdocParamTagTypeLiteral nested `@param` TS8032 cases.
  - B: TS2540 read-only `NS` (ensureNoCrashExportAssignmentDefineProperrtyPotentialMerge), moduleExportAlias5 TS2339,
    jsDeclarationsFunctions TS2323 (`export { j }` twice).
- (agent D, JS mutants round 2, seeds 2/3: 18 and 19 left, all one-offs or other owners)
  - A JS variable with both a TS annotation (TS8010) and a JSDoc `@type` on a function initializer: tsgo's reparser
    makes the `@type` the function's FullSignature (the declaration already has a Type), so the function prints as
    the tag's signature (`(this: Foo) => void`, `(firstParam: V) => any`); tttt keeps the contextual typing. Artificial.
  - TS8024 quotes a JSDoc `@param` name with escapes decoded in tsgo (`a\u{62}c` -> `abc`): JSDoc names aren't
    registered with names_register_source, so name_cooked() can't decode them.
  - An astral-plane JSDoc `@param` name underlines one `~` in tsgo (render of extendedUnicodePlaneIdentifiersJSDoc).
  - TS5024 maxNodeModuleJsDepth with a string value; TS8030 for `@type {Foo}` naming a deleted type alias
    (jsDeclarationEmitFunction).
- webpack NormalModuleFactory.js:658-666 (TS2322 missing): `/** @type {TypedHookMap<...>} */ createParser: new
  HookMap(() => new SyncBailHook([...]))`: tsgo infers HookMap<SyncBailHook<ParserOptions | [EmptyParserOptions],
  HtmlParser, ...>> under that context and it isn't assignable; tttt's inference produces something that is.
  Generic inference from a contextual intersection-heavy type, not JSDoc-specific. (Agent C)
- webpack node_modules/zod/v4/classic/schemas.d.cts:806 TS5115 (instantiations appear infinitely circular) missing:
  reached through webpack's use of zod's types, not zod's own corpus. (Agent C)

## 7.1 new tests fan-out (agent B, 2026-10-09)
- `contentMapper*` (10 on B's list, 15 in the tree): the 7.1 `contentMappers` feature, written up as plan 56, not implemented.
- `importAttributesDeclarationEmit` (TYP): tsgo prints an import type's attribute keys `any` or `error` depending on
  what was checked before (probe: deleting unrelated lines flips it): order-dependent in tsgo, not chased.
- `globalArrayAugmentationWithAmbientModuleReexportMerge1` (SYM/TYP): errors pass; tsgo's failed cross-file merge of an
  export alias with a const keeps two symbols (mergeSymbol returns source), tttt's binder merged them into one.
- `declarationEmitAugmentationUsesCorrectSourceFile` (SYM/TYP): an augmentation merging into an `export { Knex }` alias's
  target merges into a clone in tsgo 7.1 (mergeSymbol's resolveSymbol + cloneSymbol), leaving the original namespace
  uninstantiated (`any`) and naming it `knex.Knex`; tttt's merge_into_exports merges into the target itself.
- `duplicateIdentifierChecks` (SYM/TYP): errors pass; the parser merges a name's accessor declarations of a type
  literal/interface into one member, tsgo keeps a symbol per conflicting declaration.
- `expandoNoInferredIndex`, `expandoObjectIndexSignatures`: `obj.y = Object.values(obj)` of a JS `{}` container is
  circular in tsgo (TS7022, y any; inference from the namespace type reads y's type), tttt's property read of y while
  y is in flight gives a silent value: find where the container's member type skips ty_resolving (agent D's
  circularity area).
- `jsTypedefMergedWithModuleExportProperty` (SYM): a `@typedef T` merged with `const T` exported through
  `module.exports.T = T` lists the typedef's declaration first; tsgo's delayed typedef bind puts the const first
  (symbol_add_decl's reordering doesn't reach this merge).
- `isolatedDeclarationsJsThisPropertyAssignmentInference`: TS9013 for `this.x = foo()` in a JS constructor under
  isolatedDeclarations (declaration diagnostics: agent C's area).

## 7.1 new tests fan-out (agent A, 2026-10-09)
- jsonRawJSON2 (symbols): a script's `interface RawJSON` merged with lib.es2026.json's `declare global` one lists the
  script's declaration first in tsgo (global augmentations merge after every file's globals, initializeTypeChecker);
  tttt's shared-lib program has the lib's augmentation merged into the globals already. Needs the lib augmentations
  to merge after the program's script globals (or declaration order fixed up), not done: harness/lib-sharing work.
- tsconfigRootdirInclude: in the harness, TS6059 with a tsconfig `include` match is tsgo's related info TS1408 ("File
  is matched by include pattern specified here", FileIncludeReason.toRelatedInfo) with relative paths, plus the
  config's TS1005; tttt prints the CLI-style chain and loses the config's syntax error. Also a CLI divergence: tsgo's
  chain says "Matched by include pattern 'src1/*' in '/abs/tsconfig.json'" (absolute), tttt 'tsconfig.json'.
- cjsObjectDefinePropertyPrivateModuleDeclarationEmit: TS4023 for `Object.defineProperty(exports, "api", { value:
  require("pkg") })` whose type uses a non-exported interface of an `export =` module (declaration emit of a CommonJS
  defineProperty export, name `'"api"'`): declaration diagnostics (agent C's area).
- importAttributes12: import type attributes (TS2858 non-string values in `import("m", { with: {...} })` types, TS2322
  against ImportAttributes): agent B's import-attributes mechanism.
- Harness: a positioned tsconfig diagnostic (TS5092 at the root value) comes out without location in the harness
  (`cstr_eq(&d->config, &i->path)` in baseline_test_case.c compares different path spellings); the CLI is right.
