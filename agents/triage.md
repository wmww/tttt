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
