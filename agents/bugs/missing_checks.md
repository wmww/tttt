# Missing and wrong checks (the suite's long tail)
The work queue for "suite checker bugs" sessions: diagnostics tttt doesn't report, or reports under another code.
Each is small (one check, a few tests). Find fresh buckets with `./failstats.py` (single-code tests: errors fail on
one code alone in `build/failstats/tests.tsv`) and `./unimplemented_codes.sh` (codes `src/` never mentions). Test
statuses checked 2026-09-27 (triage, suite 8952).

## Buckets (tests whose errors fail on this one code, 2026-09-27, after suite checker bugs 3)
- Mixed bags (sample, don't assume one cause): missing TS2339 52, spurious TS2322 46, missing TS2322 44, spurious
  TS2304 44, spurious TS2339 29, spurious TS2345 14. Missing TS2339 overall is ~380 in 166 tests: the gates in
  `check_access()` (JS, callables, mapped, unresolved bases) and whatever is typed `any` by failure.
- Module resolution / emit: missing TS2303 15 (package pattern exports, below), TS8002 9 (JS, plan 26), TS2209 6,
  TS5055 11, TS5069 6, TS2742 10 (declaration emit: out of scope); spurious TS2307 14 (APISample_*: resolving
  `typescript` from the harness).
- Checker: missing TS2353 11, TS2538 8 (below), TS2449 6, TS7023 5, TS2352 5, TS1259 5 (esModuleInterop default
  import, a TODO in scope.c), TS18050 5, TS7010 5, TS2318 5 (the lazily looked-up globals: `Awaited`,
  `TypedPropertyDescriptor`, iteration types; the eager ones are done in `program_report_missing_global_types`);
  spurious TS2349 13 (betterErrorForUnionCall, classExtendsNull, functionType), TS2403 12 (below), TS7006 8, TS2300 10.
- TS7053 6 are JS (lateBoundAssignmentDeclarationSupport*: element access on a CommonJS module object is skipped in
  JS, `ty_check_element_implicit_any`'s TODO).

## Known missing checks
- **Conditions**: TS2774/TS2801 (uncalled function / promise in a condition): tsc's
  checkTestingKnownTruthyCallableOrAwaitableOrEnumMemberType looks for uses of the tested symbol in the body; needs a
  symbol-use walk over expressions (tttt has no generic expression visitor).
- **Circularity**: TS2502 for accessors, class properties, parameters (circularAccessorAnnotations,
  circularGetAccessor, circularOptionalityRemoval; variables are done). TS2310 for cycles found while resolving
  (circularBaseTypes, circularConstraintYieldsAppropriateError); a cyclic interface still gets its bases' members (tsc:
  none). TS1176 (`implements` on an interface: interfaceThatInheritsFromItself, parser recovery first).
- **Classes**: TS2467 (computed name referencing the containing declaration's type parameter: interface members have
  no computed-name scope marker). TS2383/TS2385/TS2386/TS2512 (overload export/access/optional/abstract agreement,
  next to TS2394 in tsc's checkFunctionOrConstructorSymbol); TS2687 skips statics and method/accessor pairs. TS2699
  (static `name`/`length`/`prototype`/... vs Function: staticPropertyNameConflicts, staticPrototypeProperty). TS2564
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
  `amalgamatedDuplicates`: duplicateIdentifierRelatedSpans*), and TS6203 related info on a static duplicated by a
  namespace export (duplicateIdentifiersAcrossFileBoundaries). TS2492 (`catch (x) { let x }`:
  catchClauseWithTypeAnnotation).
- **Operators**: TS18050 ("The value 'null' cannot be used here", 680 missing in 38 tests): tsc's message for
  null/undefined operands; `expr.c` uses `type_extends_strict_nulls` + TS2365. `u += 1` with `u: undefined` is TS18048
  in tsc (`check_non_null_type` exists). `x **= v` with a non-reference LHS: TS2362 here, TS2364 only in tsc
  (compoundExponentiationAssignmentLHSIsValue). `for (x in o)` with a non-declaration target: TS2322 here, TS2405 in tsc
  (re-probed).
- **Type predicates**: TS1228 (predicate in a constructor type, accessor, parameter type), TS2775/TS2776 (assertion
  callee without explicit types), TS1225 (no such parameter), a predicate function returning non-boolean.
- **Implicit any**: TS7010 from widening (`function f() { return null; }` without strictNullChecks:
  implicitAnyFunctionReturnNullOrUndefined); TS7031/TS7006 from widening of binding pattern defaults (`function
  f([a1] = [undefined])`, noImplicitAnyDestructuring*: tsc's reportErrorsFromWidening, hacks.md `widen_nullable`);
  TS7051 (a parameter named like a type); TS7041 (`this` captured by a top-level arrow); TS7017 (`typeof globalThis` has
  no index signature); TS7055/TS7057 (generators).
- **Target/module-gated**: TS2705/TS2468 (async function needs the `Promise` constructor): tsc reports it from
  createPromiseReturnType, ie only when something asks for an async function's *inferred* return type, which is lazy
  (asyncFunctionNoReturnType reports it, asyncFunctionWithForStatementNoInitializer doesn't); reporting it for every
  unannotated async function was tried and lost 6 tests. Needs a "return type was inferred" signal that matches tsc's
  demand, not tttt's (tttt infers more eagerly). TS2802 (iteration needs `downlevelIteration`), TS1056 (accessors below
  ES5), TS2659 (`super` in an object literal below ES2015), TS2496's other places (`arguments` in ES5 arrows), TS2815
  (`arguments` in a class field initializer). Await grammar leftovers: `await` in a class field initializer inside an
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
- **TS2303 through namespace imports** (15+ node-modules tests miss only this: nodeModulesPackageExports,
  nodeModules*PatternExports*, nodeModulesConditionalPackageExports, x3 module kinds): `import * as cjs from
  "inner/cjs/index"` resolving to the importing file itself plus `export { cjs }`. tsc's resolveESModuleSymbol calls
  getTypeOfSymbol on the module for namespace imports, which resolves every export alias, so the chain re-enters `cjs`;
  tttt's alias cycle detection (ALIAS_CIRCULAR, `symbol_resolve_alias`) only sees direct chains. Find the smallest rule
  that doesn't compute module types in the binder. Same area: recursiveExportAssignmentAndFindAliasedType6 wants the
  TS2303 span to be the whole import-equals statement (`PortItem.full_span` of `import x = require()` ends before the
  name: `parse_import_equals()` takes the caller's anchor; check TS2440's tests before changing it), and a circular
  `import self = require(...)` / `export = self` types `self` as `Window & typeof globalThis` (tsc: any).
- **Templates**: TS1125/TS1121 for escapes in template literal types, TS2590 (union too complex), `${symbol}` not
  rejected (templateLiteralTypes1/3/4).
- **Misc**: TS2311 (`await` as a callee) and TS2663 in class computed names (need tsc's
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
