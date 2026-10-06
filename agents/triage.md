# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-05 (triage session, todo entry 10 of the ninth window; suite 13097 -> 13099, 0 lost, every
corpus unchanged, scenarios 214 / 0 failing). Four Opus sub-agents: (A) tsc's deferred checks ported (class
expression members, JSX elements, `void` operands; union order, react -5% from variance measurement order, see
perf_leads.md), TS2448 in class expression initializers, plan 35 item 3's creation-order fixes. (B) has_inferable_index
as tsc's isObjectTypeWithInferableIndex, a union's contextual signature keeps its first member's `this`, deep nesting
unwraps mapped types (deeplyNestedMappedTypes). (C) Windows drive paths in the harness, iterative harness binary walk,
symbol_chain cache leak, the react +0.8% creep checked (-0.24% back), and two copies of webpack's types.d.ts (symlinked path) from ~60 s
to 1.5 s by caching failures after a depth overflow and reporting tsc's TS2321. (D) hacks.md and bugs/*.md checked
against the code and baseline_results.ts (816 -> 769 and 750 -> 587 lines), dead code and stale comments. Plan 35's
queues merged into one.


- `ty_object_literal_of()`'s intern key leaves out `asserted`/`asserted_required`: two literals differing only in the
  annotation the types baseline reuses share one type, whichever came first. spreadUnion2's `{ ...u, ...u }` printed
  `a?: number | undefined` only because the merged literal collided with `{ ...u }`'s (spread_into() now marks merged
  props PROP_SYNTHETIC_COPY, which keeps the collision); merged.asserted reusing the right side's non-optional `a: number`
  for an optional property looks wrong on its own.
- Did-you-mean suggestions skip `#private` candidates always; tsc keeps them when the access is inside the declaring
  class (isValidPropertyAccessForCompletions). Untested which name tsc then suggests.
- (plan 46 extends-object, agent c) mut1499 (doubleMixinConditionalTypeBaseClassWorks with the FooConstructor alias
  deleted): tttt adds TS2510 at `class C extends Mixin2(Mixin1(Object))`; tsc's types there show the class expression
  whose base got TS2507 as `typeof (Anonymous class) & C` (a standalone probe gives plain `typeof`): some tsc
  resolution-order effect worth understanding before matching it.
- (agent c) Types: `class L<RT> extends Object<RT[...]>` (TS2508) then `class X extends L<X>` (TS2507): tsc prints the
  extends expression `>L : L<X>`, tttt `typeof L` (mut7384's types component).
- `o[sym]` on `{ [k: string]: C }` with an access node: tsc says TS2538 "cannot be used as an index type" and reads the
  string index signature (type C); tttt says TS7053 and any (`check_access`; union or not). Probe: agent e, plan 46 cause 2.
- valibot/es5 flag-flip cell: spurious TS7053 at `schemas/variant/variantAsync.ts:153/189` (`any` index into
  `VariantObjectEntries<string> | VariantObjectEntriesAsync<...>`), only at target es5. Uninvestigated.
- TS2344 elaboration for `NonNullable<V[K]>` without strictNullChecks: tsc adds "Type 'NonNullable<V[K]>' is not
  assignable to type 'SVM'. / Index signature for type 'string' is missing in type '{}'." (the structural step on the
  intersection's apparent type, tsc's emptyObjectType); tttt prints the header only.
- (agent d) `function g<T, K extends keyof T>(t: T, k: K) { t[k] }`: tttt types `t[k]` as `T[string] | T[number] |
  T[symbol]` (tsc `T[K]`), so `t[k].foo` misses TS2339 on 'T[K]'. Probe: /tmp/d-probes/idx.ts shape.
- (agent d) intersection()'s dedupe_unions() merges unions with the same members but different alias/origin; tsc keeps
  them apart (origin is part of the union's identity) and then intersects them as sets, losing the alias.
- A class field and a property signature of an interface merged with the class are one property symbol in tsc
  (symbols baseline `Symbol(C.x, Decl(class), Decl(interface))`, the type the first declaration's); tttt keeps one per
  declaration (probe: `interface C { x: number } class C { x: string }`). TS2717 is reported since 2026-10-06.
- `typeof x` of an auto-typed `let x = undefined` inside a signature without a body (interface member, function type)
  is TS7005 + TS7034 in tsc (the signature is its own flow container), tttt reads the outer flow (seed-3 mut3837;
  setting outer_reference for bodiless signatures in identifier name resolution wasn't enough).
- Nested destructuring defaults: `var [a, [b, { x, y: c }] = ["abc", { y: false }]] = [1, ["hello", { x: 5, y: true }]]`
  reports TS2339 'x' on `{ y: boolean }` in tttt, nothing in tsc (the nested literal default isn't given the pattern's
  implied properties, with_pattern_props() only applies at the top of a default).
- (agent a, plan 46 mut0806) `class D extends class extends Base1 { root() { return undefined } }` with
  `abstract root(): D` under strictNullChecks: tsc's checkClassExpression runs checkClassLikeDeclaration (member vs base
  relation) inside D's base resolution, which needs D's members: TS2310/TS2506. tttt checks the class expression later.
- (agent a, mut3125) AllowMissing for destructuring defaults (`binding.c`/`expr.c` `anonymous.object_literal` checks)
  should go by tsc's isObjectLiteralType of the *unwidened* parent: `var a = [{y: 0}]; for (var {x = ""} of a)` is TS2339
  in tsc. Switching to ty_is_unwidened_object_literal breaks `= { a: {} }` and `of [{}]` because tttt's binding parent
  types (ty_binding_type) and array literal element types are already widened. Needs unwidened parent types.
- (agent a, mut6649) tsc reports `x.slice()` in a loop twice (TS2339 on 'number' from an incomplete loop pass, then on the
  final union); tttt once. Evaluation-order artifact of tsc's getTypeAtFlowLoopLabel.
- (agent a) Object literal members' TS1255 `!` and modifier grammar errors are still the parser's, so they're reported
  inside destructuring assignment targets where tsc's checkGrammarObjectLiteralExpression never runs (TS1162 moved).
- (agent a) `async function* f(): AsyncGenerator<...>` at target es2017 (no AsyncGenerator in lib): tsc also reports the
  global TS2318 "Cannot find global type 'AsyncIterableIterator'"; tttt doesn't.
- (agent c, flag-flip cause 6/8 leftovers) tsc resolves JSDoc type literal members lazily: names inside `@returns
  {{ new (): Set<T> }}` are only resolved (and reported, TS2304/TS2583) when the members are needed; tttt reports
  them always (probe: a JS function whose return is never related). Same laziness decides TS6133 on an unused outer
  `@template T`: tsc's resolveEntityNameFromAssignmentDeclaration also resolves every JSDoc type reference from the
  host function (getAssignmentDeclarationLocation, isUse) and marks what it finds, so an inner `<T>` reference marks
  the outer T once resolved (webpack deprecation.js:204 +TS6133 under `unused`).
- (agent c) Missing elaboration line: a class static side against a generic construct signature type: tsc says
  "Types of construct signatures are incompatible." / "Type 'new (v: boolean) => C' is not assignable to type 'new
  <T = number>(v: T) => ...'" before "Types of parameters"; tttt starts at the parameters.
- (agent c) vscode eopt extHostTypeConverters.ts:647/662 spurious TS2375 (tsc reports only the metadata TS2412 there):
  the target is `Dto<...>` (a conditional over a homomorphic mapped type over intersections); tsc's indexed access /
  missing-type handling there makes the optional props accept undefined. Needs a reduced repro.
- (agent c) vscode eopt/nosnc extHostTestItem.ts:191/192 missing TS2352: comparability of TestItem vs TestItemImpl
  (ITestItemLike has `uri?: URI`, TestItem `uri: Uri | undefined`); under nosnc too, so not only eopt.
- (agent c) xstate eopt spawn.ts:132 TS2345->TS2379: the argument is a parameter contextually typed by a generic rest
  `...[options]: ConditionalRequired<[options?: X], ...>`: tsc's parameter type is the conditional's element
  (`ConditionalRequired<...>[0]`-like), tttt's `X | undefined`, which then counts as an exact-optional mismatch.
- (agent c) drizzle sloppy/nosnc mysql-proxy/migrator.ts:25: tsc's `....orderBy(...)` result is undefined-ish (TS2532 +
  TS2722 at the chain), tttt's `.limit` is never (TS2349): non-strict drizzle builder types diverge.
- Union best match (seed-3 mut3155): an object argument against `string | Intl.Locale | readonly string[]` gets no
  elaboration in tsc; tttt's findBestTypeForObjectLiteral picks `string` and adds "Type '{...}' is not assignable to
  type 'string'". Probe /tmp/b-probes/bm.ts shape (`const o = {...} as const; f(o)`); tsc's reason not found yet.
- Return type inference from a contextual `never` (seed-3 mut5211): `const b: never = cm({ a: 12 }, { foo() {...} })`
  with `cm<S, A>(s: S, a: A): { s: S } & A` is TS2345 on the second argument in tsc (A fixed to never via the return
  mapper), TS2322 on the declaration in tttt.
- Spread copies print their optional property's annotation in errors (`{ sn?: number; }` for `{ ...o }`, seed-3
  mut7379) while the declared type literal prints `number | undefined`; tttt adds `| undefined` to both.
- (agent a, seed 5 mut5252) `a2.x` with `a2: ReturnType<T>` (`T extends () => unknown`) misses TS2339: tsc's default
  constraint of a conditional leaves the `any` false branch out (true branch `R`, unconstrained, so apparently unknown);
  making base_constraint_uncached's TY_CONDITIONAL do that wasn't enough, the property access path skips it elsewhere.
- (agent a, seed 5) Unlooked-at seed 5 mismatches: missing TS2411 (mut4694/mut6347, optional property vs index
  signature), missing TS2461/TS2722 with `let foo = undefined` evolving in a loop (mut6952/mut7343), spurious TS2322
  with `const x: never =` (mut5018), missing TS2540, TS2741 (objectRestAssignment), TS2687, TS2403, TS2344, TS2353,
  spurious TS2538/TS7015/TS2411/TS2339/TS2664; text/elaboration/render ones (~15). `MUTATE_DIR=... ./mutate.py -n 8000
  --seed 5` reproduces them.
- (agent d, seed 4) Error text of a fresh arrow function: a parameter annotated with a union holding a reducible
  intersection (`Container<"a"> | (Container<"b"> & ... & Container<"a">)`) prints reduced (`(t: Container<"a">) => ...`);
  tsc prints the annotation (mut0934, mut1233). Only the error message (no underline/enclosing) loses it; a declared
  function type prints fine.
- (agent d, seed 4) `const pairs: never = [[{}, 1], [{}, 2]] as const`: tsc's message names `[[{}, 1], [{}, 2]]`, tttt the
  readonly tuples (mut1482). `const SYM: never = Symbol()` then `{ [SYM]: "sym" }`: tsc prints `{ [SYM]: "sym"; }`, tttt
  `{ [x: number]: "sym"; }` (mut6736). Union member order: mut2452 (`PromiseLike<..> | 42`), mut3152.
- (agent d, seed 4) Parameter properties in an overload and the implementation (`constructor(public names: number);
  constructor(public names: string, ...)`): tsc's node.symbol is the class property, so TS2403 compares them (mut1636).
- (agent d, seed 4) Same-position diagnostics TS2552 and TS2538 at `[nameX]` in a destructuring pattern: tsc lists 2552
  first despite compareDiagnostics' code order (mut6126); with `/// <reference no-default-lib="true"/>` and a few empty
  global interfaces tsc reports nothing at all, tttt TS2304 (mut1536).

- (agent C, 2026-10-06, the errors-component tail /tmp/merge/C2.txt) What was left after the fixes, by mechanism:
  - Text only (codes and positions match; errtextdiff territory): 29 tests, e.g. genericRestParameters3 (tuple
    sources print as arrays in elaborations), mappedTypeRelationships (constraint elaboration chain),
    destructuringUnspreadableIntoRest (Omit key order), conditionalTypes1 (`T2` vs `Foo<T & U>` alias choice),
    interfaceExtendsObjectIntersectionErrors (a tuple alias prints as the alias; tsc's tuple references carry none),
    tsxUnionElementType3/4 (related info prints `{ children?: ReactNode }` once), mappedTypeNotMistakenlyHomomorphic
    (TS2728 related info missing), objectLiteralExcessProperties (the error's source keeps `prop: true` where the
    printed initializer type widened it: two types for one expression).
  - Circularity / depth (plan 35 item 12 and plan 39): circularBaseTypes (TS2313), circularConstraintYieldsAppropriateError
    (TS2310), circularOptionalityRemoval (TS2502), recursiveMappedTypes, recursivelyExpandingUnionNoStackoverflow,
    limitDeepInstantiations, recursiveConditionalCrash4, circularIndexedAccessErrors, circularInlineMappedGenericTupleTypeNoCrash,
    infiniteConstraints (spurious TS2321), functionImplementations (a return of a recursive call), the "NoCrash" ones.
  - Limits not implemented: TS2563 (largeControlFlowGraph), TS2590 (normalizedIntersectionTooComplex,
    templateLiteralTypes1), TS2859 (relationComplexityError), TS2799's position (excessivelyLargeTupleSpread).
  - Mixins: class members inherited from an intersection base are the first member's property, not tsc's synthetic
    one (mixinAccessModifiers: spurious TS2415 for `class C4 extends Mix(Protected, Protected2)`, c4.p/C4.s messages).
  - Inference: contravariantOnlyInferenceFromAnnotatedFunction (A = string from an annotated `fn` inside a reverse
    mapped source), inferFromGenericFunctionReturnTypes3 (Enum.A vs Enum: literal widening of class instantiations
    across return inference), genericCallWithGenericSignatureArguments3 (messages `=> 1` vs `=> number`).
  - thisTypeInFunctionsNegative: spurious TS2684 for `super.polymorphic()` with `this: this` (the this-argument of a
    super access); exportEqualsDefaultProperty: a default import of an `export =` object with a "default" property
    (needs the export= value's properties at bind time, TS1259 spurious); mergedDeclarations7 (named import of an
    `export =` namespace's interface vs value property).
  - JS (plan 26), declaration emit (plan 40: TS9005/TS9006/TS6232/TS5088/TS7056), lib resolution
    (libTypeScriptOverride*/SubfileResolving*: `@typescript/lib-*` packages), noLib tests, and parser recovery
    (parser0_004152, parserSkippedTokens20, jsxInvalidEsprimaTestSuite, conflictMarkerTrivia3, reservedNamesInAliases,
    varianceAnnotationsWithCircularlyReferencesError, ...): not looked into.
