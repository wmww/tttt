# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-07 (triage session, todo entry 10 of the tenth window; suite 13299 -> 13303, 0 lost, every
corpus unchanged, sweep 8588 -> 8589, flagflip 51 / 47 / 11 -> 51 / 45 / 10, `$ExpectType` 68619 flat). Four Opus
sub-agents (type core + corpus cells, missing checks, display/text, JS/modules) took every item: 44 fixes, ~12 items
gone by earlier work, the rest written up into bugs/*.md, plan 26 Leftovers and hacks.md. Fresh mutation runs: seed 4
(`/tmp/tc-mutate4`, 26 -> 21) and seed 5 (`/tmp/tb-mutate`, 29 -> 25), leftovers in missing_checks.md.

- Plan 48 stage 4 leftovers (2026-10-07):
  - Mutation seed 2 (`./mutate.py -n 8000 --seed 2`, cache /tmp/mutate/c9c005c80317): 34 mismatches on tsc 6 (6 under
    5.8 after its drain): elaboration TS2322 x4, missing TS2339 x4 (overrideBaseIntersectionMethod: never-const),
    spurious TS9023 x3 (isolatedDeclarationLazySymbols), TS2304 in moduleElementsInWrongContext3, ... A drain session.
  - Named error types: tsc 6 prints an unresolved type reference as written (`t.Aliases` when `@babel/types` isn't
    installed) and keeps that error type through instantiation and indexed access (`NodePath<t.Aliases[K]>["type"]`
    is `t.Aliases`); tttt prints the reference by name but collapses what's computed from it to `any`
    (`$ExpectType` babel__traverse, babel__helper-plugin-utils: 19 assertions, broken packages only).
  - tttt never type-checks the default lib files; tsc does without skipDefaultLibCheck/skipLibCheck, so a lib
    combination that leaves a name undefined (react-intl-redux: `esnext.intl` without es2021.intl, TS2552
    DateTimeRangeFormatPart in lib.esnext.intl.d.ts) is a tsc error tttt misses (the sweep's last mismatch).
  - `$ExpectType` knockout.mapping-tests.ts:109: tsc orders `KnockoutObservableArray<KnockoutObservableType<any>> |
    KnockoutObservableArray<any>`, tttt the other way round (same target: type argument lists, or deferred references
    by node position?).
- valibot under the commit's tsc with `--ignoreDeprecations 6.0` (without it tsc skips semantic diagnostics after the
  TS5107): 26 TS2554 "Expected 1 arguments, but got 0" on `expectTypeOf<...>().toEqualTypeOf<...>()` in
  `src/methods/partial/partial.test-d.ts` and others, i.e. tsc 6 finds types unequal that tttt finds equal (before and
  after stable ordering). Missing-diagnostic lead for plan 48 stage 5 (valibot re-pin), 2026-10-07.
- typeofImportDefer (plan 48 stage 3, agent B): `typeof import.defer("./a").Foo` in a type: tsc's parseImportType recovery says '(' expected at the `.`, TS1141 over `.defer`, ')' expected at `(`, then TS2339 'Foo' on '"./a"'; tttt says TS2304 'import'. One test, quirky recovery.
- legacyNodeModulesExportsSpecifierGenerationConditions (agent C): in the synthetic-default object of `await
  import("inner")`, tsc doesn't reuse the `() => ` of `export const x: () => Thing` (Thing inaccessible from index.ts)
  where ty_display.c's "module's variable" annotation path reuses it partially; tsc's tryReuseExistingTypeNode seems to
  give up whole there. Find the rule before touching display_annotated().
- computedTypesKeyofNoIndexSignatureType (agent C): wants `K_1`..`K_9` because tsc's instantiateMappedType clones the
  key type parameter per instantiation (ty_mapped_info_instantiate() doesn't); declarationEmitHigherOrderRetainedGenerics
  renames the type parameters of a second overload (`O1_1`, not R1) of a printed overload set: tsc's name scope rules
  across sibling signatures, unexplored. (The value-parameter and mapped-key renames are done, plan 48 fan-out 2.)
- Plan 48 stage 3 agent D leftovers (types component), 2026-10-07:
  - thislessFunctionsNotContextSensitive1: a *failed* generic call's non-context-sensitive function argument
    (`F({ b: function () { return 123; } }, wrongArg)`) gets `() => number`; tsc keeps `() => 123` (its own signature is
    the contextual return type, as for a successful call: set_contextual_return / ty_late_contextual_return only see
    resolved calls). Also TS2820's target prints the alias `ExtractFields<...> | undefined`.
  - instantiateContextualTypes: `Promise.resolve().then(() => cond ? 'A' : 'B')` under a contextual `Promise<DooDad>`
    infers TResult as the alias DooDad; tsc infers the alias-less union `"ELSE" | "SOMETHING"` (failed under 5.8 too).
  - nestedGenericSpreadInference: tsc 6 removed SingleSignatureType's outerTypeParameters (getOrCreateTypeFromSignature)
    and isTupleOfSelf; `wrap(<T>(x: T) => x)` is `{ x: (x: unknown) => unknown }` in tsc 6, tttt keeps it generic.
  - destructuringParameterDeclaration-like strictness corners are done; jsxNamespacePrefixInName(React): `<a attr:=...>`
    recovery types `a` undefined in tsc, any in tttt (parser recovery).
  - Merged generic interfaces (9e63c0cb) instantiate every anonymous type of their members with the owner's type
    parameters like tsc (isTypeParameterPossiblyReferenced is true for multi-declaration type parameters): +3.8% zod
    instructions (ZodType<...> is merged, 53 member types now instantiated per schema type), +1% tsc. An identity-only
    cost (the instances are structurally equal); a relation fast path for two instances of one root whose syntactic
    free parameters map alike would win some of it back.
- Plan 48 stage 3 agent B leftovers (errors component, all pre-6.0 or display/elaboration, not new 6.0 checks):
  bigintPropertyName (bigint literal property names: 6.0 parses them, binds them as no member (`{1n: 1}` is `{}`), TS1539; parser + binder work);
  inlineJsxFactoryDeclarationsLocalTypes ('Its return type' must qualify `import("renderer2").predom.JSX.Element` like tsc's getTypeNamesForErrorDisplay; same 5.8 baseline, passed before the switch);
  complexRecursiveCollections (TS2430 elaborates `map(...).filter(...).filter` where tsc's first failing property is `map(...).size`: property comparison order);
  requireOfJsonFileWithoutResolveJsonModule (6.0 resolves the .json: tsc's JSON parser recovery on non-JSON text gives TS1005/TS1136 and an object type `{ contents: any; Not: any; read: any; }`, tttt gives TS1328);
  arityAndOrderCompatibility01, styledComponentsInstantiaionLimitNotReached, bigintWithLib, conditionalTypes1/2 (elaboration chain depth/order);
  parser recovery (plan 27): parser0_004152, jsxInvalidEsprimaTestSuite, thisTypeInFunctionsNegative.
- Plan 48 stage 3 agent B, third pass (2026-10-07), leftovers:
  - varianceAnnotationsWithCircularlyReferencesError: `<in in>`: tsc reads the second `in` as a reserved-word name
    (TS1359, a missing name, not consumed), then a second empty type parameter with a missing name (consumed by the
    list's no-progress rule); both are '(Missing)', TS2300, and share the In modifier through the merged symbol (TS2637
    twice). tttt fails the whole list; needs missing type parameter names.
  - scannerUnexpectedNullCharacter1: a NUL byte in the source ends tttt's parser (`*p.pos` is EOF everywhere); tsc
    reports TS1127 and goes on. Needs an end pointer instead of the terminator, or a loader-side substitution that the
    baseline printer undoes.
  - reachabilityChecksNoCrash1: the last stray `}` is TS1109 in tsc (an ExpressionStatement of a missing identifier),
    TS1128 in tttt; the rest matches since the object-literal `const` fix.
  - manyCompilerErrorsInTheTwoFiles: tsc's scanner returns Unknown after only the `#` of a `#!` (pos++), so the `!`
    is scanned again and recovery reports TS1134 at it; tttt's TS18026 mapping consumes both.
  - tsxStatelessFunctionComponentOverload1 (and `declare function f({a: b})` generally): TS7031 for a renamed binding
    element of a bodiless signature is reported by tsc only when something computes the parameter's type (a call, the
    overload/implementation compatibility check); tttt skips bodiless renamings eagerly. Lazy reporting (ty_error_at
    when the binding type is computed) would match.
  - JS `this.y = null` in a constructor function that nothing reads: tsc types the access as autoType (no TS7008);
    tttt reports TS7008 eagerly in js_assignment_implicit_any_check. Same laziness issue.
  - pushTypeGetTypeOfAlias: `exports.blah = exports.someProp` is an alias in tsc whose resolution reaches its own
    module's type: TS2303. tttt types it as a property.
  - jsdocFunctionTypeFalsePositive (types): the recovered `@param {<}` type `() => any` prints its return as a reused
    node; tsc's missing type node isn't reusable.
  - jsxComplexSignatureHasApplicabilityError: spurious TS2769 (no elaboration even) on a generic JSX element
    `<ReactSelectClass<ExtractValueType<WrappedProps>> {...props} .../>`; not investigated, likely a relation of the
    spread generic props against the conditional ExtractValueType.
  - Two non-tsc facts found: inference_count() (the non-fixing mapper cache key) ignored priority replacements (fixed);
    stableTypeOrdering(stabletypeordering=false) depends on type-id creation order and can flip with unrelated changes.
- enumWithBigint: tsc names a bigint enum member `__missing` (types show `E.__missing`) but the symbols baseline prints its declaration name (`E[0n]`); our symbol name drives both. Needs a name/printed-name split for enum members (stage 3, agent A).
- Plan 48 stage 3 agent D, second pass leftovers (types component), 2026-10-07:
  - moduleAugmentationDuringSyntheticDefaultCheck: tsc merges `declare module "moment-timezone"` into the clone that
    module's `export = moment` (a namespace import) resolves to, not into moment's own symbol; tttt merges into moment.
  - enumIdentifierLiterals / enumWithUnicodeEscape1: enum member names are their source text in tttt
    (EnumCase.name), tsc's are the numeric literal's canonical text (`1.0` -> "1", `0xF00D` -> "61453") and the cooked
    string (`'gold ✰'`); needs a canonical symbol name that isn't a source span.
  - accessorBodyInTypeContext: `type A = { get foo() { return 0 } }` (TS1183): tsc infers the getter's type from the
    body (number) and walks it; tttt's interface accessor without annotation is any and its body isn't walked.
  - jsdocParamTagTypeLiteral: `@param {string} [opts1.z]` members print `z?: string | undefined` in tsc (no `?` token
    to reuse); the structure printer can't tell such a prop came from a bracketed tag. Also its `foo2` with an inline
    `/** @param opts2 ... */` on the parameter is `{...}` in tsc, `{...}[]` in tttt.
  - importDeclWithExportModifierAndExportAssignment(InAmbientContext): `export = x` of a namespace holding only an
    interface prints `typeof x` in tsc (also under 5.8); tttt's harness prints any/error for an uninstantiated one.
  - nodeModulesJson (symbols): `config3.version` through `import * as config3` of a JSON module in a .cts prints
    `Symbol(version, ...)` unquoted in tsc, tttt quotes it like the JSON declaration's `"version"`.
  - variadicTuples1: several generic-tuple indexing/spread printing differences (`[string, ...T, number][1]` is
    `number | T[number]` in tsc...), old failures, worth a session of their own.
- Plan 48 stage 3 agent D, third pass leftovers (types/symbols, 2026-10-07):
  - noImplicitAnyUnionNormalizedObjectLiteral1: union order of two widened object literals with the same symbol (the
    spread literal) goes by type id; tsc's getWidenedTypeWithContext makes both members anew in member order, tttt's
    separate widen_nullable() pass keeps the unchanged `{ c: string; ... }` (older id) and remakes the other.
  - sliceResultCast: `x.slice` of `[number, string] | [number, string, string]` is a union of two equal signatures in
    tsc (each tuple's Array members are instantiated with the tuple as `this`, Array being merged makes `this` possibly
    referenced everywhere); retarget_this_props() only retargets props that mention `this` (a deliberate perf
    shortcut), so tttt's members are shared and the union collapses.
  - stringLiteralTypesInImplementationSignatures2: `{ foo(x: 'hi') {}, foo(x: 'a') {} }`: two methods are one binder
    symbol (MethodExcludes), its type has both signatures (`{ (x: "hi"): void; (x: "a"): void; }`, both names print it);
    tttt's literal keeps the last method's type. Needs ty_anonymous_from_overloads() of the methods plus the harness.
  - jsdocPropertyTagInvalid: a JSDoc typedef of another file prints bare (`(p: MyType) => void`) in tsc's types
    baseline, from JS and TS importers alike, while declaration emit says `import("./a.js").MyType`; tttt qualifies.
    serializeTypeName/trackExistingEntityName didn't explain it; instrument symbolToTypeNode under the typeWriter's
    flags (typeToTypeNode with IgnoreErrors, AllowUnresolvedNames) to find the rule.
  - jsdocTemplateClass: `/** @typedef {(t: T) => T} Id2 */` on a constructor is local to the class to tsc (not
    accessible where `foo`'s type is printed: expanded), tttt's file-level claim of the comment names it Id2.
  - thisInPropertyBoundDeclarations (48_lost): the literal `{ a: function() { return this; } }` of an unannotated
    property prints `{ a: () => any; }` in tsc 6 (the widened type, `this`'s, in a cycle) where tttt shows the fresh
    literal around it, `{ a: () => { a: any; }; }`; which type tsc's typeWriter gets for the expression is unclear.
  - moduleElementsInWrongContext: `declare module "ambient"` inside a block of a script is a global ambient module to
    tsc (a block is no container); tttt declares block contents during the resolve walk, after link, so it stays a
    module of its own and the block's imports of it are any. Imports in blocks now go to the container's table.
  - sourceFileMergeWithFunction: a global `declare function foo()` merges into the UMD alias of `export as namespace
    foo` (tsc's mergeSymbolTable of globalExports; the merged symbol's type is the alias's, `typeof import("./types")`).
  - mergedClassWithNamespacePrototype: a class's `prototype` export shadows a merged `namespace prototype` in tsc's
    exports table (bindClassLikeDeclaration overwrites it), so the namespace prints `typeof Foo.prototype`.
  - jsExportMemberMergedWithModuleAugmentation3 (symbols): tsc merges x.js's expando `module.exports.x` with y.d.ts's
    type alias x into one symbol (the export= clone's exports merge); tttt keeps two.
  - typeFromPropertyAssignment30: the expando `c.p = {}` of `const c: Combo = () => 1` is `p: {}` in tsc's type of the
    arrow; tttt takes Combo.p's declared type for it.
  - typeFromPropertyAssignment18: `var GLOBSTAR = m.GLOBSTAR = {}` in JS: the variable's expandos (`GLOBSTAR.p`) and
    `m.GLOBSTAR.q` go into one namespace, m.GLOBSTAR's (tsc's getExpandoSymbol through the chained assignment), printed
    `typeof m.GLOBSTAR`; tttt makes two.
  - uniqueSymbolsPropertyNames: a `unique symbol` member of an interface prints through a variable of that interface
    type in scope (`[Op.equal]` with `const Op: OpTypes`: getContainersOfSymbol's firstVariableMatch over every value in
    scope); out_unique_symbol_name() only knows the global `XConstructor` pattern. Asking ty_type_of_symbol() of every
    value in scope from the printer risks circularity reports; annotated variables only would do for this test.
  - tsc's serializedTypes cache across baseline lines (found on genericCallOnMemberReturningClosedOverObject, and the
    likely cause of thisInPropertyBoundDeclarations above): visitAndTransformType caches each object type's printed
    node per (enclosing declaration node, type id, flags) in NodeLinks.serializedTypes. The type writer's enclosing
    declaration is node.parent, so `>x : X` prints F = `<T2>(t2: T2) => any` (X in a cycle) under the
    VariableDeclaration, and the literal's line on the same declaration reuses that cached F instead of expanding X once
    more: `{ foo: <T2>(t2: T2) => any; ... }`. The tsc API without the earlier line prints the expanded form, like
    tttt. Needs the harness to pass each line's parent declaration and the printer to keep that cache (C's printer
    area: ty_display.c only models the within-one-print case).
  - functionOverloadErrors / overloadModifiersMustAgree: a namespace's local function symbol holds the exported
    overloads too (tsc's declareModuleMember: the exported declaration's local has flags ExportValue, which
    FunctionExcludes doesn't exclude), so the local's type lists every overload; tttt's local has only its own.
  - genericClassesRedeclaration: in a second `declare module TypeScript` block, `StringHashTable` resolves to the
    block's local whose exportSymbol is the duplicate (TS2300) class, printed qualified; tttt finds the first export.
  - importEqualsError45874: tsc's mergeSymbol returns the source when the target is an alias resolving to
    unknownSymbol, so a global `import Foo = globals.toString.Blah` and another file's `const Foo` stay separate
    (no error); tttt merges them at declare time, before aliases resolve.
  - expandoFunctionContextualTypesNoValue: `import Foo from "blah"` (unresolved) and `export function Foo` share the
    local (Alias|ExportValue), and `Foo` references reach the export through getExportSymbolOfValueSymbolIfExported:
    `typeof import("./x").Foo`; tttt's references resolve to the unresolved alias (any).
  - exportAsNamespace_augment / exportAssignmentMembersVisibleInAugmentation: a UMD global merged with a `declare
    global` namespace, and an `export =` namespace whose module exports grow by augmentations (getCommonJsExportEquals'
    clone): tsc prints `typeof import("./a")` / `typeof import("foo")` / `typeof foo`, tttt any/error; the harness
    helper namespace_export_equals_clone() covers only a module's own other exports.
  - genericCallInferenceUsingThisTypeNoInvalidCacheReuseAfterMappedTypeApplication1: `inner.log(100)` is
    `F<Effect<100, never, never>, this>` in tsc (a deferred conditional F over `this` that tttt resolves); not dug into.
  - moduleExportsAliasLoop1/2 (symbols): `exports.fn2 = Foo.min` names `Foo.min`'s symbol `fn2` (with min's
    declarations) in tsc: the alias-like CommonJS export; old failures.
- interfaceMergeWithNonGenericTypeArguments: `class M extends B<any>` (B not generic, TS2315) merged with `interface M extends I`: tsc also reports TS2346 on `super()` (no base constructor for that many type arguments), but not without the interface merge; the mechanism (how the merge changes getBaseConstructorTypeOfClass) is unexplored (stage 3, agent A).
- constructorWithIncompleteTypeAnnotation: tsc's baseline has the harness's TS-1 pre-/post-emit count mismatch (its TS7017 only post-emit); not matchable by a checker.
- Plan 48 stage 3 agent B, second half of A's list (A2), leftovers:
  libTypeScriptOverride*/libTypeScriptSubfileResolving* (libReplacement: `/// <reference lib="dom">` must resolve `@typescript/lib-dom` from node_modules; the harness's shared lib program can't be replaced per test);
  templateLiteralTypes1 (TS2590 goes at the innermost type node being checked: tsc's checkSourceElement sets currentNode per type node, tttt's type walk has no spans per TypeNode; template literal and tuple-spread cross products don't report TS2590 at all, ty/template.c TODO);
  largeControlFlowGraph (errors pass; types: tsc's first deep flow query comes from the end of the file, so every `data` is errorType there, tttt's from the 2000th statement);
  unicodeEscapesInNames02(target=es5) (tsc's baseline has the harness's TS-1 pre/post-emit count mismatch: not matchable);
  parenthesizedContexualTyping2 (TS2345 at the outer parentheses of the argument, and the third argument isn't contextually typed by the arity-matching overload);
  mergedDeclarations7 (namespace import of an export= clone: `this` return types compare the clone with the interface);
  recursivelyExpandingUnionNoStackoverflow (`type N<T, K> = T | { [P in K]: N<T, K> }[K]`: tsc doesn't cache an alias instantiation while it is being made, so it recurses with fresh types to the instantiation depth (TS2589) before a mapped symbol finds itself circular (TS2615, its message printing the mapped type unresolved `{ [P in "M"]: any; }`); tttt interns the instance and meets it again at once: TS2615 only. Not a count to tune).
- Plan 48 fan-out agent A (errors, M..j), leftovers, 2026-10-07:
  - circularBaseTypes, infiniteConstraints (spurious TS2321), circularConstraintYieldsAppropriateError (TS2310):
    tsc's getResolvedBaseConstraint circularity (stack of recursion identities, circularConstraintType,
    hasNonCircularBaseConstraint in getConstraintOfIndexedAccess) and the TS2313 "Circularity originates" currentNode
    are not modelled by relate.c's base_constraint(); circularConstraintYieldsAppropriateError also needs Foo's base
    type arguments (the default `C['someProp']`) instantiated while Foo's base types resolve. Plan-sized (plan 39 area).
  - awaitedTypeNoLib: without lib tsc narrows `result instanceof Thenable` keeping NotPromise<TResult> (TS2345) and
    reports TS2318 'Awaited'; where the Awaited lookup comes from was not found.
  - declarationEmitBindingPatternsUnused: interface accessors keep only the setter's parameter type node
    (parse_interface), so `set x({ name: alias }: Named & { o: typeof alias })` has no parameter scope (spurious TS2304)
    and no TS2842 for an unused renaming. Needs the accessor's FunctionImpl kept in InterfaceMember.
  - jsDeclarationsClassStaticMethodAugmentation: `Clazz.method.prop = 5` (expando on a static method): tsc's class
    exports hold static members; tttt's lookup_for_property_access() finds no symbol for `Clazz.method`.
  - jsExportMemberMergedWithModuleAugmentation2: `module.exports = { a: "ok" }` is a ValueModule export= that a
    `declare module "./test"` augments (TS2300 with the literal's `a`); tttt says TS2671.
  - JS declaration emit diagnostics not implemented: TS9005/TS9006 (jsDeclarationsExportAssignedConstructorFunctionWithSub,
    jsDeclarationsTypeReassignmentFromDeclaration2), TS6232 (jsDeclarationsCrossfileMerge), TS18042 for a JSDoc
    `@typedef ns.x` namespace imported by name (jsDeclarationsImportAliasExposedWithinNamespace), TS5088
    (arrayFakeFlatNoCrashInferenceDeclarations: cyclic).
  - ensureNoCrashExportAssignmentDefineProperrtyPotentialMerge: `B.NS = require(...)` + `Object.defineProperty(B, "NS")`
    are TS2323 in tsc (both declare NS of a module.exports'd container).
  - genericCallWithGenericSignatureArguments3 (strict false): tsc infers U = `(n: Object) => 1` for a nested arrow's
    literal return where tttt widens to number (contextual return through U in inference).
  - TS4110 for an array node in an alias (`type A = A[number][]`-like) still reports at the current node with the tuple
    text; tsc says TS4109 'Array' at the array node. No test.
- Plan 48 fan-out 2, agent C leftovers (types component), 2026-10-07:
  - allowJscheckJsTypeParameterNoCrash: in JS, `this.data2 = 1` inside an object literal method `data1` merges into the
    literal's `data2` method symbol (both declarations in the symbols baseline), and tsc then drops data2 from the
    literal's type (`{ data1(val: any): void; }`, `this.data2 : any`); tttt keeps them apart. Binder-level quirk.
  - callSignaturesWithParameterInitializers2: `{ foo(x = 1), foo(x = 1) { } }` (a bodiless method then the same name)
    are one symbol of two signatures in tsc (`{ foo(x?: number): any; foo(x?: number): void; }`), tttt keeps the last.
  - bundledDtsLateExportRenaming: with outFile tsc names modules non-relative from the common source directory with
    minimal endings (getSpecifierForModuleSymbol's isBundle: baseUrl = common source dir, `non-relative`):
    `import("nested").B`; module_specifiers.c has no bundle mode (needs the common source directory in Generator).
  - augmentExportEquals7: same mechanism as D's exportAssignmentMembersVisibleInAugmentation (mergeSymbol of an
    augmentation into an `export =` target clones it; the clone is what the augmentation name means).
  - accessorBodyInTypeContext (above) stays: needs the getter body of a type literal checked and its return inferred.
- Plan 48 fan-out agent A, complexity-limit list (A2), leftovers, 2026-10-07:
  - longObjectInstantiationChain1-3 (errors): TS2339's message type (no enclosing declaration) is elided by tttt
    (`Omit<Omit<..., ...> & ...`) where tsc prints the whole chain; printer, agent C's symbolDepth area.
  - recursiveConditionalTypes, styledComponentsInstantiaionLimitNotReached: elaboration chains (tsc's goes deeper /
    through other members), not a limit. mappedTypeRecursiveInference: member order of the printed reverse-mapped
    `Deep<...>` (tsc has `dispatchEvent` first).

## Corpora re-pin (plan 48 stage 5): webpack, nest, react
- **nest** (pin unchanged, `tsconfig.corpus.json` with `strict: false`): tttt 0. **react** (pin unchanged,
  `tsconfig.corpus.json` with `baseUrl: null`): tttt 0 (after making a JSON `null` in compilerOptions unset baseUrl and
  the booleans; other string/list options still ignore `null`).
- **webpack** (new pin 86d31e06, TS ^6.0.3, tsc 0): tttt 0 (from 113: the `if (c);else` parser fix, `module.exports=`
  inside comma expressions, JSDoc `import("./m").C<A>` of an expando class applying its type arguments, no TS2440 for
  `@import`, no TS2344 in `@returns`, no TS7008 for a method's `this.x = []` that the base class's x overrides).
  Flagflip webpack left (all other webpack cells 0/0):
  - unused: 10 missing TS6133/TS6196, all imports used only inside a JSDoc `{T=}` of a `@typedef`'s `@property`
    (lib/Compilation.js:117, module/ModuleFactory.js:8). tsc's checkSourceElement never visits a JSDocOptionalType,
    so such a name is referenced only if something resolves that property's type before the file's unused check
    (order-dependent laziness); tttt marks every name when binding. Emulating it needs reference marking at lowering
    time plus a checker-count-independent cutoff: plan-sized, not worth it for these alone.
  - nuia: TS2488 'string[] | undefined' reported once per union (tsc caches noIterationTypes); tsc's first report is
    inside an expression-bodied reduce callback (syntax-printer.js:16066, checked during inference), tttt's at a later
    spread (16069) because it reports the callback body when it checks it, later. The once-per-type emulation in
    iterate.c follows tttt's check order, which differs from tsc's for inference-time return expressions.
  Seen on the way (tsc differs, not in a corpus): `const old = this.labels; this.labels = []; this.labels = old;` in
  two methods of a class extending any: tsc TS7022 on `old` (first method) and on `labels` (first assignment), tttt
  TS7022 on both `old`s. And tsc ignores a JSDoc comment written after `{` on the same line
  (`constructor() { /** @type {string[]} */ this.x = []; }` is TS7008 under tsc), tttt attaches it.

## Corpora re-pin (plan 48 stage 5): xstate, tanstack-router, vue
- xstate @ 38dcaffb (Oct 2026, TS6 upstream): tsc 4d4f005c 0, tsgo 7.0.2 0, tttt 0 (648 files, 0.74 s). The old
  pin's 30 strict-by-default errors are fixed upstream; the overlay is now just upstream's root tsconfig limited to core.
- vue @ 4ab865a8 (Sep 2026, TypeScript ~6.0.3 upstream): tsc 4d4f005c 0, tttt 0 (760 files, 1.15 s). tsgo 7.0.2
  reports 10 (also at the old pin, upstream doesn't run tsgo): TS2578+TS2769 x3 in dts-test/defineComponent.test-d.tsx
  (the overload error lands lines below the `@ts-expect-error`), one pair in setupHelpers.test-d.ts, TS2322 in
  functionalComponent.test-d.tsx:25, TS2345 in runtime-dom/src/directives/vModel.ts:446 (`DirectiveBinding<any,
  string>` vs `<any, "number">`). tsgo-vs-tsc 6 divergences; worth a look when tsgo 7 becomes the oracle.
- tanstack-router @ 30fc108a (Oct 2026, TS6 upstream): tsc 4d4f005c 0, tsgo 7.0.2 0, tttt 0 (626 files, 0.89 s).
  The three runtime tests excluded for 5.9's lib.dom are back in; the old pin's 27 TS2591 (`types: []`) are gone
  because react-router's (and router-core's) own tsconfigs now say `types: ["node"]`, which the overlay extends.

## Corpora re-pin (plan 48 stage 5): vscode, typescript-eslint, excalidraw
- excalidraw (53973c3a, Oct 2026, upstream on 5.9.3; tsc 4d4f005c and tsgo 7.0.2 clean): tttt 0.

## Corpora re-pin (plan 48 stage 5): type-fest, valibot
- type-fest @ e9f614f1 (v5.10.0, upstream on ^5.9.2; tsc 4d4f005c and tsgo 7.0.2 clean): tttt 0. Peak RSS 2.3 GB
  (IntRange<0, 999> tests): check in the perf session.
- valibot @ d4da61a0: tttt 0.
- Flagflip left in valibot/xstate/excalidraw cells (drizzle, effect, tanstack-router clean):
  - valibot/nosnc -TS2578 x2 (forward.ts:51, forwardAsync.ts:55): iterating `path: ValidPath<TInput, TPath>` (a
    deferred conditional) gives tsc a generic key (`KeyOf<TInput> | KeyOf<TInput[...]> | ...`), so `unknown[key]`
    is no error; tttt iterates the conditional's constraint and gets `string | number` (TS7053). Iteration types of a
    deferred conditional type vs tsc's apparent type/base constraint.
  - xstate/sloppy,nosnc +TS2589 at graph/utils.ts:91 (`snapshot.getMeta()[id]`, Record<StateId<any> & string, ..>):
    a self-contained probe gives TS2589 on both sides, so it's which instantiations tsc has cached by then.
  - xstate/eopt +TS2322 input.test.ts:272: generic Spawner signatures compared, `options` intersections that print the
    same are unrelated in tttt (`input?: number` & `input: unknown` reads number | undefined against number).
  - excalidraw/nuia TS2488 -2/+1: tsc reports TS2488 once per union type object (getIterationTypesOfIterable caches
    noIterationTypes); tsc's array literal `[0, 0]` is its own tuple type object (ObjectFlags.ArrayLiteral), so
    `cond ? pts[1] : [0, 0]` and `outline[0]` are different unions there and the same one in tttt.
  - excalidraw/nonia TS2345->TS2769 renderOverrides.test.tsx:527 (`entries.map` inside `new Map`, entries from an
    `it.each([...] as const)` callback): cut-down probes match tsc; not reduced further.
  - 9751c955 (TS2344 on deferred conditional arguments) costs valibot ~0.75%: mostly one relation,
    `TSchema extends ... ? IssueDotPath<TSchema> : string` against keyof any (Record's K, flatten.ts:41), ~13 ms;
    legitimate work tsc does too, but tttt's relation through IssueDotPath's constraint may be slower than needed.

## Corpora re-pin (plan 48 stage 5): effect
- effect stays at e730f59d (v3 branch head, v3.22.2, upstream on TypeScript 5.8.3): main is Effect 4 on TypeScript
  ^7.0.2 (tsgo, `@effect/tsgo` patch) and isn't clean under tsc 6: its packages/effect/src alone gives 2 TS2345
  (SchemaAST.ts:3109, SchemaCompiler/runtime.ts:73, an Exit-vs-Effect callback union under exactOptionalPropertyTypes)
  where tsgo is clean, so it can't be checked against our oracle. Revisit (a second, Effect 4 corpus) when tsgo 7
  becomes the oracle. tsc 6 also took 43 s on that src vs tsgo's 8.6 s: a heavy perf corpus candidate.
- The overlay unsets the base config's deprecated `downlevelIteration` (no effect at ES2022) and `esModuleInterop:
  false` with `null` (6.0's default true; the checker sees the same under NodeNext): tsc 4d4f005c 0, tttt 3 (the known
  TS2578 x3 in dtslint Tagged{Class,Error,Request}, plan 19). tsgo 7.0.2 reports 3 that tsc 6 doesn't (upstream
  doesn't run tsgo on v3): TS2345 at src/Schema.ts:3770 (`typeSchema(self)`: `SchemaClass<any, any, never>` not
  assignable to `S extends Schema.Any` inside `dual(2, <S, FD>(...) => transformOrFail(self, typeSchema(self), ...))`)
  and TS2769 x2 in packages/vitest/src/internal/internal.ts:132,153. tsgo-vs-tsc 6 divergences, like vue's.

## Corpora re-pin (plan 48 stage 5): zod
- zod @ 0b216ef6 (2 Oct 2026, main's head; upstream still on TypeScript ~5.5.4): tsc 4d4f005c 0, tsgo 7.0.2 0, tttt 0
  (packages/zod/tsconfig.test.json, no overlay needed: tsc 6 reports no deprecations on it). No tttt bugs to drain.
- zod-root: tsc 6 53 (26 TS2307 bench/fixture deps not in the lock, 14 TS6133 + 8 TS6196 unused, TS2345/TS2403/
  TS2412/TS2305/TS7016 singles), tttt the same 53 (identical file:line:code). tsgo 7.0.2 runs away on it (18 GB RSS
  after 17 min single-threaded, then killed; `tsc -p .` multi-threaded was OOM-killed too). Not bisected: which
  package/file blows up tsgo might be a perf hazard worth knowing about (packages/bench and scripts/ are new since the
  old pin).

## Corpora re-pin (plan 48 stage 5): typescript-eslint
- typescript-eslint @ 70629871 (Oct 2026, TS6 upstream): tsc 4d4f005c -b 0, tsgo 7.0.2 -b 0, tttt 0 (its 4 TS2345, a
  widened `value: 'await'`, were indexed accesses of a reducible intersection resolved eagerly; fixed).
## Corpora re-pin (plan 48 stage 5): vscode
- vscode @ 2afa73ea (main's head, 2026-10-07; upstream builds with `@typescript/typescript6` ^6.0.2 and checks with
  tsgo 7.0.2): checked through src/tsconfig.corpus.json from the overlay, upstream's src/tsconfig.json minus 3 test
  files tsc 6 reports TS7022/TS7024 circularity in (9 errors; tsgo 7 is clean on them, see below): tsc 4d4f005c 0,
  tsgo 7.0.2 0. The program more than doubled since the Mar 2025 pin: ~10.1k source files / 3.41M lines (+870
  node_modules files; the agent host, chat and sessions code), so walls/RSS/instruction counts aren't comparable with the old pin's. tsc 6 needs more than
  8 GB of heap (OOM at `--max-old-space-size=8000`); tttt's typecheck peaks at 5.5 GB resident with 4 checkers.
- tttt 1 (8 at this pin's start; 414 before the enum evaluator followed `N.C` to a namespace's const variable: ~400 of them came from
  vscode's `AgentSessionProviders` enum over `namespace SessionType { export const Local = 'local' }`). Left:
  - (Fixed: an object literal's own getter spread over an optional property was typed as its function. Left from
    that probe: `{ set a(v: string) {}, ...p }` with `p: { a?: number }` is `{ a: number | undefined }` in tttt,
    `{ a: number }` in tsc; and the order/printing of `{ get g() {..}, ...(x as Partial<R>) }`: tsc sorts R's
    properties first (first declaration) and prints them `name?: string | undefined`.)
  - tsc 6 vs tsgo 7 (why the overlay excludes agentFinderRestProvider.test.ts, codexProviderConfiguration.test.ts,
    extHostNotebookStatusCommands.test.ts): `class C { count = 0; constructor(r: () => number) {} } function id(n:
    number) { return n; } const c = new C(() => id(c.count));` is TS7022+TS7024 under tsc 6 and clean under tsgo 7.0.2.
    tttt matches tsgo on that file but reported 3 of tsc's 5 pairs in vscode (agentFinderRestProvider.test.ts:613,
    codexProviderConfiguration.test.ts:148/156; not :138 nor extHostNotebookStatusCommands.test.ts:37), so its
    circularity detection there is neither oracle's. Decide which behaviour to follow when tsgo 7 becomes the oracle.
  - TS2322 at stateToProgressAdapter.test.ts:1956 (the last vscode one): `{ ...image, data: '' }` spreads the
    declared union `ToolResultContent` instead of `image`'s initializer-narrowed member (`const image:
    ToolResultContent = { type: EmbeddedResource, ... }`). Clean in single-file imitations and with only the
    sessionState imports; reproduces with the whole test file copied into a probe project (imports rewritten to
    absolute paths, `types` + node): an automatic reduction got it to ~2200 lines without finding the trigger.
- Flagflip vscode nosnc/sloppy/nonia/nosft/nuia (agent C, 196 missing / 6 spurious -> 5 / 0). Left:
  - nonia resultCodeEditorView.ts:170-192 TS2339 x5 on '{}': with noImplicitAny off tsc reports isConflicting
    missing on `m.left` (TLeft of the generator `join` inferred as unknown) at every use after
    `if (x === active)`, tttt only at the last one; a hand-written imitation infers TLeft and is clean.
  - (Fixed: nosnc agentHostAutomationService.test.ts:680 TS7018, for-of `const` variables weren't constants. The
    destructured-reference / widened-literal mechanism behind it is written up in plans/49_destructured_widening.md.)
  - nosft copilotAgent.ts:5860 TS2339 'never' (fixed by something on ai-main by the time of the last run; if it
    comes back: `(await p.catch(() => [])).find(w => ...)` with `readonly W[] | never[]`: tsc's union property
    type orders never[]'s `find` first and its union signature makes w never; tttt's id order put readonly W[]'s
    first).
- Flagflip vscode/eopt and vue cells, left after agent D's pass (each reproduced, root cause known, not fixed):
  - TS7006 x18 (vscode/eopt chatToolProgressPart.test.ts): tsc's addImplementationSuccessElaboration runs
    chooseOverload([implementation]) on a failed overloaded call, which is the *first* Normal-mode check of a
    context-sensitive arrow after the failing argument; the implementation's `...rest: unknown[]` gives it no
    contextual signature, ContextChecked is set, its parameters are implicitly any. tttt's add_implementation_success
    then rechecks the arguments against the overload, and tttt has no "context checked without a signature" state for
    context-sensitive functions (KEY_CONTEXT_NONE is only for non-sensitive ones). Repro: a class (not interface)
    `createInstance` with 2 overloads + implementation `(c: any, ...rest: unknown[])`, a failing first argument, then
    `{ render: (md, opts) => ... }`.
  - TS2719->TS2322 x3: tsc's CLI messages truncate long types (typeToString's defaultMaximumTruncationLength, `...
    N more ...`), and when source and target truncate to the same text the message is TS2719. tttt never truncates
    message types (the suite runs with noErrorTruncation). Plan-sized display work, CLI only.
  - eopt elaboration choice (extHostTypeConverters.ts:673/688, chatWidget.ts:2214, copilotChatSessionsProvider.ts:904,
    agentSideEffects.test.ts:5768): tsc reports one nested property (TS2412 at e.g. `viewModel`) where tttt reports
    another (TS2375 at a `styles: { a: o.a }` literal whose read keeps `missing`). Repro e.ts-style: `plain("c", {
    styles: { a: st.a, b: st.b }, viewModel: vm })` with `vm: CVM | undefined` failing structurally: tsc reports only
    viewModel, yet `{ s: { a: st.a } }` alone is reported at s. Not understood.
  - Errors tsc reports from loop flow evaluations: `let v; v = r[u - 1]; while (..) { v = p[v] }` (vue/nuia
    renderer.ts:2633 TS2538) and `node = node.nextSibling` over an index signature (vue/npafis Teleport.ts:562 TS4111):
    the reference's final type is circular any, but tsc's checkExpression of the assignment source during the loop's
    flow analysis (with the incomplete type) reported. tttt only reports from the typecheck pass.
  - vue/nir TS7030 compat/props.ts:18: a return expression contextually typed `any` (Proxy handler `get`) should make
    `inject<T>(key)` infer T = any (so the inferred return is any and noImplicitReturns is quiet). tttt's
    set_contextual_return ignores an `any` contextual return type; dropping that (only `void` ignored) fixes the probe
    but gives tanstack-router 9 TS2344 (expect-type tests): something else relies on it.
- Mutation testing multi-file/JS (`MUTATE_DIR=/tmp/d-mutate ./mutate.py --tests multi,js -n 3000 --seed 12`): 31 of
  3000 differ, 20 after agent D's fixes; all singletons except `text TS2339` x3 (augmentExportEquals3/4: `import * as
  x` of an `export =` class+namespace that a `declare module` augments prints as `typeof import("file1")` in tsc,
  `typeof foo` in tttt: tsc's getCommonJsExportEquals clone, cjsExportMerged). Others seen: `module.exports = items`
  before `const items = []` under `@type {typeof import(...)}` loses the annotation (jsDeclarationsTypeReassignment
  FromDeclaration), and typeFromParamTagForFunction's `declare function require(id: number)` gets TS2345 at the
  require calls in tsc only when other files of the program are ES modules (not reduced).
  - Unreduced: TS7006 x3 + TS2365 `'{}' + number` in inlineEdits longDistanceHint (`derived(this, r => { ... return obs.map(heights => ...) })
    .flatten()` with overloaded `derived`/`map`; a single-file imitation is clean), and TS2322 at
    stateToProgressAdapter.test.ts:1956: `{ ...image, data: '' }` spreads the declared union `ToolResultContent`
    instead of `image`'s initializer-narrowed member (`const image: ToolResultContent = { type: EmbeddedResource, ... }`;
    also clean in a single-file imitation; the const enum is re-exported through sessionState.ts).

## Mutation testing, single-file seeds 11, 13, 14 (4000 mutants each)
Drained to these leftovers (`MUTATE_DIR=/tmp/a-mutateN ./mutate.py --seed N -n 4000` regenerates them):
- Order of two diagnostics at one position (TS2741 before TS2454 at `[d1, d2]` against `Base[]`,
  derivedTypeDoesNotRequireExtendsClause with `extends Object`): tsc's check order, not code order.
- Circularity only an elaboration finds: `const a = { prop: 42, get self() { return a } } satisfies { prop: string;
  self: any }` is TS7022+TS7023 under tsc (elaborateError types the getter while `a` is resolving), clean in tttt.
  Same family: a duplicate late-bound getter (`get [Symbol.hasInstance]()` twice) gets TS2355 from the merged
  symbol's type (symbolProperty44), and recursiveClassBaseType's TS2310/TS2506 once `root()` returns void.
- Display: a union printed from a generic call's argument loses its origin (`(Object | SecondInterface) & {...}`
  shown as `Object & {...}` after Object.assign; intersectionSatisfiesConstraint); `MyMap<M>[keyof M] | undefined`
  as a TS2322 target is shown unsimplified where tsc shows the best member normalized (indexedAccessNormalization);
  structurally equal spread object types are one type in tttt, two members in tsc's union (spreadUnion, TS2403 text);
  the 11th `{ y2: ... }` level is `...` in tttt where tsc has a real `any` (cyclicGenericTypeInstantiationInference).
- `interface C1 extends Object?.Bar {}`: tsc resolves an optional-chain heritage expression as an entity name too
  (TS2702/TS2503/TS2694 besides TS2499); tttt only reports TS2499.
- Variadic inference with a spread of any in the middle: `pipe(1, ...anyArr, (...x) => {})` against
  `pipe<T extends unknown[]>(...args: [...T, (...values: T) => void])` infers T but gives x no contextual type
  (spurious TS7019); with `string[]` it works (variadicTuples2).
- allowUnescapedParagraphAndLineSeparatorsInStringLiteral with a closing backquote deleted: parser recovery (plan 27).
