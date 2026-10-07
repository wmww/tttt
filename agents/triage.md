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
  - typescript-eslint (`-b`): 1 TS2345 'parameter of type never' at eslint-plugin/src/rules/naming-convention.ts:719,
    predates this session (session start's build reports it too); check it against tsc 6 when stage 5 re-pins.
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
- **webpack** (new pin 86d31e06, TS ^6.0.3, tsc 0): tttt 113, by code TS2349 x43, TS2345 x31, TS2339 x29, TS1128 x6,
  TS2440 x2, TS2344 x1, TS7008 x1. Root causes seen:
  - Parser: `if (c); else {...}` (empty then-statement followed by `else`) gives TS1128 at `else`
    (`if (t);else{t.y=2}` reproduces). In the minified schemas/WebpackOptions.check.js.
  - CommonJS: `module.exports=r,module.exports.default=r;` (export assignment inside a comma expression, the end of
    every generated schemas/**/*.check.js) isn't bound as the module's export: `require("./x.check")(...)` gives TS2349
    "not callable" (most of the 43) and `module.exports.default` TS2339.
  - JSDoc generics: `@typedef {import("../util/SourceProcessor").PrintContext<CssPath, Node, CssPrintOptions>}` (a JS
    class with `@template TPath, TNode, [TPrintOptions=object]`) keeps its type parameters uninstantiated: TS2345
    "'Node' is not assignable to 'TNode'" and TS2339 on `writer.options.X` (`PrintOptions & TPrintOptions`), in
    lib/{css,html}/syntax-{parser,printer}.js.
  - Singles: TS2440 `/** @import { SharedModuleFilter } from "./SharePlugin" */` vs a local declaration
    (sharing/*SharedPlugin.js), TS2344 `InstanceType<ExportInfo>` (graph/ExportsInfo.js:462), TS7008 `labels` in
    javascript/syntax-parser.js:15742.

## Corpora re-pin (plan 48 stage 5): xstate, tanstack-router, vue
- xstate @ 38dcaffb (Oct 2026, TS6 upstream): tsc 4d4f005c 0, tsgo 7.0.2 0, tttt 0 (648 files, 0.74 s). The old
  pin's 30 strict-by-default errors are fixed upstream; the overlay is now just upstream's root tsconfig limited to core.
- vue @ 4ab865a8 (Sep 2026, TypeScript ~6.0.3 upstream): tsc 4d4f005c 0, tttt 0 (760 files, 1.15 s). tsgo 7.0.2
  reports 10 (also at the old pin, upstream doesn't run tsgo): TS2578+TS2769 x3 in dts-test/defineComponent.test-d.tsx
  (the overload error lands lines below the `@ts-expect-error`), one pair in setupHelpers.test-d.ts, TS2322 in
  functionalComponent.test-d.tsx:25, TS2345 in runtime-dom/src/directives/vModel.ts:446 (`DirectiveBinding<any,
  string>` vs `<any, "number">`). tsgo-vs-tsc 6 divergences; worth a look when tsgo 7 becomes the oracle.
