# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-28 (triage session, todo entry 10 of the fourth window; suite 10516 -> 10543). Fixed: webpack's
checker-count dependence (overload groups keyed by block only; webpack 30 at every K), the baseline walk typing each
file with its own checker, TS2769 candidates printed instantiated, type-predicate narrowing keeping the candidate
(map_type), ~20 `TYF_OBJECT` gates that skipped intersections, explicit type arguments failing a constraint
(candidateForTypeArgumentError, TS2344), TS2375 against union targets, regular object literals (evolving arrays of
objects, the `literal_lacks_prop_of` HACK gone), TS7010/7011/7018 from reportErrorsFromWidening, tsc's line breaks
(`\r`, U+2028/9) and whitespace (NEL, U+200B). Routed: eager contextual instantiation (plan 37 F's return_only probe)
to `bugs/type_core.md`, the zod-without-variance TS2589, intersection property lists, spread identity, and the
smaller leftovers to `bugs/*.md` and hacks.md.

## 2026-09-28, sixth window's corpus session (todo entry 1)
- Distributive conditional base constraint: with `S2<T> = T extends string ? T : number`,
  `g2<T extends unknown[]>(x: [...{ [K in keyof T]: S2<T[K]> }])` widens `g2(['a'])` to `[string]`, tsc `["a"]` (its
  forConstraint constraint of `S2<T[number]>` keeps the true branch with T's implied `string`).
- A corpus's output can depend on the checker count (4 by default): a file can look fine alone and fail in the corpus.
  Reproduce with `--checkers 1` and the suspect file first in `files`.
- xstate's output still depends on the checker count: 5 diagnostics only at `--checkers 1` (actor.test.ts:1060/1499/
  1566, assign.test.ts:20, invoke.test.ts:1257/1276: callbacks typed `AssignArgs<MachineContext...>`), no limit hit:
  some cached inference state (plan 38 territory?). checkerdiff.py should flag it; it's a determinism bug.
- `StateId<U>` to `StateId<T>` (xstate's StateId, no variance measurement) gives TS2589 next to the TS2322 tsc also reports:
  the definitely-assignable checks instantiate deeply nested indexed-access chains. Repro:
  test/probes/xstate/state_id_deep_nesting.ts.
- `interface I<C> { a: StateNode<C, any> }` where StateNode requires `C extends MachineContext`: tsc's TS2344 missing.
- A binding default `{} as any` misses tsc's empty `^^^` types-mask line (bindingDefaultAnyRemovesUndefined.ts types).
- tsc prints a written `<T extends any>` as `extends unknown`.
- ~30 callers of ty_non_nullable still use its stripped-constraint behaviour for type parameters (hacks.md
  "ty_non_nullable"); `x!` and optional-chain roots use ty_non_nullish_part (tsc's getNonNullableType) now.
- (sub-agent C) Elaboration into a `NoInfer` member: against `NoInfer<{ type: 'A' }> | Expr` tsc elaborates to `'"B"'
  is not assignable to '"A"'`, tttt reports the whole argument (best_matching_type doesn't pick the substitution).
- Union-source elaboration: for `Snap<...>` (a union alias) not assignable to `number`, tsc adds a line for the
  union's first member; tttt omits it.
- Missing TS2353 for `const x: Trans<E> = { NEXT: ... }` in a generic function with `Trans<E> = { [K in E['type'] |
  '*']?: ... }`.
- TS2305 where tsc gives TS2724 ("Did you mean 'StateSchema'?") for a missing named import.
- tttt adds TS6500 related info on `NoInfer<{...}>` targets; tsc doesn't.
- xstate meta.test.ts 270/434: elaboration stops one level too high (at `e2:`, tsc goes on to the `meta:` under the
  `@ts-expect-error`). Not reduced.
