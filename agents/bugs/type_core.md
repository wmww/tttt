# Type core, inference and flow bugs
Wrong types (not missing diagnostics: `missing_checks.md`; not printing: `display.md`). Pruned 2026-10-09 against
the tsgo 7.1 suite: every test named here fails there unless the entry says otherwise (`mutNNNN` entries are mutants of
passing tests).

## Inference
- **First pass skips context-sensitive arguments entirely**: tsc checks them with their functions replaced by
  anyFunctionType and still infers from the (non-inferrable) literal to anything but a naked type parameter (hacks.md
  "Skip-sensitive rounds"). [open]
- `infer_rest_variadic`/`check_rest_arg` keep an argument's type from whichever overload candidate typed it first
  (`if (arg->type) continue`), though its contextual type is the candidate's rest element; tsc re-checks per candidate.
  Clearing the non-spread rest arguments' types at the top of infer_rest_variadic changed nothing anywhere (suite,
  corpora, instructions) and no probe observes it (`q<T extends ["a"]>(n: number, ...a: T)` before `q<T extends
  string[]>(s: string, ...a: T)` passes). Look here if overloads with differing rest types ever disagree with tsc. [note]
- Older tsc-corpus finding: literal widening in an object literal that has a contextual type (no probe; the visitNode
  overload probe, test/probes/plan05/visitNodeInference.ts, passes since). [open]
- (triage 2026-10-07) The contextual type of an argument for a rest parameter typed as a mapped type over `keyof
  [...E, "abc"]` (index 0 -> `{ bar: [...E, "abc"][0] }`) keeps the literal in tsc: TS2345 messages print `{ bar:
  "abc"; }` where tttt prints `{ bar: string; }`. [text]
- (2026-10-07) map-to-obj (DefinitelyTyped, 2 `$ExpectType`): `id(new Map([["a", 1]]))` with `id<K extends
  string>(m: Map<K, V>)`: tsc's return mapper for the inner `new Map` comes from the contextual type with the outer K
  as createOuterReturnMapper has it (nothing inferred: its constraint string), so "a" widens; tttt's inner return
  inference maps the inner K to the outer K, whose primitive constraint keeps "a". Instantiating the return source
  with the outer's constraints (infer_type_arguments' `return_source`) fixes it but cost xstate and valibot +1.8%
  each even gated to primitive constraints: needs a cheaper formulation. [fixed b502608dd]
- (2026-10-07) ember__service (2, also v3): an object literal method's literal return ("") widens when the call has
  two overloads and the first round (subtype) fails on `this`: the return type is computed (and memoized) during that
  round without the "own contextual signature" that keeps the literal. Repro: `declare class Core { constructor(p?:
  object); static extend<S, I>(): ECC<I>; static extend<S, I, T1 extends {[k: string]: any}>(this: S & ECC<I>, a:
  T1): ECC<T1 & I> }` with `ECC<T> = (new (p?: object) => T) & (new (...a: any[]) => T)`: `new (Core.extend({ m() {
  return ""; } }))().m()` is "" in tsc. Same family as the item above (a return type memoized before the context
  that would keep the literal exists). [fixed 5851d4ae4]
- (2026-10-07) babel__traverse (4) and babel__helper-plugin-utils (2): with @babel/types unresolved, a contextual
  type `{ [k: string]: H<t.Node> } & { [k: string]: H<t.Aliases> }` (H a function | object union) gives a method
  parameter NodePath<t.Aliases> in tsc, NodePath<t.Node> in tttt: which unresolved member (tsc keeps named error
  types, tttt collapses them to any) survives the intersection of contextual signatures. Depends on the named error
  types item (triage "TS 6.0 retarget leftovers"); broken packages only. [fixed 9828bdd42]

- **Call resolution leftovers of the inference-flow port (from the inference-flow port's "not done", each untried, no failing test
  known)**: `ty_call_effects()` (call.c, flow.c's getEffectsSignature) resolves a call on its own, without contextual
  type, where tsc takes getResolvedSignature (would need the chosen signature in `CallResolution`); JSX elements
  (`ty_resolve_jsx_element()`) aren't remembered per node like calls (`KEY_CALL_RESOLUTION`), they resolve per
  request; SkipGenericFunctions for generic functions nested in literal arguments (`Arg.nested_generic`) applies to
  the applicability check only: leaving them out of the first inference round lost zod's `pick([z.object, ...])` and
  xstate inferences, because tttt marks literals from syntax where tsc skips only where a contextual signature exists
  (checkFunctionExpressionOrObjectLiteralMethod under SkipGenericFunctions): a port marks from the contextual type. [note]
- **Inference context by ancestry** (from the contextual-check state work's optional item, kept: it prevents a class of latent bugs).
  `my outer_inference` is dynamic state saved/cleared/restored at ~15 sites in call.c; tsc's getInferenceContext
  finds the innermost inferring call by walking up from the node. A stack of (node, OuterInference) pushed per
  argument check and looked up by span containment would replace the save/restore sites and stop the flow engine's
  leak (`V_LOGAUD=1` in the contextual-check state work's prototype C, see git history: 121 entries where flow narrowing evaluates an
  unrelated type-guard call while another call infers). No baseline change expected; the contextual-check state work's latent bug
  (`infer_rest_variadic()`'s first pass forgetting to set `my outer_inference`) is the kind it would prevent. [note]
- Eager predicate inference during instantiation (`inference_mapper` -> `signature_predicate` ->
  `ty_infer_type_predicate` checks a body; tsc reads predicates lazily). Its test (recursiveGenericSignature
  Instantiation) passes; do it if another shows up (from the laziness work). [note]

- A spread's properties under a `const` type parameter (2026-10-23, corB): `defineContract<const C>(c: C)` with
  `defineContract({ operations: { [k]: { ...base, kind: "action" } } })`: tsgo prints the inner object as `{ input:
  ...; kind: "action"; }` (the spread result's properties aren't readonly, getSpreadType drops it), tttt as `{
  readonly input: ...; readonly kind: "action"; }` (types baseline only; openclaw's plugin-sdk/feature-plugin.test.ts).
  [open]

- (types only) ttttNonNullOperandNotInferential: the diagnostics match; `mapping[k]` inside `mapping[k]!` in a generic
  call's argument prints `M[keyof S]` where tsc prints its constraint `string | number | undefined` (the non-null
  operand is checked without the inferential check mode, getNarrowableTypeForReference substitutes the constraint). [open]
## Relations, instantiation, reduction
- (cor, 2026-10-28) An interface whose base is a mapped type carries TYF_COULD_CONTAIN_TYPE_VARIABLES where tsc's
  couldContainTypeVariables is false (a non-generic interface reference has no type arguments): `interface MenuProps
  { open(): void } interface P extends Partial<MenuProps> { x?: boolean }`, then `P & Deps` has the flag where
  `MenuProps & Deps` doesn't. It made expand_generic_homomorphic() (members.c) give `Readonly<Props>` with `Props
  extends P & Deps` no properties (freelens, worked around in df95ec2f5 by also asking intersection_has_generic_part();
  repro ttttMappedOverIntersectionWithMappedBase). Other code that tests the flag on such types takes slower or
  generic paths; where lowering sets it for these interfaces wasn't chased. [open]
- (det3, 2026-10-24) An object spread after one with an index signature kept it when what was spread second was
  `{} | undefined` (tsc's tryMergeUnionOfObjectTypeAndEmptyObject returns the `{}`, whose missing index signature
  getUnionIndexInfos then drops) or a union member undefined of the cross product (getSpreadType merges undefined like
  an object without index signatures): microsoft/vscode at 2bc7d5ab, agentService.ts's two spurious TS2345 (the
  literal's union subtype-reduces to `{}` in tsc on widening). spread_acc_type; test/cases/rw_vscode_TS2345.ts.
  [fixed 2026-10-24]
- (det, 2026-10-23, fixed; no cut-down repro) A generic function argument's instantiation in context ran its
  constraint check under whatever relation was in progress further up (`my relating`, ty_is_related_within): openclaw's
  zod `z.object({...}).transform(omitUndefinedFields)` (src/infra/restart-sentinel-store.ts) evaluated from inside an
  identity relation (Awaited's conditional -> compare_signatures -> infer_to_union's is_identical -> a prop's
  declaration) inferred T = object for good. Now only compare_signatures passes `in_relation`. The repro needs zod's
  real types: `TTTT_CHECK_ONLY=openclaw/src/infra/outbound/delivery-queue-recovery-params.ts,openclaw/src/gateway/server.config-security-policy.test.ts
  tttt -p openclaw/src/gateway --checkers 1 --threads 1` from thirdparty/sources (TS2339 'requiresRestart' on 'object'
  before the fix); zod-like `.d.ts` imitations (a generic `transform<U>` with an `omit<T extends object>` argument,
  reached through a signature relation, a class property, `Awaited<ReturnType<...>>`) didn't route through identity. [fixed 2026-10-23]
- (atproto probe, 2026-10-20; symbols baseline only) Property access on an intersection whose apparent type is any
  (a generic member constrained to any) finds the object member's symbol in tttt; tsc's
  getApparentTypeOfIntersectionType makes the whole apparent type any, no symbol. Probe:
  ttttMappedOverApparentlyAnyIntersection with `i: InferOutput<TShape> & { $type: TType }`, `i.$type`. find_prop_in()
  isn't the path property access takes; look at what property access does with an intersection's apparent type. [text]
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
  `wpl/examples/aggressive-merging/webpack.config.js` (types node, typeRoots wpl/node_modules/@types). [open]
- Distributive conditional base constraint: with `S2<T> = T extends string ? T : number`,
  `g2<T extends unknown[]>(x: [...{ [K in keyof T]: S2<T[K]> }])` widens `g2(['a'])` to `[string]`, tsc `["a"]` (its
  forConstraint constraint of `S2<T[number]>` keeps the true branch with T's implied `string`). [open]
- `StateId<U>` to `StateId<T>` (xstate's StateId, no variance measurement) gives TS2589 next to the TS2322 tsc also
  reports: the definitely-assignable checks instantiate deeply nested indexed-access chains. Repro:
  test/probes/xstate/state_id_deep_nesting.ts. [open]
- **Intersection reduction by constraint**: `T & P` for a type parameter T with a primitive constraint is reduced
  (`reduce_type_variable_intersection`, and `(T & "a") | (T & "b")` back to T), but not for indexed access type
  variables (see hacks.md) nor type parameters with a deferred declared constraint. [open]
- **Permissive instantiation** (conditional.c's definitely_unrelated) maps the type parameters a walk collects to the
  wildcard, where tsc's permissiveMapper maps every one it meets; the wildcard has to stay the wildcard wherever tsc
  special-cases it (getIndexType, instantiateMappedType's instantiateConstituent: both fixed 2026-10-18, a missed one
  decides a conditional type definitely false too early, ramdaToolsNoInfinite2's TS5114). [note]
- **isTupleLikeType's length half**: an array-like context whose `length` is a number literal isn't tuple-like
  (only the "0" property rule is in `context_is_tuple_like`). [open]
- **Conditional true branch with `infer`s**: relating a deferred conditional through its true branch with `infer`s as
  unknown is done only when the check type is generic; with a generic extends type tsc has real inferences. [open]
- A homomorphic mapped type over a `satisfies`/const object doesn't resolve its keys (the `computedOptions` mapped
  type in the tsc corpus's `utilities.ts` printed a huge `keyof "a" & keyof "b"` key). Not re-probed. [open]

- Base constraint of an indexed access comes out wrong in valibot: in
  `library/src/methods/required/requiredAsync.ts` (the `'~run'` member's `InferIssue<TSchema>`, TSchema constrained
  to `SchemaWithoutPipe<LooseObjectSchema<...> | ...>` = `(A | B | ...) & { pipe?: never }`), `ty_base_constraint` of
  `TSchema['~types']` is `undefined`, where tsc has `{ input; output; issue } | undefined`. Found when constrained type
  variable reduction (`reduce_type_variable_intersection`) turned `TSchema['~types'] & {}` into never; not reduced to
  a small repro (a two-member union of interfaces with an optional `'~types'` under exactOptionalPropertyTypes is
  fine). Possibly a constraint computed while an alias or the type parameter's constraint is in flight and memoized. [open]

- **`awaitedTypeStrictNull` overflows the stack** in `instantiate_mapped_over_list` under the debug build + valgrind
  (8MB main stack; the release build passes; reproduced 2026-10-05 with `scripts/test.sh -f awaitedTypeStrictNull`). Deep
  but maybe not infinite recursion; worth checking whether the recursion is bounded. [open]

- **No general property list for intersections** (tsc's getPropertiesOfType): `ty_has_exact_optional_unassignable`
  (relate.c, takes the first member's properties for unions now), jsx.c `container_prop_name`, members.c
  `builder_add_intersection_members` and the member loop in class_decl.c (~line 509) take each member's apparent type
  and require `TYF_OBJECT`, so a type parameter constrained to an intersection is skipped. `spreads_into_nothing`
  (expr.c) ignores intersections (tsc: empty when every member is). `with_synthetic_default` (lower.c) keeps index
  signatures and private/readonly members that tsc's getSpreadType drops. check.c's `number_index` check and the
  private-name check are still object-only gates (low value). [open]
- **Spurious TS2589 on zod without variance annotations** (deep-partial.test.ts:111, since the explicit type argument
  constraint check of triage 2026-09-28): `toExtend<z.ZodObject>` against expect-type's recursive `MismatchInfo` goes
  >100 levels (`props_related_impl` -> `prop_type` -> `ty_indexed_instantiated` -> `find_prop` ->
  `conditional_from_root` -> ~135 `instantiate_inner`). tttt's order of `MismatchInfo`'s key union starts with `array,
  nonoptional, _zod...`, tsc's with `default`, which fails at once. Union order again; the TS2344 text there also
  differs (property order, tsc's `... 62 more ...` truncation). `perf_leads.md` "Variance measurements" says how to
  build that zod variant (strip `in`/`out` both after `<`/`,` and at line starts inside multi-line lists). [open]
- (triage 2026-10-07, mut3125) AllowMissing for destructuring defaults should go by tsc's isObjectLiteralType of the
  unwidened parent. tsc has three states (fresh, regular, widened; getWidenedTypeOfObjectLiteral builds a type
  without ObjectFlags.ObjectLiteral), so `var b = {y: 0}; var {x = ""} = b` is TS2339 while `var {x = ""} = {y: 0}` is
  TS2353. tttt's `ty_widened_objects()` keeps `anonymous.object_literal`, so `binding_assigned_type` (lower.c) and
  binding.c treat widened declarations as literals. Fix: the widened twin gets `object_literal = false`, after auditing
  every reader (union partial-property rule, excess properties, the isObjectLiteralType relation rules) and the array
  literal element types tttt widens early (`for (var {x = ""} of [{}])`). Medium to large. (cor3, 2026-10-29) bun 99-V4's
  getTypeFromBindingElement__03/04 are this: a parameter pattern defaulted by a named constant (`function f({ a, b = a }
  = o)` with `const o = { a: 1 }`) is padded by tttt (ty_pad_initializer_type()) and its missing `b` allowed
  (AllowMissing), where tsc's isObjectLiteralType of the widened `o` is false: TS2339 on b, no padding, and no
  spurious TS7022 on a through the padded default. Switching the five AllowMissing/padding sites (lower.c
  ty_pad_initializer_type and the AllowMissing branch, binding.c x2, expr.c x2, binding_add_padded_implicit_any_errors)
  to ty_is_unwidened_object_literal() fixes those two and breaks three suite tests (destructuringWithLiteralInitializers'
  `{ a: { x = 0, y = 0 } } = { a: {} }`, shorthandPropertyAssignmentsInDestructuring's `for ({ s1 = 5 } of [{}])`):
  the nested and the array element literals are widened early by check_for_mutable_location()'s ty_widened_objects()
  (expr.c ~644 and ~1200), so OBJECT_LITERAL stands for both tsc's widened and its regular-but-nested types. The fix
  needs the early widening to keep regular literals regular (or a widened kind of its own). [open]
- (mut1640) An element *write* with a `never` index on a union of objects (`both[sym] = 'x'`, `sym: never`, members
  `{ s: number, '0': number, [sym]: boolean } | { [n: number]: number, [s: string]: string | number }`): tsc reads the
  union's number index info (getUnionIndexInfos, getApplicableIndexInfo with never: `number | boolean`), tttt `never`. [open]
- The conditional base-constraint fallback (`base_constraint_uncached` TY_CONDITIONAL, used when there's no
  distributive constraint) leaves an `any` branch out like tsc's getDefaultConstraintOfConditionalType but uses the
  raw true branch, not tsc's inferred one. `ReturnType<T[M]>` property access (seed-5 mut0710/mut3585) still misses
  TS2339. [open]
- genericCallInferenceUsingThisTypeNoInvalidCacheReuseAfterMappedTypeApplication1: argued out of reach,
  tsgo_divergences.md (a tsgo instantiation-cache collision). [dup]
- (2026-10-07) knockout.mapping `$ExpectType` (knockout.mapping-tests.ts:109): tsc orders
  `KnockoutObservableArray<KnockoutObservableType<any>> | KnockoutObservableArray<any>`, tttt the other way round.
  Not investigated (type id order of two references to one target: which type argument list is made first). [fixed oracle 2026-10-23]
- (2026-10-07) node (4 `$ExpectType`, all versions): `[value: number] | [value: number]` from two declarations: tsc's
  tuple targets are keyed by their labels' declarations (getTupleTargetType's namedMemberDeclarations), tttt's by the
  label names, so the two tuples are one type and the union collapses. [fixed 54a1c4a5c]
## Circularity (deferred references leftovers; the resolution stack's are in missing_checks.md)
- recursiveConditionalTypes (types only): `unbox(b4)` fixed 2026-10-15 (ttttRecursiveUnionInferenceDepth), and the
  getter's lazy typing since (ttttLiteralGetterDuringResolution: an object literal getter a call's resolution types
  has `this` any in an argument literal, the literal's own type in a literal nested in one; computed uncached so the
  body's deferred check types `this` with the resolved signature, ty_object_literal_accessor_type()). Left, the
  nested case: tsgo's `this` there is the widened literal *after* its check, whose getter property is the getter
  itself, a self-referential type (W = { readonly value: W }: an instrumented tsgo's inference walks W.value back to
  W); tttt types the getter while checking the literal, so `this` is the literal as far as it is built, and unbox's
  inference then gets one candidate more (`{ readonly value: any }` where tsgo infers `{ readonly value: { readonly
  value: any } }`), and the deferred `this` prints differently. Needs object literal getter properties typed lazily
  (a Prop whose type is the getter's, resolved on first ask, shared by the widened copy): medium-sized, broad (widening,
  freshness, relations of literals). [text]
- type-fest's `test-d/opaque.ts` (~90% of type-fest's check time) runs into TS2589 at line 68 (`Tagged`/`UnwrapTagged`
  over `TagContainer`) plus four TS7022 tsc doesn't report: a correctness bug with a perf symptom. [open]
- Spurious TS7022 on `const can = composite && o?.e && !out` when the type of `e` mentions an unresolved name (`Map`
  without lib es2015). Probe: test/probes/plan08/alias_optional_chain.ts minus its `@target`. [open]
- `check_with()` (ty/expr.c) computes an expression with a contextual type outside the LazyType, so no cycle detection
  (`let [x = x] = []` is stopped by the pattern's names being on the resolution stack; other contextual
  self-references can still overflow the stack). [open]
- Mutually recursive object literal getters through generic calls (probe test/probes/triage/getter_cycle.ts, zod's
  getter-schema shape with toy types): tsc reports TS7022 on all three variables and TS7023 on both getters, tttt
  nothing, and types the cycle differently (`variantA` any where tsc has `Schema<{ kind: "a"; readonly child: any }>`).
  Where the cycle is entered differs: tsc defers the getter bodies, and the getter's return type is asked for while
  `tree`'s initializer resolves `variantA`'s type. Not caused by the deferral/checkReturnStatement work (base fails it
  identically); worth a TTTT_DEBUG_CYCLE look. [open]
- (mut0806) `class D extends class extends Base1 { root() { return undefined } }` with `abstract root(): D`
  (strictNullChecks): tsc's checkClassExpression -> checkClassLikeDeclaration runs eagerly inside D's
  getBaseConstructorTypeOfClass; relating `() => undefined` to `() => D` calls isWeakType(D), which needs D's members
  while its base constructor resolves: TS2506 + TS2310, D ends with no base (types print `typeof (Anonymous class)`).
  tttt defers class expression members and meets neither cycle. Resolution-stack territory, low value. [dup]
- TS7023 through a handler's inferred return (re-probed 2026-10-19, from the corpora-to-zero work's (corpora.md) drizzle notes):
  `test/probes/plan19/drizzle/proxy_recursive_wrap2.ts` (`--strict`), `function wrap2(target: object) { return
  Q(target, { get(target, p) { return wrap2(target); } }) }` with `Q<T>(t: T, h: { get?(target: T, p: string): any
  }): T`: tttt reports TS7023 on both functions of the probe (the generic `wrap` too), tsgo nothing. The drizzle
  shape itself (proxy_recursive_wrap.ts, c08d13b3) passes. c08d13b3 blamed the method's inferred type predicate
  evaluating `wrap2(target)` while wrap2's return type is in flight; how tsgo's ordering avoids the cycle
  (contextuallyCheckFunctionExpressionOrObjectLiteralMethod decides `get`'s return type from its body too) wasn't
  worked out. Not corpus-visible. [open]
- An unused private member is history-dependent in tsgo too (bun's 39-P7, 2026-10-23): x.ts `import './y'; export
  class C { private x = 1 }`, y.ts `new C()["x"]`. y.ts comes first in program order, so one checker has marked `x`
  referenced when x.ts reports unused members; a checker that never checked y.ts reports TS6133. `tsgo --checkers 1`
  and the suite baseline report nothing, tsgo's default (4 checkers) reports TS6133; tttt at `--checker-memory 0` and
  `TTTT_CHECK_ONLY=x.ts` report it too. Same rule as tsc, so scripts/oracles.py's recycle comparison skips it with
  92-R16b (HISTORY_DEPENDENT). Removing it would mean a program-wide reference pass tsgo doesn't have. [tsgo-shared] [tsgo]
- First-checker-wins circularity diagnostics make `--checkers 4` output vary run to run (bun's 92-R16b, 2026-10-23):
  a.ts `type A = { [K in "a"]: B[K & keyof B] }; declare const a: A; a.a;`, b.ts the same with A and B swapped.
  TS2615 is reported where the cycle is first met, so it depends on history, in tsc too: with `"files": ["b.ts",
  "a.ts"]` tsgo and tttt K1 both report it at b.ts, and K1 in file order both report it at a.ts only.
  `TTTT_CHECK_ONLY=b.ts --checkers 1` reproduces the b.ts copy. With `--checkers 4`, tsgo (2 files) reports a.ts
  only. tttt reports it at b.ts too in about 1 run in 3 (30 runs: 20 one way, 10 the other), so here the two files
  sometimes land in different checkers: the file-to-checker assignment (work taking?) isn't deterministic for a
  forced K above the file count. The default K is 1 on projects this small. Worth a look at how files are dealt
  when K exceeds what the sizes call for: tsgo's assignment is static. [open]
## Classes and declarations
- Mixin leftovers: a class extending a base that constructs `any` should get tsc's anyBaseTypeIndexInfo
  (`[x: string]: any`) instead of the `ty_has_unresolved_base()` gate; tsc's display of `new Outer<string>().m` clones
  generic method type parameters (`Outer<string>.Outer.m<U>.In`). [open]
- (triage 2026-10-15, re-probed) An object literal method merged with a late-bound one (`declare const m: "mm"; {
  mm() { return 1 }, [m](x: number) { return "" } }`): tsgo types both method nodes, the member and `o.mm` by the
  merged binder symbol (`{ (): number; (x: number): string; }`, the literal `{ mm(): number; mm(x: number): string;
  }`), tttt by the last alone. Two early `mm()`s are separate symbols to tsgo (last wins, tttt agrees). Needs a method
  function type built from several FunctionImpls (an unread `ObjectLiteralProp.methods` list and an unused
  `KEY_LITERAL_METHOD_OVERLOADS` kind were removed 2026-10-28: start from tsgo's merged symbol, not those). [open]
- (triage 2026-10-16) **An accessor pair split between a written and a late-bound name**: `get x(): number` with
  `set [k](v: string)` (k of type `"x"`, not a literal key) is one symbol under tsc (the late setter merges into the
  early `x`: read `number`, write `string`, `c.x = 1` is TS2322). tttt types `c.x` as `any` and reports TS2540 (no
  setter); `get [k]` with `set x` reads the setter's type. ty_class_member_symbols() pairs accessors only among the
  late-bound ones (`accessor_late()`), the binder only among written names. Fix: pair across both in the table (early
  declarations first, mergeSymbol's order) and set `late_accessor_class` on every accessor of a class with a
  late-bound one, but the early pass would then type late keys (the circularity classDeclarationShouldBeOutOfScopeIn
  ComputedNames guards): only look the table up when a late-bound accessor's key could name the written one. Rare in
  real code (no oracle shows it). [open]

- (types only) ttttTypeofFunctionExpressionInFakeScope: a named function expression's `typeof somefn` prints as
  `typeof x` in tsc (the variable it initializes), and `typeof w[]` needs parentheses, `(typeof w)[]`: the printer's
  typeof-of-a-function-expression naming and array element precedence. [text]
## Narrowing and flow
- **Destructured references and widening** (from the destructured-references work; tsc's getNarrowedTypeOfSymbol re-reads a binding element of
  a `const` (for-of/for-in `const` too) or never-assigned parameter pattern of >= 2 elements from the *unwidened*
  parent type when the parent is a union, even when flow narrows nothing; widening drops object-literal-ness;
  ContainsWideningType propagates from own properties only). tttt: `ty_destructured_narrowed_type()` (flow.c),
  `binding_element_of()` (lower.c), `ty_is_unwidened_object_literal()`. **Rule: a binding element's declared type and
  its reference type come from the same element function, differing only in widening** (reading the reference with
  `ty_access_reading()` once gave +51 TS18048 under noUncheckedIndexedAccess on webpack). Open audits, no failing test
  known: (1) the other users of `Symbol.is_const` now that for-of/for-in consts have it (closure narrowing of captured
  consts, isConstantReference, TS2588, `abstract_literals`), `using`/`await using` in for-of; (2) destructured
  never-assigned parameters, contextually typed `({ a, b }) => ...` against a union contextual type and with a
  default, under nosnc and eopt; (3) `scripts/displaydiff.py` `we-any`/widening buckets on destructured references. [note]
- The JSX attribute types (`KEY_JSX_ATTRIBUTE`) and an IIFE parameter's contextual type depend on check order (the
  last check wins). (An IIFE's parameters are pushed from `ty_quick_expr_type()` too now: before, a variable
  referenced above its IIFE initializer typed the function without them.) [note]
- Destructuring assignment targets are checked as a whole, not property by property (tsc's
  checkDestructuringAssignment; hacks.md "Destructuring assignments"). [open]
- A shorthand assignment pattern's default (`({ b = d } = o)`, `ObjectExprItem.shorthand_default`) was checked with
  the target's type as contextual type (tsc: none, `{ f = x => x } = o` is TS7006, a generic `t` stays `T` in the
  message) and an any or unknown default kept undefined in the property (tsc's hasTypeFacts(IsUndefined) is false for
  them: spurious TS2322 on `({ b = x as any } = o)`), a type variable now by its base constraint. expr.c's destructuring
  assignment check. Test ttttShorthandAssignmentDefault [fixed PENDING].

- (types only) ttttFlowIssueSingleQuery: the diagnostics match now (the extra flow query was removed), but `r.previous`
  on the narrowed silent `never` prints `never` where tsc prints the error type (`previous : any`, `r.previous : error`):
  tsc's property access on a silent never returns errorType without a diagnostic. [open]
## Corpus cells (flagflip) left after triage 2026-10-07 (each re-checked that day)
- (Fixed 2026-10-07, vscode now 0: TS2322 at stateToProgressAdapter.test.ts:1956. Mechanism, for similar cases: the
  arrow's return type inference asks whether its end is reachable; the reachability walk meets `assert.deepStrictEqual(
  calls, 1)` (node's `assert`, a generic assertion signature, so its effects need the call resolved, as in tsc's
  getEffectsSignature), which types `calls`, its initializer and the `image` reference before the declarations are
  checked. initial_binding_type() then typed `image`'s initializer without the annotation as contextual type, so
  `type: ToolResultContentType.EmbeddedResource` widened to the enum, no member of the declared union took it, and
  the cached assignment reduction was the declared union. It looked like an electron trigger because electron's
  typings bring @types/node, which is what makes `import assert from 'assert'` resolve.) [fixed 2026-10-07]
- valibot/nosnc -TS2578 x2 (forward.ts:51, forwardAsync.ts:55): `for (const key of path)` with `path: ValidPath<TInput,
  TPath>` (= `TPath extends LazyPath<TInput, TPath> ? TPath : LazyPath<TInput, TPath>`, deferred). tsc's key is
  generic, `KeyOf<TInput> | KeyOf<TInput[KeyOf<TInput> & keyof TInput]> | (KeyOf<TInput> & keyof TInput)` (tsctypeat;
  it prints `string | number` only where an annotation substitutes the constraint), so `pathInput[key]` on unknown
  isn't TS7053 and the `@ts-expect-error` is unused; tttt's key is `string | number`. tsc iterates the conditional's
  apparent type, its constraint: the true branch narrowed by the extends type and the false branch LazyPath's tuples,
  whose elements stay generic (object types are their own base constraint). tttt's iteration resolves all the way to
  primitives. Same root as the "conditional base-constraint fallback" item above. Repro: a project with one file
  importing `RequiredPath, ValidPath` from valibot's `methods/forward/types.ts`, strictNullChecks false, iterating
  `path` and indexing an `unknown`. [fixed oracle 2026-10-23]
- xstate/eopt +TS2322 input.test.ts:272: generic Spawner signatures compared, `options` intersections that print the
  same are unrelated in tttt (`input?: number` & `input: unknown` reads number | undefined against number). [fixed oracle 2026-10-23]
- xstate/sloppy +TS2589 graph/utils.ts:91 (`snapshot.getMeta()[id]`, Record<StateId<any> & string, ..>): a
  self-contained probe gives TS2589 on both sides, so it depends on which instantiations tsc has cached by then. Low
  value; drop unless it spreads. [fixed oracle 2026-10-23]
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
  resolved. Low value unless more corpora show it. [fixed oracle 2026-10-23]
- (Fixed 2026-10-07: webpack/nuia TS2488 -1/+1, by where a union is first asked; vscode/nosft copilotAgent.ts:5860
  TS2339, overloaded method types in the stable order.) excalidraw/nuia's TS2488 -2/+1 is still the array-literal
  identity mechanism (above in triage, tsc's `[0, 0]` its own type object). [fixed oracle 2026-10-23]
- Perf lead, not looked at: 9751c955 (TS2344 on deferred conditional arguments) costs valibot ~0.75%, mostly one
  relation (`TSchema extends ... ? IssueDotPath<TSchema> : string` against keyof any, flatten.ts:41, ~13 ms): tttt's
  relation through IssueDotPath's constraint may be slower than tsc's. [note]

## Moved from hacks.md by the hacks audit (2026-10-08)
- A `declare x;` field without type or initializer in a class without a constructor doesn't take the base class
  property's type (tsc's getWidenedTypeForVariableLikeDeclaration: getTypeOfPropertyInBaseClass under noImplicitAny):
  `class B { x: string = "" } class D extends B { declare x; } const s: number = new D().x;` is TS7008 at `x` and
  `any` in tttt, TS2322 in tsc. class_decl.c's declared_field_base_type(): the base property's type without a
  constructor (static blocks), else the start of the flow of `this.x` through them (getFlowTypeOfProperty's initial
  type); the class comes from the names walk (field.ts_assigned_class_). Test ttttDeclareFieldBaseType [fixed
  34eaa8dc1].
- Generators: no next type inferred from how `yield` results are used (tsc's getReturnTypeFromBody collects
  contextual types of yield expressions); `ty_generator_context` filters only unions. [dup]

## TS 7.1 retarget leftovers (2026-10-09)
- Type member accessors (done 2026-10-15, duplicateIdentifierChecks): each `get`/`set` signature is an InterfaceMember
  of its own (accessor_kind), interface_members_bind() partitions a member list like tsc's declareSymbol (a conflicting
  declaration is a symbol of its own, `conflicting`, not a member; an accessor conflicting with another kind makes the
  symbol a full accessor), builder_add_interface_member() merges the rest (read: getter annotation, else setter's,
  else getter body; written: setter annotation). Left: the partition is per member list, not across the declarations
  of a merged interface (tsgo's same-file declarations share one table: mergeSymbol's rules apply across files);
  declaration emit of an unannotated getter paired with a typed setter prints `any` where tsgo prints the setter's
  type (display_node_interface has no owner type); a setter's body still isn't kept (only a pattern parameter's
  FunctionImpl is, InterfaceAccessor.setter_function). [open]
- TS5114/TS5115/TS2589 limits: tsgo's eager-base runaway is modelled since 2026-10-15 (members.c probe_eager_bases(),
  documentation.md "Eager base runaways"; keyofGenericExtendingClassDoubleLayer passes, mutuallyRecursiveInference
  matches tsgo's CLI and is argued in tsgo_divergences.md). recursiveMappedTypes hits it elsewhere (TS2589 vs TS2502);
  recursiveIndexedAccessSimplification (2026-10-15: the cause is the union member, probe `type Recur<T> = (T
  extends unknown[] ? {} : { [K in keyof T]?: Recur<T[K]> }) | [...Recur<T>[number][]]; function f<T>(r: Recur<T>) {
  const n: number = r; }`: tsgo prints `Recur<T>[number][] | ...`, tttt `any[] | ...`. tsgo resolves a tuple with a
  variadic element eagerly and normalizes `[...A]` to the deferred array A, whose element `Recur<T>[number]` stays a
  deferred indexed access (the alias union is a generic object type); tttt's array element resolves to the error type
  through the TS4109 cycle, so `[string, ...Recur<T>[]]` is assignable to `any[]` and the TS2322 is never made. Check
  how lower.c's deferred array/tuple references lower `Recur<T>[number]` inside the alias's own body) has its TS4109
  since aa320754, but lacks the TS2322 at `return ['marker',
  ...l]` and the two TS2589 (8,12)/(12,37): the instantiations of `Recur<T>` in join/a run into the limit in tsgo
  (each a fresh deferred reference with its own args), tttt's relation at the return is never even made (no
  TTTT_DEBUG_RELATE line: probably a cut-short instantiation remembered from the alias check, not reported again,
  check_assignable()'s instantiation_limit_hits rule). xstate under sloppy/nosnc (flagflip) gets one spurious
  TS5115 at graph/utils.ts:91 next to tsgo's at State.ts:395 (another StateId instantiation tsgo finds cached; tsgo's
  sites move with its checker count: -p with 4 checkers reports State.ts, setup.ts, stateUtils.ts); the eager-base
  runaway's guard is an approximation (RESOLVING_MAPPED_CONSTRAINT stands for tsgo's mapped type key constraint
  pushTypeResolution): a spurious or missing TS5115 on real code is first checked against it (log the
  instantiation and members-in-flight stacks in members_reentered_unguarded(); an instrumented tsgo printing
  runtime.Callers' frames at a failed pushTypeResolution shows tsgo's guard). [open]
- Variances still measured differently from tsgo on xstate (25 of 160 type parameters, 2026-10-12; compare with
  tsgo's `--generateTrace` getVariancesWorker events, script idea: dump both, key by symbol name): mostly the
  unreliable/unmeasurable flags (tsgo `out (unreliable)` where tttt has plain or invariant) and some independent vs
  measured. The restart/Unknown/marker mechanics are ported (relate.c measured_variances()). [dup]
- crossProductUnionIntersectionRepeatedPropDeclarationsNoOOM1 (types, 2026-10-14): the `w`/`props?.w` lines now
  match tsgo's 1M-length cut (sticky truncation and the serializedTypes reuse); the `newShapePartial.props` lines
  still differ: tsgo reaches the length limit (its `Partial<any> | ...` elisions) around 307k characters, tttt doesn't.
  An instrumented tsgo (approximateLength logged at each typeToTypeNode: a nodebuilderimpl.go overlay on the probe
  test binary, `go test -overlay ... -c ./internal/testrunner`) showed the counts agree until ~11k, then tsgo reuses a
  cached `{ w: number; h: number; }` object that tttt prints afresh: a type identity difference (tsgo's object is the
  same type as one printed earlier in the line, tttt's another instance), inside `Partial<A & B & ...>` arguments.
  Find which instance (spread/intersection property types) and why tttt makes a new one; about a day of work.
  2026-10-15 (agent D, instrumented tsgo probe binary logging approximateLength at every typeToTypeNode, against the
  same log from ty_display()): the first divergence is earlier, at length ~11k (text offset ~6.8k of the line),
  `... & N["props"] & Dimensions> | undefined) & Dimensions)) | (Partial<...`: tsgo visits `undefined` and then
  goes straight to the next union, never visiting the trailing `& Dimensions` as a type (no typeToTypeNode call: it
  is most likely a reused existing node, the `& Dimensions` of `props: TLShapePartial<...>["props"] & Dimensions` in
  ShapePartialWithDimensions, counted by its source range), while tttt prints Dimensions again (+~26 there). Further
  on the alias type arguments are walked differently (tsgo: a union with an origin of intersections, tttt: an
  intersection), so the counts drift. Next step: why tttt's node reuse (reused_node_length(), tryReuseExistingTypeNode)
  doesn't take that intersection's `Dimensions`. Logging recipe: overlay nodebuilderimpl.go (fmt.Fprintf at
  typeToTypeNode / visitAndTransformType), `go test -overlay ... -c ./internal/testrunner`, run it like
  scripts/tsgoprobe.sh with TTTT_TESTDATA; split the logs into prints where the length drops. [fixed oracle 2026-10-23]
- Inference: `$ExpectType` map-to-obj `MapToObj(new Map([["hello", "world"]]))`: tsgo infers `Map<string, string>`
  inside the call (the outer unfixed K is silentNever through the NoDefault mapper, so the tuple's "hello" has no
  literal context), tttt keeps `"hello"` (also `f<K extends string>(m: Map<K, string>)`); mut0348
  (objectRestBindingContextualInference, `K extends Object`): with `const { prepare, ...rest } =` tsgo infers
  K = "image" from the binding pattern + argument, tttt widens to string; mut2626 (noInferCommonPropertyCheck1,
  `test2(null, ...)`): tsgo's `NoInfer<T1> & NoInfer<T2>` with T1 = null is `never`, tttt keeps `null &
  NoInfer<Partial<...>>`. [open]
- Inference from an intersection to one generic reference (`SyncBailHook<[OA], PA> & SyncBailHook<[OB], PB>` to
  `SyncBailHook<T, R>`, tapable): tsgo infers T = `OB | [OB]` (through the members' `AsArray<T>` conditional rest
  parameters), tttt `[OB]`; both then fail the same assignment (webpack's HookMap TS2322, fixed 2026-10-12 by the
  any-key index read), only the message's types differ. [text]
- types/redux-define (DT sweep): TS2344 "only assignable to the non-distributed 'SubAction'" (distributed type
  parameters, the distributed type parameters' area). [fixed oracle 2026-10-23]
- flagflip xstate/eopt: one spurious TS2322 (input.test.ts:272, a spawn signature); vue/nuia 2 spurious + 2 other-code. [fixed oracle 2026-10-23]

## The conditional type-variable walk resolves signatures tsc never looks at
`definitely_unrelated()` / `definitely_related()` in `src/ty/conditional.c` collect the type variables a conditional's
check type mentions with `mentions_type_variables()`, which resolves every signature of a function type: return types
still to be inferred from bodies, type parameter constraints. tsc never looks there: `getPermissiveInstantiation`
instantiates lazily, and the relation asks only for what it compares. Over a class whose method needs the mapped type
being resolved that is a cycle (TS2615/TS2502), and whether it happens depends on what a checker resolved first.

Patched (2026-10-13) for the two "is it a function" idioms only: `callable_definitely_related()`
takes the true branch of `X extends (...args: any[]) => any` and `X extends Function` for a callable X before any walk
(dexie's NonInsertProps, mikro-orm's CleanKeys; test ttttConditionalCallableAgainstAnyFunction). Any other extends
type over such a class still walks.

Tried before: returning "unsure" from the walk when a function's return type isn't inferred yet (so the plain relation
decides). Fixed dexie, but regressed excalidraw (TS7006/TS2493/TS2345 in vitest `mockImplementationOnce` callbacks) and
vscode (1): "unsure" also makes `definitely_related` relate without the restrictive instantiation and report
`mentions`, which changes other conditionals' answers. A real fix makes the walk lazy like tsc's permissive
instantiation (instantiate the check type with the wildcard mapper without walking, relate, and only resolve what the
relation asks for), which is plan-sized: definitely_related's restrictive instantiation needs the same. [design]

## Deep nesting cut-offs depend on type ids (tsgo's too)
isDeeplyNestedType counts a recursion identity only at increasing type ids, so whether a relation is cut off as Maybe
(or explores further and finds a real failure) depends on which types earlier files created first. tsgo has the same
dependence; tttt matches it only where the types involved are the same ones (same identities, so the same maybe-stack
hits and stack contents). mermaid's spurious create-types-from-json-schema.mts(129,23) TS2345 at K=1 (fixed
2026-10-14) was this shape: `JSONSchemaType<MermaidConfig>` through an import must be a union by that alias, not
`UncheckedJSONSchemaType<MermaidConfig, false>` (hacks.md `through_import`), else the inner
`UncheckedJSONSchemaType<MermaidConfig, false>` hit the maybe stack at once, the comparison took another path, and
since `.build/jsonSchema.ts` had created `UncheckedJSONSchemaType<any, false>`'s members early the cut-off tsgo gets
never came. To debug one: `TTTT_DEBUG_RELATE_NESTED=1` marks results "(deeply nested)" and prints "on the maybe
stack" hits; compare with tsgo's recursiveTypeRelatedTo instrumented the same way (an overlay build like
`scripts/variancetrace.py`, printing at the end of each top-level relation so typeToString can't shift ids inside it).

## Without a global Array (noLib or a missing lib; triage 2026-10-14)
- A tuple's rest element is stored as its array type, which is `{}` then: `[number, ...string[]]` prints
  `[number, ...{}]` (tsgo: `...string[]`, tsc stores the rest element's element type, getTypeFromRestTypeNode), its
  slices `[...{}]` (tsgo `{}`), `k(...t)` infers `[number, ...{}]` (tsgo `[number, ...any[]]`). Only noLib tests can see
  it (tsgo's CLI stops at TS2318). The crash itself is fixed (ttttNoLibTupleSpread). [open]
- type-fest checked without lib (`--keep-going`) overflows the 64 MB stack in conditional tail recursion
  (test-d/union-max.ts: `InternalUnionMax`, each of its ~1000 tail steps lowers type arguments inside the recursion):
  hacks.md `raise_stack_limit` for `ty_instantiate_tail`. [note]

## A nested call's return inference through a union contextual signature misses one branch (supabase msw, 2026-10-19)
Found reducing supabase studio's CreateCustomOidcDialog.test.tsx (whose real, context-sensitive form is fixed: commits
"An object indexed by never reads the plain never" and "The operands of || and ??..."). With the arrow's parameter
*annotated* (`({ request }: any) => ...`, so it is checked in the first inference pass, the outer P and M uninferred),
tsgo types `HttpResponse.json({...})` as `HttpResponse<string | number | boolean | APIErrorBody | Record<string, any> |
null | undefined>` (its contextual signature is the union of both branches of the deferred conditional parameter type,
the second instantiated with silent never), tttt as the same without `APIErrorBody`: the second branch's return
contributes nothing. Both report a TS2322 there, so it only shows in types/message text. Smaller shapes (Paths[P] or
an `infer` conditional instead of the RemapPaths/SuccessResponse chain below) don't reproduce. Repro (`scripts/filetest.sh`): [text]
```ts
// @strict: true
// @target: es2022
// @lib: es2022,dom
type MaybePromise<T> = T | Promise<T>;
type DefaultBodyType = Record<string, any> | string | number | boolean | null | undefined;
type JsonBodyType = Record<string, any> | string | number | boolean | null | undefined;
type PathParams = Record<string, string | readonly string[] | undefined>;
declare const bodyType: unique symbol;
type DefaultUnsafeFetchResponse = Response & { [bodyType]?: never };
interface StrictRequest<B extends JsonBodyType> extends Request { json(): Promise<B>; }
declare class HttpResponse<BodyType extends DefaultBodyType> extends Response {
  readonly [bodyType]: BodyType;
  static json<BodyType extends JsonBodyType>(body?: NoInfer<BodyType> | null | undefined): HttpResponse<BodyType>;
}
type ResponseResolverReturnType<R extends DefaultBodyType = undefined> = ([R] extends [undefined] ? Response : R extends Record<string, any> | undefined ? HttpResponse<R> | DefaultUnsafeFetchResponse : HttpResponse<R>) | undefined | void;
type MaybeAsyncRet<R extends DefaultBodyType> = MaybePromise<ResponseResolverReturnType<R>>;
type AsyncRet<R extends DefaultBodyType> = MaybePromise<ResponseResolverReturnType<R> | Iterable<MaybeAsyncRet<R>, MaybeAsyncRet<R>, MaybeAsyncRet<R>> | AsyncIterable<MaybeAsyncRet<R>, MaybeAsyncRet<R>, MaybeAsyncRet<R>>>;
type HttpResponseResolver<Params = PathParams, Req extends DefaultBodyType = DefaultBodyType, R extends DefaultBodyType = DefaultBodyType> = (info: { request: StrictRequest<Req>; params: Params }) => HttpResponse<R> | undefined;
type TPA = { custom_jwks?: unknown; id: string; inserted_at: string; jwks_url?: string | null; oidc_issuer_url?: string | null; type: string; updated_at: string }
interface paths {
  '/v1/projects/{ref}/config/auth/third-party-auth': {
    get: { responses: { 200: { content: { 'application/json': TPA[] } } } }
    post: { responses: { 201: { content: { 'application/json': TPA } }; 401: { content?: never } } }
    put?: never; delete?: never; patch?: never; head?: never; options?: never
  }
  '/v1/projects/{ref}/other': {
    get: { responses: { 200: { content: { 'application/json': string } } } }
    post?: never; put?: never; delete?: never; patch?: never; head?: never; options?: never
  }
}

type RemapParams<T extends string> = T extends `${infer Before}{${infer Param}}${infer After}`
  ? `${Before}:${Param}${RemapParams<After>}`
  : T
type TrimQueryParams<T extends string> = T extends `${infer Endpoint}?${string}` ? Endpoint : T
type RemapPaths = { [K in keyof paths as RemapParams<K>]: paths[K] }
type Endpoints = keyof RemapPaths
type Methods = 'get' | 'post' | 'put' | 'delete' | 'patch' | 'head' | 'options'
type SuccessResponse<P extends Endpoints, M extends Methods> = RemapPaths[P][M] extends {
  responses: { 200: { content: { 'application/json': infer R200 } } }
}
  ? R200
  : RemapPaths[P][M] extends {
        responses: { 201: { content: { 'application/json': infer R201 } } }
      }
    ? R201
    : never
export type APIErrorBody = { message: string }
type TypedResolver<P extends Endpoints, M extends Methods> = HttpResponseResolver<
  PathParams,
  DefaultBodyType,
  SuccessResponse<P, M> | APIErrorBody
>
export declare const addAPIMock: <P extends Endpoints, M extends Methods>(a: SuccessResponse<P, M> extends never
  ? { method: M; path: P; response?: HttpResponseResolver }
  : {
      method: M
      path: P
      response: SuccessResponse<P, M> | TypedResolver<P, M>
    }) => void

export const mockCreateIntegration = (requests: unknown[], response?: () => Response) => {
  addAPIMock({
    method: 'post',
    path: '/v1/projects/:ref/config/auth/third-party-auth',
    response:
      response ??
      (({ request }: any) => {
        return HttpResponse.json({
          id: '00000000-0000-0000-0000-000000000000',
          type: 'oidc',
          oidc_issuer_url: 'https://login.example.com',
          inserted_at: '2026-01-01T00:00:00Z',
          updated_at: '2026-01-01T00:00:00Z',
        })
      }),
  })
}
```

## New suite tests of the 7.1.0-beta (2026-10-23) that tttt fails: pre-existing divergences
The beta's #64850 added tests reduced from top-1000 repo failures; the nightly tsgo already gave their expected
output (checked with the old CLI), so these are tttt bugs the re-pin exposed, not beta changes:
- contextualReturnSpreadPickInference: `return { title: "", ...pick(page, ["intro"]) }` against `Promise<{ title;
  intro }>` with `pick<T extends object, Key extends keyof T>(object: T, ...props: Many<Key>[]): Pick<T, Key>` and
  `page: any`: tsgo infers Key `"intro"` (the spread's contextual type), tttt `string` -> spurious TS2741. [open]
- iteratorZip (both exactOptionalPropertyTypes configs; the new esnext.iterator lib): the erroring
  `Iterator.zip(..., { mode: "shortest", padding: [1] })` prints the literal `{ ...; padding: [number] }` where tsgo
  has `number[]` (the overload whose argument types are printed after resolution fails), and f2's
  `IteratorZipKeyedResult` instantiation prints differently (types and dts). [open]

- **A rest parameter's element at a position is `R[index]`, not R's element type** (bun 77-R19a: `interface Array<T> {
  0: string }` then `f(...a: number[])`): tsgo's getTypeAtPosition/tryGetTypeAtPosition takes
  `getIndexedAccessType(restType, numberLiteral(pos - paramCount))` for an array rest, so an augmented numeric
  property wins at its position: `f("s")` is fine, `f(1)` is TS2345 (number to string), and `(...a: number[]) => void`
  isn't assignable to `(a: number) => void`. tttt's ParamShape has one `rest_element` for every position (call.c), and
  relate.c's type_at_position() likewise. Only reachable by augmenting Array (or a rest type with numeric-named
  properties); a fix would expand such properties into positional entries of the shape. [open]
