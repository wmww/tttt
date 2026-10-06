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
