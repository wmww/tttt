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

## Queue (after stage 4)
What the agents left, roughly by size (their test names are examples):
1. **Union order** (14 first-line in fan-c's list): what stage 4's creation-order fixes didn't reach. tsc re-checks
   each expression when the type writer prints it, so object literals there look newest
   (conditionalOperatorWithIdenticalBCT, contextualTyping36: bugs/display.md); es2022IntlAPIs makes a lib literal
   early somewhere. Plan 21 territory otherwise.
2. **node16 `import * as` of a CommonJS module**: the *PackageExports*/*PatternExports* chains (27 tests) were a
   circular import alias keeping its target (fixed 2026-10-04). What's left really is tsc's per-import clone
   (resolveESModuleSymbol -> cloneTypeAsModuleType, members through getTypeWithSyntheticDefaultImportType's spread:
   readonly exports become new symbols without a parent): nodeModulesTypesVersionPackageExports x3 (`mod.x` of a
   cloned import is `Symbol(x)`, not `Symbol(mod.x)`), the `typeof m4`-vs-`m26` naming in bugs/display.md, maybe
   nodeModulesResolveJsonModule. A per-import module type is a refactor of how `import * as` resolves; small payoff.
3. **Line-missing one-offs** (~50): alias vs structure for recursive conditional/mapped types
   (recursiveConditionalCrash1-3), imports/exports inside functions (moduleElementsInWrongContext2/3), missing
   identifier recovery (`for ()`, reservedWords3, JSX namespaced names).
4. **Underlines** (38 first-line): stage 4 did nested function types and mapped properties; strictFunctionTypesErrors
   (tsc refuses to reuse `typeof Foo.f1` there but not in a reduced probe) is unexplained.
5. **One type per expression** (~5): tsc re-checks arguments outside inference mode, or has a separate rest-element
   check (voidReturnIndexUnionInference, narrowingDestructuring, callChain.3, mappedTypeIndexedAccessConstraint).
6. **Inference**: variadic middle rest parameters (`...args: [unknown, ...T, unknown]`, typeParameterConstModifiers:
   the parameter shape can't represent it), reverse-mapped `?`/`-?` keeping undefined (mappedTypesArraysTuples),
   deep cases (multiSignatureTypeInference, nestedGenericSpreadInference, reverseMappedTypeRecursiveInference).
7. **Spread of a union with generic members** (spreadObjectOrFalsy, spreadInvalidArgumentType): `SpreadAcc` can't keep
   them, a refactor.
8. Merged/duplicate
   declarations (defaultNamedExportWithType1, typeAliasesForObjectTypes); scanner `\u` escapes in property names
   (scannerUnicodeEscapeInKeyword2); enum members with numeric names use the source text; declaration visibility
   (privacyInterface: depends on declaration emit); JS tests (~25) belong to plan 26.
9. tsc artefacts not worth mimicking: awaitedType x2 (first alias a union was created with), fixingTypeParametersRepeatedly1.
10. Left by fan-out agent A of 2026-10-04 (types: semantics/inference), each investigated far enough to name the
    mechanism:
    - **Generic function nested in a returned literal**: tsc's instantiateTypeWithSingleGenericCallSignature runs for
      any identifier with a contextual signature inside an inferring call (`map("", () => ({ x: identity }))`
      instantiates identity with the context's *fixing* mapper); tttt only does it for argument-level
      `generic_function`/`nested_generic` Args (call.c). inferentialTypingWithFunctionTypeNested,
      nestedGenericSpreadInference.
    - **Circular returns are any in tsc**: `let x = { foo: () => x }` gives foo `() => any` (x's type is being
      resolved when the arrow's return is asked); tttt resolves the return later and unrolls x
      (genericCallOnMemberReturningClosedOverObject; the opposite of plan 42's direction).
    - A spread namespace member declared by `export const f = (o: T) => {}` prints its parameters syntactically in tsc
      (typeFromExpression); tttt's spread props don't keep the declaration (declarationEmitResolveTypesIfNotReusable).
    - A reverse mapped optional property prints `a?: number | undefined` in tsc (its type includes the undefined),
      `a?: number` in tttt.
    - An import's non-string module specifier (`import * from Zero from "./0"`) is an expression tsc types (`Zero :
      any`); tttt's parser drops it (invalidSyntaxNamespaceImportWith{AMD,Commonjs,System}).
    - An ambient module nested in a script's ambient module is an augmentation to tsc (isModuleAugmentationExternal):
      unmerged and uninstantiated it's any; tttt declares a global ambient module (privacyGloImportParseErrors).
    - `x = 10 as T` parameter initializers print the asserted annotation syntactically (isolatedDeclarationsAddUndefined).
    - Merged interfaces with differently named type parameters keep each declaration's own (TS2428,
      twoGenericInterfacesDifferingByTypeParameterName); tttt maps the second's to the first's.
    - **Reuse needs accessibility, which declaration emit's painting changes**: tsc's trackExistingEntityName only
      reuses a name that isSymbolAccessible (shouldComputeAliasesToMakeVisible false); a non-exported alias inside a
      non-exported namespace of a module isn't, so tsc prints its structure (instantiateContextualTypes,
      declarationEmitInlinedDistributiveConditional, inlineMappedTypeModifierDeclarationEmit,
      mappedTypeGenericInstantiationPreservesHomomorphism, deepKeysIndexing). But with `@declaration` the emit runs
      first and its late painting sets `isVisible` on what it emitted, so the same name is reused afterwards
      (privacyFunctionParameterDeclFile & co.). Adding symbol_type_accessible_at() to name_accessible() in ty_display.c
      was +5 -4 for that reason; doing it right needs the declaration walk's painted set kept for the types baseline.
    - **Naming from a block scope**: TyDisplay.enclosing is the file/namespace scope, so a name shadowed by a local
      where the type is printed isn't noticed: tsc qualifies with `import("f")` (enumAssignmentCompat6,
      expandoFunctionBlockShadowing, multipleExportDefault3, exportAssignmentOfExportNamespaceWithDefault), and a
      reused `typeof b` of a function local in its own return annotation falls back (functionVariableInReturnTypeAnnotation).
      Needs the innermost scope at the printed position (the resolve walk could record it per types-baseline node).
    - CommonJS naming (plan 26 territory): `typeof mod1."d"` for an element-access export with expando members
      (moduleExportsElementAccessAssignment: tsc's createEntityNameFromSymbolChain keeps the written quotes, but
      typeFromPropertyAssignment39's `foo["baz"]` prints `foo.baz`, rule unknown), `typeof x.F` for a JS class
      reached through a require (lateBoundAssignmentDeclarationSupport6: the value-meaning chain misses
      `module.exports.F = F`), jsDeclarationsFunctionsCjs, moduleExportAliasImported, moduleExportPropertyAssignmentDefault,
      jsdocTypeReferenceToImportOfFunctionExpression.
    - Unexplained so far: typeArgumentInferenceWithObjectLiteral (`x => x` gets E1.X: when T is fixed tsc has only
      non-widened candidates), inferConditionalConstraintMappedMember (tsc: never), wellKnownSymbolExpando (tsc: no
      late-bound expando for `f[Symbol.iterator] = ...` in a TS file), typeGuardRedundancy (tsc keeps both signatures
      of `x.substr | x.toFixed` in a conditional's subtype reduction), fakeInfinity2/3 (`() => Infinity`, not the alias).

## Left by fan-out agent A of 2026-10-05
Mechanisms found but not fixed (types-semantics tail):
- **Merged generic interface members aren't node-reused** (es2018ObjectAssign, usePromiseFinally, likely
  useRegexpGroups/doYouNeedToChangeYourTargetLibraryES2016Plus): `p.finally` of `Promise<number>` prints
  `(onfinally?: () => void)`, not the written `(() => void) | undefined | null`; a local generic interface declared
  twice behaves the same, once it doesn't (probe: `interface Q<T> { fin(cb?: (() => void) | undefined | null): Q<T> }`
  plus `interface Q<T> { x: T }`). tsc's reason not found (canReuseTypeNodeAnnotation looked equal); instrument tsc.
- **Contextual type of a `yield` is only known top-down**: fixed for `const y: T = yield` (UnaryOpExpr.declared_context);
  a yield as a call argument, assignment right side or return operand still gets its TNext only once the statement is
  checked, which is after a function expression's type is asked when it's called right away.
- **Static side resolution lowers the base's type arguments first** (cloduleGenericOnSelfMember,
  classDeclarationShouldBeOutOfScopeInComputedNames probably): `class S extends B<typeof S.Base>` reads `S.Base`
  through S's static members, which members.c builds after class_base() lowered the type arguments -> cycle -> any.
  tsc publishes the static members (exports + base constructor props) before default construct signatures.
- **Default `{}` of a nested pattern padded with the pattern's implied types** (destructuringParameterDeclaration10
  strict=false, third function): `{ a: { json = [] } = {} }` contextually typed: the `{}` gets `json?: undefined[]`
  (not widened, widenTypeInferredFromInitializer is literal-only), which subtype-reduces into string[]. tttt ends with
  any[] | string[]; making binding_element_implied_type literal-only as a contextual type wasn't enough (the element's
  own type in ty_binding_element_type widens the default fully before the union).
- **Spread of a union with a generic member** (spreadInvalidArgumentType o8): `{ ...(T | {a: number}) }` is
  `T | { a: number }` (getSpreadType maps over the union, a generic member stays itself); tttt spreads T's constraint.
- **Narrowable references during inference** (intersectionSatisfiesConstraint, voidReturnIndexUnionInference,
  mappedTypeIndexedAccessConstraint, narrowingTypeofUndefined2): constraint substitution of generics with union
  constraints differs where the contextual type is a call's resolved parameter; needs a tsc trace.
- **Enum member names are source text** (enumIdentifierLiterals `1.0` -> "1", enumWithUnicodeEscape1): EnumCase.name
  is a Span; tsc uses the numeric literal's canonical text and the cooked string.
- `export { undefined }` (reExportUndefined1): tsc resolves it to undefinedSymbol (type undefined); tttt's alias has
  no target. sliceResultCast: tsc keeps two identical-looking `slice` signatures of two tuple types (this-instantiated).
- Single-member enum (typeArgumentInferenceWithObjectLiteral): `enum E1 { X }` makes E1 the regular E1.X literal;
  tsc ends with the fresh E1.X for `x` in `f1({ w: x => x, r: () => E1.X }, E1.X)` (prints `E1.X`).

## Left by fan-out agent D of 2026-10-05 (symbols component)
Done: 13 commits, see git log (pattern-root circularity, static member + namespace type merges, heritage entity names,
widened objects for written/called properties, Unicode trivia in Decl positions...). Left, by mechanism:
Second round done: literal-method `this.x` members, `export default` of a local type + exported value, shared
type parameters of merged interfaces, unmergeable augmentations, augmentations shadowing `export *`, augmentation
export context, UMD alias declarations, JSX attribute merges, spread/widened property containers. Left:
- **`a: function () { this.x = v }`** (a function expression that isn't a JS constructor): tsc declares x in the
  function's own members, which changes its type (`typeof a`, constructible: `new o.a().x` is the member):
  objectPropertyAsClass, thisInObjectJs, functionExpressionNames (also `A = function () {}` and a binding default as
  constructors). Checker work on JS function types; checkExportsObjectAssignPrototypeProperty needs `this` in a
  `defineProperty(F.prototype, ...)` descriptor to be F's instance.
- **Spread of a namespace** (`{ ...ns }`): tsc keeps the export symbols (`stuff.func`); the copies would need the
  export symbol, which PropDecl.symbol can't hold without becoming the type source
  (spreadExpressionContextualTypeWithNamespace). nodeModulesTypesVersionPackageExports x3: the readonly const of a
  synthetic-default namespace prints bare in tsc (a clone), `mod.x` here.
- importEqualsError45874 (a global `import x = <unresolvable>` and a script's `const x`: tsc's mergeSymbol returns the
  source when the alias resolves to unknownSymbol, two symbols), moduleAugmentationWithNonExistentNamedImport (same,
  `declare global { namespace Bar }` against a UMD alias of an unresolvable import), duplicateVarsAcrossFileBoundaries
  (a global `var p` merging into the clone of the namespace an `import p = P` alias resolves to),
  moduleAugmentationDuringSyntheticDefaultCheck (a second augmentation through an `export = import * as` chain doesn't
  add its declaration). All are tsc's checker-time mergeSymbol through aliases; the binder merges before resolving.
- objectBindingPatternKeywordIdentifiers02/04: parser recovery of `{ while: while }` (two missing names in tsc).
- mappedTypeConstraints: f2's `obj.b` of `Pick<T | U, keyof (T | U)>` gets both declarations alone, one when f3
  (`Pick<T & U, ...>`) follows: some memo shared between the two mapped types (order dependent).
- isolatedDeclarationLazySymbols: `foo[o["a.b"]] = v` adds a declaration to foo (isDynamicName) but no member
  (not late bindable); tttt binds only entity-name keys. Adding the declaration alone needs care (expando-ness).
- reexportMissingDefault5: `export { default as Foo } from "./b"` (synthetic default) prints `default` where tsc says
  `"b"`; symbol_text finds an alias named default. aliasesInSystemModule1/2: `import cls = alias.Class` of an
  unresolved module: tsc prints `Class : Symbol(alias)` (unknown-symbol artifact). declarationFileNoCrashOnExtraExportModifier:
  `export { Sub }` inside `declare namespace exports` (merged with `export = exports` class) is `exports.Sub`.

## Validation
`./displaydiff.py` fixed-by-one counts for the worked buckets go down, suite up, no corpus regressions. A fix to an
inference rule that shows up here should be checked on zod and valibot (inference-heavy) for instruction counts too.

## Left by fan-out agent C of 2026-10-05
Missing/extra lines and JS tests. What remains, by mechanism (test names are examples):
- **Recovery shapes tttt's parser doesn't build**: `import { * } from` (tsc: a block `{ <missing> * <missing> }`,
  es6ImportNamedImportParsingError), `function a4([while, for, public])` (destructuringParameterDeclaration6),
  `<this.#prop />` (a property access whose private name becomes a missing identifier, privateNameJsx). Initializers
  of interface/type literal properties and index signature parameters (`bar: number = 5`) are dropped by the parser:
  tsc keeps and types them (errorOnInitializerIn{Interface,ObjectTypeLiteral}Property, indexSignatureWithInitializer1;
  needs an `Expr*` on InterfaceMember, resolved/checked/walked).
- **CommonJS entity-name aliases**: `exports.x = a.b` is an alias in tsc (isAliasableExpression), its type errorType
  when `a.b` doesn't resolve as an entity name (resolveNameWithNamspace); tttt binds a property. Same family:
  moduleExportDuplicateAlias{,2,3} (a property + alias symbol whose type is the alias's), lateBoundAssignmentDeclarationSupport6.
- `typeof import("./a").f<T>` (instantiation expression on an import type, typeofImportInstantiationExpression); `fx<>`
  (empty type argument list) has no node; `type B = BoxFunc<string>` of `typeof boxify<T>` keeps the alias name in tsc
  (instantiationExpressions' last line).
- `import { f } from "./a.js"` where f's JSDoc names a typedef of a.js: tsc reuses `MyType` unqualified
  (jsdocPropertyTagInvalid), tttt qualifies `import("/a").MyType`.
- `function toString() {}` in a JS script prints `typeof toString` in tsc, tttt its structure merged with something
  (inexistentPropertyInsideToStringType). callbackTag4: reuse of a `@this {{...}}` type literal's members.
- The other line-missing tests of the list were really display/accessibility (the queue's "reuse needs accessibility"
  item): recursiveConditionalCrash1-3, deepKeysIndexing, inlineMappedTypeModifierDeclarationEmit...

## Left by fan-out agent B of 2026-10-05 (types display)
Each named far enough to start from; tests are the examples still failing.
- **Type writer re-check** (contextualTyping36, conditionalOperatorWithIdenticalBCT): tsc's getTypeOfExpression rechecks
  each printed expression, so its object literals are newer than everything; bugs/display.md.
- **Name literals made eagerly by contextual typing** (es2022IntlAPIs): `contextual_prop_type()` builds
  `ty_prop_name_literal(name)` for every object literal property; tsc's getTypeOfPropertyOfContextualType only makes
  the literal for generic mapped types or index signature lookups. Making the key lazy is a hot-path refactor of
  get_prop_context()/intersection_prop_context(). Probe: `f({ timeZone: 'x' }); let k: 'calendar' | 'timeZone'`.
- **Lib literals made early** (correlatedUnions: dom's `CSSMathOperator` "sum" exists before the test's "concat";
  something lowers that alias, tsc never does). Not found.
- **Property annotations aren't lowered by the check walk** (typeInferenceWithExcessPropertiesJsx): tsc's
  checkPropertySignature calls getTypeOfSymbol, so `args: [] | [unknown]` in a type literal makes its tuples before a
  later alias body; tttt lowers member annotations lazily. Lowering them in interface_member_typecheck() would need the
  prop's own LowerCtx (ty_of_node differs, see the union comment in type_typecheck()).
- **Interface merged with a lib interface** (promisePermutations x3): see hacks.md maps_merged_type_param().
- **Members not instantiated** (arrayFlatNoCrashInference x2): `arr.flat` of `T[]` isn't an instantiation at all in
  tttt (members that can't mention T are kept), but Array is merged so tsc instantiates it: `depth?: D | undefined`,
  `this: A` not reused.
- **tsc's `fn_1`** (inferredReturnTypeIncorrectReuse1): an instantiated method `pipe<fn extends ...>(fn: fn)` prints
  its type parameter `fn_1` (the uninstantiated one `fn`); enterNewScope's params fake scope puts `fn` (or
  unknownSymbol) next to it. Mechanism not pinned down.
- **Local function symbol merges the exported overloads** (functionOverloadErrors: `export function fn1(); function
  fn1(n: string); function fn1() {}` in a namespace is one local symbol with three declarations in tsc, see
  statement.c function_local_symbol_decls()); overloadConsecutiveness (`m2(), m2() {}` in a class).
- **Clones per import** (crashDeclareGlobalTypeofExport `{ default: Root; }`, verbatimModuleSyntaxRestrictionsCJS's
  `Promise<typeof import("/decl")>` for a dynamic import where tttt names it `typeof esmy2`): queue item 2.
- **Ambient `export =` naming** (jsxNamespacedNameNotComparedToNonMatchingIndexSignature): an augmentation of `declare
  module "react" { export = React }` prints `typeof import("react")`, but augmentExportEquals3_1 & co print
  `import("file1.d.ts")` for the same shape; naming by the ambient module lost those 4.
- **Narrowing to `(A[] | [A]).every`** (arrayDestructuringInSwitch1, after the map_type origin fix): tsc keeps both
  members' (identical-looking) `every` signature sets, tttt one.
- **Inference**: genericRestParameters1 (`f10(...v)` with a tuple constraint gives `[...V]`, see the call.c comment on
  param_shape(); `f11(1, ...u)` keeps `1`), isomorphicMappedTypeInference (f22/f24 infer unknown),
  keyofAndIndexedAccess2 (union order of `window[action]`), noImplicitAnyUnionNormalizedObjectLiteral1/spreadUnion
  (spread of a union: member order), dependentDestructuredVariablesFromNestedPatterns, arrayFrom,
  complexRecursiveCollections (union order).
- **Others, one each**: declarationEmitPartialReuseComputedProperty (`[-1]` of a renamed computed name has `]:` not
  reused, an artefact), declarationEmitResolveTypesIfNotReusable (agent A's note), privacyGloFunc/privacyInterface
  (accessibility + declaration painting, agent A's note), mergedClassWithNamespacePrototype (`typeof Foo.prototype`
  for a namespace `prototype` merged into a class), typeAliasesDoNotMerge/multipleExportDefault3/enumAssignmentCompat6/
  expandoFunctionBlockShadowing/exportAssignmentOfExportNamespaceWithDefault (naming from a block scope or conflicting
  declarations, agent A's note), exportAssignmentMembersVisibleInAugmentation (augmentation into the export= target,
  bugs/display.md), symbolDeclarationEmit12/privateNameDuplicateField (duplicate members), ramdaToolsNoInfinite2 (alias chosen),
  typeParameterConstModifiers (queue item 6), jsxComplexSignatureHasApplicabilityError (T_1 + JSX props type).
