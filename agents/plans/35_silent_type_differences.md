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
3. **Union order / creation order**: union member order is type creation order. Done (2026-10-05/06, agent A, see
   plan 44's union-order bullet too): the check walk makes references, unions, tuples, function-type parameters and
   alias bodies where tsc's checkSourceElement does (union and generic reference nodes memoized per checker,
   KEY_UNION_NODE, like tsc's links.resolvedType); a declared signature types its last required parameter first
   (getMinArgumentCount: arrayFrom); discriminant lookups, interface heritage/index constraints, inference skipping
   type-variable-free literal arguments, tuple length literals. Fixed complexRecursiveCollections, arrayFrom,
   spreadObjectNoCircular1, strictSubtypeAndNarrowing, typeInferenceLiteralUnion,
   templateLiteralTypesPatterns, typeGuardOfFormTypeOfFunction... Left, each a mechanism of its own:
   - **Array literal clones**: tsc's createArrayLiteralType gives every tuple/array reference an ArrayLiteral clone
     (a new id, once per reference, made when an array literal first produces it). So `c ? [b, 1] : ["c", ""]` is
     `["a" | "b", number] | ["c", string]` even when `["c", string]` was written earlier: its clone is newer
     (assignmentCompatWithDiscriminatedUnion, dependentDestructuredVariablesFromNestedPatterns,
     typeInferenceWithExcessPropertiesJsx). tttt has no such type; adding one touches identity everywhere. Plan-sized.
   - **Object literal types are interned in tttt** (ty_object_literal by structure); tsc makes a new one per check, and
     per spread member (spreadUnion: `{ ...union, ...union }` reuses `{ b: string }` from the line before).
   - **The type writer re-check**: tsc's harness re-checks each printed expression, so object literals there are
     newest (contextualTyping36, conditionalOperatorWithIdenticalBCT, heterogeneousArrayLiterals `[{}, Object]`).
     Needs object literal types to know their node (harness side table). Not worth it alone.
   - **getReducedType enumerates an intersection's properties** (getPropertiesOfUnionOrIntersectionType), making the
     intersection type of each property exactly two members have, in member property order (keyofAndIndexedAccess2:
     `window[action]`). Implemented and measured 2026-10-06: vue +0.46%, drizzle +0.25%, tsc +0.2% instructions for
     one test: dropped.
   - noImplicitAnyUnionNormalizedObjectLiteral1 (widening/normalizing a union of object literals: which normalized
     member tsc makes first), correlatedUnions (tsc's harness has "sum" before "concat"; a plain tsc program with
     lib es5+dom gives tttt's order, so it is something of the harness setup, lib or options), ramdaToolsNoInfinite2.
   - Underline-only diffs in this family (callbackTag4, objectLiteralNormalization, promisePermutations,
     prototypePropertyAssignmentMergeWithInterfaceMethod, intraBindingPatternReferences, expandoFunctionContextualTypesJs,
     declarationEmitPartialReuseComputedProperty) are annotation reuse (item 4), not creation order.
   The harness sets skipDefaultLibCheck; tsc's CLI default checks the libs, which makes ~2700 string literals first,
   so CLI messages can order unions differently from tttt: not a suite concern.
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
10. **Inference** (2026-10-06, agent B: done for inferentialTypingWithFunctionTypeNested (a generic function among
    a function argument's return expressions is instantiated in context while the call infers: Checker.
    argument_return_depth in check_with()), genericRestParameters1 (an instantiated rest type parameter keeps its arity,
    a lone spread of a tuple-constrained V is V, rest arguments' contextual T[i] by position despite spreads),
    typeParameterConstModifiers (readonly parameter tuple for a const rest type parameter), isomorphicMappedTypeInference
    (K's constraint in inferToMappedType, Pick<T & U, K>), typeArgumentInferenceWithObjectLiteral (single-member enum
    regular literal is its own base type), narrowingDestructuring (IIFE argument checked with contextual any); arrayFrom
    passed already). Left:
    - nestedGenericSpreadInference: `call(wrap(<T>(x: T) => x), 1)`. tsc resolves `wrap(...)` in the first pass (it
      doesn't return a function, so SkipGenericFunctions doesn't skip it) and prints `{ x: (x: A[0]) => A[0] }`: the
      arrow is instantiated in the context of wrap's X as the outer contextual type *as written* says (returnMapper
      over the raw `{ x: (...args: A) => T }`); tttt's X comes from the contextual type instantiated with the outer
      inferences (A silent never, so `any`). Doing that (tried: infer X from the raw contextual when a generic
      function argument's parameter is a naked type parameter) gives the right wrap type but the outer call then ends
      with T = A[0] where tsc has number: how tsc gets number isn't understood (instrument a tsc copy).
      source_type_at() now gives `A[0]` for a generic rest parameter (tryGetTypeAtPosition), part of it.
    - inferConditionalConstraintMappedMember: tsc gives never only when the check type is an inline mapped type
      (`{ [K in keyof T]: ... } extends { [_ in keyof T]: infer U }`); through an alias `M<T>` it gives "foo" | "bar"
      like tttt. Probably mapped-to-mapped inference (inferFromObjectTypes) on the unresolved instantiation; not
      investigated further.
    - dependentDestructuredVariablesFromNestedPatterns: union order of `[undefined, Error] | [unknown, undefined]`
      (creation order: the tuple from the `as` type node is made first in tsc), agent A's family.
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
15. tsc's checker-time mergeSymbol through aliases (mergeSymbol resolves an alias target: unresolved, the source wins
    the table; resolved, the source merges into a clone of the target, which then names it): importEqualsError45874,
    duplicateVarsAcrossFileBoundaries, moduleAugmentationWithNonExistentNamedImport, moduleAugmentationDuringSyntheticDefaultCheck.
    tttt's binder merges a global alias and a var of another file into one symbol. Needs a deferred pass at
    binder_link() over cross-file global merges involving aliases. Also aliasesInSystemModule1/2 (unknown-symbol
    artefact), declarationFileNoCrashOnExtraExportModifier (`exports.Sub`). (Agent D took the TS ones, 2026-10-06.)
    Done 2026-10-06: reexportMissingDefault5 (synthetic default re-export named by path), modulePreserve4.
16. spreadExpressionContextualTypeWithNamespace (`.St`: PropDecl.symbol can't hold the export symbol without becoming
    the type source), mappedTypeConstraints (order-dependent memo), objectBindingPatternKeywordIdentifiers02/04 (parser
    recovery of `{ while: while }`). Done 2026-10-06: module elements in a function/block are declared (TS1232/TS1233,
    resolved quietly: PortItem.wrong_context), an export specifier where tsc's container puts it (function/script
    locals, namespace/module exports); isolatedDeclarationLazySymbols (any dynamic element name binds the target's
    namespaces). Left: moduleElementsInWrongContext names `"ambient"` through the block's `import * as Foo` (tsc
    doesn't); the merged alias|function `foo` is any where tsc's getTypeOfSymbol takes the function first.
17. JavaScript classes and expandos. Done 2026-10-06: top-level namespace assignments per tsc's
    isTopLevelNamespaceAssignment (conditions, comma lists), TS function variables get namespace declarations,
    `module` redeclared by each nested `module.exports`, @enum on exports/aliases/`A.B.C` (type alias + namespace
    declarations, delayed JSDoc declarations last, one per node), `this.x` in a function expression that is its own
    class, prototype assignments merging with class-body members and `F.prototype = {...}` members with `this.x`
    (decl spans appended in builder_add_assignment_members), defineProperty descriptor methods' `this`, enum member +
    JS assignment, `typeof import("./m").x` of an `export =` object. Left: multipleDeclarations' constructor function
    (`this.m = null` then `C.prototype.m = function`: two symbols in tsc, TS2300 under checkJs; tttt declares the
    prototype assignment in the declare step, before the this-assignment, and can't split it off later without the
    access node); thisTypeInAccessors (an accessor without `this` takes its pair's `this` parameter: needs the pair's
    parameter symbol before either is resolved); typedefCrossModule3 / jsExportMemberMergedWithModuleAugmentation{,3}
    (getCommonJsExportEquals' recordMergedSymbol: `ns` resolves to the merged clone everywhere, `ns.Foo` gets the
    typedef's declaration); misspelledJsDocTypedefTags (a JS global augmentation merging into lib's Animation);
    allowJscheckJsTypeParameterNoCrash; expandoFunctionContextualTypesNoValue (expando on an import-merged function);
    lateBoundMethodNameAssigmentJS (`this[s] = this[s].bind(this)` circular: error/any in tsc);
    jsDeclarationsFunctionClassesCjsExportAssignment (tsc artefact: duplicated declaration, no `this` symbol);
    thisPrototypeMethodCompoundAssignmentJs (`this` named `this`).
15. importEqualsError45874 (`.St`), moduleAugmentationWithNonExistentNamedImport, duplicateVarsAcrossFileBoundaries,
    moduleAugmentationDuringSyntheticDefaultCheck (`.St`), sourceFileMergeWithFunction (a UMD `export as namespace foo`
    merged with a global `declare function foo`), undefinedTypeAssignment4 (a script's `class undefined` merges with
    tsc's undefinedSymbol), declarationFileNoCrashOnExtraExportModifier (`exports.Sub`), reexportMissingDefault5
    (`default` vs `"b"`). Done 2026-10-06: tsc's unknownSymbol named by an alias of it (aliasesInSystemModule1/2,
    isolatedModulesImportExportElision, constructorWithIncompleteTypeAnnotation; unknown_symbol_text(), hacks.md).
16. Done 2026-10-06: spread namespace members keep their symbol (PropDeclRare.spread_variable/spread_namespace),
    `{ while: while }` recovery, type predicates naming no parameter. Left: mappedTypeConstraints (`Pick<T | U, ...>`
    declarations depend on whether f3 follows: an order-dependent memo). isolatedDeclarationLazySymbols
    (`foo[o["a.b"]] = v` adds a declaration but no member). Module elements in a function/block
    (moduleElementsInWrongContext{,2,3}, plainJSGrammarErrors, `.St`): tsc binds them anyway; tttt's `wrong_context`
    declares nothing. Also from agent D's 2026-10-06 list: bivariantInferences (tsc's isTypeParameterPossiblyReferenced
    is true for a type parameter with several declarations, `interface Array<T>` merged: every instantiation of a
    member's type is new, so `(A[] | B[]).m` lists the declaration once per member; tttt's free-param scan shares
    one), thisTypeInAccessors (an accessor without a `this` parameter takes the other accessor's, getSignatureFromDeclaration;
    the later sibling's symbol doesn't exist yet at the resolve walk), dynamicNamesErrors (a late-bound member merges
    into the early-bound one of its name, first declaration's name and type), defaultDeclarationEmitShadowedNamedCorrectly
    (`export default class C` named `C`, not `me.default` through a self-import), nodeModulesJson (a CJS namespace
    import's JSON properties print bare), templateStringMultiline3 (CR-only file: tsc's codeLines lose the leading
    blank lines, mechanism not found).

**JavaScript (plan 26 territory)**
17. `a: function () { this.x = v }` declares x in the function's own members (objectPropertyAsClass, thisInObjectJs,
    functionExpressionNames: `.S.`, also `A = function () {}` and a binding default as constructors); JS this-members
    merged with `F.prototype = {...}` accessors and `this` in a `defineProperty(F.prototype, ...)` descriptor
    (jsDeclarationsFunctionLikeClasses2, checkExportsObjectAssignPrototypeProperty, `.St`). `@enum` on `exports.a = {}`
    (enumTagOnExports `.St`).
18. CommonJS naming and aliases: moduleExportDuplicateAlias{,2,3} (every `exports.x = undefined`/`= a` is an alias in
    tsc, left sides any; tried, see hacks.md; moduleExportsAliasLoop1/2 show it: `Math.min : Symbol(fn2)`),
    moduleExportAlias4 (class named through `var wat = require(itself)`: the chain doesn't see a require alias's
    `export =` target), amdLikeInputDeclarationEmit and jsDeclarationsExportDoubleAssignmentInClosure (`module.exports =
    v` inside a callback is the file's `export =` in tsc, any container; tttt looks into blocks and IIFEs only),
    moduleExportsElementAccessAssignment (`typeof mod1."d"`), lateBoundAssignmentDeclarationSupport6/7 (`x.F.F[_str]`
    naming), jsDeclarationsFunctionsCjs, moduleExportAliasImported, moduleExportPropertyAssignmentDefault,
    jsdocTypeReferenceToImportOfFunctionExpression, jsdocReferenceGlobalTypeInCommonJs (`Puppeteer.connect` qualified).
    Done 2026-10-06: the access of `exports.x = v` shows the property lookup (nothing on an `export =` of another type),
    `module.exports = v` is never an expando of a local `module`, a top-level `module.exports.f.x = v` makes f a
    namespace whatever its value (no expando type for a defineProperty value).

## Validation
`./displaydiff.py` fixed-by-one counts for the worked buckets go down, suite up, no corpus regressions. A fix to an
inference rule that shows up here should be checked on zod and valibot (inference-heavy) for instruction counts too.
