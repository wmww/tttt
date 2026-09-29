# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-29 (triage session, todo entry 10 of the sixth window; suite 11962 -> 12040, 0 lost). Fixed: the two
log_error tests (the suite's checker log_error output is empty), TS2724/TS2614 (tsc's errorNoModuleMemberSymbol),
TS2511 for `new` of a union with an abstract member, class instances in subtype reduction (isTypeDerivedFrom),
`o[77] = v` expandos, two stale hacks (TS2358 in JS, TS7053 on callables), `error_ctx_scratch()`, and through
three Opus sub-agents: escaped identifier names (`name_cooked()`, 20+ tests), `export =`/`export default` of entity
names as aliases, augmentations of `export =` modules and of `export default` aliases, the Node16 ESM namespace import's
synthetic default, and nine small checker bugs (narrowingMutualSubtypes, mappedTypeGenericWithKnownKeys, JS TS2565,
returnTagTypeGuard, rest-tuple union signatures, noUncheckedIndexedAccess, classExtendsNull2, varianceReferences,
generatorImplicitAny). Their perf cost was bisected and gated (tsc corpus +0.6% instructions for the session). hacks.md 985 -> 523 lines. Gone on their own: xstate's checker-count dependence
(checkerdiff: identical at K=1 and 4), derivedClassSuperProperties, `<T extends any>` display, blockScopedBindingUsedBeforeDef,
asyncFunctionNoReturnType, jsdocCallbackAndType, unionTypeWithIndexAndTuple. Routed: circularity to plan 39 (a
planning stub), the rest to `bugs/*.md` (missing_checks.md has a new "Elaboration" section) and plan 26 "Leftovers".

- (instantiation limit session) TS2589 location for a call: `const r1 = a.get()` whose return type runs out of tail
  recursions is reported by tsc at the declaration name `r1` (4,7), tttt at `a.get()` (probe
  `test/probes/instantiation_limit/call_location.ts`); a property access (`a.x`) is at the access in both.
- (vue dts-test session) Overload failure fallback: tsc's pickLongestCandidateSignature takes the first candidate with
  enough parameters (getLongestCandidateIndex over all overloads), `resolve_with_signatures` the last unfit one; the
  failed call's type differs (probe `plan19/vue/overload_first_check_skips_generic_functions.ts`, types only).
- SkipGenericFunctions for generic functions nested in literal arguments (`nested_generic`) isn't done: leaving them out
  of the first round lost zod `pick([z.object, ...])` and xstate inferences, because tttt marks literals nested_generic
  where tsc has no contextual signature to skip for (see `infer_type_arguments`).
- Relating nested indexed-access sources (`T[a][b]...[k]`, react-hook-form Path/FieldPathValue) steps one index
  constraint per relation and recomputes each variant ~12 times (Maybe results inside aren't cached): 4% of the react
  corpus. tsc computes the constraint of the whole chain in one getConstraintOfType; see hacks.md (Relations).
- Symbols: a property of `Readonly<LooseRequired<T>> & {...}` (vue's DefineProps) lists its declaration once where tsc
  lists it twice (the intersection modifiers type `T & Required<T>` of LooseRequired); `this` of a ThisType marker
  prints no Symbol line (probes in `plan19/vue/`).
- (drizzle type-tests session) The union-of-tuples rest argument probe `plan19/drizzle/rest_tuple_union_context.ts`
  failed only through the CLI (`tttt file.ts`, `./run.sh`), never in the test harness, before its fix: the harness
  types a call argument with its contextual type some other way. Find out why the harness and the CLI differ there
  (the harness may hide more CLI-only bugs).
- Each checker (`--checkers 4`) measures variances and builds relation caches on its own, and a TS2589 count is charged
  to whichever node first needs them; tsc pays once. Harmless since drizzle's runaways are fixed, but a TS2589 near
  the 5M limit could depend on the checker count.

- (perf session 2026-09-29) New per-checker caches assume the computation is a pure function of the type ids:
  `ty_union` of two types (KEY_UNION_PAIR), `ty_intersection` without an alias (KEY_INTERSECTION_OF), flow's
  `assignment_reduced_type` (KEY_ASSIGNMENT_REDUCED). tsc caches the same things, and the suite and corpora didn't
  move, but a result computed while something it reads is still in flight (constraints in
  `remove_constrained_type_variables`, members for empty-object checks) is pinned. If an order-dependent union or
  narrowing bug shows up, disable these first.
- (skipTrue session) An array literal `[]` whose contextual type is a deferred conditional type of tuple branches
  (`const x: ConditionalRequired<[options?: O], IsNotNever<K<T>>> = []`) is `never[]`, not `[]`, so TS2322; tsc takes
  the tuple-likeness from the conditional's constraint (both branches) and reports nothing.
- `??`, `||` and optional chains call `ty_non_nullish_part()` directly, which filters null/undefined even without
  strictNullChecks (tsc's getNonNullableType is the identity there). Test-neutral today; switch them to
  `ty_non_nullable()` if a non-strict test shows it (2026-09-29 NonNullable port leftover).
- Members of an ambient non-const enum without initializer (`declare namespace n { enum E { A, B } }`) get 0, 1...
  in `ty_resolve_enum()` (its TODO); tsc makes them computed (unknown values), which is what TS4126 "One value of
  'E.A' is the string ..., and the other is assumed to be an unknown numeric value" reports (enumAssignmentCompat6,
  which also needs `import("f")` qualification in the header). Changes what number literals such members accept.

- (error-header session, plan 34 bucket B) Left from the header/name queues, each needs more than a message fix:
  - Pattern ambient module augmentations (`declare module "a.foo"` in a module file against `declare module "*.foo"`):
    tsc merges a per-name clone (patternAmbientModuleAugmentations); tttt makes an unrelated ambient module and imports
    memoised earlier still see the pattern (ambientDeclarationsPatterns_merging1/2).
  - TS2395 for `import x = a.b` next to an exported declaration needs the alias's target spaces; resolving it in
    `scope_check_export_merges()` (during declaration) memoised node module refs as missing on webpack (reverted,
    importedEnumMemberMergedWithExportedAliasIsError). Do it after binder_link.
  - An `import x = From` alias used as a value makes tsc check `From` as an expression (markAliasSymbolAsReferenced:
    TS2304 next to TS2503), declarationEmitUnknownImport2.
  - Paths in messages: a deduplicated package file keeps its own name in TS2306 (duplicatePackage_globalMerge), TS6053
    shows the reference as written ('../x.ts', selfReferencingFile2), `import("/app/...")` absolute in
    moduleResolutionWithSymlinks_preserveSymlinks: the harness/canonical path vs tsc's file names.
  - Property access on a generic mapped type (`Omit<this, K>`) is never checked (hacks.md: check_access returns); tsc
    resolves its members from getLowerBoundOfKeyType of the constraint (destructuringUnspreadableIntoRest).
  - A destructured property of a generic parent (`T extends number`, `var {r} = t`) is TS2339 on 'T' in tttt, nothing in
    tsc (the indexed access is deferred).
  - JS: `var G = m.G = {}` makes one expando symbol of both hosts in tsc (the initializer's symbol,
    typeFromPropertyAssignment18); `class D extends getSomething()<number>` TS2315 (declarationEmitExpressionInExtends4).
  - Inference: K of `setState<K extends keyof S>(s: Pick<S, K>)` from `{ a }` with a generic `S & State<T>` comes out as
    the constraint `"a" | keyof S` (indexedAccessRelation); `(n: Object) => 1` keeps its literal return
    in tsc when inferred for U (genericCallWithGenericSignatureArguments3).
- A tuple's members (from Array<T>) have the array, not the tuple, as their `this` type: `sort(): this` of
  `[number, number, number]` returns `number[]`, so the tuple isn't assignable to `interface J extends Array<Number> {
  2: number }` (tsc: its sort returns the tuple). Relations also turn a tuple into its array against any declared
  interface without a literal `length` (structured_related_members, before the reference case), which loses `2`.
  destructuringArrayBindingPatternAndAssignment2.
- (error text session A, plan 34 related info/underlines + single codes) Left over, each small or blocked:
  - The mock fs treats DOS-drive names (`A:/x.ts`, `c:/root/...`) as relative: commonSourceDir2/4 (TS5009),
    commonSourceDir5 and pathMappingBasedModuleResolution7_* (spurious TS2307/TS2792). module_name_is_relative()
    now knows drives; join() and the harness don't.
  - TS4109 (circularTypeArgumentsLocalAndOuterNoCrash1): a deferred reference's arguments are never resolved when
    its alias is only related to `{}` (tsc resolves the source's members there, and the cycle shows).
  - conditionalTypes1: tsc suggests `extends NonNullable<T>` (TS2208) for `T` to `T & {}`, but not `extends T & U`
    for `T` to `T & U` (intersectionTypeInference); constraint_resolves_through() treats both as circular.
  - TS2307 for a namespace-internal import (TS1147) is only reported on a value use of its alias, not a type use.
  - unusedLocalsAndParameters: `for (let x: y) { z(x); }` recovers `z` as a declaration that tsc doesn't report
    unused (TS6133).
  - variableDeclarationInStrictMode1: tsc's TS2300 on the lib side (`lib.es5.d.ts(--,--)`), dropped with every
    lib-file error.
  - mappedTypeInferenceErrors, reverseMappedTypeContextualTypeNotCircular: TS6500 should point at the object literal
    property a reverse mapped type inferred from (tsc links declarations), and the inferred types differ too.
  - JS: TS2303 (pushTypeGetTypeOfAlias, `exports.a = exports.b` circular), TS8022 for an `@extends` attached to
    nothing (extendsTag2/4, reported without a position). APILibCheck needs the built tsserverlibrary.d.ts files.
- Names inside a module lib file's `declare global {}` resolve to the file's module-level locals before the globals:
  lib.esnext.iterator.d.ts's `from(value: Iterator<T, ...>)` gets the local `abstract class Iterator` (with
  IteratorObject's members), so `Iterator.from(i)` with a plain `Iterator<string>` is TS2345. In tsc the augmentation's
  own `var Iterator` makes the name resolve (getMergedSymbol of the augmentation's exports) to the merged global symbol,
  interface included, before the file's locals. (await)usingDeclarationsWithIteratorObject.
- (error text D session) TS7023 that tsc gets from re-entrance: checking a return expression prints a type in an error
  message (or relates structurally in an assertion) that needs the function's own return type, which is in flight
  (thisInObjectLiterals, accessorInferredReturnTypeErrorInReturnStatement, checkingObjectWithThisInNamePositionNoCrash,
  trivialSubtypeReductionNoStructuralCheck). tttt's inference doesn't produce error messages, so it never re-enters.
- `// @skipDefaultLibCheck: false` tests expect lib-file diagnostics (`lib.es5.d.ts(--,--)`: TS2374, TS2411, TS2300
  from merges with the test's declarations): duplicateNumericIndexers, objectTypeWithStringIndexerHidingObjectIndexer,
  objectTypeHidingMembersOfExtendedObject, variableDeclarationInStrictMode1. tttt never checks lib files and drops their
  errors (`error_ctx_error_s`); tsc's CLI default does check them (skipLibCheck/skipDefaultLibCheck off), which is a
  perf question too.
- The harness's TS-1 "Pre-emit (n) and post-emit (m) diagnostic counts do not match" (5 tests:
  multiLinePropertyAccessAndArrowFunctionIndent1, parseErrorIncorrectReturnToken,
  parserStatementIsNotAMemberVariableDeclaration1, templateInsideCallback, unicodeEscapesInNames02): emit checks
  expressions the checker skipped (a top-level `return`'s operand after TS1108). Emulating needs to know what emit
  resolves.
- TS7031 from widening misses elements with a default (`var [a5 = undefined] = []`) and parameter patterns
  (`function f([a1] = [undefined])`: tttt widens the parameter root first, tsc's getTypeForBindingElementParent
  doesn't): noImplicitAnyDestructuringVarDeclaration/ParameterDeclaration.
- Destructuring accessibility (`ty_check_destructured_member`) only knows private members; protected ones need the
  classes around the pattern. Abstract properties read by destructuring `this` in a constructor (TS2715,
  abstractPropertyInConstructor lines 78-79) likewise.
- `({ n: number } & { s: string }) & undefined` isn't reduced to never (restUnion3 wants TS2700 from it).
- `x instanceof Object` on a generic `x: T` narrows to something still possibly primitive: spurious TS2638 in
  inKeywordAndUnknown (tsc: `T & Object`).
- `super.m<T>()` in a class whose base is an unresolved import: the member is any, not the error type, so TS2347
  (unusedInvalidTypeArguments).
- TS1376/TS1377 related info of a type-only specifier should span from the specifier's `type` (importSpecifiers1,
  exportSpecifiers).
- `verbatimModuleSyntax`'s TS2748 for an ambient const enum used directly (not through an
  import) isn't reported (only isolatedModules); the import half is checkAliasSymbol's (verbatimModuleSyntaxAmbientConstEnum).
