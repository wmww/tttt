# Missing and wrong checks (the suite's long tail)
The work queue for "suite checker bugs" sessions: diagnostics tttt doesn't report, or reports under another code.
Each is small (one check, a few tests). Find fresh buckets with `./failstats.py` (single-code tests: errors fail on
one code alone in `build/failstats/tests.tsv`) and `./unimplemented_codes.sh` (codes `src/` never mentions). Test
statuses checked 2026-09-27 (triage, suite 8952); buckets refreshed 2026-09-28 (suite checker bugs 4).

## Buckets (tests whose errors fail on this one code, 2026-09-29, after suite checker bugs 7, suite 12316)
- Single-code tests: missing TS2742 10 (plan 40), missing TS2322 8, missing TS7023 4 (tsc's re-entry through
  error-message printing, triage), spurious TS2345 4, missing TS2589 4, missing TS2344 4 (generic arguments: the
  HACK in `ty_check_type_reference_constraints`), missing TS2304 4, spurious TS2339 4, spurious TS2307 3, missing
  TS4023 3 (plan 40), then parser codes (TS1009, TS1099). 223 single-code rows in all. Errors component: 462 failing
  (was 790), 106 of them tests where tsc reports errors and tttt none.
- Done by suite checker bugs 7 (besides error text): TS4111, TS18055, TS18057, TS2467, TS18027, TS2668, TS2664,
  TS2671, TS2439, TS2306, TS6137, TS5061, TS18007, TS6807, TS2398, TS2717 (private names, computed), TS2317, TS2434,
  TS2484, TS2715, TS2341 in patterns, TS2815, TS2513, TS2542, TS2371, TS2862, TS2635, TS2729, TS2206/TS2207,
  TS2490/TS2767, TS2498, TS2540, TS2666/TS2667, TS7055/TS7025, const enum TS2475/TS2748/TS2476, TS2526, TS2461,
  TS2700, TS7032/TS7033, TS7031, TS2608, TS2639, mapped type key checks (TS2322), union calls (TS2349/TS2351).
- Done by suite checker bugs 6: TS2774/TS2801/TS2845 conditions, TS2564/TS2565 and definite assignment grammar,
  TS2313, TS7023/TS7024, TS2401/TS2376, overload agreement (TS2385/TS2386/TS2512, TS2386 for method signatures too),
  TS2818, TS2686, TS7036, TS2708 for `export import`, TS17012, TS1147/TS1194, TS2452, TS2702/TS2713, TS8024/TS8032,
  TS2462, TS1182, TS2778-2781, TS2408, TS2659, TS1064, TS2492, TS2675, TS18045, TS2806, tsconfig option diagnostics
  at the option (TS5xxx, paths validation, deprecations by version), TS6054.

## Known missing checks
- TS4105 (private/protected member accessed through an indexed access on a generic constraint:
  indexedAccessPrivateMemberOfGenericConstraint, where tttt reports TS2536 instead) and TS4109/TS4110 (type arguments
  circularly referencing themselves: circularlyReferentialInterfaceAccessNoCrash, see also `type_core.md`).
- `interface I<C> { a: StateNode<C, any> }` where StateNode requires `C extends MachineContext`: tsc's TS2344 on the
  member's type argument is missing (xstate).
- Missing TS2353 for `const x: Trans<E> = { NEXT: ... }` in a generic function with `Trans<E> = { [K in E['type'] |
  '*']?: ... }` (xstate; excess property checks against a generic mapped type).
- TS2712 (a dynamic `import()` in ES5 without a global `Promise` value, with TS2468): tsc reports it unconditionally in
  checkImportCallExpression (createPromiseReturnType).
- TS2510 ("Base constructors must all have the same return type") for an intersection base whose construct
  signatures return different types.
- TS2344 on type references (`ty_check_type_reference_constraints`, check.c): 7 spurious in unknownType2
  (`AssertTrue<false>`), one missing each in instantiationExpressions:163 and typeGuardFunctionErrors:152 with generic
  arguments (`U`, `T | "d"`): the HACK there skips generic arguments.
- **Module resolution elaboration**: tsc's alternateResult TS2307/TS7016 chains ("There are types at '...', but this
  result could not be resolved under your current 'moduleResolution' setting" / "...when respecting package.json
  \"exports\"", moduleNameResolver.ts nodeModuleNameResolverWorker) and TS6504 for JS files a resolution put in the
  program (node10AlternateResult_*, resolutionModeImportType1/TypeOnlyImport1(node10)).
- `declare const B = -1n` under a pre-ES2020 target lacks TS2737.
- **JS (seen in the 2026-09-28 corpus session)**: TS2565 for a property declared by a JS expression statement
  (`/** @type {F} */ C.prototype.m;`) read in the same flow container (tsc's getFlowTypeOfAccessExpression with
  assumeUninitialized: jsDocDeclarationEmitDoesNotUseNodeModulesPathWithoutError); TS7032 (a setter without a parameter
  type, `@type` on a setter being ignored) is absent entirely (probe plan19/webpack/jsdoc_accessor_type.ts).
- **Circularity** (plan 39 stage 3): circularOptionalityRemoval; TS2310 for cycles found while resolving
  (circularBaseTypes, circularConstraintYieldsAppropriateError).
- **Decorators**: TS2449 for a class used in the decorators of its own methods' parameters (tsc counts them; tttt's
  check only knows class and member decorators).
- **Classes**: TS2383 (overload export agreement); TS2386 across merged
  interface declarations (interface.c checks per body); TS2687 skips statics and method/accessor pairs. Abstract members satisfied by an interface merged into the class: tsc looks there
  before TS2515 (mergedInheritedMembersSatisfyAbstractBase). TS1094 (accessor type parameters).
- **Accessibility** (plan 13 leftovers): destructuring (`const { priv } = a`, `({ priv } = a)`:
  privateProtectedMembersAreNotAccessibleDestructuring: protected members); TS2704 (`delete` of a
  readonly property); `this.x = v` in the constructor of a class merged with an interface declaring `readonly x`
  (tttt exempts any property whose parent is the class); `globalThis.globalThis = x` (TS2540). Private names: TS18015,
  TS2806 (set-only private accessor), TS2804/TS18010/TS2300/TS2717 duplicates (privateNameDuplicateField).
- **Declarations**: `export function g() {}` + `function g() {}` is TS2395 here, TS2393 in tsc (re-probed). TS2300
  for a second `export =`. TS1319 (default export outside a module, TODO in `scope_export_default_expr`). TS1330-1335
  (misplaced `unique symbol`, silently `symbol`). TS2475 (const enum used as a value). TS18055/TS18056 (isolatedModules enum string/initializer rules; the enum
  member value checks are in `enum_decl_typecheck`).
- **Operators**: `void` operands/objects (UndefinedFacts in tsc: `v.x` with `v: void` is TS18048) aren't checked like
  undefined. `for (x in o)` with a non-declaration target: TS2322 here, TS2405 in tsc
  (re-probed).
- **Type predicates**: TS1228 (predicate in a constructor type, accessor, parameter type), TS2775/TS2776 (assertion
  callee without explicit types), TS1225 (no such parameter), a predicate function returning non-boolean.
- **Implicit any**: widening errors past `report_errors_from_widening` (binding.c; plan 37 A stage 2: TS7031 on
  binding elements, TS7010/7011/7025 on widened inferred returns, TS7008 class fields, TS7018 through a spread) and of
  generator yield/next types; TS7023 through indirect self-references in return expressions
  (thisInObjectLiterals, trivialSubtypeReductionNoStructuralCheck);
  TS7031/TS7006 from widening of binding pattern defaults (`function
  f([a1] = [undefined])`, noImplicitAnyDestructuring*);
  TS7051 (a parameter named like a type); TS7041 (`this` captured by a top-level arrow); TS7017 (`typeof globalThis` has
  no index signature); TS7055/TS7057 (generators).
- **Target/module-gated**: TS2705/TS2468 (async function needs the `Promise` constructor): tsc reports it from
  createPromiseReturnType, ie only when something asks for an async function's *inferred* return type, which is lazy; reporting it for every
  unannotated async function was tried and lost 6 tests. Needs a "return type was inferred" signal that matches tsc's
  demand, not tttt's (tttt infers more eagerly). Computing function expressions' return types when checked (triage 2026-09-29,
  wt-c bf163ec4/d4805f83, gains destructuringControlFlowNoCrash) was reverted: with getters included it put 6 TS7023
  in zod's recursive getter schemas (tttt's getter circularity differs from tsc's, plan 39), and eager return types
  aren't tsc's model anyway. operationsAvailableOnPromisedType also wants TS2773 ("Did you forget to use 'await'?") on
  operators, calls, property access and iteration. TS2802 (iteration needs `downlevelIteration`), TS1056 (accessors below
  ES5), TS2659 (`super` in an object literal below ES2015), TS2496's other places (`arguments` in ES5 arrows), TS2815
  (`arguments` of an outer function in a field initializer, or in an expression statement directly in a static block:
  tsc's isInPropertyInitializerOrClassStaticBlock; classStaticBlock6, which also wants TS2662 for `arguments` there). Await grammar leftovers: `await` in a
  namespace body counts as top level (tsc: TS1308), TS18037 (await in a static block) is parser territory
  (classStaticBlock6/7/26).
- **TS2538 with `any`**: `any[[]]` (a tuple as an index type on `any`): tsc's getPropertyTypeForIndexType reports past
  the `any` object shortcut (the `{[z]: x}` pattern cases pass now).
- **Iteration**: TS2489/TS2490 (iterator without `next`/`value`), TS2763-2765 (iterator next-type mismatches);
  generator next types aren't inferred from `yield` uses (tsc's `nextTypes`).
- **Modules**: exports in non-module containers dropped silently (`scope_add_port()`, TODO TS1194); `import { type
  require }` isn't type-only for TS2441; TS1361 through `import x = types.A` of a type-only namespace (importEquals1);
  typeOnlyMerge3 (TS1362 through an import merged with a namespace after TS2440); mergeSymbolRexportFunction (TS2451
  path); TS2497 (default/namespace import of an `export =` module without esModuleInterop); TS2686 from `typeof Foo` in a
  type (expressions are done); TS2664/TS2671 (module augmentation errors); duplicatePackage_globalMerge prints
  TS2306 paths as `/node_modules/...` (tsc `/tests/node_modules/...`); a nested `declare module "m"` gets TS2435 but is
  still bound as a global ambient module (tsc: a local namespace). TS2846 (import of a `.d.ts` by name),
  TS2877/TS2876 (`rewriteRelativeImportExtensions`), TS6046 (unknown lib name), TS6278 and TS7016's "npm i @types" elaboration (need package ids).
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
- **Errors inside lib files** under `// @skipDefaultLibCheck: false` (4 tests: duplicateNumericIndexers,
  objectTypeWithStringIndexerHidingObjectIndexer, objectTypeHidingMembersOfExtendedObject,
  variableDeclarationInStrictMode1): tttt never checks lib files and `error_ctx_error_s` drops what lands in one
  (binder TS2300 for a global `eval` redeclared, TS2374/TS2411 from checking the lib's `interface Object`/`Number`
  declaration merged with the test's). tsc's CLI default checks lib files (the harness sets skipDefaultLibCheck), so
  this is the harness-vs-CLI gap and a perf question: checking only lib declarations whose symbol merges with a
  user declaration (gated on the option) would be the cheap port. Catch (triage 2026-09-29): with the lib checked
  first, tsc also reports TS2411 in *user* interfaces it otherwise accepts (`interface Foo { a: number; [x: string]:
  Object }` next to `interface Object { data: A; [x: string]: Object }`), an order artifact of relating to
  `Object` while it is being checked; objectTypeHidingMembersOfExtendedObject's user-file error is that. So the
  check order (lib declarations before the user's) would have to be tsc's too. Low value (4 tests), routed here.

- TS2507 "Type 'X' is not a constructor function type" for an `extends` expression is missing (only `new` reports
  it), and so is its related info for a type parameter (TS2735 "Did you mean for 'T' to be constrained to type
  'new (...args: any[]) => ...'?"). Plan 32 leftover.

- Override and variance checks exist now (triage 2026-09-28); left: JSDoc `@template in/out/const` grammar errors
  (jsdocTemplateTag8), varianceAnnotationsWithCircularlyReferencesError (tsc's recovery for `type T1<in in>`).

## Module and import checks (triage 2026-10-05: default imports, isolatedModules/verbatimModuleSyntax done)
- TS2866 (an import shadowing a global value the file uses as a value, under isolatedModules:
  isolatedModulesShadowGlobalTypeNotValue isolatedModules=true): tsc's resolveName side, not checkAliasSymbol.
- TS1280/TS1281 (instantiated namespaces and enums of global scripts under isolatedModules, cross-file unqualified
  member references): isolatedModulesGlobalNamespacesAndEnums. TS1280 belongs in `namespace_decl_typecheck()`.
- nodeModulesJson: TS1543/TS1544 (JSON imports under node18/nodenext need `with { type: "json" }`, only a default
  import). typeOnlyESMImportFromCJS(module=node18): TS1541/TS1542 (type-only imports of ESM from CJS need
  `resolution-mode`) where tttt reports TS1479.
- The jsxImportSource runtime import (`jsx_runtime_module()`) reports TS2875/TS2792 and now TS1479/TS1471, not the
  other resolveExternalModule diagnostics (TS7016 for a JavaScript runtime).

## JavaScript binder leftovers (suite checker bugs 5, 2026-09-28)
Spurious diagnostics in JS tests whose causes were investigated but not fixed:
- **Class members aren't binder symbols**, so `Clazz.method.prop = 5` (a static method as an expando host, tsc's
  bindPotentiallyMissingNamespaces giving the method symbol a namespace declaration) can't declare prop:
  jsDeclarationsClassStaticMethodAugmentation (TS2339 on `() => void`).
- **Chained expando initializers** `var G = m.G = {}` (typeFromPropertyAssignment18): tsc types G as `typeof m.G` and
  merges the exports of the variable and of the member (getTypeOfFuncClassEnumModule -> getSymbolOfExpando ->
  mergeJSSymbols), so `G.q` (declared on m.G) and `m.G.p` (declared on G) resolve.
  The hosts share one symbol in tsc (the initializer's), so an approach is one exports table for both, and the
  variable's type being the member's (`typeof m.GLOBSTAR`).
- **Entity-name CommonJS exports aren't aliases**: tsc's exportAssignmentIsAlias makes `exports.blah =
  exports.someProp` an alias (SymbolFlags.Alias), whose resolution under a `module.exports = function` reaches itself
  through the function's expando exports: TS2303 "Circular definition of import alias 'blah'" at `exports.blah`
  (pushTypeGetTypeOfAlias). tttt binds a property of type any; only lower.c's JSDoc value references follow such an
  assignment to what it names. Making these real SYMBOL_ALIAS symbols would give that and the alias semantics.
- **CommonJS exports inside functions**: tsc binds `exports.x = v` / `module.exports = v` from any container
  (bindExportsPropertyAssignment has no container check); `add_nested_commonjs_exports()` only walks top-level
  statement lists, so an IIFE's exports are missing. Needs a declare-step walk into function
  bodies (the link step must see them, so the resolve walk is too late).
- **`@typedef` inside a class body** (jsdocTemplateClass): tsc binds it in the class (blockScopeContainer = the class,
  getEnclosingBlockScopeContainer of the host), where the class's `@template T` is visible; tttt's
  `jsdoc_bind_comments()` only lets statement lists claim comments, so the file claims it and `T` is TS2304.
- **`Object.defineProperty(o, "p", {...})`** declares nothing (tsc's bindObjectDefinePropertyAssignment,
  isBindableObjectDefinePropertyCall); 32 tests mention defineProperty.
- **`this.x = v` at the top level of a CommonJS module** should declare an export (bindThisPropertyAssignment, SourceFile
  container with commonJsModuleIndicator); only the script (global) case is done (`add_global_this_chain()`).

## Wrong codes that are type-core bugs
Spurious TS2403 ("Subsequent variable declarations must have the same type") means tttt computed two spellings of one
type differently; each is one type-core bug, good one-per-session material (all re-checked failing 2026-09-27):
mappedTypeModifiers/mappedTypes4 (`Required<>`/`-?` identity), objectLiteralGettersAndSetters (the rest pass since).

## JSX (`./failstats.py --jsx`)
Overloaded components don't get tsc's TS2769 report (tsxStatelessFunctionComponentsWithTypeArguments4); TS6229 isn't
checked for union tag types or inside an overload chain; TS2604 on a union of tag types (tsxDynamicTagName3).

## Elaboration (which member or level a relation error descends to; plan 34 is the text of the rest)
- Into a `NoInfer` member: against `NoInfer<{ type: 'A' }> | Expr` tsc elaborates to `'"B"' is not assignable to
  '"A"'`, tttt reports the whole argument (best_matching_type doesn't look through the substitution). tttt also adds
  TS6500 related info on `NoInfer<{...}>` targets, tsc doesn't.
- Union sources: for `Snap<...>` (a union alias) not assignable to `number`, tsc adds a line for the union's first
  member; tttt omits it.
- xstate meta.test.ts 270/434: elaboration stops one level too high (at `e2:`, tsc goes on to the `meta:` under the
  `@ts-expect-error`). Not reduced.
- mappedTypes6: `-?` template elaboration (tsc's NonNullable<T[P]> display and chain).

