# Plan 35: silent type differences (the types component's "other" and "any" buckets)
Written by the insights of 2026-09-27 (third of the day). Short on purpose: stage 1 builds the tool and rewrites
this plan's "Queue" with what it finds.

## Why
The types component is the one that fails most (2797 tests, against 2372 for errors). `./displaydiff.py` names the
display mechanisms (union order, `typeof`, `import()`...), and plan 21 worked those. What remains in the tests that
pass errors is mostly **not display**: tttt computes a different type and no diagnostic shows it. Fixed by one kind
(TS + JS): `other` 394 + 35, `we-any` 127 + 39 (tttt says `any` where tsc has a type), `tsc-any` 101 + 14 (tsc says
`any`, usually error recovery), `line-extra` 118 + 6 (tttt annotates something tsc doesn't). Examples in `other`:
`objectFreeze` infers `boolean` for a `false` return, `genericCombinators2` infers `Collection<number, Date>` where
tsc falls back to `any`, `localTypes3` is plan 32 stage B. These are checker semantics bugs that a clean corpus can't
see and the errors component doesn't catch: the same class of bug as a wrong inference that later produces a
spurious error in a real project. No plan or session has owned them; the lesson of plans 27/34 applies (a bucket gets
worked once a tool names its mechanisms).

## Stage 1 as planned: the tool, then the first fixes (one session)
- Split `other` and `we-any` in `displaydiff.py` (or a sibling script) by what can be told mechanically: the AST kind
  of the annotated node (call result, property access, parameter, literal, `this`...), the shape of the difference
  (literal vs widened, type arguments differ, `any` vs a type, a member missing/extra, generic vs instantiated,
  `T` vs its constraint), and whether the node is inside a generic function (plan 32 B). The goal is buckets of the
  form "call results whose inferred type arguments differ: N tests", not a perfect classification.
- `tsc-any` and `line-extra`: check how many are one mechanism each (tsc's error-recovery `any`/`errorType` for
  unresolved names and bad calls; which nodes tsc's type writer skips).
- Write the ranked queue below (tests fixed alone per mechanism), then fix from the top for the rest of the session.
- Record the numbers in `measurements.md` (replace the displaydiff line).

## Stage 1 (done, 2026-09-27): suite 9427 -> 9666
`./displaydiff.py --fine` names each catch-all line `<shape>/<annotated node>` (`displaykinds.fine_kind`: shape =
we-any, tsc-any, union-fewer/more-members, type-args-differ, signature-differs, object-differs, nested-we-any...;
node = declaration, identifier, call, literal, property-access...), and ranks the tests by the kind of their **first**
differing line (a wrong root type spreads over lines of other kinds, so "fixed alone" undercounts). `--shape` drops
the node. `-k KIND --names` lists the tests fixed alone and the tests whose first line is that kind.

Fixed (mostly error recovery and type-writer rules, not inference): shorthand ambient modules are any; tsc's writer
skips a number/string literal under `export default`; a non-instantiated namespace's name has no type line;
invalid assignment targets are errorType; wrong type-argument counts are errorType (TS only); `import type`/`export
type` names show the declared type; `import X = N` of an uninstantiated N is errorType; IIFE parameters are typed by
the arguments. we-any 127 -> 93, tsc-any 102 -> 32, line-extra 118 -> 20 (TS tests fixed alone); `other` (393 + 39)
barely moved: it is many small mechanisms.

## Stage 2 (done, 2026-09-28): suite 9833 -> 9951
The queue below from stage 1, worked top to bottom, and what the buckets showed on the way:
- Done: (1) TS fields typed by the constructor / static blocks' flow (`field_type_from_assignments()`); (2) tagged
  templates resolve as calls (`CallInfo.tagged_template`, see hacks.md), which exposed two general bugs: non-strict
  null/undefined weren't subtypes of anything (relate.c `is_simple_related`) and inferred type arguments didn't widen
  null/undefined; (3) past-the-end tuple elements are undefined; (4) `export as namespace` of no value is errorType;
  (5) wrong type-argument counts instantiate tsc's longest candidate; (6) synthetic default imports of declaration
  files, node_modules files get their package.json format; (7) IIFE generators yield into the call's context;
  (8) property names print like tsc (`1.1` unquoted, `"1"` quoted when string-named); (9) `m() {} m(x) {}` has one
  signature. Beyond the queue: heritage names are property accesses to the type writer (+21), `(null)` has a line,
  signature parameter initializers are walked, namespaces with `;` are instantiated, `catch {}` blocks are walked,
  **class field initializers are contextually typed by their annotation** (a real bug: spurious TS7006 on
  `f: (a: number) => number = (a) => a`), missing destructured properties with defaults are undefined.
- we-any/declaration (first differing line) 95 -> 50; line-missing 73 -> 72 (the heritage/null/catch fixes moved tests
  into other buckets as much as out); `./displaydiff.py` fixed-by-one: other 383 + 54, we-any 65 + 41.

## Stage 3 (done, 2026-09-29, todo entry 9): suite 11472 -> 11962
Four Opus sub-agents, each owning the failing tests whose **first** differing line was in one family
(`./displaydiff.py --fine --first-tsv`): A any/error recovery, B inference/signatures, C objects/unions/literals, D
display/type writer. ~150 commits, 0 lost, corpora unchanged, instructions +0.3-1.2%. Tests with errors passing and
types failing 968 -> 467. Mostly tsc error-recovery rules (errorType for unresolved names, element accesses, bad
spreads), binder-style member symbol merging (`ClassItem.symbol_first`, duplicate interface properties), index
signatures from computed names, accessor pairs with setters (write types), property-name printing, union order from
tsc's early `""`/`0`/`0n` literals, `NonNullable<T>` for `||`/`??`, `removeNullableByIntersection`, higher-order
inference for generic functions in literal arguments, reverse mapping for Readonly/Partial-like mapped types,
noUncheckedIndexedAccess's missingType, `import x = N.C` and instantiation expression lines, `typeof f` for
self-referencing functions. Split by first-line kind duplicated little (accessor pairs C/D, non-array rest targets and
`!` in optional chains B/C).

## Stage 4 (done, 2026-10-04, todo entry 5): suite 12377 -> 12712
Four Opus sub-agents (types semantics, types display, symbols names, symbols declarations), test lists by first
differing line, ~110 commits, 0 lost, corpora unchanged but xstate 12 -> 8. Types component 703 -> 560 failing,
symbols 506 -> 217. Main mechanisms: **type creation order** (class value types first, type parameters/type
literals/intersections made when the check walk meets them, written `{}` interned at checker start, and *GCC
evaluating call arguments right to left*: `./argorder.py` lists the calls that make two types as arguments),
annotation reuse (nested function types, mapped properties, aliases through another import), lib files merged in
tsc's lib order, parameter properties and fields as one property, circular import aliases resolving to nothing (the
node16 PackageExports chains, +28), exact bigints, which `yield`s count, overload failure's longest candidate, and in
the symbols harness a lot of tsc symbol identity rebuilt from tttt's split members (hacks.md: computed names, union
property declarations, merges of type parameters/namespace locals/imports). Most of the harness work is display-only
by necessity: tttt's members aren't Symbols.

## Stage 5 (done, 2026-10-05, todo entry 9): suite 12933 -> 13097
Four Opus sub-agents (A types semantics, B types display, C missing/extra lines + JS, D symbols; lists by first
differing line, symbols overlap to D), then second lists (A: plan 44's sweep causes; C: CommonJS aliases and parser
shapes; D: its own binder leftovers). ~120 picks, 0 lost, corpora unchanged. Types component failing 488 -> 345,
symbols 178 -> 131 (tests with errors passing and types failing 395 -> 258). Main mechanisms: underline reuse rules
(infer names, fresh type parameters for `this`/overload/merged-interface instantiations, inference-renamed T1),
`typeof` naming fallbacks, error-recovery identifiers getting lines, instantiation expressions (tsc's
getInstantiationExpressionType), binder merges (augmentations, static member + namespace type, `declare global`,
auto-accessors, merged class/interface type parameters by name), JS `this.x` in literal methods and function
expressions as classes, CommonJS `exports.x = a.b` aliases, const contexts through spreads, generator TNext from
`const y: T = yield`. What they left is in the queue below.

## Queue (after stage 5, merged 2026-10-05)
One queue from the stage-4 queue and the four "Left by fan-out agent" sections of 2026-10-05; tests that pass now
are gone. `..t`/`.S.` = the component failing (types, symbols). Roughly by size and by how close each is to a fix.

**Display and naming (types component)**
1. **Reuse needs accessibility** (done 2026-10-06): name_accessible() asks symbol_accessibility_at(), and the
   suite's walk sees what declaration emit painted (Checker.painted_kept, symbol_chain_set_kept_painted()). The tests
   listed here turned out to be mostly other mechanisms, also done that day: distributive conditionals whose check
   type was instantiated print through `infer T` (display_distributive_conditional(); with tsc's eleven-deep elision
   per conditional root and its print-once cache, TyParamNames.printed), homomorphic / modifier-preserving mapped
   types through `infer T_1` (display_mapped()), keyof-declared mapped constraints as `keyof X`, and an instantiated
   conditional type goes by its root's alias (KEY_CONDITIONAL_ROOT_ALIAS: `type W<O> = Extract<O, string>` makes W<X>
   an `Extract<X, string>`, except through an import or as another alias's body). Left: recursiveConditionalCrash3
   (a mapped key named K_1 where tsc says K_2: tsc gives every instantiated mapped type a fresh key type parameter,
   instantiateMappedType's cloneTypeParameter, tttt keeps the declared one; computedTypesKeyofNoIndexSignatureType
   too), instantiateContextualTypes (`Promise<DooDad>` where tsc infers the alias-less `"SOMETHING" | "ELSE"` through
   `then`'s return-type inference; needs a tsc trace). Declaration emit doesn't print with TyDisplay.param_names, so
   none of the `infer T` forms reach `.d.ts`-shaped output or its diagnostics.
2. **Naming from a block scope** (done 2026-10-06 for blocks): Block.scope (set by block_add_names()) is where the
   suite names types inside a function body or block (enumAssignmentCompat6, expandoFunctionBlockShadowing). Left:
   a signature's own scope (functionVariableInReturnTypeAnnotation: `typeof b` of a body local in the return
   annotation, tsc `any`); duplicate declarations that resolve to the other symbol (typeAliasesDoNotMerge,
   multipleExportDefault3: `import("f").A`); exportAssignmentOfExportNamespaceWithDefault (the default import of an
   `export =` whose target has a `default` export is that member, not the target, so tsc qualifies).
3. **Union order / creation order**: tsc's type writer re-checks each printed expression, so its object literals are
   newest (contextualTyping36, conditionalOperatorWithIdenticalBCT; bugs/display.md). Looked at 2026-10-05: emulating
   it as "object literals re-made by re-checking E print last" needs object literal types to know their node (a
   harness-only side table: TY_ANONYMOUS has no origin) and only fixes conditionalOperatorWithIdenticalBCT;
   contextualTyping36 also needs the re-check's subtype reduction (mutual subtypes keep the older). Not worth it alone.
   Done 2026-10-05: contextual property lookups no longer make the name's literal (es2022IntlAPIs), the walk makes a
   union's literal members and a property signature's bare literal annotation (assignmentCompatWithDiscriminatedUnion).
   Left: literals in other nested annotation positions (`"a"[]`, tuples) are made on demand, not at the walk;
   contextual lookups through intersections with type variables still make the literal eagerly. Lib literal made
   early (correlatedUnions: dom's `CSSMathOperator` "sum", not found). typeInferenceWithExcessPropertiesJsx. Also
   keyofAndIndexedAccess2 (generic intersection member order), complexRecursiveCollections, arrayFrom
   (`ArrayLike<T> | Iterable<T>`), dependentDestructuredVariablesFromNestedPatterns (tuple references made in another
   order), noImplicitAnyUnionNormalizedObjectLiteral1, spreadUnion (spread of a union: member order). The harness sets
   skipDefaultLibCheck; tsc's CLI default checks the libs, which makes ~2700 string literals first (lib.dom: every
   `keyof HTMLElementTagNameMap` etc.), so CLI messages can order unions differently from tttt: not a suite concern.
4. **Underline/annotation reuse**: merged generic interface members aren't node-reused (useRegexpGroups,
   doYouNeedToChangeYourTargetLibraryES2016Plus: `(onfinally?: () => void)` not the written
   `(() => void) | undefined | null`; a local generic interface declared twice behaves the same; instrument tsc).
   Members of a merged lib interface not instantiated (arrayFlatNoCrashInference: `arr.flat` of `T[]`; tsc instantiates
   because Array is merged), promisePermutations 1-3 (hacks.md maps_merged_type_param(); also tsc rebuilds a reused
   function type node, `progress?: (progress: any) => void` underlined as new punctuation around reused names).
   isolatedDeclarationsAddUndefined (`x = 10 as T` parameter initializers print the assertion syntactically).
   callbackTag4 (`@this {{...}}` members), jsdocPropertyTagInvalid (a JSDoc typedef of an imported JS file reused
   unqualified), intraBindingPatternReferences (a union member from a binding default reuses `x: number` whole).
   Done 2026-10-06: properties copied from a module's variable (spread, rest, synthetic default) reuse its annotation,
   const literal initializer or function expression parameters (PropDeclRare.spread_variable); a generic reference
   whose type an instantiated signature changes isn't reused (identityRelationNeverTypes); deeplyNestedMappedTypes
   (the root-alias rule of item 1).
5. **`T_1` renaming**: inferredReturnTypeIncorrectReuse1 (`fn_1` from enterNewScope's params fake scope; mechanism not
   pinned down), jsxComplexSignatureHasApplicabilityError.
6. **Clones per import** (resolveESModuleSymbol -> cloneTypeAsModuleType; members through
   getTypeWithSyntheticDefaultImportType's spread become parentless symbols): nodeModulesTypesVersionPackageExports x3
   (`.S.`, `mod.x` prints bare in tsc), crashDeclareGlobalTypeofExport (`{ default: Root; }`),
   verbatimModuleSyntaxRestrictionsCJS (`Promise<typeof import("/decl")>` vs `typeof esmy2`). A per-import module type
   is a refactor of how `import * as` resolves; small payoff.
7. **Ambient `export =` naming**: jsxNamespacedNameNotComparedToNonMatchingIndexSignature (`typeof import("react")`
   vs augmentExportEquals3_1's `import("file1.d.ts")` for the same shape), exportAssignmentMembersVisibleInAugmentation
   (bugs/display.md).
8. **Duplicate declarations/members**: functionOverloadErrors (`export function fn1(); function fn1(n: string);
   function fn1() {}` in a namespace is one local symbol in tsc, the export's declarations included: the local's
   type needs the export's declarations of the block too, see statement.c function_local_symbol_decls()),
   symbolDeclarationEmit12, privateNameDuplicateField, mergedClassWithNamespacePrototype, sliceResultCast (two
   identical-looking `slice` signatures kept), arrayDestructuringInSwitch1 (both members' `every` signature sets kept
   in `(A[] | [A]).every`). Done 2026-10-06: a method body is an implementation only right after another declaration
   (overloadConsecutiveness, members.c adjacent_methods()).
9. **Enum member names are source text** (enumIdentifierLiterals `1.0` -> "1", enumWithUnicodeEscape1): EnumCase.name
   is a Span; tsc uses the numeric literal's canonical text and the cooked string.

**Semantics (types component)**
10. **Inference**: generic function nested in a returned literal (tsc's instantiateTypeWithSingleGenericCallSignature
    runs for any identifier with a contextual signature inside an inferring call; tttt only for argument-level
    `generic_function`/`nested_generic` Args in call.c): inferentialTypingWithFunctionTypeNested,
    nestedGenericSpreadInference. Variadic middle rest parameters (`...args: [unknown, ...T, unknown]`,
    typeParameterConstModifiers: param_shape() can't represent it), genericRestParameters1 (`f10(...v)` gives `[...V]`,
    `f11(1, ...u)` keeps `1`), isomorphicMappedTypeInference (f22/f24 infer unknown),
    dependentDestructuredVariablesFromNestedPatterns, arrayFrom, inferConditionalConstraintMappedMember (tsc: never),
    typeArgumentInferenceWithObjectLiteral (single-member `enum E1 { X }`: tsc ends with the fresh E1.X).
11. **Narrowable references** (2026-10-06, agent C: done for intersectionSatisfiesConstraint (an intersection whose
    apparent type is a union isn't related structurally, seen in a tsc trace), voidReturnIndexUnionInference (property
    accesses retyped outside inference), mappedTypeIndexedAccessConstraint (base constraint of an indexed access),
    narrowingTypeofUndefined2 (`[...x]` of a generic array-like is `X[number][]`), restInvalidArgumentType and
    genericObjectSpreadResultInSwitch (rest elements retyped, CheckMode.RestBindingElement)). Left:
    narrowingDestructuring: an IIFE argument `(({ kind, ...rest }) => rest)(t)` is substituted in tsc (its contextual
    type comes from the resolving signature), and a nested pattern's rest element still takes the substituted type.
12. **Circularity order**: circular returns are any in tsc (`let x = { foo: () => x }` gives foo `() => any`;
    genericCallOnMemberReturningClosedOverObject; the opposite of plan 42's direction). Static side resolution lowers
    the base's type arguments first (cloduleGenericOnSelfMember, classDeclarationShouldBeOutOfScopeInComputedNames:
    `class S extends B<typeof S.Base>` cycles to any; tsc publishes the static members before default construct
    signatures). cyclicGenericTypeInstantiation{,Inference}: tsc's `typeof z` circularity bottoms out in any 11 levels
    down, tttt's type is infinite. dynamicNames: `[rI.x]` in `interface RI<T>` needs the early-bound members while the
    late-bound ones are resolved (tsc's getResolvedMembersOrExportsOfSymbol publishes earlySymbols first).
13. Spreads: done (2026-10-06, SpreadAcc is a union of intersections, mapped over every union spread).
14. One each: destructuringParameterDeclaration9/10 strict=false (a nested pattern's default `{}` padded with implied
    types, see binding_element_implied_type / ty_binding_element_type), `yield` as a call argument/assignment
    right side/return operand gets its TNext only once the statement is checked (fixed for `const y: T = yield` via
    UnaryOpExpr.declared_context), fakeInfinity2/3 (`() => Infinity`, not the alias), wellKnownSymbolExpando (no
    late-bound expando for `f[Symbol.iterator] = ...` in TS), instantiationExpressions' last line /
    typeofImportInstantiationExpression (`typeof import("./a").f<T>`, `fx<>` has no node), ramdaToolsNoInfinite2
    (alias chosen), inexistentPropertyInsideToStringType (`function toString()` in a JS script),
    declarationEmitPartialReuseComputedProperty (artefact). Not worth mimicking: awaitedType-like "first alias a union
    was created with" cases. Found by agent C (2026-10-06), not done:
    - Object literal methods of one name are one binder symbol (PropertyExcludes is None): `{ foo(x: 'hi') {}, foo(x:
      'a') {} }` is `{ foo(x: "hi"): void; foo(x: "a"): void }`, each name typed with both signatures
      (stringLiteralTypesInImplementationSignatures2, callSignaturesWithParameterInitializers2).
    - overloadModifiersMustAgree: `declare function bar(); export function bar(s); function bar(s?) {}` is one local
      symbol in tsc whose implementation is skipped (it follows an overload); tttt keeps the implementation.
    - Members of an interface declared in several places (Array, String...) are instantiated per `this` in tsc
      (isTypeParameterPossiblyReferenced is true for a multi-declaration symbol), so `x.slice` of `[A, B] | [A, B, C]`
      is a union of two identical-looking function types (sliceResultCast, arrayDestructuringInSwitch1). tttt's
      free_params is exact; mimicking it costs instantiations on every lib method.
    - Accessor bodies in type literals (`get foo() { return 0 }`, TS1183) are typed from the body in tsc
      (accessorBodyInTypeContext); a getter without a body in an object literal (`get e,`) names void.
    - typeArgumentInferenceWithObjectLiteral: a single-member enum's regular member literal is the enum type itself
      (prints E1), the fresh one prints E1.X.
    - noCrashOnNoLib: without lib, array types are `{}` (missing global Array).
    - intersectionsAndEmptyObjects: `{} & M` with M an import()'s synthetic default type is {} (the spread type's
      symbol has no members, so isEmptyAnonymousObjectType counts it empty).
    - globalThisBlockscopedProperties: `keyof typeof globalThis` has "undefined" (tsc's undefinedSymbol is a global).
    - importDeclWithExportModifierAndExportAssignment{,InAmbientContext}: `export = x` of an uninstantiated namespace
      next to other exports prints any, tsc typeof x (the merged export= path).
    - shadowedInternalModule (`import any = b.A` prints the alias's type), typeFromPropertyAssignment30 (an expando
      `c.p = {}` of an annotated const is typed without context in tsc).
    UnaryOpExpr.declared_context), reExportUndefined1 (`export { undefined }` is undefinedSymbol), wellKnownSymbolExpando (no late-bound expando for `f[Symbol.iterator] = ...` in
    TS), instantiationExpressions (`fx<>`: the parser drops an empty type argument list, keeping it trips code that
    assumes one; its StringBoxFunc line is done) / typeofImportInstantiationExpression (`typeof import("./a").f<T>`),
    ramdaToolsNoInfinite2 (left: literal `5` made later in tsc, union order), inexistentPropertyInsideToStringType (`function toString()`
    in a JS script), declarationEmitPartialReuseComputedProperty (artefact). Not worth mimicking: awaitedType-like
    "first alias a union was created with" cases.

**Symbols component and binder (tsc's checker-time mergeSymbol through aliases; tttt's binder merges first)**
15. importEqualsError45874 (`.St`), moduleAugmentationWithNonExistentNamedImport, duplicateVarsAcrossFileBoundaries,
    moduleAugmentationDuringSyntheticDefaultCheck (`.St`), aliasesInSystemModule1/2 (unknown-symbol artefact),
    declarationFileNoCrashOnExtraExportModifier (`exports.Sub`), reexportMissingDefault5 (`default` vs `"b"`).
16. Spread of a namespace keeps tsc's export symbols (spreadExpressionContextualTypeWithNamespace `.St`: PropDecl.symbol
    can't hold it without becoming the type source). mappedTypeConstraints (`Pick<T | U, ...>` declarations depend on
    whether f3 follows: an order-dependent memo). isolatedDeclarationLazySymbols (`foo[o["a.b"]] = v` adds a declaration
    but no member). objectBindingPatternKeywordIdentifiers02/04 (`.St`, parser recovery of `{ while: while }`).
    Module elements in a function/block (moduleElementsInWrongContext{,2,3}, plainJSGrammarErrors, `.St`): tsc binds
    them anyway; tttt's `wrong_context` declares nothing.

**JavaScript (plan 26 territory)**
17. `a: function () { this.x = v }` declares x in the function's own members (objectPropertyAsClass, thisInObjectJs,
    functionExpressionNames: `.S.`, also `A = function () {}` and a binding default as constructors); JS this-members
    merged with `F.prototype = {...}` accessors and `this` in a `defineProperty(F.prototype, ...)` descriptor
    (jsDeclarationsFunctionLikeClasses2, checkExportsObjectAssignPrototypeProperty, `.St`). `@enum` on `exports.a = {}`
    (enumTagOnExports `.St`).
18. CommonJS naming and aliases: moduleExportDuplicateAlias{,2,3} (every `exports.x = undefined`/`= a` is an alias in
    tsc, left sides any; tried, see hacks.md), moduleExportAssignment4 (`module.exports = exports['default']`
    circularity is errorType), moduleExportsElementAccessAssignment (`typeof mod1."d"`), lateBoundAssignmentDeclarationSupport6,
    jsDeclarationsFunctionsCjs, moduleExportAliasImported, moduleExportPropertyAssignmentDefault,
    jsdocTypeReferenceToImportOfFunctionExpression.

## Validation
`./displaydiff.py` fixed-by-one counts for the worked buckets go down, suite up, no corpus regressions. A fix to an
inference rule that shows up here should be checked on zod and valibot (inference-heavy) for instruction counts too.
