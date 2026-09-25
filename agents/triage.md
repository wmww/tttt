# Triage
Uninvestigated items. Triage sessions drain this. Last drained 2026-09-23 (triage after plan 13): the older checker
items were re-probed then; fixed ones are gone, "(re-probed)" marks ones confirmed open with the current symptom.
Plan 15 (missing checks) is retired: `./unimplemented_codes.sh` still lists the codes `src/` never mentions, and
"Missing checks after plan 15" below has the tail worth knowing about.

## Category-sized (worth a plan or a whole session)
- **JS files: JSDoc types, CommonJS**. `.js` files load only with `allowJs`, semantic diagnostics in them only with
  `checkJs`/`@ts-check` (`SourceFile.unchecked`, `error_ctx_remove_unchecked`), so ~900 `@allowJs` tests mostly fail
  *quietly*: they need JSDoc types (`@param`, `@type`, `@typedef`), `module.exports` / `require` as declarations,
  expando assignments (`f.x = 1`, `this.x = 1`, prototype assignments), TS2306 on CommonJS files that aren't modules.
  Needs its own plan; JSDoc is big. `check_access()` still lets JS files through. It's also most of the spurious
  TS2304, the biggest spurious code (2666 in 628 tests, 135 tests fail their errors on it alone; 2026-09-23): the
  names tttt can't find are `module` (116 tests, 94 of them in .js files), `require` (70), `exports` (28); then JSX
  names (`div`, `className`) and keywords after bad recovery (`with`, `await`, `yield`, `public`).
- **Syntax gaps** (after plan 14: ~700 tests still have a spurious syntax error, `./failstats.py --syntax`): JSX
  (plan 22; `./failstats.py --jsx`); recovery fidelity shapes (`''`, `'};'`,
  `'{'`, `'];'`), among them parameter-list recovery after `a = yield =>` (tsc: "',' expected" and the function
  survives; tttt loses the function: FunctionDeclaration10_es6 & ~27 more) and malformed import types
  (nodeModulesImport*DeclarationEmitErrors). Private names outside classes (object literals, interfaces, parameters,
  enums: tsc parses them and reports TS18016/TS1451; `--syntax -f '^privateName.*'`). A bare `super` (TS1034).
  Recovery fidelity matters since the harness typechecks next to parse errors like tsc: parserGreaterThanTokenAmbiguity12-19
  (`1 >> = 2` parses as an assignment and gets TS2364), restParamModifier (TS1014 on top of tsc's error). Plan 14
  leftovers: the import attributes' type against the global `ImportAttributes` (TS2322), `new.target`'s symbol in
  anonymous functions (`(Anonymous function)`) and an error type as a computed key (should act like any:
  invalidNewTarget types), TS2339/TS2488 on destructuring `null` (`const {a} = null`, not only `using`), TS1432
  top-level `for await`. Older: TS1161 for an unterminated regex (`parse_regex` just fails), TS1263/1264 misplaced `let
  x!`, `const c;` in ambient files, `override`/TS4112+, overloads of `*()`-style nameless members, TS2303 "Circular
  definition of import alias".
- **Binding patterns as types** (re-probed; tsc's getTypeFromBindingPattern, checkArrayLiteral inDestructuringPattern,
  checkObjectLiteral's contextualTypeHasPattern): a binding pattern isn't the contextual type of its initializer (`var
  [, , [, b]] = [3, 5, [0, 1]]` is a spurious TS2461 at ES5, declarationEmitDestructuringArrayPattern5; `let [a = 1, b]
  = [undefined, "s"]` gives `a: string | number`, tsc `number`); an object literal initializer of a pattern should get
  the pattern's defaulted properties as optional ones (`function f({ x, y = 0 } = { x: 0 })` takes `{ x: number; y?:
  number }`, so `f({ x: 1, y: 1 })` is a spurious TS2353: destructuringWithLiteralInitializers; `literalTypes2` f6), and
  pattern properties the literal lacks are TS2525; an array literal that is a destructuring assignment target types as
  an array instead of a tuple of the targets; unannotated destructured parameters don't get the pattern's type
  (`({ a = 1 }) => a` is `{ a?: number }` in tsc, `any` here); object rest (`{ a, ...rest }`) is the whole object
  instead of getRestType(); computed keys in patterns miss their symbol lines (probe `binding_pattern_display.ts`).
  Worth a plan.
- **Substitution types** don't exist: in the true branch of `T[K] extends unknown[] ? (...args: T[K]) => void : never`
  tsc knows `T[K]` is an array (getSubstitutionType / getImpliedConstraint). Instances: @types/node events.d.ts (x4,
  hidden by skipLibCheck), stringMappingReduction (`EMap[C]` in `C extends Keys ? EMap[C] : ...` needs `C & Keys`:
  spurious TS2345, re-probed), numericStringLiteralTypes (spurious TS2370 on `...values: UnwrapContainers<T>`, a
  homomorphic mapped type over a generic tuple as a rest parameter type: maybe the same). Needs a Ty kind or a flag on
  indexed/type-param references made in a conditional's true branch.
- `this` leftovers: TS2526 for `this` in static property initializers (`static y = <this>undefined`) and a few other
  contexts (thisTypeErrors, 7 missing); TS7041 (`this` captured by a top-level arrow); TS7017 (`typeof globalThis` has
  no index signature); `ThisType<T>` arguments aren't instantiated with the inference in
  progress (Vue-style `defineComponent` options).
- **Target-gated diagnostics** (`compiler_options()->target` exists): TS2705/TS2468 (async needs `Promise`),
  TS1375/TS1378 (top-level await), TS2802 (iteration needs `downlevelIteration`), TS1056 (accessors, ES3), TS2659
  (`super` in an object literal below ES2015).
- **Evolving arrays** (tsc's autoArrayType: `let x = []` / `x = []` on an auto-typed `let x;`, then `x.push(v)`,
  `x[i] = v` grow the element type; needs FLOW_ARRAY_MUTATION nodes in the builder). Auto types (`let x;`) exist, the
  array half is `any[]` (hacks.md "Narrowing"). controlFlowArrays and friends.

## Checker gaps
- `in`/`out` variance annotations on type parameters are parsed and dropped (`GenericItem` has no field): tsc takes the
  variance from them instead of measuring (`getVariancesWorker`), and checks them (TS2636/TS2637). Small now that
  `measure_variances` is shared by declared types and aliases.
- valibot `actions/args/args.ts:43` (and argsAsync): spurious TS2370 on `(...args: InferInput<TSchema>) => ...` with
  `TSchema extends BaseSchema<unknown[], unknown[], ...>`: `ty_extends_array` doesn't see through the constraint of
  `NonNullable<TSchema['~types']>['input']` to `unknown[]`. Probe it before assuming where it breaks.
### Inference
- `silentNeverPropagation` (re-probed: spurious TS2345, `'state'` missing): `convert(createModule({ a: 12 }, { foo()
  { return true } }))` infers the inner TActions from the return type context `ModuleWithState<{a}> & {foo():
  boolean}` and fixes it before the method's own candidate is seen. tsc only fixes what a contextual *signature's
  parameters* mention (the method has none).
- **Context-free type of return-sensitive functions** (badInferenceLowerPriorityThanGoodInference, re-probed: spurious
  TS18046): tsc's first round types an argument like `() => ({ a: 1, b: x => {} })` (no sensitive parameters,
  sensitive through its returned expression) as a return-only signature whose return type is checked with
  `SkipContextSensitive` (nested sensitive functions are the non-inferrable `anyFunctionType`, nothing memoised:
  `links.contextFreeType`), and infers from that. tttt skips the whole argument in pass 1, so `a`'s candidate arrives
  only in pass 2, after `b`'s `x` is typed `unknown`. Needs a "skip context sensitive" check mode.
- `unknown` where tsc infers a type (~20 TS18046 in the corpus; suite: intraExpressionInferences,
  inferFromGenericFunctionReturnTypes2, mappedTypeRecursiveInference, reverseMappedUnionInference, promiseChaining).
- Exposed by plan 12's excess property checks, each a spurious TS2353 over a wrong inferred type (re-probed):
  mappedTypeMultiInference (`K` of `...cssSets: { [P in K]?: Style }[]` inferred from the first rest argument only),
  typeParameterConstModifiersWithIntersection (`const TConfig extends Config<T1>` inferred as its constraint through
  an intersection parameter `{ produceThing: T1 } & TConfig`).
- Reverse mapped types (plan 08) are eager objects, no arrays/tuples, cut at depth 4 (`reverse_mapped_type`); tsc's are
  lazy. Contextual types from a generic mapped type substitute the whole constraint for K, not the property's key.
- Contextual signature of a type with several call signatures (tsc `getIntersectedSignatures`;
  signatureCombiningRestParameters2). Arrow functions in an object literal whose contextual type is an interface with
  optional/overloaded methods didn't get their parameters typed (852 TS7006 in the tsc corpus once, the biggest
  category then: `const checker: TypeChecker = { getFalseType: (fresh?) => ... }`); 107 tests have a spurious TS7006
  in the suite now.
- promiseTry (re-probed: 4 spurious TS2578): `Promise.try(cb, ...args)` with `U extends unknown[]` inferred from both
  the callback's parameters and the rest arguments gets no TS2554 arity error. The corpora have more TS2578-exposed
  missing checks (valibot: TS2352 on `dataset as SuccessDataset<TOutput>`, writes to readonly `schema['~run']`;
  type-fest: `EmptyObject` assignments, abstract members missing in `class X extends withBar(Bar) {}`): `grep -A3 TS2578
  /tmp/realworld.txt` after `./realworld.py --corpus X`.
- Exposed by TS2339 on `never` (2026-09-23): contextualOverloadListFromArrayUnion (`(never[] | string[]).map(item =>
  item.length)`: `item` is never, tsc takes the union signature's parameter `string`).
- Older tsc-corpus findings, from before the corpus went clean (worth a probe each): `visitNode(node, visitor,
  isExpression)` resolving to the overload without the predicate (`test/probes/plan05/visitNodeInference.ts`);
  `CompilerOptions` -> `Pick<CompilerOptions, "target" | "module">`; literal widening in an object literal that has a
  contextual type.

### Relations, instantiation, reduction
- Tuple normalization: `[...unknown[], ...unknown[]]` stays a tuple type where tsc makes it `unknown[]` (two
  variable-length spreads). Now visible as TS2322 in valibot `looseTupleAsync.ts:92` and `tupleWithRestAsync.ts:124`
  (`SuccessDataset<unknown[]>` not assignable to `SuccessDataset<[...unknown[], ...unknown[]]>`), which Promise's
  bivariance used to hide.
- TS2430 leftovers now that it's on: interfaceExtendsObjectIntersectionErrors (3 missing), interfaceThatIndirectlyInherits
  FromItself (2 spurious, tsc reports the circularity instead), mappedTypesAndObjects, mergedInheritedMembersSatisfy
  AbstractBase (1 missing each). `./failstats.py --code TS2430`.
- mappedTypeNotMistakenlyHomomorphic (re-probed: spurious TS2353 x2, missing TS2741 x2): `keyof Gen<ABC.A>` misses `a`,
  the intersection with the conflicting-discriminant member isn't reduced before `keyof`. Related: conflicting
  discriminants and privates are reduced at construction (`has_conflicting_discriminants`), only for non-generic
  object members; tsc reduces lazily in getReducedType, so a generic intersection instantiated later isn't
  re-checked, and the "The intersection 'A & B' was reduced to 'never' because..." elaborations (6 tests) need the
  unreduced intersection. mixinAccessModifiers / mixinPrivateAndProtected: mixin intersections (`mixB<typeof
  A>.(Anonymous class) & A`) aren't reduced.
- contextualTypeShouldBeLiteral (re-probed): contextual types of union members are reduced (`string | "done"` is
  `string`, hacks.md), so the discriminant `value` widens and the wrong member is picked (spurious TS2353 + TS2322).
- genericMappedTypeAsClause (re-probed): `{ a, b }` against the generic `MappedModel<T>` is TS2322 where tsc says
  TS2353 (the excess check doesn't look at the resolved members of a generic mapped type); TS2312 missing.
- bigintPropertyName (re-probed): a parse failure in a.ts line 12 now, plus `{ 2n: x }` named `2n` (tsc `2`: spurious
  TS2353 and TS2538).
- genericClassExpressionInFunction (re-probed): `s.genericVar = 12` on an instance of a class extending a generic class
  expression is "not assignable to type 'V'" (the base class's type parameter isn't instantiated).
- `unionRelationshipCheckPasses` (re-probed: only the types baseline fails now): `{ foo?: number | undefined }` ->
  `{ foo?: undefined } | { foo: number }` needs tsc's discriminated-union decomposition of the source
  (`typeRelatedToDiscriminatedType`).
- Permissive instantiation for conditional types is missing (`ty_conditional_resolve` defers where tsc picks the
  false branch); a flat `TyMapper` can't say "every type parameter -> any".
- Circular aliases: `type N<T, K extends string> = T | { [P in K]: N<T, K> }[K]` is TS2589 + TS2615 in tsc
  (recursivelyExpandingUnionNoStackoverflow, re-probed), here it prints `N<number, "M">`. TS2456 is only a TODO in
  `ty_declared_type_of`.
- A homomorphic mapped type over a `satisfies`/const object doesn't resolve its keys (the `computedOptions` mapped
  type in the tsc corpus's `utilities.ts` printed with a huge `keyof "a" & keyof "b"` key).
- `expandoFunctionSymbolProperty` (re-probed): `f[symb] = ...` on a function declaration isn't an expando property,
  so `f` misses `[symb]` (spurious TS2741).
- mappedTypeAsClauseRelationships (re-probed): elaborates one line tsc doesn't ("Type 'T[(P extends string ?
  `bool${P}` : never) & keyof T]' is not assignable to type 'T[P]'") for `+?` with a remapping `as`.
- templateLiteralTypes1/3/4: TS1125/TS1121 for escapes in template literal types, TS2590 not reported, `${symbol}` not
  rejected.

### Classes, declarations, modules
- Plan 13 leftovers: destructuring (`const { priv } = a`, `({ priv } = a)`) doesn't check accessibility
  (privateProtectedMembersAreNotAccessibleDestructuring, destructuringAssignment_private); JSDoc
  `@private`/`@protected`/`@readonly`; TS2704 (`delete` of a readonly property); `this.x` in a constructor of a class
  merged with an interface declaring `readonly x` should error (we exempt any property whose parent symbol is the
  class); `globalThis.globalThis = x` is TS2540 in tsc (`typeof globalThis` is skipped). Private names: TS18015
  (reportUnmatchedProperty's private-name case), TS2806 (set-only private accessor), TS2804/TS18010/TS2300 duplicates
  (privateNameDuplicateField). TS2401/TS2376 (where `super()` must be with initialized properties or parameter
  properties).
- propertyAndAccessorWithSameName (re-probed: 7 missing TS2300): tsc's symbol merging when a property and accessor
  clash isn't mirrored. noSymbolForMergeCrash: `type A = {}` clashing with `interface A` gives 3 spurious TS2300 and
  misses TS2649.
- genericRecursiveImplicitConstructorErrors3 (re-probed): all TS2314 are missing there, a spurious TS2355 because the
  erroneous return annotation isn't `any` (tsc's errorType is any-like everywhere), and 4 spurious TS2339. A failed
  type-argument-arity call returns `any` where tsc still resolves a signature. `lower_identifier_internal` doesn't
  treat a bare `C` inside generic class `C<T>` as `C<T>` any more, but nothing reports tsc's TS2314 for it either.
- overrideLateBindableIndexSignature1 (re-probed): late bound index signatures work now; only TS4127 ("cannot have an
  'override' modifier because its name is dynamic") is missing.
- `for (x in o)` with a non-declaration target: tsc's TS2405 is TS2322 here (re-probed). TS2403 ("Subsequent variable
  declarations must have the same type", 156 in 68 tests) doesn't exist: `var z: number; var z: string;` passes.
- Type predicates: TS1228 (predicate in a constructor type, accessor, parameter type), TS2775/TS2776 (assertion callee
  without explicit types), TS1225 (no such parameter), `x is T` where the function returns a non-boolean (TS2322 on
  return): none exist.
- TS18050 ("The value 'null' cannot be used here", 680 missing in 38 tests) is what tsc says for null/undefined
  operands; `expr.c` operator checks use `type_extends_strict_nulls` + TS2365 instead. `u += 1` with `u: undefined` is
  TS18048 in tsc (checkNonNullType on `+` operands when neither is string-like; `check_non_null_type` in ty/check.c
  exists now). `compoundExponentiationAssignmentLHSIsValue`: `x **= v` with a non-reference LHS is TS2362 here, TS2364
  only in tsc. TS7005 for `declare var foo;` without a type (implicitAnyDeclareVariablesWithoutTypeAndInit).
- Missing TS2339 is ~380 in 166 tests: the gates in `check_access()` (JS, callables, mapped, unresolved bases),
  bracket access (TS7053), and whatever is typed `any` by failure. TS2550 instead of TS2339 for members of newer libs
  (doYouNeedToChangeYourTargetLibrary*: ~80 diagnostics): done in plan 15 (`lib_features.c`), re-probe.
- Module augmentation / `export =` merges don't add members (moduleAugmentationsImports3/4, augmentExportEquals6_1,
  umd-augmentation-3/4, moduleAugmentationDoes*MergeOfReexport). `aliasUsageIn*` (4 tests): the static side of a
  class reached through `import x = require()` vs another import reports a missing property.
- Plan 04 stage 1 leftovers: ~35 tests lost their symbols component then: `declare global` in odd places, `export {
  x }` next to an exported `x`, moduleAssignmentCompat1-4 (`Symbol(A)` without a Decl for a non-instantiated
  namespace used as a value). Declaration order in the symbols baseline when an import merges with a function
  (`test/probes/plan04/aliasMergesWithLocalValue.ts`: tsc binds function declarations first).
- Module resolution leftovers (`hacks.md` "Module resolution"): json modules as types (~35 tests), resolution-mode
  attributes (~20), `tryLoadInputFileForPath` (8), TS2846 (import of a `.d.ts` by name, 7), TS2877/TS2876
  (`rewriteRelativeImportExtensions`), Windows paths. `selfReferentialDefaultNoStackOverflow` wants TS7022 on `export
  default { mixins: [DefaultSpinner] }` importing itself. TS2688 for a type reference directive / `types` entry that
  resolves for us but not for tsc (typingsLookup3, tripleSlashTypesReferenceWithMissingExports,
  resolutionModeTripleSlash4).
- Diagnostics without a file that depend on emit paths: TS5055 (38 baselines, "would overwrite input file"), TS5056,
  TS6054 (unsupported extension, 7), TS6082/TS5053/TS5069 (outFile/sourceMap/declaration combinations), TS5101/TS5107
  deprecation warnings under `ignoreDeprecations`. With a tsconfig tsc puts option diagnostics on the option's span in
  the json; we skip them.

### Narrowing, flow, misc
- taggedPrimitiveNarrowing (exposed by TS2339 on `never`): `typeof hash !== "string"` then `throw` narrows `string & {
  __hash: true }` to never instead of keeping it.
- Spurious TS7022 on `const can = composite && o?.e && !out` when the type of `e` mentions an unresolved name (`Map`
  without lib es2015): some circularity through error types. Probe: test/probes/plan08/alias_optional_chain.ts minus
  its `@target`.
- `check_with()` (ty/expr.c) computes an expression with a contextual type without going through the LazyType, so it
  has no cycle detection (`let [x = x] = []` used to recurse forever; `lower_binding_access` avoids it, other
  contextual self-references can still overflow the stack).
- Plan 16 leftovers: TS2561/TS2820 want tsc's spelling suggestion, which exists now (`spelling_suggestion()`, plan
  15): 11 diagnostics in 5 tests. TS2793 ("would have succeeded against the implementation") only for function
  declarations, not overloaded methods/constructors (`add_implementation_success`). Destructuring assignment targets
  are checked as a whole, not property by property (for-of47/48: tsc's checkDestructuringAssignment reports at the
  default).
- `error.c` "Error added with invalid span" is ~64 lines per suite run (TS2511 on a lib class in 15 tests, names found
  in a `_` of some lib). `grep build/failstats/stderr.txt`. Also one "Declaration B acquired multiple times".

- Duplicate type parameters `<A, A>` are one merged symbol in tsc's binder (the checker reports TS2300 on the second)
  but two in tttt's (`scope_init_declaration`), so the types baseline renames the second `A_1` where tsc prints `<A,
  A>` (duplicateTypeParameters3, lost types in plan 21 stage 5). Merging them in the binder is the tsc shape; check what
  it does to TS2300 and to the generic's type parameter list.
- `export default class {}` prints `typeof default` from other files where tsc names the alias that imports it
  (`import { default as b }` -> `typeof b`, importHelpersWithImportOrExportDefault): tsc's getNameOfSymbolAsWritten
  special-cases `default` by binding context. Plan 21 stage 2 material.
- tttt instantiates member signatures of generic classes/interfaces that can't reference the mapped type parameters;
  tsc keeps them (getObjectTypeInstantiation filters outer type parameters with isTypeParameterPossiblyReferenced).
  Display compensates (`mentions_mapped()`, hacks.md); doing it in `instantiate.c` would be correct *and* cheaper
  (fewer signature copies, identity-preserving relations). Measure on valibot/type-fest.

## Parser
- **Unbounded recursion depth**: the parser and checker recurse once per nesting level, so
  binderBinaryExpressionStress needs 4-8 MB of stack with GCC 15 -O3 (passes at the default 8 MB, fails at 4 MB; GCC 13
  needed 16 MB, which the 24.04 box band-aided with a 64 MB RLIMIT_STACK constructor, removed after the 26.04 upgrade).
  tsc walks binary chains iteratively; a compiler upgrade or bigger frames can bring the overflow back. Threads get
  smaller stacks still.
- `parse_label` should refuse reserved words (`default:`/`case x:` are fine today only because of parse order).
- A leading `&`/`|` in a type becomes an `any`/`never` operand (`parse_type`), which every consumer has to know to skip.
- No tokenizer + backtracking with fail cache. Fine for speed in principle (`parse_fail`/
  `hop_start_if_ends_on_word_break` are ~5% of the corpus profile), but parse-error fidelity against tsc is what the
  errors baseline measures.

### Found by plan 15 session 1 (2026-09-24)
- **TS2403 false positives = type-core bugs** (each test's errors fail on a spurious TS2403 because tttt computes two
  spellings of one type differently): rest/spread of unions and intersections (restUnion, restUnion2,
  restIntersection, spreadIntersection, spreadUnion: tsc's getRestType/getSpreadType), `Required<>`/`-?` and mapped
  type identity (inferenceOptionalProperties(+Strict), mappedTypeModifiers, mappedTypes4), optional tuple elements
  carry `| undefined` in tsc's element types (optionalTupleElementsAndUndefined), error recovery gives `any` where tsc
  keeps the declared type of a private/unresolved member (constructorParameterProperties, objectLiteralGettersAndSetters,
  ModuleWithExportedAndNonExportedImportAlias), `new C()` without inference gives `T<{}>` not `T<unknown>`
  (newOperatorConformance), overload choice with `any` arguments (anyAssignabilityInInheritance), array literal
  element unions (`[[], [null, null]]`, arrayLiteralWidened), booleanFilterAnyArray (`Ari<any> | Ari<unknown>`),
  wrappedAndRecursiveConstraints2 (inference through a recursive constraint). Good one-bug-per-session material.
- `({ f } = this)` inside method f counts as a read of the private f (tsc's rightIsThis self access):
  noUnusedLocals_destructuringAssignment.
- tsc's requiresScopeChange exceptions to "body variables aren't visible from parameters" (`??`/`?.` before ES2020,
  object rest before ES2017, static fields of class expressions) need a walk over the parameter list: the `.2`
  variants of nullishCoalescingOperatorInParameter*, optionalChainingInParameter*, classWithStaticFieldInParameter*,
  parameterInitializersForwardReferencing.2.
- Class members with computed names nothing knows (`[s]: number` with `s: string`) contribute index signatures in tsc
  (computedPropertyNames12_ES5/ES6 want TS2411 against them); tttt drops them.
- Errors tsc reports *inside lib files* (objectTypeHidingMembersOfExtendedObject, duplicateNumericIndexers: global
  interface augmentations checked through the lib's first declaration) are dropped by `error_ctx_error_s` with a
  log_error. Not worth much; the log noise is the issue.
- The `{ a = dflt }` default of a shorthand assignment pattern is now kept and resolved but not checked (neither the
  default's type against the property nor its display): `ObjectExprItem.shorthand_default`.
- Overloads: the flag agreement codes (TS2383/2385/2386) aren't done; tsc's checkFunctionOrConstructorSymbol has them
  next to TS2394.
- abstractClassUnionInstantiation fails when run alone (`-f`), passes in the full run: its TS2511 errors land on an
  invalid span (the "Error added with invalid span" log_error in failstats); something order-dependent in the shared
  lib state.
- The CLI (`program.c`) still loads the default lib when a file says `/// <reference no-default-lib="true"/>`; only
  the test harness honours it.

### Found by plan 15 session 2 (2026-09-24)
- contextuallyTypedOptionalProperty(exactOptionalPropertyTypes=true): `T` inferred from `match(y => y > 0)` for an
  optional property keeps `undefined` (tsc's missing type is stripped), so the new operand check says TS18048.
- mergeSymbolReexportedTypeAliasInstantiation: a `type Row2<T>` added by `declare module '.'` to a module that
  re-exports an interface Row2 — tttt resolves the reference to the interface (spurious TS2315).
- Default exports are separate symbols per declaration (`scope_default_export_table` reports TS2528 itself), so
  TS2323/TS2813/TS2814 for two `export default`s are missing; tsc merges them into one `default` symbol.
- Empty statements (`;`) aren't in the AST, so TS1036 for `;` in ambient blocks is missing (3 tests).
- TS1117 is implemented but blocked by parser gaps in its tests: `\u0061` identifiers, `{ x = 1 }` outside
  patterns (tsc: TS1312 recovery), `#z` in object literals; computed literal names aren't resolved for it either.
- Property suggestions (TS2551) don't filter inaccessible members (tsc's isValidPropertyAccessForCompletions).
- TS2311 (`await` as a callee) and TS2663 in class computed names need the identifier's parent / tsc's
  includeClassComputedPropertyName.
- The JSX `<T>` vs type assertion and arrow functions in `?:` recovery: `(a?) => {} ? b : c` and `() => {} || a`
  are now parse errors like tsc, but tsc's exact recovery tokens (TS1005 vs ours) aren't checked.

### Missing checks after plan 15 (retired 2026-09-24; counts: missing diagnostics / tests)
- **Stack depth: binderBinaryExpressionStress passes at the default 8 MB stack and crashes at 7 MB.** Inlining one
  small helper into `expr_typecheck()` was enough to overflow it (session 3; fixed with `noinline`). Checking (and the
  resolve walk?) recurse once per operand of a long `a + b + c + ...` chain; tsc checks binary chains iteratively
  (checkBinaryExpression's state machine). Real code has such chains (generated files): worth a fix before it bites.
- Default exports: `default` should be one merged symbol like tsc's (TS2528 22/12, TS2813 19/16, TS2814 22/16,
  TS2323 for defaults): `scope_default_export_table` keeps one symbol per declaration. A session.
- TS2774 (59/5, "function is always defined" in conditions), TS7053 (71/23, implicit-any element access), TS2537
  (41/10), TS1262 (16/13), TS2496 (11/11, `arguments` in ES5 arrow functions: the check exists, the misses are other
  places), TS1117 (43/8, blocked by parser gaps, see session 2 below), TS2311 (`await` as a callee).
- Regular expression *patterns* (TS1487 116/4, TS1508, TS1535, TS1530, TS1517, TS1518, TS1507...): tsc's
  scanRegularExpressionWorker, ~1000 lines; only the flags are checked (`regex_check_flags`).
- TS2612 needs isPropertyInitializedInConstructor (flow at the end of the constructor), and so does TS2564
  (strictPropertyInitialization, 20/6, absent).
- Emit helpers: `for await` (__asyncValues) needs the enclosing function; esDecorators-classExpression-...-8 needs the
  shorthand default `{ C = class {} }` checked. nodeModulesImportHelpersCollisions2/3: only a trailing blank line in
  the errors baseline differs (harness).
- TS1361: `import x = types.A` of a type-only namespace import (importEquals1); mergeSymbolRexportFunction (an
  augmentation merging a const into a type-only re-export: tsc's TS2451 path).
- TS2417 found a real case tsc hides behind a circularity: thisConditionalOnMethodReturnOfGenericInstance (tsc too
  finds `C<any>` not assignable to `B<any>`, but runs into a circular method return type there; tttt reports).
- TS2506 across files through namespace-qualified bases (classExtendsItselfIndirectly2).
- Abstract members satisfied by another base type merged into the class (an interface): tsc looks there before
  TS2515; tttt doesn't.
- TS1042 on object literal members (`{ public foo: 1 }`, modifiersInObjectLiterals): the parser rejects them first.
- The JS-only codes (TS8002 132/12, TS8006, TS8010) belong to the JS plan; TS5055 (emit paths) is out of scope.

## Harness and tools
- `./release/baseline_tests -f complexRecursiveCollections --err` (outside `test.sh`) hangs forever in
  `anon_pipe_write`: the harness writes to a pipe of its own that nobody drains while the test's output is big. The
  valgrind build via `./test.sh` does finish. Two harness runs at once also wait on each other (a lock?), which looks
  like a hang too.
- `./release/baseline_tests -f enumLiteralsSubtypeReduction -d` hangs (re-probed: >20 s) while the same run without
  `-d` takes 0.2 s: probably the diff of a huge output through a pipe that nobody drains.
- The harness's `-f` regex (STC's cregex) silently matches *nothing* once the pattern is a few kB long (a 30-name
  alternation works, ~100 names don't): no error, just 0 tests. `failstats.py --jsx` runs in 30-name chunks because
  of it. A `-f @file` list of names would be the real fix.
- `filetest.sh` always uses `/tmp/testfile`: two agents running it at once collide. Take the directory from `mktemp
  -d` or an env var.
- `@libFiles` is ignored (`hacks.md`): plan 22 stage 0 mounts `/.lib`. `@baselineFile`, `@fullEmitPaths` too. Bad option values are ignored (tsc's harness throws).
- `filetest.sh`/diffapp with a multi-config test file: `run_baseline_test_case()` gets an empty configuration there.
  `./filetest.sh` on a multi-file probe shows the comment lines before the first `// @filename` as a file of their
  own in the errors baseline, so the errors component fails although the diagnostics match.
- Harness runs a test twice when the first run leaves net allocations (lib cache fill), ~170 times per suite run;
  "### TEST" markers can repeat, `failstats.py` dedupes. `failstats.py` counts by test *name*; a handful of names
  exist in both `compiler/` and `conformance/`, so its pass counts can differ from the harness by one or two.
- `./release/tttt file.ts` without `-q` prints the parsed AST before typechecking; `display_expr` prints `?` for
  expression types there. Is that dump worth keeping at all? `display.c`'s syntax printer is only needed for debug
  output and written type *names* now: `DISPLAY_TYPE_MAX_DEPTH`, `BriefType` may be dead; `DisplayUnderlineTarget` /
  `underline_display()` is (types underlines are `TyDisplay.underline` since plan 07), delete it.
- `tttt` only works from the repo root: the lib is looked up as `thirdparty/lib/esnext.d.ts` relative to the cwd (`cd
  corpus && tttt -p .` fails with "File 'thirdparty/lib/esnext.d.ts' not found" and checks nothing, exit 0). Resolve it
  relative to the executable (or an install prefix) like tsc does with its `lib` directory.
- Nodes aren't cloned any more: collapse `owned_symbol`/`symbol_ref` (`GenericItem`, `InferredType`, mapped keys)
  and `key.expr.owned`/`ref` into one pointer each; drop `LAZY_TYPE_FUNCTION` (`hacks.md`).
- The stdlib checks clean now (`./release/tttt -q --no-lib --keep-going thirdparty/lib/esnext.full.d.ts`, 2026-09-23):
  a cheap regression check for lib-related changes.

## Perf (see `measurements.md` for numbers)
- **tsgo baseline broke with TypeScript 7.0.2** (2026-09-25, new box): `bench.sh` takes whatever `tsc` >= 7 is on
  PATH, now the 7.0.2 release, which exits 1 on the synthetic corpus (TS1540: `module X {}` namespaces are an error)
  and on valibot (TS5108: `moduleResolution=node10` removed). Decide: pin tsgo to one version in `bench.sh`/setup
  (reproducible across boxes), and/or rewrite `module` to `namespace` in `bench/make_corpus.py` and override the
  valibot option for tsgo, so tsgo checks what tttt checks.
- Alias variances are in (2026-09-25): two instantiations of one alias infer and relate by their type arguments
  (tsc's getAliasVariances). What's left of valibot's time is inference (`infer_from_signature_lists` ->
  `infer_from_returns` over schema objects' `~run`/`~standard` members) and `ty_conditional_resolve`'s `infer X` walks;
  re-profile before picking the next lever.
- valibot `methods/required/required.ts:258` and `requiredAsync.ts:269`: since alias variances, tttt finds the first
  overload compatible with the implementation (TS2578 unused `@ts-expect-error`; tsc says TS2394). The erased overload
  return `Omit<any, ...> & { entries: RequiredEntries<ObjectEntries, undefined, undefined>; ... }` now relates to the
  implementation's `SchemaWithRequired<Schema, ObjectKeys<Schema> | undefined, ...>`; the variance fast path decides
  some pair tsc doesn't (`RequiredEntries` measures invariant+unreliable for TKeys, `InferObjectInput` covariant+
  unreliable). `TTTT_DEBUG_RELATE_NESTED=1` shows the pairs; suspects: no `Unmeasurable` (hacks.md, `get_variances`),
  or a marker reaching a type whose variance tsc reports as unmeasurable.
- Cache call resolutions per `CallExpr` in the checker (each call resolves twice today).
- type-fest's check (0.6 s) is ~90% `test-d/opaque.ts` alone, which runs into TS2589 at line 68 (`Tagged`/
  `UnwrapTagged` over `TagContainer`) plus four TS7022 that tsc doesn't report: a correctness bug with a perf symptom.
- type-fest `GreaterThan<2, 1>` is `false` (`test-d/greater-than-or-equal.ts:8`, and a probe importing
  `source/greater-than.d.ts`): the nested `[...] extends infer R extends [boolean, ...] ? ... : never` chain (two
  `infer R` in one declaration, the inner shadowing the outer) picks a wrong branch.
- `./bench.sh`'s "stdlib only" section prints ~0 ms for every phase (seen after plan 17): probably checks nothing.
- tsc corpus profile after the 2026-09-24 perf session (6.0 G Ir): parsing is ~19% of instructions (~6000 per
  source line: backtracking sub-parsers, `hop_start` chains, `parse_fail` 1.5 M times from `parse_expr` alone;
  `hop_start_if_ends_on_word_break` 1.6% self). A first-char dispatch (or a real token) in `parse_expr`,
  `parse_type` and the keyword probes is the lever; the binary operator switch alone didn't move wall time.
  Checker: `is_simple_related` 6.7% self (7.6 M calls: `assigned_is_assignable`, `first_unfit_argument`,
  `ty_union_subtype_reduced`, `ty_is_comparable_either_way` from discriminant narrowing; tsc keys unions of 10+
  members by discriminant, `narrow_by_discriminant()` asks every member), `ty_union` 5.8% self (flatten + dedupe +
  intern of big enum unions like `SyntaxKind | undefined`, rebuilt by `ty_optional_param_type`, `with_undefined`,
  `filter_type`: cache the "+ undefined" results?), `ty_declared_type_of` -> `intern_slot` 1.4% (cache the enum type
  on the member literal).
- Name resolution is 77 ms on the corpus (plan 04 wanted 40): cache misses more than work; every identifier walks the
  scope chain (locals + container exports per level, value then type meaning on failure), declare + resolve are two
  walks of the AST. Ideas: one table probe per scope for all meanings, skip empty block scopes, merge the walks.
- `find_prop`'s intersection memo caches whatever depth it was computed at (`depth > 8` gives "not found"), harmless
  so far. `ty_intersection` distributes over non-primitive unions with no size limit (tsc: TS2590 at 100k).
- Peak RSS ~120 bytes per source byte: AST node size / per-node allocations / `DEBUG_ALLOCATOR` (always defined in
  `prelude.h`, even in release) are the suspects. Measure with it off. `program_destroy` is skipped in the CLI
  (release) already; long term, arenas.
- No `perf` on the dev box; callgrind works (`valgrind --tool=callgrind ./release/tttt -q files...`, then
  `callgrind_annotate [--inclusive=yes]`). Worth a `./profile.sh` wrapper if more sessions need it.
- valibot `keyof(objectSchema)` (methods/keyof/keyof.test-d.ts) gives `PicklistSchema<[], ...>`-like types where tsc
  has `['foo', 'bar', 'baz']`: valibot's union-to-tuple conversion (overloaded function intersection inference?).
  Unreduced, ~5 TS2344.
- Without strictNullChecks, a written `W["_i"] | undefined` constraint displays as `W["_i"]` (tsc keeps the
  `| undefined` in the display). Probe: `declare function eo<const W, const D extends W | undefined>(w: W, d: D): D;`
- `() => 'foo' as const` returns `"foo"` now, but tsc's underline reuses the `"foo"` (it prints `^^^^^^     `).
