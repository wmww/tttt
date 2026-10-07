# Display bugs (types/symbols baselines)
What tttt prints differently from tsc, where the underlying type is right (or nearly). `./displaydiff.py` ranks the
mechanisms across the suite (`other` is mostly type-core bugs the types baseline shows first); these are the known
rules. Plan 21 (closed) did the bulk. Named tests were checked failing on 2026-10-05 (suite 13098); probe files
(`*.ts` without a test name) weren't re-run.

## Aliases
- `shows_alias()` (ty_display.c) hides type aliases declared in a block/function body when printing for a place.
  tsc's isDeclarationVisible also hides non-exported aliases inside namespaces in some cases; unverified.
tsc's rule, as far as it is understood (getTypeAliasInstantiation, getConditionalType, instantiateMappedType,
getIndexedAccessType): a type gets a *new* alias name only when it is the body of an alias declaration (`type X =
Foo<...>`) and that body instantiation created it: an object type (not a homomorphic mapped type over a non-union), a
union made by distributing a conditional or an indexed access, a deferred conditional/indexed access. A deferred
conditional keeps its root alias; a resolved one is whatever its branch is. Both halves are in `lower.c` (the
`renames` logic of TYPE_IDENTIFIER) and `instantiate.c` (TY_CONDITIONAL, `ty_realias`). Still wrong (probes in
`test/probes/plan19/valibot/`):
- keyof_enum_object.ts: `type V = EnumValues<typeof specialEnum>` (a mapped type indexed by `keyof`, resolving to all
  the enum's members) prints `V`, tsc `specialEnum`. Probably: a union that is exactly an enum's members is the enum
  type in tsc (same interned union), and the alias doesn't win.
- symbol_key_string_index.ts: `E[K[number] & string]` stays deferred where tsc resolves it to the property type.
  Evaluation, not display.
- optional_mapped_index_signature.ts: an error message prints `C` (alias) where tsc prints the structure.
- An imported alias (`import { Simplify } from ...; type X = Simplify<A>`): tsc passes the import's target alias as
  the new alias (getTypeFromTypeAliasReference's `resolveTypeReferenceName(node, SymbolFlags.Alias)` branch). Not
  looked at in tttt.

## Structure
- A union containing an intersection with a nullish member prints without parentheses: `T | undefined & {}` where
  tsc prints `T | (undefined & {})` (without strictNullChecks, where it isn't reduced).
- Union order of literal keys follows type ids: the rest of a generic `this` is `Omit<this, "getter" | "setter" |
  "method" | "publicProp">` in tttt, `"publicProp" | ...` first in tsc (the pattern's key literal is older there);
  destructuringUnspreadableIntoRest fails on this alone.
- **Deferred tuple access**: `p[1]` with `p: [string, ...T]` prints `T[number]`, tsc keeps `[string, ...T][1]`
  (variadic_tuple_iterate.ts). tttt resolves the element access through the variadic part eagerly.
- **union_signatures_with_intersection.ts**: union member order and `(A | B) & C` vs `(A & C) | (B & C)` of signature
  types; `T & readonly [...]` where tsc prints T's constraint.
- **Numeric-named properties**: tsc prints a property declared with a numeric literal name as written (`1.1`,
  `9.671406556917009e+24`) and a string-literal one quoted (`{ "1": string }`); the suite's cases pass.
  `Prop.name_is_value` doesn't tell a number name from a string one, which shows in object literals:
  `{ "0": 0 }` prints `{ 0: number; }`, tsc `{ "0": number; }` (probe: `var m:
  MyMap<number> = { "0": 0 }`, types baseline).
- `{ 2n: 1 }` is `{ 2: number }` here, `{}` in tsc (plus TS1539, `bugs/missing_checks.md`).
- tttt instantiates member signatures of generic classes/interfaces that can't reference the mapped type parameters;
  tsc keeps them (getObjectTypeInstantiation filters with isTypeParameterPossiblyReferenced). Display compensates
  (`mentions_mapped()`, hacks.md); doing it in `instantiate.c` would be correct and cheaper. Measure on
  valibot/type-fest.

- **Order dependence of `Array<T>` vs `T[]`**: an annotation's display depended on when the global Array type was
  first built (moot since plan 37 D builds Array at checker start), but the mechanism is still there for other global
  generics: if `X<T>` vs another spelling flips with `-f` vs a full run or with `--checkers`, find what the printer
  keys on.

- tsc's type writer re-checks every expression it prints (getTypeOfExpression -> checkExpression, uncached), so the
  object literal types in an expression's printed type are newer than everything the main check made: `true ? {} : 1`
  prints `1 | {}`, and in `[{ id: 4 }, <{ id: number }>({})]` subtype reduction keeps the type literal (mutual subtypes:
  the older stays) while the variable's own type, from the main check, keeps the object literal
  (conditionalOperatorWithIdenticalBCT, contextualTyping36). tttt checks once; emulating it would mean re-checking or
  re-sorting by "made in this expression".
- strictFunctionTypesErrors: `typeof Foo.f1` (a static method of a non-exported class in a namespace) isn't reused by
  tsc there but is in a reduced probe; cause not found.
- A conditional type as a constraint prints parenthesized: `<Expected extends (Extends<A, Expected>) extends true ?
  ...>`, tsc without the parentheses (expect-type).

- TS2344 elaboration for `NonNullable<V[K]>` without strictNullChecks (`type F<V, K extends keyof V> =
  Box<NonNullable<V[K]>>`, `Box<T extends { [k: string]: string }>`): tsc says "Index signature for type 'string' is
  missing in type '{}'." (structuredTypeRelatedToWorker relates getApparentType(source); V[K] with no base constraint
  is unknown, emptyObjectType without strictNullChecks), tttt names 'NonNullable<V[K]>'. relate.c keeps an
  intersection source as is, and apparent_type_resolve's intersection case keeps unknown/any members (its HACK);
  removing only that HACK changes nothing. Needs the structural step on the intersection's apparent type.
- quickIntersectionCheckCorrectlyCachesErrors with a required `children`: tsc's chain goes `{ o: object }` -> `CP &
  {...}` -> the different-subtype line -> `CP` to `{ children }`, from relation cache state (what the test is named
  after); tttt explains `CP` to `{ children }` directly. Evaluation order, low value.
- jsdocThisType with a TS annotation on the JS variable (`/** @type {M} */ export const f1: never = function() {...}`,
  `M = (this: Foo) => void`): tsc's getContextualSignature returns getSignatureOfTypeTag first, so the message prints
  `(this: Foo) => void`; tttt types `this` right (jsdoc_signature_type) but its contextual signature comes from the TS
  annotation. Fix: the JSDoc signature as contextual signature ahead of the annotation. JS-only (already TS8010).
- tanstack-router `test/probes/plan19/tanstack-router/conditional_source_weak_target.ts`: tsc first relates the
  source's constraint `(TFrom extends TTo ? Opt<TFrom> : Req<TFrom>) | Opt<TFrom>` to the whole intersection target,
  then repeats the chain against the weak member `{ comp?: "a" }` (14 lines, printing the inner check type as `TFrom`
  where the source has `string`); tttt goes straight to the weak member (4 lines). Plan 34 queue.
## Names and specifiers
- enumAssignmentCompat6 (passes errors): in f.ts, a function-local `enum DiagnosticCategory` shadows the file's exported
  one; tsc prints the exported one as `import("f").DiagnosticCategory` in the types baseline (not accessible by name
  there), tttt as `DiagnosticCategory`.
- **`import("…")` specifiers**: tsc prefers a package entry/`index` that re-exports the symbol and non-relative
  paths: declarationEmitTopLevelNodeFromCrossFile2, declarationsForInferredTypeFromOtherFile (types).
- **`import * as ns` of a CommonJS file from an ES module file (node16+)** is a clone of the module in tsc
  (resolveESModuleSymbol, cloneTypeAsModuleType): its type is `typeof ns` by that alias, and the symbol chain doesn't
  name the module through it (`typeof m26` via `import m26 = require`, not an earlier `import * as
  m4`). tttt's namespace import is the module itself (hacks.md `namespace_import_is_clone()`). Making the chain skip
  such imports isn't enough: the type itself must be a per-alias clone (tsc names things through some of those clones).
  Left: nodeModulesTypesVersionPackageExports x3 (`mod.x` from an ES module file is bare `x` in tsc); plan 35 queue
  item 2.

- JS corpus leftovers (probes in `test/probes/plan19/webpack/`): a `require("m").X` alias that resolves to nothing
  prints `error` in tsc (tttt `any`); tsc lists a same-named `@typedef` as a second declaration of the alias symbol;
  `module.exports = f` of a function declaration prints `typeof f` (require_property_value_only.ts); a JS `const S =
  Symbol()` read through a `@type {typeof S}` const prints `symbol` in tsc (jsdoc_accessor_type.ts).
- jsFileFunctionOverloads (`<T_1>`): tsc binds all `@template T`s of a JS function's JSDoc comments (`@overload`
  ones included) into one type-parameter symbol per name, so typeParameterShadowsOtherTypeParameterInScope never
  renames. Putting the overloads' generics in the harness scope fails either way (`type_param_name()` compares type
  identities). Fix: one type parameter per same-named JSDoc template of a host function, or treat them as equal there.
- A function declaration's name prints with the function as enclosing declaration, its parameters in scope: a JSDoc
  `{{y: typeof b}} b` reuses `typeof b` only when b resolves to the same symbol there (tsc prints `typeof
  import("index").b` for a plain outer function, `typeof b` when b has expandos; tttt the other way round).
  jsDeclarationsFunctions, jsDeclarationsFunctionsCjs, callbackTag4.
- A JSDoc typedef reference reused from a parameter annotation prints bare from another file (`(p: MyType) => void`),
  where a TS `export type U` reuses as `import("/a").U`: tsc's trackExistingEntityName apparently doesn't apply to JSDoc
  typedefs; reason not found. jsdocPropertyTagInvalid.
- declarationFileNoCrashOnExtraExportModifier: an `export { Sub }` inside `declare namespace exports` prints
  `exports.Sub` in tsc (tttt `Sub`; reason not found).
## Underlines (node reuse)
- A binding default `{} as any` misses tsc's empty `^^^` types-mask line (bindingDefaultAnyRemovesUndefined types).
- `() => 'foo' as const`: tsc's underline reuses the `"foo"` (`^^^^^^     `), tttt underlines the whole return type.
- readonly_through_prettify.ts: `| undefined` of optional properties under exactOptional-less strict: tsc's
  underline has gaps (reused nodes), tttt's doesn't.

## Symbols baseline
- **Augmentation of an `export =` module merges into the export= target** in tsc (mergeModuleAugmentation resolves
  the module through resolveExternalModuleSymbol first): `declare module "foo" { namespace M {} var M; export = M }`
  then `module "foo" {}` elsewhere adds a declaration to M, not to "foo". tttt merges into the module symbol
  (binder_link). Most tests that showed it pass now (augmentExportEquals*, module_augmentUninstantiatedModule):
  re-check whether the merge itself changed. Still failing symbols: importEqualsError45874,
  moduleAugmentationWithNonExistentNamedImport, moduleAugmentationDuringSyntheticDefaultCheck.
- Test files with mixed line endings: tsc's harness splits by `\r\n` when the file has one (splitContentByNewlines),
  so a bare `\n` line stays inside a line and leading "empty" lines survive (genericArray0: line numbers off by one).
  Porting the split in load_test_source() lost 21 tests (directives and rendering of such files): needs the
  renderer to follow too.
- `typeof undefined` of a local class named `undefined` (undefinedTypeAssignment4): small, not done.
- A module's own exports reached through an alias of the module (`import * as self from "./self"`) are named
  `self.x` at their declarations (baseline_test_case_module_alias_text): functions, variables, type aliases,
  classes and interfaces ask (namespaces and enum members use their full chain). An `export default class C`
  reached through the alias is `self.default` in tsc, `C` here (its local symbol isn't the `default` export), and its
  members `self.default.p` (defaultDeclarationEmitShadowedNamedCorrectly).
- `infer X extends C<...>`: references inside an `infer` type parameter's constraint are missing
  (partial_check_overload.ts).
- Mapped properties over an intersection: tsc lists the declarations from every intersection member that has the
  property, tttt the first (mapped_over_intersection_readonly.ts); intersection_with_type_param.ts is the reverse
  (tttt misses a repeated Decl). vue's DefineProps is one: a property of `Readonly<LooseRequired<T>> & {...}` lists
  its declaration once, tsc twice (the modifiers type of LooseRequired is `T & Required<T>`, both members have it).
  Also there: `this` of a ThisType marker prints no Symbol line (probes in plan19/vue/).
- `Pick<T, "a">` with T generic: the mapped property expanded from the key's lower bound has no declaration
  (`p.a : Symbol(a)`, tsc `Symbol(a, Decl(...))` from T's constraint).
- Duplicate class/interface members show as separate symbols (tsc: one merged symbol): the TS2687 tests (members
  aren't binder symbols, hacks.md).
- **Spread doesn't keep the property's identity**: tsc's getSpreadSymbol returns the same symbol when readonly-ness
  doesn't change, so `{ ...x }.a` with `x: N.A` shows `Symbol(N.A.a)` and reuse carets in the types baseline; tttt
  `Symbol(a)`. Setting `.reuse` in `spread_props_of` (ty/expr.c) did it but lost 14 tests: `spread_into`, excess
  property checks and JSX spread attributes read the builder's other fields (and object literal widening may rebuild
  the properties).
- Leftovers of the 2026-10-04 fan-out (fan-s1, symbols "name" bucket), each one test or a known mechanism: - Spread
  identity (above): spreadExpressionContextualTypeWithNamespace (`{ ...ns }.f` is `ns.f`). - Late-bound computed
  members merge with the written member of the same name, named and ordered like it (`[c0]` with c0 = "1" after `1:`
  is `T0[1]`, Decls in binding order): dynamicNamesErrors. - Expando declarations inside nested expressions (`(Foo.x =
  1)`, `if (Foo.y = 1)`) aren't declarations of Foo here (expandoFunctionNestedAssigments*), nor expandos of a `var`
  initialized with a function expression in TS (typeFromPropertyAssignment29). - `typeof import("./a").A.foo`: `foo`
  resolves to A (should be the static property): importTypeTypeofClassStaticLookup. - A setter without `this`
  parameter takes its getter's (and vice versa): tsc's getSignatureFromDeclaration / getAccessorThisParameter. tttt's
  this_ref is decided at lookup, before the other accessor's parameter symbol exists when the setter comes first:
  thisTypeInAccessors (types too). - Two test units with the same file name: tsc's vfs keeps the last content, both
  units print with their own text and the one file's symbols (moduleResolutionWithExtensions). - Union member order of
  instantiated anonymous types (discriminateWithDivergentAccessors1's Decl order), plan 21. - `export { globalThis as
  global }` inside `declare global` gives globalThis a declaration (globalThisGlobalExportAsGlobal,
  globalThisDeclarationEmit3).

## Triage 2026-10-07 (agent C): display/printer leftovers, each reproduced, root cause where found
- **Named error types** (DT `$ExpectType` babel__traverse, babel__helper-plugin-utils, redux-saga-test-engine): tsc 6's
  resolveTypeReferenceName gives an unresolved name a synthetic symbol (getUnresolvedSymbolForEntityName: TypeAlias,
  CheckFlags.Unresolved, one per dotted path, declaredType unresolvedType), and getTypeFromTypeAliasReference makes of
  it an `any`-flagged intrinsic error type per (symbol, type arguments) (the `errorTypes` map) with aliasSymbol and
  aliasTypeArguments = the argument *types*. typeToTypeNodeHelper prints any type with an alias as that reference. It
  isn't instantiated (instantiateTypeWorker has no case for it), and an indexed access of it is itself (any object), so
  `NodePath<t.Aliases[K]>["type"]` stays `t.Aliases`. tttt reuses the written reference node instead, so arguments
  print as written: probe `import { type Effect } from "@nonexistent/types"; declare const k: "K"; type KK = typeof k;
  declare function throwError<M>(m?: M): Effect<KK, M>; const e = throwError("x");` is `Effect<"K", M>` in tsc,
  `Effect<KK, M>` in tttt; computed types (indexed access, instantiation) collapse to plain any. Doing it: an error
  Ty with `alias`/`alias_args` interned per (unresolved path symbol, args) from lower_identifier(), every
  `== intrinsics[TY_ERROR]` identity check audited (TYF_ERROR flag checks are fine). Plan-sized.
- variadicTuples1, last line: `function c2<T extends unknown[]>() { return (...args: [...T]) => 1; }` prints the
  returned arrow's rest parameter `T` (tsc `[...T]`); through a local variable it's right. The signature's rest
  parameter type is T, not the tuple, so when the annotation can't be reused (it mentions c2's fresh T) the type
  prints bare. Wherever the rest param's `[...T]` is normalized to T (call.c's rest_variadic?), the display needs the
  tuple.
- uniqueSymbolsPropertyNames (`[Op.equal]`, tsc names an interface's `unique symbol` member through a variable of
  that interface type in scope, getSymbolChain's firstVariableMatch: every value in the enclosing scope tables, inner
  first, whose getTypeOfSymbol is the interface's declared type). out_unique_symbol_name()'s `XConstructor` HACK is
  the global special case. Cheap faithful-enough version: walk d.enclosing's scope chain (Scope.locals, then the
  globals) for variables whose written annotation is a TYPE_IDENTIFIER resolving to the owner interface; that also
  covers `declare var Symbol: SymbolConstructor`, so the HACK could go. Not done (one test).
- declarationEmitHigherOrderRetainedGenerics: in tsc's printed overload set the second overload's type parameters are
  renamed `<R1, O1_1, E1_1, A_1, R2, O2_1, E2_1, B_1, C_1>`: everything but R1 and R2. Those two appear in the first
  overload only inside a reused `Kind<F, R1 & R2, ...>` node, the renamed ones in printed unions (`O2 | O1`, reordered)
  or parameter lists, so tsc's typeParameterNamesByText seems to collect only names that went through
  typeParameterToName. Unverified; instrument typeParameterToName on this test before touching the renamer.
- legacyNodeModulesExportsSpecifierGenerationConditions: underline only (`() => ` of `export const x: () => Thing`
  partly reused by tttt's "module's variable" annotation path in display_structure(); tsc reprints it all because
  Thing isn't accessible from the importer). tsc's tryReuseExistingTypeNode gives up on the whole annotation when a
  name inside isn't accessible; tttt's reuse is per node.
- jsdocFunctionTypeFalsePositive: `@param {<} x` recovers to `() => any`; tsc prints the missing return node (not
  reusable), tttt's display_signature() reuses the FunctionImpl's missing `ret` as `any` (annotation_is() accepts it;
  for TS's `(a: ) => {}` reuse as any is right). Needs a mark on missing nodes parsed inside JSDoc.
- jsdocTemplateClass, jsdocPropertyTagInvalid: as described in the 2026-10-07 triage (typedef scope of a comment on
  a constructor; a typedef of another file printed bare in the types baseline), not looked at again.
- thisInPropertyBoundDeclarations / genericCallOnMemberReturningClosedOverObject: tsc's typeWriter shares
  NodeLinks.serializedTypes across baseline lines (visitAndTransformType caches an object type's printed node per
  (enclosing declaration node, type id, flags)), so a later line under the same declaration reuses a node printed
  while a cycle cut it short (`<T2>(t2: T2) => any`). Modelling it needs the harness to pass each line's parent
  declaration and the printer to keep a cache keyed by it across ty_display() calls; ty_display.c only models the
  within-one-print case. Two tests; not worth it unless more show up (displaydiff's `other` bucket would tell).
- complexRecursiveCollections (TS2430 elaboration `map(...).filter(...).filter` vs tsc's `map(...).size`): after
  Seq<K,M> vs Collection<K,M> tsc's relation of `filter` (two overloads with `this`-typed predicates) succeeds, or is
  cut by isDeeplyNestedType as Maybe, so `size` (declared after `filter` in Collection) is the first failing
  property; tttt goes one level deeper and fails `filter`. Recursion-identity depth, not property order (member order
  matches since the getNamedMembers work).
- Elaboration chain shapes (conditionalTypes1/2, recursiveConditionalTypes, styledComponentsInstantiaionLimitNotReached):
  tsc's chains interleave lines from a failed constraint attempt with the later ones (`T` to `NonNullable<T>`: the
  source constraint `string | undefined` against `T & {}`, then "'undefined' is assignable to the constraint of type
  'T', but...", then T against `{}` *nested under it*), i.e. its errorInfo save/restore around
  structuredTypeRelatedTo's generic-source fallback; and it reports deeper through cached failures (recursiveTypeRelatedTo
  re-explores a cached Failed entry when reporting, unless Reported). Each test is a different path; plan 34 territory.
- intersectionSatisfiesConstraint mutation (`T extends Object | S`): `g(m)` with `m = assign(p, lit)` (A & B
  instantiated with A = T) prints the argument as `Object & { o: number; }`, tsc `(Object | S) & { o: number; }`; the
  same with `q: T & { o: number }` written is right. narrowable_type() substitutes base_constraint_or_type() of the
  intersection: for the instantiated one the result has lost S (subtype-reduced union, or a memo made during the
  call's inference). Probe: `interface S { c: number } declare function g<U extends { c: number; o: number }>(x: U):
  U; declare function assign<A, B>(a: A, b: B): A & B; declare const lit: { o: number }; function f<T extends Object |
  S>(p: T) { const m = assign(p, lit); g(m); }`.
- Seen on the way: a reverse mapping of `typeof globalThis` (`Deep<T> = { [K in keyof T]: Deep<T[K]> }` inferred from
  `{ a: number } & typeof globalThis`) expands `Intl`'s members one level less than tsc (`Intl: { Collator: any; ...}`
  vs tsc's `Intl: { Collator: { supportedLocalesOf: any; }; ...}`): shouldUsePlaceholderForProperty depth for a
  namespace's members.
- accessorBodyInTypeContext (types): `type A = { get foo() { return 0 } }` (TS1183) is `foo: number` in tsc, its body
  walked (`>0 : 0`); tttt's parser turns a type literal's accessor into an InterfaceMember and drops the FunctionImpl
  (body, parameter scope), so the getter is any and nothing in it is bound or walked. Same root as
  declarationEmitBindingPatternsUnused (a setter's parameter scope). Needs InterfaceMember to keep the accessor's
  FunctionImpl (boxed in InterfaceAccessor), the binder to walk it, and the getter's type inferred from the body when
  unannotated. Parser + binder + checker; a session.
- vscode/eopt elaboration choice (TS2412 at `viewModel` in tsc vs TS2375 at a nested `styles: { a: st.a }` literal in
  tttt, extHostTypeConverters.ts:673/688 and three more): not looked at again; the e.ts-style repro in the 2026-10-07
  triage stands. Likely elaborateObjectLiteral's order (tsc elaborates the first property whose *declared* target
  property fails with an expression it can descend into, and stops at the first reported one per literal).
- bundledDtsLateExportRenaming: with outFile tsc names modules non-relative from the common source directory with
  minimal endings (getSpecifierForModuleSymbol's isBundle: baseUrl = common source dir, `non-relative`):
  `import("nested").B`; module_specifiers.c has no bundle mode (needs the common source directory in Generator).
