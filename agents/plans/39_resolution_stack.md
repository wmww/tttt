# Plan 39: one resolution stack for circularity
Stub by the 2026-09-29 triage; planned and **stage 1 landed** by the planning session of 2026-09-29 (the prototype
was small, 0 lost and corpora-neutral, so it went in instead of a paper plan). Stage 2 is todo entry 4.

## Decision
Worth it, and cheap. tsc detects circularity with one stack (`pushTypeResolution`/`popTypeResolution`,
`resolutionTargets`, `resolutionStart`); tttt had one in-flight state per memo that only flagged the one re-entered.
The stack sits *beside* the memos: each keeps its layout and its in-flight state (which still decides what a re-entry
returns), and additionally pushes `(kind, key)` on `Checker.resolving`. A re-entry calls `ty_resolving_cycle()`, which
marks the entry it finds and everything above it; each marked entry finds out at its pop and becomes circular.
Cost: +0.06..0.22% instructions (`./instrcount.sh`, push/pop are inline); per-checker, so `--checkers 1` vs 4 and the
thread count are unaffected (`./threadcheck.sh`, `./checkerdiff.py` identical).

## Stage 1 (done, 2026-09-29): suite 12047 -> 12052, 0 lost, every corpus unchanged (zod-root byte-identical)
- `Checker.resolving` / `ty_resolving_push/pop/cycle` (ty.h, checker.c). Pushers: declaration LazyTypes
  (`LAZY_TYPE_INITIALIZED_BINDING`, `FOR_EACH_LOOP_VAR`, `CLASS_ITEM_FIELD`, `CLASS_ITEM_ACCESSOR`; not `EXPR`, not
  `FUNCTION`), `function_return_type()` (inferred return types), `prop_type()` for declared and mapped props (not
  instantiated ones: tsc's `getTypeOfInstantiatedSymbol` doesn't push, it asks its target, so a re-entered
  instantiated prop marks from its first declared/mapped target), and type aliases (`aliases_in_flight` is gone).
- A declaration LazyType re-entered while in flight now returns `TY_CIRCULAR` at once (tsc), instead of the old
  "evaluate again, bail on the third hit" scheme; that alone changed nothing in the suite. Expressions keep `hits`.
  A declaration whose entry was marked becomes `TY_CIRCULAR` whatever it evaluated to, so its existing TS7022/TS2502
  check fires (for-of34/35: `v` whose iterator's `next()` returns `v`; circularGetAccessor).
- Barriers (`resolving_start`, tsc's `resolutionStart`): `resolve_call()` and `has_conflicting_discriminants()`.
  - tsc only sets it for a call's *first* resolution (nested ones get `resolvingSignature`). tttt resolves a call again
    inside itself (plan 38 stage 3), and doing tsc's rule made zod's recursive getter schemas TS7023 (`get children()
    { return z.array(Node) }`). So every resolution sets it (hacks.md `resolve_call`). Cost: `res1` in
    circularReferenceInReturnType (tsc's TS7022 at `const res1 = fn1(() => res1)`).
  - The eager intersection reduction (hacks.md `has_conflicting_discriminants`) asks prop types tsc asks lazily; a
    cycle found there marked arktype's mapped `sequence` prop (4 TS2615 in zod-root).

## Stage 2 (todo entry 4)
Each item is a push point or a report tsc has and tttt doesn't. Before guessing at tsc's nesting, log it: patch a copy
of `_tsc.js`'s `pushTypeResolution` to print the stack when it finds a cycle (the 2026-09-29 session did this in
`/tmp/tsclog`, 3 lines, see memory note "instrument a tsc copy"); tttt's side is `ty_resolving_cycle()` (a
`log_warn` of the marked entries, with prop names via `ty_prop_name_cstr`, was the whole debugging tool).
1. **Annotated declarations don't push**: `var d: Array<typeof d>` (implicitAnyFromCircularInference),
   `var f: Array<typeof f>` & co (recursiveTypesWithTypeof: TS2502 instead of TS2403, the declared type becomes any),
   `var v: Alias` where `type Alias = typeof v` (circularTypeofWithVarOrFunc). tsc pushes `(symbol, Type)` for every
   variable/parameter/property, annotated or not; tttt lowers an annotation through `ty_binding_type()` ->
   `ty_from_node()` without the binding's LazyType. Find where `ty_type_of_symbol()` reaches an annotated binding
   (`value_entry_type()`) and push there (or route annotated bindings through the LazyType); the existing
   `binding.c` TS2502 check then fires. Watch `check_subsequent_declaration()` (TS2403 must see the any).
2. **Signature return types don't push**: TS2577 "Return type annotation circularly references itself"
   (circularTypeofWithVarOrFunc, 3 of them) needs `signature_return_type()` (members.c) to push for an annotated
   return too, and report 2577 at the annotation on a failed pop. thisConditionalOnMethodReturnOfGenericInstance
   (spurious TS2417: tsc's `method()` return requested while it resolves is errorType) is the same push point; an
   instantiated signature should ask its target like instantiated props do in stage 1.
3. **Which function of a nested cycle reports**: implicitAnyFromCircularInference now reports TS7023 at `foo` in
   `function h() { return foo(); function foo() { return h() || "hello"; } }` (tsc: only `h`); circularReferenceInReturnType2
   reports TS7022 at `A` and TS7023 at `fields` where tsc has one TS7024 at the arrow. Both are about the call
   barrier: tsc re-resolves under the barrier and the cycle is found at a nested copy. Log tsc's stack first.
4. **Relation reads a return type tsc skips**: circularReferenceInReturnType `res3` (spurious TS7022 + TS7024): tsc's
   `compareSignaturesRelated` doesn't look at the source's return type when the target's is `any` or `void`. Not a
   stack issue; fix in relate.c if it's that simple.
5. **Accessors / object literal methods**: TS7023 tsc reports while *printing* a type whose return type is in flight
   (thisInObjectLiterals, accessorInferredReturnTypeErrorInReturnStatement, trivialSubtypeReductionNoStructuralCheck,
   checkingObjectWithThisInNamePositionNoCrash, conflictingDeclarationsImportFromNamespace1/2) and tttt reports too
   early/elsewhere (thisInObjectLiterals has a spurious one at `f`). Probably out of scope; take only what falls out.
6. **Base types and constraints (TS2310, TS2313)**: circularBaseTypes, recursiveMappedTypes,
   incorrectRecursiveMappedTypeConstraint: base type resolution (`members.c` `base_state`) and type parameter
   constraints (`constraint_state`) are in-flight states that don't push. tsc pushes `ResolvedBaseTypes`,
   `ImmediateBaseConstraint`. Add them as kinds and see what moves; stop at 0 lost.
7. dependentDestructuredVariables: TS7022 on every `testN` of `function foo({ value1, test1 = value1.test1, ... })`
   (tsc: once, TS7031 at `value1`); the defaults go through `defaults_in_flight`, not the stack. Then the file's 20
   TS2339 are narrowing destructured variables by each other, a separate feature: don't chase them here.
8. Cleanup once the above are in: `aliases`-style special cases that the stack subsumes (`defaults_in_flight`,
   `jsdoc_values_in_flight`, `ty_node_reaches_alias_in_flight`/`members_judgeable` in the eager intersection
   reduction) — only where replacing them is 0 lost.

## Validation (every stage)
The tests above; `./fasttest.sh --summary` 0 lost; `./realworld.py` on every corpus unchanged (tsc, type-fest,
valibot, react, nest, zod, vue 0; webpack 6; xstate 19; drizzle 8) **and zod-root byte-identical to the previous build**
(`tttt -q --keep-going -p thirdparty/corpora/zod | sort`, diffed: that is where stage 1's regression showed and the
pinned corpora didn't); `./threadcheck.sh`, `./checkerdiff.py`; `./instrcount.sh` within ~0.3%.
