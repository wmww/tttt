# Display bugs (types/symbols baselines)
What tttt prints differently from tsc, where the underlying type is right (or nearly). `scripts/displaydiff.py` ranks the
mechanisms across the suite (`other` is mostly type-core bugs the types baseline shows first); these are the known
rules. Plan 21 (closed) did the bulk. Pruned 2026-10-09 against the tsgo 7.1 suite: every named test fails there;
probe files (`*.ts` without a test name) weren't re-run.

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

- A conditional type as a constraint prints parenthesized: `<Expected extends (Extends<A, Expected>) extends true ?
  ...>`, tsc without the parentheses (expect-type).

- TS2344 elaboration for `NonNullable<V[K]>` without strictNullChecks (`type F<V, K extends keyof V> =
  Box<NonNullable<V[K]>>`, `Box<T extends { [k: string]: string }>`): tsc says "Index signature for type 'string' is
  missing in type '{}'." (structuredTypeRelatedToWorker relates getApparentType(source); V[K] with no base constraint
  is unknown, emptyObjectType without strictNullChecks), tttt names 'NonNullable<V[K]>'. relate.c keeps an
  intersection source as is, and apparent_type_resolve's intersection case keeps unknown/any members (its HACK);
  removing only that HACK changes nothing. Needs the structural step on the intersection's apparent type.
- jsdocThisType with a TS annotation on the JS variable (`/** @type {M} */ export const f1: never = function() {...}`,
  `M = (this: Foo) => void`; probe rechecked 2026-10-09): errors match now, the symbols differ: tsgo resolves `this` in
  the body to `Symbol(Foo, Decl(types.d.ts, 0, 0))`, tttt to M's `this` parameter. tsgo's contextual signature comes
  from the JSDoc type tag ahead of the annotation. JS-only (already TS8010).
- tanstack-router `test/probes/plan19/tanstack-router/conditional_source_weak_target.ts`: tsc first relates the
  source's constraint `(TFrom extends TTo ? Opt<TFrom> : Req<TFrom>) | Opt<TFrom>` to the whole intersection target,
  then repeats the chain against the weak member `{ comp?: "a" }` (14 lines, printing the inner check type as `TFrom`
  where the source has `string`); tttt goes straight to the weak member (4 lines). Elaboration chain shape.
## Names and specifiers
- JS corpus leftovers (probes in `test/probes/plan19/webpack/`): a `require("m").X` alias that resolves to nothing
  prints `error` in tsc (tttt `any`); tsc lists a same-named `@typedef` as a second declaration of the alias symbol;
  `module.exports = f` of a function declaration prints `typeof f` (require_property_value_only.ts); a JS `const S =
  Symbol()` read through a `@type {typeof S}` const prints `symbol` in tsc (jsdoc_accessor_type.ts).
## Underlines (node reuse)
- `() => 'foo' as const`: tsc's underline reuses the `"foo"` (`^^^^^^     `), tttt underlines the whole return type.
- readonly_through_prettify.ts: `| undefined` of optional properties under exactOptional-less strict: tsc's
  underline has gaps (reused nodes), tttt's doesn't.

## Symbols baseline
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
  aren't binder symbols, hacks.md). (A class property followed by accessors of its name is one symbol typed by the
  accessors since 2026-10-12: ClassField.joined_accessor.)
- es6ExportEqualsInterop/nodeModulesJson: fixed in the harness only (export_equals_property_alias()); the checker's
  symbol chains still can't name a member through an import alias (member identity).
- **Spread doesn't keep the property's identity**: tsc's getSpreadSymbol returns the same symbol when readonly-ness
  doesn't change, so `{ ...x }.a` with `x: N.A` shows `Symbol(N.A.a)` and reuse carets in the types baseline; tttt
  `Symbol(a)`. Setting `.reuse` in `spread_props_of` (ty/expr.c) did it but lost 14 tests: `spread_into`, excess
  property checks and JSX spread attributes read the builder's other fields (and object literal widening may rebuild
  the properties).

## Triage 2026-10-07 (agent C): display/printer leftovers, each reproduced, root cause where found
- **Named error types**: tttt already models tsc 6's unresolved-name error types (ty_unresolved(): one any/error type
  per name and argument types, printed as the reference; tsc's getUnresolvedSymbolForEntityName + errorTypes, never
  instantiated, an indexed access of it is itself). Since 2026-10-07 the arguments are types, not reprinted nodes
  (redux-saga-test-engine). Left: babel__traverse x4 and babel__helper-plugin-utils x2, where a visitor method's
  `path` is `NodePath<t.Aliases>` in tsc and `NodePath<any>` in tttt. Visitor<S> intersects mapped types over two
  unresolved key sources (`[N in Node as N["type"]]` with `export import Node = t.Node`, and `[K in keyof t.Aliases]`),
  both string index signatures, and the method's contextual signature comes from the intersection of their value
  types (`VisitNode<S, A> & VisitNode<S, B>`, each a function | object union). A self-made imitation matches tsc;
  reduce with `scripts/expectprobe.py` (a cut-down project: tsconfig with idx.d.ts = babel__traverse's index.d.ts and the
  test's first 18 lines; the reducer drifts, keep the `"./idx"` import and `t.Aliases` text).
- Elaboration chain shapes (conditionalTypes1/2 fixed by normalizing the target and the apparent intersection source,
  2026-10-14; recursiveConditionalTypes' errors pass since, no suite test left):
  tsc's chains interleave lines from a failed constraint attempt with the later ones (`T` to `NonNullable<T>`: the
  source constraint `string | undefined` against `T & {}`, then "'undefined' is assignable to the constraint of type
  'T', but...", then T against `{}` *nested under it*), i.e. its errorInfo save/restore around
  structuredTypeRelatedTo's generic-source fallback; and it reports deeper through cached failures (recursiveTypeRelatedTo
  re-explores a cached Failed entry when reporting, unless Reported). Each test is a different path (`scripts/errtextdiff.py`).
- Seen on the way: a reverse mapping of `typeof globalThis` (`Deep<T> = { [K in keyof T]: Deep<T[K]> }` inferred from
  `{ a: number } & typeof globalThis`) expands `Intl`'s members one level less than tsc (`Intl: { Collator: any; ...}`
  vs tsc's `Intl: { Collator: { supportedLocalesOf: any; }; ...}`): shouldUsePlaceholderForProperty depth for a
  namespace's members.
- Type member accessor bodies (TS1183, 2026-10-14): a getter's body is kept (InterfaceAccessor.getter_body), bound,
  checked and walked, and types an unannotated getter. Left: an annotated getter's returns aren't checked against the
  annotation (`get x(): number { return "" }`, tsc's TS2322: the annotation moved to the member's type, the kept
  FunctionImpl has none), and a setter's body isn't kept at all (`set x(v: number) { v.toFixed() }`: nothing in it
  is bound or walked; its parameter's annotation is moved to the write type, so keeping it needs a TypeNode copy or a
  shared node). A setter whose parameter is a pattern now keeps its FunctionImpl with the annotation shared
  (InterfaceAccessor.setter_function, borrowed type: 2026-10-15), the model to extend to every setter. No suite test
  left.
- vscode/eopt elaboration choice (TS2412 at `viewModel` in tsc vs TS2375 at a nested `styles: { a: st.a }` literal in
  tttt, extHostTypeConverters.ts:673/688 and three more): not looked at again; the e.ts-style repro in the 2026-10-07
  triage stands. Likely elaborateObjectLiteral's order (tsc elaborates the first property whose *declared* target
  property fails with an expression it can descend into, and stops at the first reported one per literal).
## TS 7.1 window leftovers (triage 2026-10-09; fan-out agents C/D, 7.1 new tests)
- `implements`/interface `extends` qualifiers are `any` in tsgo's types baseline (ported as observed in
  baseline_test_traversal.c); the checker path that makes them any wasn't found.
- Index symbols of a destructuring assignment's rest (`({ ...r } = strMap); r.z`): tsgo's getRestType gets the object
  literal's symbol there (checkObjectLiteralDestructuringPropertyAssignment); ty_object_rest_type() passes none (a
  binding's rest has its binding's symbol since 2026-10-14). Not probed.
- Union order left (checker identity, not display):
  - `$ExpectType` knockout.mapping (two KnockoutObservableArray instantiations).
- **tsc's 160-char approximateLength truncation** (`@noErrorTruncation: false`; the suite's tests pass):
  `... | ... | ...`, `... 4 more ...`; tsgo's approximateLength of a type literal member is shorter than its text and
  truncation is sticky. Ported (TyDisplay.approx counts where tsgo's node builder counts, `display_over()`). Left:
  a reused annotation counts its reprinted text plus one (tsgo: its source range with the leading trivia, newlines and
  indentation included), and tsgo's pseudo types (serializeTypeForDeclaration with an enclosing declaration types a
  declaration syntactically, pseudotypenodebuilder.go): their keywords and object literals count nothing, their names
  count their source range. tttt prints those types as checker types, so a JSON module's object (msw/test's
  package.json exports) truncates later than tsgo. Only an object literal property's function expression with
  annotated parameters is exempted from elision, as its pseudo type is.
- mut0422: display of a mapped type over `Tags<"_tag", Value>`: tsgo keeps the alias in the key constraint.
- Elaboration: a `function c(): () => number` returning a string-returning arrow, assigned elsewhere: the TS2322 chain
  repeats "Type 'string' is not assignable to type 'number'." twice under a signature return mismatch (kw4 probe).
- Relation paths that differ from tsgo's (not elaboration rules): reactReduxLikeDeferredInferenceAllowsAssignment and
  circularlyConstrainedMappedTypeContainingConditionalNoInfiniteInstantiationDepth (tsgo elaborates 9 levels;
  missing_checks.md has the two fixes tried).
- The pseudochecker (syntax-based declaration types: `a`'s line printing a whole initializer's syntax) is plan 55.
- `Partial<{ a: number } & (object extends R ? { c?: F } : { c: F })>`'s `p.c` (trpc's shape; errors fixed by
  adbed7b9): tsgo's symbol has both declarations of `c` (`Decl(1, 70), Decl(1, 82)`), tttt only the first: the
  homomorphic mapped property over the conditional's union keeps one declaration.
- `type D = { get d(); set d(v: number) }`: `d` is number in tsgo (the typed setter says it), any in tttt; an
  accessor pair in a type literal/interface takes its type from the getter side only.
- **`typeof s` vs `unique symbol` in messages** (found 2026-10-12, agent B): with `declare const s1: unique symbol`,
  `let v: "a" | typeof s1 = ...; const r: number = v;` makes tsgo print `string | typeof s1` and tttt `string | unique
  symbol`. A mapped type's property of that type prints `unique symbol` on both sides. tsgo's typeToTypeNode names
  the symbol when the type isn't AllowUniqueESSymbolType and the symbol is accessible. Find which flags the message's
  typeToString passes there.

## Suite tail leftovers (2026-10-12 fan-out, agent C: TS tests failing types/symbols only)
Looked at, root cause where found, not fixed (one test each unless named):
- es6ExportEqualsInterop/nodeModulesJson/import_reference-to-type-alias/importedAliasesInTypePositions/
  escapedIdentifiers/tripleSlashReferenceAbsoluteWindowsPath/symbolProperty44: fixed in the harness (2026-10-12).
- $ExpectType leftovers (2026-10-14, agent D; 68637/68640):
  - map-to-obj-tests.ts:9/12: `MapToObj(new Map([["hello", "world"]]))` with `MapToObj<K extends string | number |
    symbol, V>(map: Map<K, V>): Record<K, V>`: tsc infers the inner `new Map`'s K as string, tttt keeps "hello" (the
    literal's contextual type through the return mapper is the outer K, whose constraint is primitive, which tttt's
    inference_call_result_fallback() treats as room for the literal). By tsgo's code as read the literal should stay
    too; an instrumented tsgo would tell where it widens. Also `f<K extends string>(m: Map<K, 1>)` with
    `f(new Map([["a", 1]]))`: tsc string, tttt "a".
  - knockout.mapping-tests.ts:109: union order of `KnockoutObservableArray<KnockoutObservableType<any>> |
    KnockoutObservableArray<any>` (tttt the reverse): not looked at.
  - tsgo keeps the intersection it distributed as the union's origin for display (getIntersectionType's
    createOriginUnionOrIntersectionType): `(VisitNode<S, any> & VisitNode<S, t.Aliases>) | undefined` where tttt prints
    the distributed `((F | O) & (F | O)) | undefined` expanded. Seen in a babel__traverse probe, no $ExpectType on it.

## Fresh object literal types vs their widened/regular types (identity)
tsgo's object literal expression has a fresh type of its own; the declared type of what it initializes is another
type (getWidenedType / getRegularTypeOfObjectLiteral make new types, cached per source type), so printing one shows
the other expanded one more level before visitedTypes stops it. tttt has one type for both, so it stops a level
earlier:
- jsFunctionWithPrototypeNoErrorTruncationNoCrash (types): `Color.prototype = { negate: function () { return this } }`:
  tsgo's `Color` prints `negate: () => { negate: () => any; ... }` where tttt prints `negate: () => any`; only the
  parameterless methods (negate, greyscale) expand. Fully traced with an instrumented tsgo (2026-10-15, agent C;
  checkObjectLiteral/getContextualThisParameterType/getTypeOfSymbol in the checker, createAnonymousTypeNodeEx/
  visitAndTransformType/createCyclicStructurePlaceholder in the node builder). Three tsgo behaviours combine:
  1. The checker checks the literal three times (getWidenedTypeForAssignmentDeclaration for the `prototype` symbol: P;
     the statement's checkBinaryExpression; checkExpressionCached from getContextualThisParameterType: F). The method
     function types are shared (cached per function symbol), and every method's `this`/return is getWidenedType(F) = W,
     a different type from P. tttt has one literal type, so P and W are the same type and the printer stops at once.
  2. Printing P's members goes through the declaration-based serializer: a parameterless `function () {...}` is
     printed syntactically (`() => ` + its return type), its function type never visited; a function with an untyped
     parameter falls back to typeToTypeNode of its function type (visitAndTransformType).
  3. visitAndTransformType caches every serialized type per enclosing declaration (links.serializedTypes, keyed by
     type id and flags). Under P.negate, W is expanded (its methods' W is the cyclic placeholder `any`) and cached,
     and so are the function types of lighten.. clearer printed inside it (`(ratio: any) => any`). Back at P,
     lighten.. clearer hit those cached `(ratio: any) => any`, and greyscale's W hits the cached expansion.
  Matching needs all three in tttt: a second literal type for the prototype assignment (checker identity), the
  syntactic printing of parameterless function expressions, and a printer cache with tsgo's keys and lifetime (per
  enclosing declaration, across the lines of a types baseline). Each is a real tsgo mechanism, but together they only
  reproduce an incidental output; not worth a session now (decided 2026-10-15; reconsider if the printer cache shows up
  in other tests or dtsdiff buckets).
- A JavaScript `{}` container's initializer (`var o = {}; o.self = o`): tsgo prints `>{} : { self: { self: any; }; }`,
  tttt `{ self: any; }` (the variable's lines agree). In ttttLocalExpandoCyclicPrint's probe, left out of the test.
Size: a fresh/regular distinction for object literal types at display (or a real fresh type identity, which
relations and widening already approximate with flags): investigate how far tttt's TYF_FRESH/regular handling goes
for object literals before choosing; ~1 session.

