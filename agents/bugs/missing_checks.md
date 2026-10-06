# Missing and wrong checks (the suite's long tail)
The work queue for "suite checker bugs" sessions: diagnostics tttt doesn't report, or reports under another code, and
the failing tests behind them. Find fresh buckets with `./failstats.py` (single-code tests: errors fail on one code
alone in `build/failstats/tests.tsv`) and `./unimplemented_codes.sh` (codes `src/` never mentions). Every test named
here was failing on 2026-10-05 (suite 13098; harness-only causes are in `harness.md`, type-core causes in
`type_core.md`).

## Buckets (2026-10-05, suite 13098)
Errors component: 146 failing (43 both missing and spurious, 29 only missing, 27 same codes and positions with other
text, 22 where tsc reports errors and tttt none, 13 only spurious, 12 where tttt reports errors tsc doesn't).
Single-code tests (61 in all): missing TS2322 5, missing TS2304 3, missing TS2589 3, missing TS1005 3, missing TS2344
2, missing TS2303 2, misplaced TS2304 2, missing TS2590 2, spurious TS2339 2, then single tests.

## Known missing checks
- TS4109/TS4110 (type arguments circularly referencing themselves: circularlyReferentialInterfaceAccessNoCrash,
  circularTypeArgumentsLocalAndOuterNoCrash1).
- `interface I<C> { a: StateNode<C, any> }` where StateNode requires `C extends MachineContext`: tsc's TS2344 on the
  member's type argument is missing (xstate).
- Missing TS2353 for `const x: Trans<E> = { NEXT: ... }` in a generic function with `Trans<E> = { [K in E['type'] |
  '*']?: ... }` (xstate; excess property checks against a generic mapped type).
- TS2712 (a dynamic `import()` in ES5 without a global `Promise` value, with TS2468): tsc reports it unconditionally in
  checkImportCallExpression (createPromiseReturnType).
- TS2344 on type references with generic arguments: one missing each in instantiationExpressions:163 and
  typeGuardFunctionErrors:152 (`U`, `T | "d"`): the HACK in `ty_check_type_reference_constraints` skips them.
- `declare const B = -1n` under a pre-ES2020 target lacks TS2737.
- TS2507 "Type 'X' is not a constructor function type" for an `extends` expression (only `new` reports it), and its
  related TS2735 for a type parameter ("Did you mean for 'T' to be constrained to type 'new (...args: any[]) =>
  ...'?"). Plan 32 leftover.
- **JS**: TS2565 for a property declared by a JS expression statement (`/** @type {F} */ C.prototype.m;`) read in the
  same flow container (tsc's getFlowTypeOfAccessExpression with assumeUninitialized); TS7032 for a setter whose type
  comes from `@type` (probe plan19/webpack/jsdoc_accessor_type.ts).
- **Decorators**: TS2449 for a class used in the decorators of its own methods' parameters (tsc counts them; tttt's
  check only knows class and member decorators).
- **Classes**: TS2386 across merged interface declarations (interface.c checks per body); TS2687 skips statics and
  method/accessor pairs (hacks.md).
- **Accessibility**: `this.x = v` in the constructor of a class merged with an interface declaring `readonly x` (tttt
  exempts any property whose parent is the class); `globalThis.globalThis = x` (TS2540). Private names: TS18015,
  duplicates across kinds (TS2804/TS18010/TS2300/TS2717: privateNameDuplicateField).
- **Declarations**: `export function g() {}` + `function g() {}` is TS2395 here, TS2393 in tsc. TS2300 for a second
  `export =`.
- **Operators**: `void` operands/objects (UndefinedFacts in tsc: `v.x` with `v: void` is TS18048) aren't checked like
  undefined. `for (x in o)` with a non-declaration target: TS2322 here, TS2405 in tsc.
- **Type predicates**: TS1228 (predicate in a constructor type, accessor, parameter type), TS2775/TS2776 (assertion
  callee without explicit types: assertionTypePredicates1), a predicate function returning non-boolean.
- **Implicit any**: widening errors past `report_errors_from_widening` (binding.c; plan 37 A stage 2: TS7010/7011/7025
  on widened inferred returns, TS7008 class fields, TS7018 through a spread) and of generator yield/next types; TS7017
  (`typeof globalThis` has no index signature).
- **Target-gated**: TS2705 comes only from an inferred return type's demand (tsc's createPromiseReturnType), and tttt
  infers more eagerly than tsc, so the two can disagree; TS1056 (accessors below ES5); `await` in a namespace body
  counts as top level (tsc: TS1308).
- **TS2538 with `any`**: `any[[]]` (a tuple as an index type on `any`): tsc's getPropertyTypeForIndexType reports past
  the `any` object shortcut.
- **Iteration**: TS2489/TS2490 (iterator without `next`/`value`), TS2763-2765 (iterator next-type mismatches);
  generator next types aren't inferred from `yield` uses (tsc's `nextTypes`).
- **Modules**: exports in non-module containers dropped silently (`scope_add_port()`, TODO TS1194); `import { type
  require }` isn't type-only for TS2441; duplicatePackage_globalMerge prints TS2306 paths as `/node_modules/...` (tsc `/tests/node_modules/...`); a nested
  `declare module "m"` gets TS2435 but is still bound as a global ambient module (tsc: a local namespace).
- **TS2303**: pushTypeGetTypeOfAlias (`exports.blah = exports.someProp`, see JavaScript below; the TS2303 there is
  getTypeOfAlias's type circularity through the module's type, not alias resolution).
- **Templates**: `${symbol}` not rejected, TS2590 at the type node (templateLiteralTypes1/3; reporting it from
  `intersect_unions()`/`ty_template_literal()` at the current node put it at the wrong node and fired where tsc
  doesn't).
- **Misc**: TS2353 for an object literal assigned to an assignment pattern (`({ a } = { a, b })`); TS2561/TS2820 want
  the spelling suggestion (`spelling_suggestion()` exists); TS2551 suggestions don't filter inaccessible members;
  TS2793 only for function declarations, not overloaded methods/constructors (`add_implementation_success`); TS2563
  (flow depth 2000 silently gives the error type: largeControlFlowGraph); TS2406 (`for (#x in o)` says TS1451).
- **Bigint property names**: TS1539 and `{ 1n: x }` as the property "1n" (TS2538 for bigint keys): bigintPropertyName,
  which also has a parse failure.
- doYouNeedToChangeYourTargetLibraryES2016Plus: spurious TS1503 (regex flags checked against the target where tsc
  doesn't there), a few TS2550 missing.
- **Regular expression patterns** (TS1487, TS1508, TS1535, TS1530, TS1517, TS1518, TS1507...): tsc's
  scanRegularExpressionWorker, ~1000 lines; only the flags are checked (`regex_check_flags`). Plan 27 stage 3.
- **Variance annotations**: varianceAnnotationsWithCircularlyReferencesError (tsc's recovery for `type T1<in in>`).
- duplicatePackage(_withErrors): the second copy of a package (same name+version) is still parsed and checked (tsc
  makes it a redirect to the first copy's AST), and the default-exported class prints `X`, not `default`, in
  `import("...").default` (display).
- nodeModulesJson (node16/node18/nodenext): symbols and types of JSON imports.
- The jsxImportSource runtime import (`jsx_runtime_module()`) reports TS2875/TS2792 and TS1479/TS1471, not the other
  resolveExternalModule diagnostics (TS7016 for a JavaScript runtime).
- declarationEmitBindingPatternsUnused: an interface setter `set x({ name: alias }: Named & { o: typeof alias })`
  keeps only its parameter's type (interface_member.h: `setter_param`), so its binding pattern gets no TS2842 and
  `typeof alias` in the parameter type is TS2304. Needs the setter's parameter as a parameter.
- `while (a) const x = 1;` still scopes x to the loop (tsc: the enclosing block).

## Circularity tsc finds and tttt doesn't (plan 39 stage 3)
- TS2310/TS2313 for cycles found while resolving: circularBaseTypes (TS2313 for a mapped type's `K in keyof T` with T
  the interface whose base is being resolved), circularConstraintYieldsAppropriateError (TS2310).
- A parameter's default reaching the parameter through a call's return type (`(n: number | undefined = [1].map(() =>
  n)[0])`: TS2502 and TS7024 in tsc, nothing here; the direct read is done in `parameter_initial_type()`).
- limitDeepInstantiations (TS2589 at the reference, we say TS2502), circularInlineMappedGenericTupleTypeNoCrash
  (TS2589), recursiveMappedTypes (TS2502/TS2615 in a mapped type over keyof).
- exportAssignmentImportMergeNoCrash (`import Obj from "./a"; export const Obj = void Obj`: tsc's merged local symbol
  makes the `Obj` read the exported const, TS2448 and `any`; tttt reads the import),
  ensureNoCrashExportAssignmentDefineProperrtyPotentialMerge.
- Resolution-order artifacts tsc reproduces on purpose: mixinWithBaseDependingOnSelfNoCrash1 (TS2345: BaseItem has no
  construct signature while Item's base resolves), selfReferentialDefaultNoStackOverflow (TS7022 on `export default`
  reaching itself through a self-import: the default export's type has no RESOLVING_DECLARATION frame),
  functionImplementations (TS2403: `f()` inside f's own return makes tsc's return type Base, not any).
- yieldExpressionInControlFlow: `o = yield* o` with an evolving `[]` isn't found circular (TS7034/TS7005).

## Limits
- excessivelyLargeTupleSpread (TS2799 for a tuple type alias body: TypeNodes have no span, so no current node).
- TS2589: recursiveConditionalCrash4, circularIndexedAccessErrors, recursivelyExpandingUnionNoStackoverflow; spurious
  in infiniteConstraints.
- normalizedIntersectionTooComplex (TS2590).
- relationComplexityError (TS2859: tsc counts each intersection-member/literal pair as a recursive relation, 20002 x
  10002 pairs; tttt short-circuits them).

## Relations and type core (more in `type_core.md`)
- mappedTypeRelationships (tsc substitutes `Partial<T>[K]` with `T[K] | undefined`, and TS2542 for writes through a
  `Readonly<T>` index), mappedTypeConstraints2 (indexed accesses of mapped types with template-literal keys: spurious
  TS2536), mappedTypeErrors (TS2339 on `Pick<T, K>.foo`: `check_access()` skips generic mapped types),
  controlFlowGenericTypes (`Box<T>` vs `Box<unknown>` display, a TS2536 at line 218), strictBindCallApply1
  (bind/call/apply inference with the `this` parameter: TS2684 instead of TS2345), variadicTuples2 (spread arguments
  against variadic rest tuples).
  controlFlowGenericTypes (`Box<T>` vs `Box<unknown>` display, a TS2536 at line 218), variadicTuples2 (spread arguments
  against variadic rest tuples), interfaceExtendsObjectIntersectionErrors (heritage errors name the alias, not the
  resolved base).
- deeplyNestedMappedTypes (`Id2<T> = { [K in keyof T]: Id2<Id2<T[K]>> }` still ends in a Maybe),
  reactReduxLikeDeferredInferenceAllowsAssignment and
  circularlyConstrainedMappedTypeContainingConditionalNoInfiniteInstantiationDepth (TS2344 with long elaborations),
  mergedDeclarations7 (tsc relates the imported interface with an uninstantiated `this`), awaitedTypeNoLib.
- distributiveConditionalTypeConstraints (constraint of a distributive conditional type is `boolean`, we print
  `IsArray<T>`), mappedTypeNotMistakenlyHomomorphic (Gen2<ABC.A> vs B relation), mappedTypeRecursiveInference (we hit
  TS2589), inferFromGenericFunctionReturnTypes3, baseConstraintOfDecorator, contextuallyTypedParametersOptionalInJSDoc
  (JSDoc optional parameter contextual typing).
- tsxUnionElementType3/4: the props intersection prints `{ children?: ReactNode; }` once where tsc has it twice.
  Mechanism (2026-10-06): react.d.ts's `props: P & { children?: ReactNode }` is in `class Component<P, S>`, which is
  merged with `interface Component<P, S>`; tsc's isTypeParameterPossiblyReferenced says yes for any type parameter
  whose symbol has more than one declaration (the merged P, S and the class's `this`), so the type literal is
  instantiated per instance (RC1's and RC2's are two types). tttt's collect_free_params() is purely syntactic. A fix
  needs the owner's merged type parameters at ty_anonymous_from_node() (LowerCtx carries no owner).

## Inference
contravariantOnlyInferenceFromAnnotatedFunction (A from an annotated arrow inside a reverse mapped type's source: tsc
infers string, tttt unknown; mechanism not found), tsxStatelessFunctionComponentsWithTypeArguments4 (overloaded components don't get
tsc's TS2769 report), intraExpressionInferencesJsx, unionAndIntersectionInference3, mappedTypeRecursiveInference2,
variadicTuples1, genericCallWithGenericSignatureArguments3 (`(n: Object) => 1` returned where the contextual type is
the outer call's U keeps its literal return type in tsc, `=> 1`; tttt widens to number).
genericRestParameters3, genericCallWithGenericSignatureArguments3 (not investigated).

## Local vs export symbol split
tsc keeps one local symbol per block for a name's exported and local declarations, tttt two symbols
(function_local_symbol_decls() and check_export_merge() patch over it; hacks.md "Exported and local declarations").
Still wrong: multipleExports (`export {x}` in a namespace whose other block exports x: TS2484),
conflictingDeclarationsImportFromNamespace1/2 (`import * as pick` + `export const pick` merge, tsc's TS7023
circularity), jsExportMemberMergedWithModuleAugmentation2 (duplicates across an import and a declaration: TS2300 vs
our TS2671; checkerInitializationCrash's symbols: tsc keeps the two conflicting cross-file symbols apart), overloadModifiersMustAgree/functionOverloadErrors types (tsc's local
symbol has all of a block's overloads).

## JavaScript (plan 26)
Binder gaps (spurious TS2339 and friends):
- **Class members aren't binder symbols**, so `Clazz.method.prop = 5` (a static method as an expando host, tsc's
  bindPotentiallyMissingNamespaces giving the method symbol a namespace declaration) can't declare prop:
  jsDeclarationsClassStaticMethodAugmentation (TS2339 on `() => void`).
- **Chained expando initializers** `var G = m.G = {}` (typeFromPropertyAssignment18): tsc types G as `typeof m.G` and
  merges the exports of the variable and of the member (getTypeOfFuncClassEnumModule -> getSymbolOfExpando ->
  mergeJSSymbols), so `G.q` (declared on m.G) and `m.G.p` (declared on G) resolve. An approach: one exports table for
  both hosts, and the variable's type being the member's.
- **Entity-name CommonJS exports aren't aliases**: tsc's exportAssignmentIsAlias makes `exports.blah =
  exports.someProp` an alias (SymbolFlags.Alias), whose resolution under a `module.exports = function` reaches itself
  through the function's expando exports: TS2303 at `exports.blah` (pushTypeGetTypeOfAlias). tttt binds a property of
  type any. Likewise `exports.x = undefined` then `= a` (moduleExportDuplicateAlias, moduleExportDuplicateAlias3:
  every one an alias in tsc, TS2722; hacks.md "CommonJS").
- `exports.x =` next to `module.exports = {...}` (typedefCrossModule2: tsc merges them).
- **CommonJS exports inside functions**: tsc binds `exports.x = v` / `module.exports = v` from any container
  (bindExportsPropertyAssignment has no container check); `add_nested_commonjs_exports()` only walks top-level
  statement lists. Needs a declare-step walk into function bodies (the link step must see them).
- **`this.x = v` at the top level of a CommonJS module** should declare an export (bindThisPropertyAssignment,
  SourceFile container with commonJsModuleIndicator); only the script (global) case is done (`add_global_this_chain()`).
- The nested `@param {..} [config.additionalFiles]` binding type and TS7031 next to a JSDoc-typed sibling
  (destructuringParameterDeclaration9).
Other JS tests: jsDeclarationsImportAliasExposedWithinNamespace (a `@typedef ns.x` namespace merged with an exported
const: TS18042), assignmentToVoidZero2,
classCanExtendConstructorFunction, moduleExportWithExportPropertyAssignment4,
templateInsideCallback, fixSignatureCaching (an expando-heavy JS-like file),
jsContainerMergeTsDeclaration3 (a d.ts class merged with a JS container const),
const: TS18042), assignmentToVoidZero2, lateBoundAssignmentDeclarationSupport4, moduleExportWithExportPropertyAssignment4,
const: TS18042), lateBoundAssignmentDeclarationSupport4, moduleExportWithExportPropertyAssignment4,
typeFromJSInitializer, typeFromPropertyAssignment32, templateInsideCallback, overloadTag2 (constructor `@overload`s:
TS2394 and the arity of the merged signature), fixSignatureCaching (an expando-heavy JS-like file),
jsDocDeclarationEmitDoesNotUseNodeModulesPathWithoutError (the @ts-expect-error covers a declaration-emit error, plan
40). Declaration emit (plan 40): jsDeclarationsCrossfileMerge (TS6232),
jsDeclarationsExportAssignedConstructorFunctionWithSub (TS9005), jsDeclarationsTypeReassignmentFromDeclaration2
(TS9006).

## Parser recovery (plan 27 territory, each its own mechanism)
- Error dedupe order at the end of the file: tsc reports two errors at the same EOF position because another error
  came in between (labeledStatementDeclarationListInLoopNoCrash4: '}' expected + TS1160; jsxAndTypeAssertion: ':'
  and '</' expected); tttt's last-error dedupe drops the second.
- tsxAttributeInvalidNames: `<test1 32data={32} />` wants ';' expected and TS1351 at the same position (scanner
  error order).
- dontShowCompilerGeneratedMembers (`<-` in a type literal: a call signature whose type parameter list fails),
  esDecorators-classDeclaration-exportModifier.2 (decorators around `export`/`abstract`),
- reservedNamesInAliases (`type void = I` is both a TS2457 alias and an expression statement),
  parseUnmatchedTypeAssertion,
  jsdocFunctionTypeFalsePositive (`@param {<} x`), scannerUnexpectedNullCharacter1 (a NUL outside a string ends
  tttt's parse: `*p.pos` is the EOF test everywhere), parser0_004152, jsxInvalidEsprimaTestSuite,
  thisTypeInFunctionsNegative, manyCompilerErrorsInTheTwoFiles (scanner recovery after `#!` mid-line).
- A class member starting with `<` (`class D { <T>() {} }`): tsc says '=>' expected at the `{`, tttt TS2304/TS1109.

## JSX
- TS6229 isn't checked for union tag types or inside an overload chain.

## Messages: elaboration and printing (plan 34)
- Into a `NoInfer` member: against `NoInfer<{ type: 'A' }> | Expr` tsc elaborates to `'"B"' is not assignable to
  '"A"'`, tttt reports the whole argument (best_matching_type doesn't look through the substitution). tttt also adds
  TS6500 related info on `NoInfer<{...}>` targets, tsc doesn't.
- Union sources: for `Snap<...>` (a union alias) not assignable to `number`, tsc adds a line for the union's first
  member; tttt omits it.
- xstate meta.test.ts 270/434: elaboration stops one level too high (at `e2:`, tsc goes on to the `meta:` under the
  `@ts-expect-error`). Not reduced.
- Elaboration chains: mappedTypes6 (`-?` template elaboration), indexedAccessRelation, jsxExcessPropsAndAssignability
  (an intersection target `T["x"] & string` elaborated through the intersection), invariantGenericErrorElaboration,
  keyofAndIndexedAccessErrors, conditionalTypes1/2, bigintWithLib,
  recursiveConditionalTypes, mappedTypeInferenceErrors.
- **Node reuse in error messages**: tsc's typeToString reuses syntactic nodes even without an enclosing declaration
  (a single `return x as T` prints `() => T` as written; done only for a T that names nothing,
  `type_node_names_nothing()`; an arrow's return type from a JSDoc `@type`'s signature: propertyAssignmentUseParentType2). tttt reuses nodes only for the types baseline
  (`d.underline && d.enclosing`); error display would need the reuse path without name-accessibility checks.
- **Alias/origin preservation and literal order**: `keyof Props` printed as `"when" | "error"`
  (reactDefaultPropsInferenceSuccess); literal creation order puts `1 | 4` where tsc says `4 | 1`
  (assignmentCompatWithDiscriminatedUnion), and the omitted keys of `Omit<this, ...>` in another order
  (destructuringUnspreadableIntoRest).

## Declaration emit (plan 40)
arrayFakeFlatNoCrashInferenceDeclarations (TS5088), declarationEmitPrivatePromiseLikeInterface (TS7056).
