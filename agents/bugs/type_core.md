# Type core, inference and flow bugs
Wrong types (not missing diagnostics: `missing_checks.md`; not printing: `display.md`). Pruned 2026-10-09 against
the tsgo 7.1 suite: every test named here fails there unless the entry says otherwise (`mutNNNN` entries are mutants of
passing tests).

## Inference
- **First pass skips context-sensitive arguments entirely**: tsc checks them with their functions replaced by
  anyFunctionType and still infers from the (non-inferrable) literal to anything but a naked type parameter (hacks.md
  "Skip-sensitive rounds").
- `infer_rest_variadic`/`check_rest_arg` keep an argument's type from whichever overload candidate typed it first
  (`if (arg->type) continue`), though its contextual type is the candidate's rest element; tsc re-checks per candidate.
  Clearing the non-spread rest arguments' types at the top of infer_rest_variadic changed nothing anywhere (suite,
  corpora, instructions) and no probe observes it (`q<T extends ["a"]>(n: number, ...a: T)` before `q<T extends
  string[]>(s: string, ...a: T)` passes). Look here if overloads with differing rest types ever disagree with tsc.
- Contextual types from a generic mapped type substitute the whole constraint for K, not the property's key.
- Older tsc-corpus findings (a probe each): `visitNode(node, visitor, isExpression)` resolving to the overload without
  the predicate (`test/probes/plan05/visitNodeInference.ts`); literal widening in an object literal that has a
  contextual type.
- (triage 2026-10-07) The contextual type of an argument for a rest parameter typed as a mapped type over `keyof
  [...E, "abc"]` (index 0 -> `{ bar: [...E, "abc"][0] }`) keeps the literal in tsc: TS2345 messages print `{ bar:
  "abc"; }` where tttt prints `{ bar: string; }`. circularInlineMappedGenericTupleTypeNoCrash itself needs tsc's TS2589.
- (2026-10-07) map-to-obj (DefinitelyTyped, 2 `$ExpectType`): `id(new Map([["a", 1]]))` with `id<K extends
  string>(m: Map<K, V>)`: tsc's return mapper for the inner `new Map` comes from the contextual type with the outer K
  as createOuterReturnMapper has it (nothing inferred: its constraint string), so "a" widens; tttt's inner return
  inference maps the inner K to the outer K, whose primitive constraint keeps "a". Instantiating the return source
  with the outer's constraints (infer_type_arguments' `return_source`) fixes it but cost xstate and valibot +1.8%
  each even gated to primitive constraints: needs a cheaper formulation.
- (2026-10-07) ember__service (2, also v3): an object literal method's literal return ("") widens when the call has
  two overloads and the first round (subtype) fails on `this`: the return type is computed (and memoized) during that
  round without the "own contextual signature" that keeps the literal. Repro: `declare class Core { constructor(p?:
  object); static extend<S, I>(): ECC<I>; static extend<S, I, T1 extends {[k: string]: any}>(this: S & ECC<I>, a:
  T1): ECC<T1 & I> }` with `ECC<T> = (new (p?: object) => T) & (new (...a: any[]) => T)`: `new (Core.extend({ m() {
  return ""; } }))().m()` is "" in tsc. Same family as the item above (a return type memoized before the context
  that would keep the literal exists).
- (2026-10-07) babel__traverse (4) and babel__helper-plugin-utils (2): with @babel/types unresolved, a contextual
  type `{ [k: string]: H<t.Node> } & { [k: string]: H<t.Aliases> }` (H a function | object union) gives a method
  parameter NodePath<t.Aliases> in tsc, NodePath<t.Node> in tttt: which unresolved member (tsc keeps named error
  types, tttt collapses them to any) survives the intersection of contextual signatures. Depends on the named error
  types item (triage "Plan 48 stage 4 leftovers"); broken packages only.

- **Call resolution leftovers of the inference-flow port (ex plan 38 "not done", each untried, no failing test
  known)**: `ty_call_effects()` (call.c, flow.c's getEffectsSignature) resolves a call on its own, without contextual
  type, where tsc takes getResolvedSignature (would need the chosen signature in `CallResolution`); JSX elements
  (`ty_resolve_jsx_element()`) aren't remembered per node like calls (`KEY_CALL_RESOLUTION`), they resolve per
  request; SkipGenericFunctions for generic functions nested in literal arguments (`Arg.nested_generic`) applies to
  the applicability check only: leaving them out of the first inference round lost zod's `pick([z.object, ...])` and
  xstate inferences, because tttt marks literals from syntax where tsc skips only where a contextual signature exists
  (checkFunctionExpressionOrObjectLiteralMethod under SkipGenericFunctions): a port marks from the contextual type.
- **Inference context by ancestry** (ex plan 51's optional item, kept: it prevents a class of latent bugs).
  `my outer_inference` is dynamic state saved/cleared/restored at ~15 sites in call.c; tsc's getInferenceContext
  finds the innermost inferring call by walking up from the node. A stack of (node, OuterInference) pushed per
  argument check and looked up by span containment would replace the save/restore sites and stop the flow engine's
  leak (`V_LOGAUD=1` in plan 51's prototype C, see git history: 121 entries where flow narrowing evaluates an
  unrelated type-guard call while another call infers). No baseline change expected; plan 51 stage 3's latent bug
  (`infer_rest_variadic()`'s first pass forgetting to set `my outer_inference`) is the kind it would prevent.
- Eager predicate inference during instantiation (`inference_mapper` -> `signature_predicate` ->
  `ty_infer_type_predicate` checks a body; tsc reads predicates lazily). Its test (recursiveGenericSignature
  Instantiation) passes; do it if another shows up (ex plan 42 stage 4).

## Relations, instantiation, reduction
- **Two copies of one declaration file (webpack through a symlink): one diagnostic left.** From a worktree (where
  `thirdparty/corpora` is a symlink) webpack's `types.d.ts` is in the program twice, as in tsc (root files keep the
  symlinked path, only node_modules results are realpathed and `node_modules/webpack -> ..`), and the examples'
  `webpack.config.js` compare against the other copy's `Configuration`. tsc reports 24 errors that way (2x TS2321
  "Excessive stack depth" at aggressive-merging/webpack.config.js:7 and :20, 22x TS2322). Since 2026-10-05 tttt
  matches 23 of them at every checker count in ~1.5 s (was ~60 s, 8x TS2859 instead): recursive_related() caches a
  failure even after an overflow, as tsc does (it re-explored the same overflowing subtrees until the relation count
  ran out), a depth overflow is TS2321 at the current node (was a HACK reporting TS2589), and check_assignable()
  still elaborates after one. Left: at aggressive-merging/webpack.config.js:20 (the array element tsc's
  elaborateElementwise checks with an error node) tsc's reporting pass overflows again (TS2321) where tttt's
  explanation finds a real failure about 40 levels down (`IteratorYieldResult<Module>` vs `IteratorResult<Module,
  any>` through Compiler -> Compilation -> Module -> Dependency -> ModuleGraph ... -> Iterable<Module>) and reports
  TS2322: the order in which the two explore the copies differs somewhere along that chain. Repro without the corpus
  checkout's symlink: /tmp project with `wpl -> <real webpack>` and a tsconfig whose only file is
  `wpl/examples/aggressive-merging/webpack.config.js` (types node, typeRoots wpl/node_modules/@types).
- Distributive conditional base constraint: with `S2<T> = T extends string ? T : number`,
  `g2<T extends unknown[]>(x: [...{ [K in keyof T]: S2<T[K]> }])` widens `g2(['a'])` to `[string]`, tsc `["a"]` (its
  forConstraint constraint of `S2<T[number]>` keeps the true branch with T's implied `string`).
- `StateId<U>` to `StateId<T>` (xstate's StateId, no variance measurement) gives TS2589 next to the TS2322 tsc also
  reports: the definitely-assignable checks instantiate deeply nested indexed-access chains. Repro:
  test/probes/xstate/state_id_deep_nesting.ts.
- **Intersection reduction by constraint**: `T & P` for a type parameter T with a primitive constraint is reduced
  (`reduce_type_variable_intersection`, and `(T & "a") | (T & "b")` back to T), but not for indexed access type
  variables (see hacks.md) nor type parameters with a deferred declared constraint.
- **Permissive instantiation** for conditional types is missing (`ty_conditional_resolve` defers where tsc picks the
  false branch); a flat `TyMapper` can't say "every type parameter -> any". Needs a wildcard type + cached permissive
  instantiation.
- **isTupleLikeType's length half**: an array-like context whose `length` is a number literal isn't tuple-like
  (only the "0" property rule is in `context_is_tuple_like`).
- **Conditional true branch with `infer`s**: relating a deferred conditional through its true branch with `infer`s as
  unknown is done only when the check type is generic; with a generic extends type tsc has real inferences.
- A homomorphic mapped type over a `satisfies`/const object doesn't resolve its keys (the `computedOptions` mapped
  type in the tsc corpus's `utilities.ts` printed a huge `keyof "a" & keyof "b"` key). Not re-probed.

- Base constraint of an indexed access comes out wrong in valibot: in
  `library/src/methods/required/requiredAsync.ts` (the `'~run'` member's `InferIssue<TSchema>`, TSchema constrained
  to `SchemaWithoutPipe<LooseObjectSchema<...> | ...>` = `(A | B | ...) & { pipe?: never }`), `ty_base_constraint` of
  `TSchema['~types']` is `undefined`, where tsc has `{ input; output; issue } | undefined`. Found when constrained type
  variable reduction (`reduce_type_variable_intersection`) turned `TSchema['~types'] & {}` into never; not reduced to
  a small repro (a two-member union of interfaces with an optional `'~types'` under exactOptionalPropertyTypes is
  fine). Possibly a constraint computed while an alias or the type parameter's constraint is in flight and memoized.

- **`awaitedTypeStrictNull` overflows the stack** in `instantiate_mapped_over_list` under the debug build + valgrind
  (8MB main stack; the release build passes; reproduced 2026-10-05 with `scripts/test.sh -f awaitedTypeStrictNull`). Deep
  but maybe not infinite recursion; worth checking whether the recursion is bounded.

- **No general property list for intersections** (tsc's getPropertiesOfType): `ty_has_exact_optional_unassignable`
  (relate.c, takes the first member's properties for unions now), jsx.c `container_prop_name`, members.c
  `builder_add_intersection_members` and the member loop in class_decl.c (~line 509) take each member's apparent type
  and require `TYF_OBJECT`, so a type parameter constrained to an intersection is skipped. `spreads_into_nothing`
  (expr.c) ignores intersections (tsc: empty when every member is). `with_synthetic_default` (lower.c) keeps index
  signatures and private/readonly members that tsc's getSpreadType drops. check.c's `number_index` check and the
  private-name check are still object-only gates (low value).
- **Spurious TS2589 on zod without variance annotations** (deep-partial.test.ts:111, since the explicit type argument
  constraint check of triage 2026-09-28): `toExtend<z.ZodObject>` against expect-type's recursive `MismatchInfo` goes
  >100 levels (`props_related_impl` -> `prop_type` -> `ty_indexed_instantiated` -> `find_prop` ->
  `conditional_from_root` -> ~135 `instantiate_inner`). tttt's order of `MismatchInfo`'s key union starts with `array,
  nonoptional, _zod...`, tsc's with `default`, which fails at once. Union order again; the TS2344 text there also
  differs (property order, tsc's `... 62 more ...` truncation). `perf_leads.md` "Variance measurements" says how to
  build that zod variant (strip `in`/`out` both after `<`/`,` and at line starts inside multi-line lists).
- (triage 2026-10-07, mut3125) AllowMissing for destructuring defaults should go by tsc's isObjectLiteralType of the
  unwidened parent. tsc has three states (fresh, regular, widened; getWidenedTypeOfObjectLiteral builds a type
  without ObjectFlags.ObjectLiteral), so `var b = {y: 0}; var {x = ""} = b` is TS2339 while `var {x = ""} = {y: 0}` is
  TS2353. tttt's `ty_widened_objects()` keeps `anonymous.object_literal`, so `binding_assigned_type` (lower.c) and
  binding.c treat widened declarations as literals. Fix: the widened twin gets `object_literal = false`, after auditing
  every reader (union partial-property rule, excess properties, the isObjectLiteralType relation rules) and the array
  literal element types tttt widens early (`for (var {x = ""} of [{}])`). Medium to large.
- (mut1640) An element *write* with a `never` index on a union of objects (`both[sym] = 'x'`, `sym: never`, members
  `{ s: number, '0': number, [sym]: boolean } | { [n: number]: number, [s: string]: string | number }`): tsc reads the
  union's number index info (getUnionIndexInfos, getApplicableIndexInfo with never: `number | boolean`), tttt `never`.
- The conditional base-constraint fallback (`base_constraint_uncached` TY_CONDITIONAL, used when there's no
  distributive constraint) leaves an `any` branch out like tsc's getDefaultConstraintOfConditionalType but uses the
  raw true branch, not tsc's inferred one. `ReturnType<T[M]>` property access (seed-5 mut0710/mut3585) still misses
  TS2339.
- (2026-10-07) sliceResultCast (types): `x.slice` of `[number, string] | [number, string, string]` is a union of two
  equal signatures in tsc (each tuple's Array members are instantiated with the tuple as `this`, Array being merged
  makes `this` possibly referenced everywhere); retarget_this_props() only retargets props that mention `this` (a
  deliberate perf shortcut), so tttt's members are shared and the union collapses. A fix must keep the shortcut's
  cost (instrcount valibot/zod) or make the retargeted members cheap.
- (2026-10-07) genericCallInferenceUsingThisTypeNoInvalidCacheReuseAfterMappedTypeApplication1 (types): `inner.log(100)`
  with `inner: XXX` and `log = implement<this>()(<N extends number>(n: N) => succeed(n))` is `F<Effect<100, never,
  never>, this>` in tsc 6 (deferred: `this` is generic), tttt resolves it to `Effect<100, never, never>` (the test's
  own comment, pre-6.0). Likely mechanism, unverified: reading a member through a non-generic class's own declared
  type doesn't instantiate `this` in tsc (resolveObjectTypeMembers' rangeEquals shortcut keeps the declared members),
  and tsc 6's SingleSignatureType has no outer type parameters to carry a `this` mapping; tttt maps `this` to XXX on
  the access. Check `class C { p!: F<1, this> }; declare const c: C; c.p` against tsc first.
- (2026-10-07) noImplicitAnyUnionNormalizedObjectLiteral1 (types): union order of two widened object literals with the
  same symbol (the spread literal) goes by type id; tsc's getWidenedTypeWithContext makes both members anew in member
  order, tttt's separate widen_nullable() pass keeps the unchanged `{ c: string; ... }` (older id) and remakes only
  the other, so the order flips.
- (2026-10-07) knockout.mapping `$ExpectType` (knockout.mapping-tests.ts:109): tsc orders
  `KnockoutObservableArray<KnockoutObservableType<any>> | KnockoutObservableArray<any>`, tttt the other way round.
  Not investigated (type id order of two references to one target: which type argument list is made first).
- (2026-10-07) node (4 `$ExpectType`, all versions): `[value: number] | [value: number]` from two declarations: tsc's
  tuple targets are keyed by their labels' declarations (getTupleTargetType's namedMemberDeclarations), tttt's by the
  label names, so the two tuples are one type and the union collapses.
## Circularity (plan 29 stage 3 leftovers; the resolution stack's are in missing_checks.md)
- circularBaseTypes, circularConstraintYieldsAppropriateError: missing_checks.md "Circularity".
- Spurious TS2589: recursiveConditionalTypes (the `unbox(b1)` calls).
- type-fest's `test-d/opaque.ts` (~90% of type-fest's check time) runs into TS2589 at line 68 (`Tagged`/`UnwrapTagged`
  over `TagContainer`) plus four TS7022 tsc doesn't report: a correctness bug with a perf symptom.
- Spurious TS7022 on `const can = composite && o?.e && !out` when the type of `e` mentions an unresolved name (`Map`
  without lib es2015). Probe: test/probes/plan08/alias_optional_chain.ts minus its `@target`.
- `check_with()` (ty/expr.c) computes an expression with a contextual type outside the LazyType, so no cycle detection
  (`let [x = x] = []` is stopped by the pattern's names being on the resolution stack, plan 39; other contextual
  self-references can still overflow the stack).
- Mutually recursive object literal getters through generic calls (probe test/probes/triage/getter_cycle.ts, zod's
  getter-schema shape with toy types): tsc reports TS7022 on all three variables and TS7023 on both getters, tttt
  nothing, and types the cycle differently (`variantA` any where tsc has `Schema<{ kind: "a"; readonly child: any }>`).
  Where the cycle is entered differs: tsc defers the getter bodies, and the getter's return type is asked for while
  `tree`'s initializer resolves `variantA`'s type. Not caused by the deferral/checkReturnStatement work (base fails it
  identically); worth a TTTT_DEBUG_CYCLE look.
- (mut0806) `class D extends class extends Base1 { root() { return undefined } }` with `abstract root(): D`
  (strictNullChecks): tsc's checkClassExpression -> checkClassLikeDeclaration runs eagerly inside D's
  getBaseConstructorTypeOfClass; relating `() => undefined` to `() => D` calls isWeakType(D), which needs D's members
  while its base constructor resolves: TS2506 + TS2310, D ends with no base (types print `typeof (Anonymous class)`).
  tttt defers class expression members and meets neither cycle. Plan 39 territory, low value.
## Classes and declarations
- Plan 32 leftovers: a class extending a base that constructs `any` should get tsc's anyBaseTypeIndexInfo
  (`[x: string]: any`) instead of the `ty_has_unresolved_base()` gate; tsc's display of `new Outer<string>().m` clones
  generic method type parameters (`Outer<string>.Outer.m<U>.In`).
- mergedDeclarations7: tsc's combineValueAndTypeSymbols (a named import from an `export =` whose value has property
  `Passport` and whose namespace exports interface `Passport`) makes a transient symbol whose declared type is a
  *fresh* interface type with its own `this`, so `use(): this` fails structurally (TS2322 "could be instantiated with
  a different subtype"). Needs a synthetic merged symbol with its own declared type. Quirk, low value.
## Narrowing and flow
- **Destructured references and widening** (ex plan 49; tsc's getNarrowedTypeOfSymbol re-reads a binding element of
  a `const` (for-of/for-in `const` too) or never-assigned parameter pattern of >= 2 elements from the *unwidened*
  parent type when the parent is a union, even when flow narrows nothing; widening drops object-literal-ness;
  ContainsWideningType propagates from own properties only). tttt: `ty_destructured_narrowed_type()` (flow.c),
  `binding_element_of()` (lower.c), `ty_is_unwidened_object_literal()`. **Rule: a binding element's declared type and
  its reference type come from the same element function, differing only in widening** (reading the reference with
  `ty_access_reading()` once gave +51 TS18048 under noUncheckedIndexedAccess on webpack). Open audits, no failing test
  known: (1) the other users of `Symbol.is_const` now that for-of/for-in consts have it (closure narrowing of captured
  consts, isConstantReference, TS2588, `abstract_literals`), `using`/`await using` in for-of; (2) destructured
  never-assigned parameters, contextually typed `({ a, b }) => ...` against a union contextual type and with a
  default, under nosnc and eopt; (3) `scripts/displaydiff.py` `we-any`/widening buckets on destructured references.
- The JSX attribute types (`KEY_JSX_ATTRIBUTE`) and an IIFE parameter's contextual type depend on check order (the
  last check wins). (An IIFE's parameters are pushed from `ty_quick_expr_type()` too now: before, a variable
  referenced above its IIFE initializer typed the function without them.)
- Destructuring assignment targets are checked as a whole, not property by property (tsc's
  checkDestructuringAssignment; hacks.md "Destructuring assignments"). The `{ a = dflt }` default of a shorthand
  assignment pattern is resolved but not checked (`ObjectExprItem.shorthand_default`).

## Corpus cells (flagflip) left after triage 2026-10-07 (each re-checked that day)
- (Fixed 2026-10-07, vscode now 0: TS2322 at stateToProgressAdapter.test.ts:1956. Mechanism, for similar cases: the
  arrow's return type inference asks whether its end is reachable; the reachability walk meets `assert.deepStrictEqual(
  calls, 1)` (node's `assert`, a generic assertion signature, so its effects need the call resolved, as in tsc's
  getEffectsSignature), which types `calls`, its initializer and the `image` reference before the declarations are
  checked. initial_binding_type() then typed `image`'s initializer without the annotation as contextual type, so
  `type: ToolResultContentType.EmbeddedResource` widened to the enum, no member of the declared union took it, and
  the cached assignment reduction was the declared union. It looked like an electron trigger because electron's
  typings bring @types/node, which is what makes `import assert from 'assert'` resolve.)
- valibot/nosnc -TS2578 x2 (forward.ts:51, forwardAsync.ts:55): `for (const key of path)` with `path: ValidPath<TInput,
  TPath>` (= `TPath extends LazyPath<TInput, TPath> ? TPath : LazyPath<TInput, TPath>`, deferred). tsc's key is
  generic, `KeyOf<TInput> | KeyOf<TInput[KeyOf<TInput> & keyof TInput]> | (KeyOf<TInput> & keyof TInput)` (tsctypeat;
  it prints `string | number` only where an annotation substitutes the constraint), so `pathInput[key]` on unknown
  isn't TS7053 and the `@ts-expect-error` is unused; tttt's key is `string | number`. tsc iterates the conditional's
  apparent type, its constraint: the true branch narrowed by the extends type and the false branch LazyPath's tuples,
  whose elements stay generic (object types are their own base constraint). tttt's iteration resolves all the way to
  primitives. Same root as the "conditional base-constraint fallback" item above. Repro: a project with one file
  importing `RequiredPath, ValidPath` from valibot's `methods/forward/types.ts`, strictNullChecks false, iterating
  `path` and indexing an `unknown`.
- xstate/eopt +TS2322 input.test.ts:272: generic Spawner signatures compared, `options` intersections that print the
  same are unrelated in tttt (`input?: number` & `input: unknown` reads number | undefined against number).
- xstate/sloppy +TS2589 graph/utils.ts:91 (`snapshot.getMeta()[id]`, Record<StateId<any> & string, ..>): a
  self-contained probe gives TS2589 on both sides, so it depends on which instantiations tsc has cached by then. Low
  value; drop unless it spreads.
- webpack/unused -TS6133 x5, -TS6196 x5 (Compilation.js:117/151/159/160, css/CssModule.js:11, json/JsonModule.js:16,
  module/ModuleFactory.js:8/10, template/DependencyTemplate.js:11/15): JSDoc `@import`ed names used only in
  `@property` tags of a `@typedef {object}` or in a `@callback`'s `@param`/`@returns`. tsc marks a JSDoc type
  reference's alias as used only when the reference is resolved, and those are resolved lazily: a typedef's members
  only when something needs them (a structural comparison, a property access), a callback's signature only when it's
  called or compared. Probe: `/** @import { RO as A1, RO as A3 } from "./a" */`, `@typedef {object} Data` with
  `@property {A1=} ro`, `@callback Cb` with `@param {A3} x`, a function taking `@param {Data} d @param {Cb} c` and
  returning them: tsc says A3 unused, and A1 too unless something resolves Data's members (an array literal of the
  parameters does). tttt marks references at name resolution (several_types.c `unused_mark_referenced`), eagerly. A
  laziness artifact of tsc's: matching it means marking JSDoc typedef/callback-body references when their types are
  resolved. Low value unless more corpora show it.
- (Fixed 2026-10-07: webpack/nuia TS2488 -1/+1, by where a union is first asked; vscode/nosft copilotAgent.ts:5860
  TS2339, overloaded method types in the stable order.) excalidraw/nuia's TS2488 -2/+1 is still the array-literal
  identity mechanism (above in triage, tsc's `[0, 0]` its own type object).
- Perf lead, not looked at: 9751c955 (TS2344 on deferred conditional arguments) costs valibot ~0.75%, mostly one
  relation (`TSchema extends ... ? IssueDotPath<TSchema> : string` against keyof any, flatten.ts:41, ~13 ms): tttt's
  relation through IssueDotPath's constraint may be slower than tsc's.

## Moved from hacks.md by the hacks audit (2026-10-08)
- A `declare x;` field without type or initializer in a class without a constructor doesn't take the base class
  property's type (tsc's getWidenedTypeForVariableLikeDeclaration: getTypeOfPropertyInBaseClass under noImplicitAny):
  `class B { x: string = "" } class D extends B { declare x; } const s: number = new D().x;` is TS7008 at `x` and
  `any` in tttt, TS2322 in tsc. Belongs next to `field_type_from_assignments()` (class_decl.c), which needs the class.
- Generators: no next type inferred from how `yield` results are used (tsc's getReturnTypeFromBody collects
  contextual types of yield expressions); `ty_generator_context` filters only unions.

## TS 7.1 window leftovers (triage 2026-10-09)
- Object literal methods of one name are one binder symbol in tsc (PropertyExcludes is None): `{ foo(x: 'hi') {}, foo(x:
  'a') {} }` is `{ foo(x: "hi"): void; foo(x: "a"): void }`, each name typed with both signatures
  (stringLiteralTypesInImplementationSignatures2, callSignaturesWithParameterInitializers2; ex plan 35).
- A merged property+accessor (one symbol since TS 7's excludes) still has 6.0's type: objectLiteralErrors,
  callSignaturesWithParameterInitializers2 types; duplicateClassElements symbols;
  symbolProperty44 (late-bound name vs method). duplicateIdentifierChecks: the parser merges a name's accessor
  declarations of a type literal/interface into one member, tsgo keeps a symbol per conflicting declaration.
- TS5114/TS5115/TS2589 limits: mutuallyRecursiveInference, keyofGenericExtendingClassDoubleLayer never hit the
  instantiation depth limit in tttt; recursiveMappedTypes hits it elsewhere (TS2589 vs TS2502);
  recursiveIndexedAccessSimplification lacks TS4109 and two TS2589; xstate under sloppy/nosnc (flagflip) gets one spurious
  TS5115 at graph/utils.ts:91 next to tsgo's at State.ts:395 (another StateId instantiation tsgo finds cached; tsgo's
  sites move with its checker count: -p with 4 checkers reports State.ts, setup.ts, stateUtils.ts); webpack misses
  zod's TS5115 at node_modules/zod/v4/classic/schemas.d.cts:806 (`interface ZodJSONSchemaInternals extends
  _ZodJSONSchema["_zod"]`, reproducible with that file alone: tsgo 162k instantiations, tttt no runaway; the same
  family as sourcesweep_checker.md's -TS5115 and vue's postcss-selector-parser one).
- distributiveConditionalBaseConstraint: spurious TS2344, `Wrapped<FromSchema<Context, Props[P]>>` vs Type through
  `FromSchema<C, S> = S extends Type ? FromSchema<C, C[keyof C]> : never` (TypeScript#63568).
- Variances still measured differently from tsgo on xstate (25 of 160 type parameters, 2026-10-12; compare with
  tsgo's `--generateTrace` getVariancesWorker events, script idea: dump both, key by symbol name): mostly the
  unreliable/unmeasurable flags (tsgo `out (unreliable)` where tttt has plain or invariant) and some independent vs
  measured. The restart/Unknown/marker mechanics are ported (relate.c measured_variances()).
- nestedSpreadsAndWidening: the spread of an 11-member union of nested spreads prints a 6 MB type (tsc 1.4 MB): tttt's
  spread of unions doesn't reduce/dedupe like tsc's getSpreadType.
- crossProductUnionIntersectionRepeatedPropDeclarationsNoOOM1: `props?.w` of a big union of intersections: tsc's type
  has `(X["w"] & number) | ...` members (union property type intersected with the declared number), tttt plain X["w"].
- reverseMappedDeclarationSerialization: reverse mapped inference through a recursive `Validator<T>`: tttt reports
  TS2321/TS5114/TS2345 on `decode(growingInput)` where tsgo infers quietly. Plan-sized.
- inferFromGenericFunctionReturnTypes3: spurious errors (relation path differs too).
- Inference: `$ExpectType` map-to-obj `MapToObj(new Map([["hello", "world"]]))`: tsgo infers `Map<string, string>`
  inside the call (the outer unfixed K is silentNever through the NoDefault mapper, so the tuple's "hello" has no
  literal context), tttt keeps `"hello"` (also `f<K extends string>(m: Map<K, string>)`); mut0348
  (objectRestBindingContextualInference, `K extends Object`): with `const { prepare, ...rest } =` tsgo infers
  K = "image" from the binding pattern + argument, tttt widens to string; mut2626 (noInferCommonPropertyCheck1,
  `test2(null, ...)`): tsgo's `NoInfer<T1> & NoInfer<T2>` with T1 = null is `never`, tttt keeps `null &
  NoInfer<Partial<...>>`.
- Inference from an intersection to one generic reference (`SyncBailHook<[OA], PA> & SyncBailHook<[OB], PB>` to
  `SyncBailHook<T, R>`, tapable): tsgo infers T = `OB | [OB]` (through the members' `AsArray<T>` conditional rest
  parameters), tttt `[OB]`; both then fail the same assignment (webpack's HookMap TS2322, fixed 2026-10-12 by the
  any-key index read), only the message's types differ.
- types/redux-define (DT sweep): TS2344 "only assignable to the non-distributed 'SubAction'" (distributed type
  parameters, plan 57's area).
- flagflip xstate/eopt: one spurious TS2322 (input.test.ts:272, a spawn signature); vue/nuia 2 spurious + 2 other-code.

## The conditional type-variable walk resolves signatures tsc never looks at
`definitely_unrelated()` / `definitely_related()` in `src/ty/conditional.c` collect the type variables a conditional's
check type mentions with `mentions_type_variables()`, which resolves every signature of a function type: return types
still to be inferred from bodies, type parameter constraints. tsc never looks there: `getPermissiveInstantiation`
instantiates lazily, and the relation asks only for what it compares. Over a class whose method needs the mapped type
being resolved that is a cycle (TS2615/TS2502), and whether it happens depends on what a checker resolved first.

Patched (2026-10-13, plan 62 stage 1) for the two "is it a function" idioms only: `callable_definitely_related()`
takes the true branch of `X extends (...args: any[]) => any` and `X extends Function` for a callable X before any walk
(dexie's NonInsertProps, mikro-orm's CleanKeys; test ttttConditionalCallableAgainstAnyFunction). Any other extends
type over such a class still walks.

Tried before: returning "unsure" from the walk when a function's return type isn't inferred yet (so the plain relation
decides). Fixed dexie, but regressed excalidraw (TS7006/TS2493/TS2345 in vitest `mockImplementationOnce` callbacks) and
vscode (1): "unsure" also makes `definitely_related` relate without the restrictive instantiation and report
`mentions`, which changes other conditionals' answers. A real fix makes the walk lazy like tsc's permissive
instantiation (instantiate the check type with the wildcard mapper without walking, relate, and only resolve what the
relation asks for), which is plan-sized: definitely_related's restrictive instantiation needs the same.

## Relation answers that depend on what was related before (mermaid, plan 62)
mermaid's `packages/mermaid/scripts/create-types-from-json-schema.mts(129,23)` TS2345 (tsgo reports it at any checker
count) is lost when that file is checked by a fresh checker: `TTTT_CHECK_ONLY=create-types-from-json-schema
release/tttt --checkers 1 -p .` in `thirdparty/sources/mermaid` reproduces it at one checker (kdiff lists it at K=16).
`UncheckedJSONSchemaType<MermaidConfig, false>` -> `UncheckedJSONSchemaType<Record<string, any>, false>`: in the full
run the structural comparison of `UncheckedPropertiesSchema<MermaidConfig>` (variances unmeasurable|unreliable, 24)
meets SankeyDiagramConfig's property first and fails (0); fresh, it meets `Config`'s, which ends Maybe through the
recursion (`TTTT_DEBUG_RELATE_NESTED=1`: every level 3), and the top-level Maybe is success. Partial's variance is
measured in the middle of the fresh relation. Unknown whether tsgo would give Maybe in some order too (then it's a
tsgo-style order dependence for bugs/tsgo_divergences.md); start by checking which member and property order tsgo
compares (`scripts/variancetrace.py`-style instrumentation of relater.go's recursiveTypeRelatedTo).

## Without a global Array (noLib or a missing lib; triage 2026-10-14)
- A tuple's rest element is stored as its array type, which is `{}` then: `[number, ...string[]]` prints
  `[number, ...{}]` (tsgo: `...string[]`, tsc stores the rest element's element type, getTypeFromRestTypeNode), its
  slices `[...{}]` (tsgo `{}`), `k(...t)` infers `[number, ...{}]` (tsgo `[number, ...any[]]`). Only noLib tests can see
  it (tsgo's CLI stops at TS2318). The crash itself is fixed (ttttNoLibTupleSpread).
- type-fest checked without lib (`--keep-going`) overflows the 64 MB stack in conditional tail recursion
  (test-d/union-max.ts: `InternalUnionMax`, each of its ~1000 tail steps lowers type arguments inside the recursion):
  hacks.md's "Real fix: a loop" for `ty_instantiate_tail`.
