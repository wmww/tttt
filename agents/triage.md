# Triage
Uninvestigated items. Triage sessions drain this. Last drained 2026-09-27 (triage after plans 26/27 stage 1): every
test named here was looked up in `build/failstats/tests.tsv`; items whose tests pass now were dropped, R18/R19 became
plan 29, substitution types and `T[K]` constraints plan 30.
"(re-probed)" marks items confirmed open with the current symptom at some point.
Plan 15 (missing checks) is retired: `./unimplemented_codes.sh` still lists the codes `src/` never mentions, and
"Missing checks after plan 15" below has the tail worth knowing about.

## From the triage of 2026-09-27
- **TS2303 through namespace imports** (22 node-modules tests miss only this: nodeModulesPackageExports,
  nodeModules*PatternExports*, nodeModulesConditionalPackageExports, x3 module kinds): `import * as cjs from
  "inner/cjs/index"` resolving to the importing file itself, plus `export { cjs }`. tsc's resolveESModuleSymbol calls
  getTypeOfSymbol on the module for namespace imports (synthetic default / ESM-CJS interop), which resolves every
  export alias, so the chain re-enters `cjs`. tttt's alias cycle detection (ALIAS_CIRCULAR, `symbol_resolve_alias`)
  only sees direct chains. Probably: when resolving a namespace import of a module whose exports include an alias
  that is in flight... find the smallest rule that doesn't compute module types in the binder.
- `PortItem.full_span` of `import x = require()` / `import x = a.b` is computed in `parse_import_equals()` from the
  caller's anchor, whose parser hasn't advanced: it ends before the name, not at the statement's end (tsc's error
  node for TS2303/TS2440 on import-equals is the whole statement, `;` included). recursiveExportAssignmentAndFindAliasedType6
  wants the full span; check TS2440's tests before changing it.
- The same test: a circular `import self = require(...)` / `export = self` chain types `self` as `Window & typeof
  globalThis` (tsc: any). The unresolved placeholder target resolves to the global scope somewhere.

## Single-code error buckets (2026-09-27, after the triage's fixes; suite 8564)
Tests whose errors component fails on one code alone (from `build/failstats/tests.tsv`: errors=0 and missing ∪ spurious
is one code). The triage fixed the three "misplaced" span buckets (TS2554, TS6053, TS1125: +56 tests) and TS2496
(+11) in minutes each; the rest are mostly small missing checks, good material for a "suite checker bugs 2" session like todo entry 8:
- missing TS2309 11 (`export =` with other exports: ExportAssignment7/8, es5ExportEquals), TS1262 11 (topLevelAwaitErrors.10-12), TS2433 10 (namespace
  merged across files with a class/function: cloduleSplitAcrossFiles), TS2301 9 (class member initializers referencing
  constructor parameters: classMemberInitializerScoping), TS2371 9 (initializers in overloads / index signatures),
  TS2318 12 (missing global types under noLib), TS2373 10 (the requiresScopeChange item below), TS2742 10
  (declaration emit, probably out of scope).
- spurious TS2349 16 (betterErrorForUnionCall, callOfConditionalTypeWithConcreteBranches, classExtendsNull,
  functionType), TS2307 14 (APISample_*/APILibCheck: resolving `typescript` from the harness), TS2554 11 (mostly JS:
  arguments-as-rest, JSDoc-optional parameters), TS7006 13, TS2741 10 (expandos, plan 26 stage 2).
- The big ones (spurious/missing TS2322 ~55 each, missing TS2339 53, spurious TS2304 39, spurious TS2339 28) are
  mixed bags: sample, don't assume one cause.

## From plan 26 stage 1 (2026-09-27)
- **`import("…")` specifiers in the types baseline**: tsc prints some module specifiers as absolute paths that the
  harness then strips (`/.src/a` -> `a`), so the text matches but the `^^^` underline is 6 longer (742 baseline lines,
  e.g. `exports` inside its own CommonJS file, intersectionsAndEmptyObjects). It's moduleSpecifiers'
  getModuleSpecifiers falling back to the file name (no specifier from a file to itself?). Find the rule, then print the
  path the harness strips.
- **`module.exports.x = v` after `module.exports = C`** is a member of the export= type in tsc (members copied into a
  new anonymous type, getInitializerTypeFromAssignmentDeclaration), symbols `wat.D` / `Symbol(C.x)`:
  jsDeclarationsExportSubAssignments, moduleExportAlias4. Probably stage 2 with expandos (same machinery).
- **`@type` on declaring assignments** (`/** @type {Foo} */ module.exports = {...}`, `exports.x = ...`): the JSDoc
  type is the declared type, the context and the assignability target (checkJsdocTypeTagOnExportAssignment1-8, all
  also need @typedef).
- **JSDoc on `export default <expr>`**: `@type` there (same tests).
- **TS6210 related info** of an arrow function under a variable's `@type` points at the arrow's parameter; tsc points
  at the JSDoc signature's (getSignatureOfTypeTag applies to the arrow too). Same root: the arrow's return type is
  "reused" from the `@type` in tsc's types baseline (jsdocTypeTagParameterType, jsdocTypeTagRequiredParameters).

## From the suite checker bugs session (2026-09-27, todo entry 8)
- **`@pretty: true`** (14 tests: prettyFileWithErrorsAndTabs, duplicateIdentifierRelatedSpans1-7,
  manyCompilerErrorsInTheTwoFiles, multiLineContextDiagnosticWithPretty...): the errors baseline starts with tsc's
  formatDiagnosticsWithColorAndContext output (ANSI colours, source line with gutter, `~` underline, related info,
  "Found N errors" summary) before the usual section. Harness work; the CLI could use the same formatter.
- **Numeric-named properties in printed types**: tsc prints a property declared with a numeric literal name as
  written (`1.1`, `9.671406556917009e+24`) and one declared with a string literal quoted (`{ "1": string }`); tttt
  quotes by the name's shape (objectTypeWithNumericProperty, binaryIntegerLiteral, octalIntegerLiteral,
  emitAccessExpressionOfCastedObjectLiteralExpressionInArrowFunctionES5). Display only.
- **Destructuring assignment flow order** (controlFlowAssignmentPatternOrder, 12 spurious TS2322;
  controlFlowBindingPatternOrder): assignments inside a pattern's computed keys and defaults happen in tsc's
  evaluation order, which the flow builder doesn't follow.

## Category-sized (worth a plan or a whole session)
- **Deferred alias instantiation and type references** (plan 19 R18/R19): `plans/29_deferred_aliases.md`.
- **JS files**: planned, `agents/plans/26_js_jsdoc.md`. What's left here is the part of spurious TS2304 that isn't
  JS (JSX names like `div`/`className`, keywords after bad recovery: `with`, `await`, `yield`, `public`). JSON modules are done (todo entry 8); what's
  left of `requireOfJsonFile*` is JS (`requireOfJsonFileInJsFile`) and error cases (WithErrors, WithNoContent,
  WithModuleEmitNone, WithComputedPropertyName).
- **Syntax gaps** (after plan 14: ~700 tests still have a spurious syntax error, `./failstats.py --syntax`): JSX
  (plan 22; `./failstats.py --jsx`); recovery fidelity shapes (`''`, `'};'`,
  `'{'`, `'];'`), and malformed import types
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
- Plan 24 leftovers: implicit-any errors from *widening* (`function f5([a1] = [undefined], {b1} = { b1: null }, c1 =
  undefined)`: tsc's reportErrorsFromWidening gives TS7031/TS7006, noImplicitAnyDestructuring*, needs tsc's widening
  null/undefined, hacks.md `widen_nullable`); object literal padding in a union
  (`{ a: 1 } | { b: "" }` -> `b?: undefined`) only happens in widening, not in tsc's other normalizations
  (getNormalizedType for relations); `const {a} = null` (TS2339/TS2488, still in the plan 14 list above).
- **Substitution types** and tsc's constraints of `T[K]` (plan 19 R17, generic TS2536, `o[k]` on generic objects,
  mapped-type indexed access, restrictive conditional instantiation, NoInfer): `plans/30_substitution_types.md`.
- numericStringLiteralTypes' spurious TS2370 (`...values: UnwrapContainers<T>`) isn't substitution types (plan 30's
  finding): tsc's getResolvedApparentTypeOfMappedType makes a homomorphic mapped type over an array-constrained T
  array-like.
- `this` leftovers: TS7041 (`this` captured by a top-level arrow); TS7017 (`typeof globalThis` has
  no index signature); `ThisType<T>` arguments aren't instantiated with the inference in
  progress (Vue-style `defineComponent` options).
- **Target-gated diagnostics** (`compiler_options()->target` exists): TS2705/TS2468 (async needs `Promise`),
  TS1375/TS1378 (top-level await), TS2802 (iteration needs `downlevelIteration`), TS1056 (accessors, ES3), TS2659
  (`super` in an object literal below ES2015).
- **Evolving arrays** (tsc's autoArrayType: `let x = []` / `x = []` on an auto-typed `let x;`, then `x.push(v)`,
  `x[i] = v` grow the element type; needs FLOW_ARRAY_MUTATION nodes in the builder). Auto types (`let x;`) exist, the
  array half is `any[]` (hacks.md "Narrowing"). controlFlowArrays and friends.

- **Display leftovers after plan 21 (closed 2026-09-26)**. `./displaydiff.py` ranks them; `other` (378 TS tests
  alone) is mostly type-core bugs the types baseline shows first. Sampled clusters, each a few percent of `other`:
  - *Union/keyof origins* (~5% of `other`, 20-30 tests): tsc keeps the union a type was written as (`origin`) through
    narrowing and filtering, and `keyof X` resolved to literals prints `keyof X`. stringLiteralTypesInUnionTypes01
    (`b : T`), partiallyDiscriminantedUnions (`Shape`), flowControlTypeGuardThenSwitch (`Both`),
    intlNumberFormatES2023, reactTagNameComponentWithPropsNoOOM(2) and partialOfLargeAPIIsAbleToBeWorkedWith (`keyof
    ...`), privateNameStaticMethodAsync (`IteratorResult<number, void>`), doNotInferUnrelatedTypes, trackedSymbolsNoCrash,
    spreadBooleanRespectsFreshness, literalFreshnessPropagationOnNarrowing. **Decided (plan 21 stage 7): not now.**
    It's a type-core feature (unions carry an origin, every filter/narrowing path keeps it, keyof keeps its operand)
    for ~25 tests; revisit when `other` is otherwise drained or a real project's messages need it.
  - *Module specifiers of `import("...")`* (~5%): tsc's getModuleSpecifier prefers a package's entry/`index` that
    re-exports the symbol and non-relative paths: declarationEmitForGlobalishSpecifierSymlink2,
    symbolLinkDeclarationEmitModuleNames(RootDir), bundledDtsLateExportRenaming, declarationEmitTopLevelNodeFromCrossFile2
    (`import("./box")` for `import("box")`), declarationsForInferredTypeFromOtherFile.
  - *`this` of the contextual signature in a function expression's type* (`data : (this: Readonly<...> & Instance) =>
    ...`): vueLikeDataAndPropsInference(2), json.stringify.
  - A computed negative number name prints `[-1]` (computedPropertyNamesDeclarationEmit6): `Prop.name_is_value`
    doesn't tell a number-literal name from a string one.
  - `A & ({} | undefined)` for a truthiness-narrowed generic (typePredicatesOptionalChaining1,
    assertionFunctionWildcardImport2); lone surrogates `"\u{D800}"` print `""` (unicodeExtendedEscapes*10).
  - The plan's own buckets that are left: tsc-typeof 21 (expando functions, JS), our-typeof 35 (not re-sampled since
    stage 1), union-order 48 (case by case), line-extra 97 / line-missing 81 (unsampled since stage 0).

## Checker gaps
- A context-sensitive arrow against a generic rest (`wrap<Args extends unknown[]>(_: (...args: Args) => void)`,
  `wrap(({ cancelable } = {}) => {})`): tttt types the parameter `unknown` (the constraint's element), tsc uses the
  binding pattern's own type `{ cancelable: any; }` (inferFromAnnotatedParameters with a generic rest type).
  inferredRestTypeFixedOnce lost its errors component to this when destructuring unknown started reporting TS2339
  (plan 25). Also missing: TS2492 (`catch (x) { let x }`, catchClauseWithTypeAnnotation).
- `in`/`out` variance annotations on type parameters are parsed and dropped (`GenericItem` has no field): tsc takes the
  variance from them instead of measuring (`getVariancesWorker`), and checks them (TS2636/TS2637). Small now that
  `measure_variances` is shared by declared types and aliases.
### Inference
- **Context-free type of return-sensitive functions** (badInferenceLowerPriorityThanGoodInference, re-probed: spurious
  TS18046): tsc's first round types an argument like `() => ({ a: 1, b: x => {} })` (no sensitive parameters,
  sensitive through its returned expression) as a return-only signature whose return type is checked with
  `SkipContextSensitive` (nested sensitive functions are the non-inferrable `anyFunctionType`, nothing memoised:
  `links.contextFreeType`), and infers from that. The skip mode exists since plan 19 R10 (`ExprCtx.skip_sensitive`),
  but a function argument is still skipped whole: the missing piece is the return-only signature (hacks.md).
- `unknown` where tsc infers a type (~20 TS18046 in the corpus; suite: intraExpressionInferences,
  mappedTypeRecursiveInference, reverseMappedUnionInference).
- Exposed by plan 12's excess property checks, a spurious TS2353 over a wrong inferred type (re-probed):
  mappedTypeMultiInference (`K` of `...cssSets: { [P in K]?: Style }[]` inferred from the first rest argument only).
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
- Permissive instantiation for conditional types is missing (`ty_conditional_resolve` defers where tsc picks the
  false branch); a flat `TyMapper` can't say "every type parameter -> any". Now costs `reactHOCSpreadprops` (React's LibraryManagedAttributes
  over `ComponentClass<P>`: tsc resolves it to the false branch). Plan-worthy: a wildcard type + cached permissive
  instantiation, the plan 22 file (deleted, git history) had no more on it. Relevant to plan 25's React corpus.
- TS function expandos (`function f() {}; f.defaultProps = ...`) now cost `tsxStatelessComponentDefaultProps` (React's
  Defaultize never sees `defaultProps`). tsc: binder bindSpecialPropertyAssignment for function symbols, members in
  resolveAnonymousTypeMembers. Covered by plan 26 stage 2 (expandos in JS and TS). Also the biggest part of plan 21's
  symbols `decls-missing` bucket (~50 tests alone after stage 6): each `f.x = ...` is a declaration of `f` and of
  `f.x` in tsc's symbols baseline.
- `import.meta` is typed now (ty_import_meta_type) but its module-kind checks aren't: TS1343 (module option doesn't
  allow import.meta), TS1470 (in a CommonJS output file).
- Plan 29 stage 3 leftovers (circularity diagnostics): recursivelyExpandingUnionNoStackoverflow lacks the TS2589 tsc
  reaches first (tttt's interned instantiations find the TS2615 cycle at once) and tsc prints the mapped type with
  `containsError` in mapped syntax (`{ [P in "M"]: any; }`, tttt `{ M: any; }`). TS4109 missing where tttt catches the
  cycle elsewhere: circularTypeArgumentsLocalAndOuterNoCrash1 (relating the deferred reference to `{}` short-circuits;
  tsc also prints `NumArray<any>` there, not `X`), circularlyReferentialInterfaceAccessNoCrash (members in flight).
  limitDeepInstantiations: `Foo<"true", {}>` never reaches the limit, a prop-in-flight cycle gives any first.
  Spurious TS2589: recursiveConditionalTypes (the `unbox(b1)` calls, 12), recursiveTypeReferences1.
- A homomorphic mapped type over a `satisfies`/const object doesn't resolve its keys (the `computedOptions` mapped
  type in the tsc corpus's `utilities.ts` printed with a huge `keyof "a" & keyof "b"` key).
- `expandoFunctionSymbolProperty` (re-probed; plan 26 stage 2 should cover it): `f[symb] = ...` on a function declaration isn't an expando property,
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
- Leftovers of plan 23 (duplicate identifiers, merges), none of them merge logic:
  - duplicateIdentifierRelatedSpans1-7 want `@pretty` (above) and tsc's amalgamated cross-file duplicates: 8 or more conflicting names between two files are one
    TS6200 per file ("Definitions of the following identifiers conflict...", related TS6201) instead of TS2300s
    (checker.ts initializeTypeChecker, `amalgamatedDuplicates`).
  - Parser gaps that show up as spurious TS2300: `class void {}`/`type void = I` (reserved words, tsc TS1005/TS2457
    recovery), enum members named `1`/`[2]`/`"3"` (TS2452/TS1164: enumErrors, literalsInComputedProperties1, parserEnum5/7),
    `\u0061` escapes in identifiers (duplicateObjectLiteralProperty), `#z: 3` in an object literal (TS18016,
    objectLiteralErrors), index signatures with two parameters (TS1096, giant), `import { as as as }`
    (importSpecifiers1), `this is T` predicates in some positions (typeGuardOfFormThisMember).
  - TS1049/TS1054 (setter/getter parameter counts) don't exist: computedPropertyNames49/50 fail only on that now.
  - TS2699 (static `name`/`length`/`prototype`/`caller`/`arguments` vs Function's, staticPropertyNameConflicts,
    staticPrototypeProperty) doesn't exist.
  - JS-only buckets (TS2323/TS2528/TS2451 in `module.exports`/`@typedef` tests) wait for JS support.
- genericRecursiveImplicitConstructorErrors3 (re-probed): all TS2314 are missing there, a spurious TS2355 because the
  erroneous return annotation isn't `any` (tsc's errorType is any-like everywhere), and 4 spurious TS2339. A failed
  type-argument-arity call returns `any` where tsc still resolves a signature. `lower_identifier_internal` doesn't
  treat a bare `C` inside generic class `C<T>` as `C<T>` any more, but nothing reports tsc's TS2314 for it either.
- overrideLateBindableIndexSignature1 (re-probed): late bound index signatures work now; only TS4127 ("cannot have an
  'override' modifier because its name is dynamic") is missing.
- `for (x in o)` with a non-declaration target: tsc's TS2405 is TS2322 here (re-probed).
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
  (doYouNeedToChangeYourTargetLibrary*): done in plan 15 (`lib_features.c`); re-probed 2026-09-27, only
  ES2016Plus fails (spurious TS1503 regex flags against the target, a few TS2550 missing).
- Module augmentation / `export =` merges don't add members (moduleAugmentationsImports3/4,
  moduleAugmentationDoesInterfaceMergeOfReexport; umd-augmentation-1..4 and augmentExportEquals6_1 pass their errors
  now but not symbols/types).
- Plan 04 stage 1 leftovers: ~35 tests lost their symbols component then: `declare global` in odd places, `export {
  x }` next to an exported `x`, moduleAssignmentCompat1-4 (`Symbol(A)` without a Decl for a non-instantiated
  namespace used as a value).
- Module resolution leftovers (`hacks.md` "Module resolution"): resolution-mode
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
- Found by the hacks.md audit (2026-09-26, re-probed 2026-09-27): `export function g() {}` followed by
  `function g() {}` is TS2395 here, TS2393 (duplicate implementation) in tsc.
- taggedPrimitiveNarrowing (exposed by TS2339 on `never`): `typeof hash !== "string"` then `throw` narrows `string & {
  __hash: true }` to never instead of keeping it.
- Spurious TS7022 on `const can = composite && o?.e && !out` when the type of `e` mentions an unresolved name (`Map`
  without lib es2015): some circularity through error types. Probe: test/probes/plan08/alias_optional_chain.ts minus
  its `@target`.
- `check_with()` (ty/expr.c) computes an expression with a contextual type without going through the LazyType, so it
  has no cycle detection (`let [x = x] = []` used to recurse forever; `ty_binding_default_type` guards it with
  `defaults_in_flight`, other contextual self-references can still overflow the stack).
- Plan 16 leftovers: TS2561/TS2820 want tsc's spelling suggestion, which exists now (`spelling_suggestion()`, plan
  15): 11 diagnostics in 5 tests. TS2793 ("would have succeeded against the implementation") only for function
  declarations, not overloaded methods/constructors (`add_implementation_success`). Destructuring assignment targets
  are checked as a whole, not property by property (for-of47/48: tsc's checkDestructuringAssignment reports at the
  default).

- `export default class {}` prints `typeof default` from other files where tsc names the alias that imports it
  (`import { default as b }` -> `typeof b`, importHelpersWithImportOrExportDefault): tsc's getNameOfSymbolAsWritten
  special-cases `default` by binding context. symbol_chain.c material.
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
- Plan 27 stage 1 leftovers worth a look before stage 2 (2026-09-27): `other:E` grew 78 -> 81 and `other:D` barely
  moved (tests whose first error now agrees but later recovery differs: expected as more lists commit, but sample
  them: `./syntaxstats.py --list other:E`). `parse_class_item_inner()` still fails where tsc commits (nameless `*`,
  `get`/`set` members), and function types in type position still backtrack freely (tsc's
  isUnambiguouslyStartOfFunctionType).
- No tokenizer + backtracking with fail cache: kept (plan 27 decided). The 2026-09-27 perf check-up profiled the
  tsc corpus, not react (the corpus tsgo is closest on); parsing is ~20% of instructions there and 116 of react's
  300 ms: see "Perf" below.

### Found by plan 15 session 1 (2026-09-24)
- **TS2403 false positives = type-core bugs** (each test's errors fail on a spurious TS2403 because tttt computes two
  spellings of one type differently): `Required<>`/`-?` and mapped
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
- Empty statements (`;`) aren't in the AST, so TS1036 for `;` in ambient blocks is missing (3 tests).
- TS1117 is implemented but blocked by parser gaps in its tests: `\u0061` identifiers, `{ x = 1 }` outside
  patterns (tsc: TS1312 recovery), `#z` in object literals; computed literal names aren't resolved for it either.
- Property suggestions (TS2551) don't filter inaccessible members (tsc's isValidPropertyAccessForCompletions).
- TS2311 (`await` as a callee) and TS2663 in class computed names need the identifier's parent / tsc's
  includeClassComputedPropertyName.
- Arrow functions in `?:` recovery: `(a?) => {} ? b : c` and `() => {} || a` are now parse errors like tsc, but
  tsc's exact recovery tokens (TS1005 vs ours) aren't checked.
- JSX leftovers after plan 22 (`./failstats.py --jsx`: 326 of 457 pass): overloaded components don't get tsc's TS2769
  report (tsxNotUsingApparentTypeOfSFC, tsxStatelessFunctionComponentsWithTypeArguments4), TS6229 isn't checked for
  union tag types or inside an overload chain, TS18042 (a type imported in a .jsx/.js file: JS semantics,
  jsxCheckJsxNoTypeArgumentsAllowed), TS2608 (ElementAttributesProperty with several members), TS2639
  (tsxNamespacedTagName2), TS2604 on a union of tag types (tsxDynamicTagName3). `.jsx` under allowJs wasn't looked at
  beyond what the suite exercises. Types-component diffs in the subset are general: inference through union-typed
  parameters (contextualTypingOfOptionalMembers: `unknown` where tsc infers),
  alias display (`React.SFC` vs `StatelessComponent`, `keyof React.ReactHTML` expanded), `{ [__proto__]: null }`
  computed names (tsxEmitSpreadAttribute). The parser's `saw_jsx`/`saw_import_meta` (moduleDetection) are set by
  attempts that backtrack too; harmless so far, tsc only sets them for real nodes.
- JSX parse error long tail (plan 22 stage 1 left it; `./failstats.py --jsx --syntax`): jsxInvalidEsprimaTestSuite,
  jsxNamespacePrefixInName(React) (`<:a>`, `<a:>`), tsxAttributeInvalidNames (`32data`: TS1351 and a number token),
  conflictMarkerTrivia3 (merge conflict markers, TS1185, aren't recognised anywhere), jsxEsprimaFbTestSuite (non-ASCII
  identifiers: `is_identifier_char` is ASCII only), and an
  unclosed fragment's TS17014 span, which in tsc starts at the end of the previous token (the node's full start,
  trivia included: tsxFragmentErrors). jsxAndTypeAssertion wants two errors at EOF (`':' expected` and `'</'
  expected`) where tsc's one-error-per-position rule should have dropped one: not understood.

### Missing checks after plan 15 (retired 2026-09-24; counts: missing diagnostics / tests)
- Stack depth (fixed 2026-09-26 for `+`-style chains and union/intersection types, 100k operands fine): logical
  chains (`a && b && ...`, `??`, `||`) still recurse once per operand in the resolve walk (`expr_lookup_names_logical`)
  and the checker, as do the harness's `expr_compile_test_info_inner` and `expr_drop`. tsc itself overflows on 5k-long
  `&&` chains, so this is parity, not a bug; but new cases in `expr_typecheck()`/`expr_lookup_names()` still grow
  the per-level frame of whatever recursion is left.
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
- `error.c` "Error added with invalid span": 58 lines per suite run (2026-09-27: TS2511 abstract class instantiation
  31, e.g. classAbstract*, newAbstractInstance; TS2503 `Intl` 13; TS2304/TS2552 `ArrayBufferLike`/`ArrayLike`/`Date`
  names looked up from lib files 14). `grep "invalid span" build/failstats/stderr.txt`. A span computed from the
  wrong node or file. Also one "Declaration B acquired multiple times" (scope.c).
- `@baselineFile`, `@fullEmitPaths` are ignored. Bad option values are ignored (tsc's harness throws).
- `filetest.sh`/diffapp with a multi-config test file: `run_baseline_test_case()` gets an empty configuration there.
  `./filetest.sh` on a multi-file probe shows the comment lines before the first `// @filename` as a file of their
  own in the errors baseline, so the errors component fails although the diagnostics match.
- Harness runs a test twice when the first run leaves net allocations (lib cache fill), ~170 times per suite run;
  "### TEST" markers can repeat, `failstats.py` dedupes. `failstats.py` counts by test *name*; a handful of names
  exist in both `compiler/` and `conformance/`, so its pass counts can differ from the harness by one or two.
- The AST dump is behind `tttt --print-ast` now; `display_expr` prints `?` for expression types there.
  `display.c`'s syntax printer is only needed for that and written type *names*: `DISPLAY_TYPE_MAX_DEPTH`, `BriefType` may be dead; `DisplayUnderlineTarget` /
  `underline_display()` is (types underlines are `TyDisplay.underline` since plan 07), delete it.
- Nodes aren't cloned any more: collapse `owned_symbol`/`symbol_ref` (`GenericItem`, `InferredType`, mapped keys)
  and `key.expr.owned`/`ref` into one pointer each; drop `LAZY_TYPE_FUNCTION` (`hacks.md`).
- The stdlib checks clean (2026-09-26): a cheap regression check for lib-related changes. Not `--no-lib
  esnext.full.d.ts` alone (`--no-lib` ignores `/// <reference lib>`, that checks one file): `./bench.sh`'s
  `lib_closure` lists the 84 files to pass as roots.

## Perf (see `measurements.md` for numbers)
- Alias variances are in (2026-09-25): two instantiations of one alias infer and relate by their type arguments
  (tsc's getAliasVariances). What's left of valibot's time is inference (`infer_from_signature_lists` ->
  `infer_from_returns` over schema objects' `~run`/`~standard` members) and `ty_conditional_resolve`'s `infer X` walks;
  re-profile before picking the next lever.
- Each call resolves twice (type pass and typecheck walk): 7.7% of the tsc corpus' instructions. `plans/28_resolve_calls_once.md`.
- type-fest's check (0.6 s) is ~90% `test-d/opaque.ts` alone, which runs into TS2589 at line 68 (`Tagged`/
  `UnwrapTagged` over `TagContainer`) plus four TS7022 that tsc doesn't report: a correctness bug with a perf symptom.
- tsc corpus profile after the 2026-09-27 perf check-up (5.91 G Ir, `measurements.md`): parsing ~20% of
  instructions (backtracking sub-parsers: `parse_fail` ~7 M calls from `parse_type_compat_literal`,
  `parse_expr_right_side`, `parse_type`, `parse_left_unary_op`...; `hop_start_if_ends_on_word_break` 1.4% self). A
  first-char dispatch (or a real token) in `parse_expr`, `parse_type` and the keyword probes is the lever; the binary
  operator switch alone didn't move wall time. Checker: `is_simple_related` 6.3% self, 3 M of 4.5 M calls go to the
  enum branch (enum literal vs enum literal, mostly different values of one enum: a same-enum shortcut before the
  generic checks would do); `ty_union` 4.5% self (small unions built over and over: `access`,
  `identifier_flow_type`, `lower`); `infer_from` 5% self (1.3 M calls, per-call prologue/visited scans);
  `ty_declared_type_of` -> `intern_slot` (cache the enum type on the member literal?).
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

## Corpus leftovers (not perf; `./realworld.py --corpus X`)
- valibot `keyof(objectSchema)` (methods/keyof/keyof.test-d.ts) gives `PicklistSchema<[], ...>`-like types where tsc
  has `['foo', 'bar', 'baz']`: valibot's union-to-tuple conversion (overloaded function intersection inference?).
  Unreduced, ~5 TS2344.
- Without strictNullChecks, a written `W["_i"] | undefined` constraint displays as `W["_i"]` (tsc keeps the
  `| undefined` in the display). Probe: `declare function eo<const W, const D extends W | undefined>(w: W, d: D): D;`
- `() => 'foo' as const` returns `"foo"` now, but tsc's underline reuses the `"foo"` (it prints `^^^^^^     `).
- TS2344 on type references (new, 2026-09-26) fires spuriously in the corpora where tttt evaluates type-level
  equality/path helpers wrongly: type-fest `test-d/get.ts` (`MismatchInfo<Get<...>>`, `StrictEqualUsingTSInternal...`
  conditionals), `test-d/pick-deep.ts` (`Paths<T>` not containing `"nested.deep.deeper.value"`), valibot
  `getDefaults.test-d.ts` / `keyof.test-d.ts` (a mapped "Expected: ..., Actual: ..." type). Each is a conditional or
  mapped-type evaluation bug; `./realworld.py --corpus type-fest --code TS2344` lists them.
- type-fest `GreaterThan<2, 1>` is `false` (`test-d/greater-than-or-equal.ts:8`, and a probe importing
  `source/greater-than.d.ts`): the nested `[...] extends infer R extends [boolean, ...] ? ... : never` chain (two
  `infer R` in one declaration, the inner shadowing the outer) picks a wrong branch.
- valibot `methods/required/required.ts:258` and `requiredAsync.ts:269`: since alias variances, tttt finds the first
  overload compatible with the implementation (TS2578 unused `@ts-expect-error`; tsc says TS2394). The erased overload
  return `Omit<any, ...> & { entries: RequiredEntries<ObjectEntries, undefined, undefined>; ... }` now relates to the
  implementation's `SchemaWithRequired<Schema, ObjectKeys<Schema> | undefined, ...>`; the variance fast path decides
  some pair tsc doesn't (`RequiredEntries` measures invariant+unreliable for TKeys, `InferObjectInput` covariant+
  unreliable). `TTTT_DEBUG_RELATE_NESTED=1` shows the pairs; suspects: no `Unmeasurable` (hacks.md, `get_variances`),
  or a marker reaching a type whose variance tsc reports as unmeasurable.
