# Plan 39: one resolution stack for circularity
Stub by the 2026-09-29 triage; planned and **stage 1 landed** by the planning session of 2026-09-29 (the prototype
was small, 0 lost and corpora-neutral, so it went in instead of a paper plan). **Stage 2 landed** the same day (todo
entry 4). What is left is listed under "Stage 3": mostly blocked on plan 38 stage 3 or on tttt's eager member
resolution, not on the stack.

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

## Stage 2 (done, 2026-09-29): suite 12052 -> 12057, 0 lost, corpora unchanged, zod-root identical, +0.0..0.4% instr
- Annotated bindings and the names in patterns push `(RESOLVING_BINDING, binding)` in `ty_binding_type()` (no memo of
  their own: the stack is scanned above the barrier, `ty_resolving_find()`, and a circular result is remembered under
  `KEY_CIRCULAR_BINDING`). Scanning below the barrier too was wrong (Window & typeof globalThis). A root pattern's
  LazyType pushes `RESOLVING_PATTERN`, and re-entered marks from the first of its names above it, or nothing: tsc
  doesn't push a pattern (dependentDestructuredVariables: TS7022 only on `value1`). This subsumed `defaults_in_flight`
  and `jsdoc_values_in_flight` (removed).
- `signature_return_type()` pushes; a failed pop is any plus TS2577 at a written return type (not an accessor's). The
  chosen signature's return type is taken outside the call's barrier (`resolved_return_type`, `resolving_call_start`:
  tsc asks getReturnTypeOfSignature after getResolvedSignature). The printer reuses an annotation whose type is the
  error type (tsc's canReuseTypeNodeAnnotation), so `() => typeAlias3` prints as tsc's.
- An inferred return type is circular only by a failed pop, not by any re-entry (a re-entry from under a barrier isn't
  a cycle: `res3`). A function expression that is a property's value is named by it (TS7023 `'fields'`).
- Interface bases push `(RESOLVING_BASES, declared)` while the extends clauses are lowered; members asked for meanwhile
  mark it; a failed pop drops the bases and records the base cycle (TS2310 for circularBaseTypes' `M2`). The
  base-constraint walk runs on the stack (`RESOLVING_CONSTRAINT`) instead of its own array.

## Stage 3 (leftovers, not scheduled)
1. **Nested call resolution** (res1 in circularReferenceInReturnType, `A`/`fields` in circularReferenceInReturnType2):
   tsc finds these cycles through a nested resolution of the same call, which has no barrier and returns
   `resolvingSignature` (any) at once. Plan 38 stage 3 (resolve once) landed, so the only nested resolutions left are
   in-flight re-entries. **Done by plan 42 stage 4** (2026-10-05: first-resolution-only barrier, return types
   re-inferred below it; both tests pass).
2. **Constraint lowering under the push** (TS2313 in recursiveMappedTypes, incorrectRecursiveMappedTypeConstraint,
   circularBaseTypes' `K`/`Y`): tsc's getResolvedBaseConstraint lowers the declared constraint while `(K,
   ImmediateBaseConstraint)` is pushed, and getTypeFromMappedTypeNode resolves the key constraint eagerly. Moving
   `declared_param_constraint()`'s lowering under the walk's push (ty_param_constraint walking first) made
   mappedTypeWithAny report TS2313 twice: tttt resolves members eagerly while lowering (`homomorphic_variable` ->
   `ty_members` in instantiation, `ty_keyof`) where tsc doesn't. **Done by plan 42 stage 3** (2026-10-05:
   `ty_mapped_info_unexpanded()`; incorrectRecursiveMappedTypeConstraint passes, recursiveMappedTypes' TS2313s).
   circularBaseTypes' `Y` TS2310 came with plan 42 stage 2 (interface bases reduced, getReducedType in getBaseTypes).
3. **Accessors / object literal methods** (old item 5): TS7023 tsc reports while printing a type whose return type is
   in flight (thisInObjectLiterals and friends). Untouched. The mechanism: checking a return expression prints a type
   in an error message (or relates structurally in an assertion) that needs the function's own return type, which is
   in flight; tttt's inference and relations don't build messages there, so they never re-enter. Tests:
   thisInObjectLiterals, accessorInferredReturnTypeErrorInReturnStatement,
   checkingObjectWithThisInNamePositionNoCrash, trivialSubtypeReductionNoStructuralCheck.
4. thisConditionalOnMethodReturnOfGenericInstance (spurious TS2417) is unchanged by the signature push; not
   investigated further.

## Validation (every stage)
The tests above; `./fasttest.sh --summary` 0 lost; `./realworld.py` on every corpus unchanged (tsc, type-fest,
valibot, react, nest, zod, vue 0; webpack 6; xstate 19; drizzle 8) **and zod-root byte-identical to the previous build**
(`tttt -q --keep-going -p thirdparty/corpora/zod | sort`, diffed: that is where stage 1's regression showed and the
pinned corpora didn't); `./threadcheck.sh`, `./checkerdiff.py`; `./instrcount.sh` within ~0.3%.
