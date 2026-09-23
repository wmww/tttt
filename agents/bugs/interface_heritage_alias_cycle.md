# TS2430 (interface incorrectly extends) is implemented but off

`interface_heritage_typecheck` exists and is gated behind `TTTT_INTERFACE_HERITAGE=1`. Turning it on makes the real
corpus report six spurious TS7022 in binder.ts/utilities.ts: checking `CallChainRoot extends CallChain` (types.ts)
early resolves some type alias from inside a relation while that alias is already being resolved further up, and
the `LazyType` memoises the error type (a cycle alias -> conditional type -> relation -> member -> same alias that
the usual evaluation order never hits).

Cost of leaving it off: 167 missing TS2430 in 40 suite tests (2026-09-22).

Next steps:
1. Bisect: enable the heritage check for one interface at a time (or log which alias hits the cycle in
   `lazy_type.c` when the flag is on) to find the alias.
2. Fix the order dependence: tsc resolves aliases eagerly but defers conditional types (and their relation checks)
   lazily; a relation reached while an alias is resolving must not memoise a cycle error for it. Probably the
   conditional type's check type / constraint is being related during alias *declaration* resolution.
3. Turn the check on unconditionally, rerun the corpus (`./realworld.py`) and suite.

complexRecursiveCollections also has 6 spurious TS2430 when on, from `agents/bugs/overload_relation_assumption.md`.
