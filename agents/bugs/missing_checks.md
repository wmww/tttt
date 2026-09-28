# Missing and wrong checks (the suite's long tail)
The work queue for "suite checker bugs" sessions: diagnostics tttt doesn't report, or reports under another code.
Each is small (one check, a few tests). Find fresh buckets with `./failstats.py` (single-code tests: errors fail on
one code alone in `build/failstats/tests.tsv`) and `./unimplemented_codes.sh` (codes `src/` never mentions). Test
statuses checked 2026-09-27 (triage, suite 8952); buckets refreshed 2026-09-28 (suite checker bugs 4).

## Buckets (tests whose errors fail on this one code, 2026-09-28, after suite checker bugs 4, suite 10188)
- The single-code tests are now mostly *spurious*: spurious TS2339 40, TS2322 35, TS2304 35, TS2345 12, TS2300 10,
  TS2403 9, TS1005 9, TS2349 8, TS2554 7; missing TS2322 13 (the leftovers: relation-engine cases that accept what tsc
  rejects, deepComparisons, genericRestTypes, invariantGenericErrorElaboration...; evolving arrays; mappedTypeErrors2's
  key constraint check), missing TS2304 9, TS2538 8. The next checker-bugs session should start from the spurious side
  (sample, don't assume one cause). Errors component: 421 tests where tsc reports errors and tttt none (was 656).
- Module resolution / emit: TS8002 9 (JS, plan 26), TS2742 10 (declaration emit: out of scope); spurious TS2307 14 (APISample_*: resolving
  `typescript` from the harness).
- Checker: TS2538 8 (below), TS7023 5, TS2344 5 (import types' arguments aren't checked against constraints:
  unmetTypeConstraintInImportCall, `identifier_type_check_arity()` doesn't see them), TS2449 through property access
  (`a.C` to a class declared later: circularImportAlias, privacyClassExtendsClauseDeclFile; tsc's
  checkPropertyNotUsedBeforeDeclaration, the access doesn't know whether a function defers it), TS2318 (the lazily looked-up globals: `Awaited`,
  `TypedPropertyDescriptor`; the eager ones are done in `program_report_missing_global_types`, IterableIterator in
  `ty_generator_type`);
  spurious TS2349 13 (betterErrorForUnionCall, classExtendsNull, functionType), TS2403 12 (below), TS7006 8, TS2300 10.
- TS7053 6 are JS (lateBoundAssignmentDeclarationSupport*: element access on a CommonJS module object is skipped in
  JS, `ty_check_element_implicit_any`'s TODO).

## Known missing checks
- **JS (seen in the 2026-09-28 corpus session)**: TS2565 for a property declared by a JS expression statement
  (`/** @type {F} */ C.prototype.m;`) read in the same flow container (tsc's getFlowTypeOfAccessExpression with
  assumeUninitialized: jsDocDeclarationEmitDoesNotUseNodeModulesPathWithoutError); TS7032 (a setter without a parameter
  type, `@type` on a setter being ignored) is absent entirely (probe plan19/webpack/jsdoc_accessor_type.ts).
- **Conditions**: TS2774/TS2801 (uncalled function / promise in a condition): tsc's
  checkTestingKnownTruthyCallableOrAwaitableOrEnumMemberType looks for uses of the tested symbol in the body; needs a
  symbol-use walk over expressions (tttt has no generic expression visitor).
- **Circularity**: TS2502 for accessors, class properties, parameters (circularAccessorAnnotations,
  circularGetAccessor, circularOptionalityRemoval; variables are done). TS2310 for cycles found while resolving
  (circularBaseTypes, circularConstraintYieldsAppropriateError); a cyclic interface still gets its bases' members (tsc:
  none). TS1176 (`implements` on an interface: interfaceThatInheritsFromItself, parser recovery first).
- **Decorators**: TS2449 for a class used in the decorators of its own methods' parameters (tsc counts them; tttt's
  check only knows class and member decorators).
- **Classes**: TS2467 (computed name referencing the containing declaration's type parameter: interface members have
  no computed-name scope marker). TS2383/TS2385/TS2386/TS2512 (overload export/access/optional/abstract agreement,
  next to TS2394 in tsc's checkFunctionOrConstructorSymbol; for method signatures the overloads of a name are one
  InterfaceMember, whose `optional` doesn't say which declaration had the `?`); TS2687 skips statics and method/accessor pairs. TS2564
  (strictPropertyInitialization, absent) and TS2612 need isPropertyInitializedInConstructor (flow at the end of the
  constructor). TS2401/TS2376 (`super()` placement with initialized/parameter properties). TS4127 (`override` on a
  dynamic name: overrideLateBindableIndexSignature1). TS2506 across files through namespace-qualified bases
  (classExtendsItselfIndirectly2). Abstract members satisfied by an interface merged into the class: tsc looks there
  before TS2515 (mergedInheritedMembersSatisfyAbstractBase). TS1094 (accessor type parameters).
- **Accessibility** (plan 13 leftovers): destructuring (`const { priv } = a`, `({ priv } = a)`:
  privateProtectedMembersAreNotAccessibleDestructuring, destructuringAssignment_private); TS2704 (`delete` of a
  readonly property); `this.x = v` in the constructor of a class merged with an interface declaring `readonly x`
  (tttt exempts any property whose parent is the class); `globalThis.globalThis = x` (TS2540). Private names: TS18015,
  TS2806 (set-only private accessor), TS2804/TS18010/TS2300/TS2717 duplicates (privateNameDuplicateField).
- **Declarations**: `export function g() {}` + `function g() {}` is TS2395 here, TS2393 in tsc (re-probed). TS2300
  for a second `export =`. TS1319 (default export outside a module, TODO in `scope_export_default_expr`). TS1330-1335
  (misplaced `unique symbol`, silently `symbol`). TS2475 (const enum used as a value). TS18055/TS18056 (isolatedModules enum string/initializer rules; the enum
  member value checks are in `enum_decl_typecheck`). TS6200 amalgamated cross-file
  duplicates (8+ conflicting names between two files are one TS6200 per file with related TS6201, tsc's
  `amalgamatedDuplicates`: duplicateIdentifierRelatedSpans2/4/7; the related spans below 8 are done, in scope.c's
  report_conflicts and check.c's declare_symbol_flags, which is where a per-file-pair count would have to gather), and TS6203 related info on a static duplicated by a
  namespace export (duplicateIdentifiersAcrossFileBoundaries). TS2492 (`catch (x) { let x }`:
  catchClauseWithTypeAnnotation).
- **Operators**: `void` operands/objects (UndefinedFacts in tsc: `v.x` with `v: void` is TS18048) aren't checked like
  undefined. `x **= v` with a non-reference LHS: TS2362 here, TS2364 only in tsc
  (compoundExponentiationAssignmentLHSIsValue). `for (x in o)` with a non-declaration target: TS2322 here, TS2405 in tsc
  (re-probed).
- **Type predicates**: TS1228 (predicate in a constructor type, accessor, parameter type), TS2775/TS2776 (assertion
  callee without explicit types), TS1225 (no such parameter), a predicate function returning non-boolean.
- **Implicit any**: widening errors past `report_errors_from_widening` (binding.c; plan 37 A stage 2: TS7031 on
  binding elements, TS7010/7011/7025 on widened inferred returns, TS7008 class fields, TS7018 through a spread) and of
  generator yield/next types; TS7023 through indirect self-references in return expressions
  (simpleRecursionWithBaseCase1 `[fn5][0]()`, thisInObjectLiterals, trivialSubtypeReductionNoStructuralCheck);
  TS7031/TS7006 from widening of binding pattern defaults (`function
  f([a1] = [undefined])`, noImplicitAnyDestructuring*);
  TS7051 (a parameter named like a type); TS7041 (`this` captured by a top-level arrow); TS7017 (`typeof globalThis` has
  no index signature); TS7055/TS7057 (generators).
- **Target/module-gated**: TS2705/TS2468 (async function needs the `Promise` constructor): tsc reports it from
  createPromiseReturnType, ie only when something asks for an async function's *inferred* return type, which is lazy
  (asyncFunctionNoReturnType reports it, asyncFunctionWithForStatementNoInitializer doesn't); reporting it for every
  unannotated async function was tried and lost 6 tests. Needs a "return type was inferred" signal that matches tsc's
  demand, not tttt's (tttt infers more eagerly). TS2802 (iteration needs `downlevelIteration`), TS1056 (accessors below
  ES5), TS2659 (`super` in an object literal below ES2015), TS2496's other places (`arguments` in ES5 arrows), TS2815
  (`arguments` of an outer function in a field initializer, or in an expression statement directly in a static block:
  tsc's isInPropertyInitializerOrClassStaticBlock; classStaticBlock6, which also wants TS2662 for `arguments` there). Await grammar leftovers: `await` in a class field initializer inside an
  async function (awaitInClassInAsyncFunction: the parser's in_async carries into class members), `await` in a
  namespace body counts as top level (tsc: TS1308), TS18037 (await in a static block) is parser territory
  (classStaticBlock6/7/26).
- **TS2538 with `any`** (8 tests): `any[[]]` (a tuple as an index type on `any`) and `{[z]: x}` parameter patterns
  with an unresolved `z` (asyncFunctionDeclarationParameterEvaluation, asyncGeneratorParameterEvaluation): tsc's
  getPropertyTypeForIndexType reports past the `any` object shortcut; find which path.
- **Iteration**: TS2489/TS2490 (iterator without `next`/`value`), TS2763-2765 (iterator next-type mismatches);
  generator next types aren't inferred from `yield` uses (tsc's `nextTypes`).
- **Modules**: exports in non-module containers dropped silently (`scope_add_port()`, TODO TS1194); `import { type
  require }` isn't type-only for TS2441; TS1361 through `import x = types.A` of a type-only namespace (importEquals1);
  typeOnlyMerge3 (TS1362 through an import merged with a namespace after TS2440); mergeSymbolRexportFunction (TS2451
  path); TS2497 (default/namespace import of an `export =` module without esModuleInterop); TS2686 (UMD global from a
  module, TODO in `scope_add_port`); TS2664/TS2671 (module augmentation errors); duplicatePackage_globalMerge prints
  TS2306 paths as `/node_modules/...` (tsc `/tests/node_modules/...`); a nested `declare module "m"` gets TS2435 but is
  still bound as a global ambient module (tsc: a local namespace). TS2846 (import of a `.d.ts` by name),
  TS2877/TS2876 (`rewriteRelativeImportExtensions`), TS2688 where tsc can't resolve a type reference directive and tttt
  can (typingsLookup3). TS6046 (unknown lib name), TS6278 and TS7016's "npm i @types" elaboration (need package ids).
- **TS2303 leftovers**: pushTypeGetTypeOfAlias (`exports.blah = exports.someProp`: tsc's isAliasableExpression takes
  dotted names, `declare_commonjs_export` only identifiers; the TS2303 there is getTypeOfAlias's type circularity
  through the module's type, not alias resolution), declarationEmitUnknownImport(2) (`import Foo = Missing` +
  `export {Foo}`), exportAsNamespaceConflict (tttt says TS2484).
- **Templates**: TS1125/TS1121 for escapes in template literal types, TS2590 (union too complex), `${symbol}` not
  rejected (templateLiteralTypes1/3/4).
- **Misc**: TS2353 for an object literal assigned to an assignment pattern (`({ a } = { a, b })`: tsc's pattern on an
  object literal type; binding patterns are done, `ty_check_object_literal_pattern()`). TS2311 (`await` as a callee) and TS2663 in class computed names (need tsc's
  includeClassComputedPropertyName); TS2561/TS2820 want the spelling suggestion (`spelling_suggestion()` exists; 11
  diagnostics in 5 tests); TS2551 suggestions don't filter inaccessible members; TS2793 only for function
  declarations, not overloaded methods/constructors (`add_implementation_success`); TS2635 (instantiation expression
  whose signatures don't fit); TS2799/TS2800 (tuple size cap); TS2563 (flow depth 2000 silently gives the error type);
  TS1064 (async return annotation isn't a Promise); TS1274 (misplaced `in`/`out`); TS2406 (`for (#x in o)` says
  TS1451); TS2537 (41 in 10 tests), TS1262 (16 in 13); TS1117 (blocked by parser gaps: `a` identifiers, `{ x = 1 }`
  outside patterns, `#z` in object literals; computed literal names aren't resolved for it either). TS1539 (`{ 2n: 1 }`,
  bigintPropertyName, which also has a parse failure).
- doYouNeedToChangeYourTargetLibraryES2016Plus: spurious TS1503 (regex flags checked against the target where tsc
  doesn't there), a few TS2550 missing; ES2015 and ES2023(target=esnext) fail types only.
- **Regular expression patterns** (TS1487 116 in 4 tests, TS1508, TS1535, TS1530, TS1517, TS1518, TS1507...): tsc's
  scanRegularExpressionWorker, ~1000 lines; only the flags are checked (`regex_check_flags`). Plan 27 stage 3.
- **Errors inside lib files** (objectTypeHidingMembersOfExtendedObject, duplicateNumericIndexers: global interface
  augmentations checked through the lib's first declaration) are dropped by `error_ctx_error_s` with a log_error.

- TS2507 "Type 'X' is not a constructor function type" for an `extends` expression is missing (only `new` reports
  it), and so is its related info for a type parameter (TS2735 "Did you mean for 'T' to be constrained to type
  'new (...args: any[]) => ...'?"). Plan 32 leftover.

- Override and variance checks exist now (triage 2026-09-28); left: JSDoc `@template in/out/const` grammar errors
  (jsdocTemplateTag7/8: the owner isn't known when tags are parsed, TODO in `ty_check_variance_annotations`),
  varianceAnnotationsWithCircularlyReferencesError (tsc's recovery for `type T1<in in>`), varianceReferences' extra
  TS2322 (line 44, variance of an alias of an interface reference), varianceAnnotations' extra error on
  `InstanceType<typeof Anon<T>>` of a class expression.

## Wrong codes that are type-core bugs
Spurious TS2403 ("Subsequent variable declarations must have the same type") means tttt computed two spellings of one
type differently; each is one type-core bug, good one-per-session material (all re-checked failing 2026-09-27):
mappedTypeModifiers/mappedTypes4 (`Required<>`/`-?` identity), inferenceOptionalProperties, optionalTupleElementsAndUndefined
(tsc's optional tuple element types carry `| undefined`), objectLiteralGettersAndSetters, newOperatorConformance (`new
C()` without inference gives `T<{}>`, tsc `T<unknown>`), anyAssignabilityInInheritance (overload choice with `any`
arguments), arrayLiteralWidened (`[[], [null, null]]`), booleanFilterAnyArray (`Ari<any> | Ari<unknown>`),
wrappedAndRecursiveConstraints2 (inference through a recursive constraint).

## JSX (`./failstats.py --jsx`)
Overloaded components don't get tsc's TS2769 report (tsxNotUsingApparentTypeOfSFC,
tsxStatelessFunctionComponentsWithTypeArguments4); TS6229 isn't checked for union tag types or inside an overload
chain; TS18042 (a type imported in a .jsx/.js file: jsxCheckJsxNoTypeArgumentsAllowed); TS2608
(ElementAttributesProperty with several members); TS2639 (tsxNamespacedTagName2); TS2604 on a union of tag types
(tsxDynamicTagName3). Types-component diffs there are general (contextualTypingOfOptionalMembers: `unknown` where tsc
infers; `{ [__proto__]: null }` computed names in tsxEmitSpreadAttribute).
