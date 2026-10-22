# Triage
Uninvestigated items: dump here what you don't want to deal with now. Whoever picks an item up drains it into a fix, `bugs/*.md`, or `todo.md`.

Last drained 2026-10-20. mikro-orm's `string[]` against
`OperatorMap<T> & FilterObject<T>` was IntersectionState.Target leaking into the generic mapped target's template
comparison (fixed; the fallback cut is wide again, mikro-orm's probe 258k types against tsgo's 393k, its chain
identical; the general stickiness is fixed since, 2026-10-21). never_reduced_literal_prop() honours
HasNeverType (fixed). The combined import's member symbols went to bugs/binder_modules.md, the apparently-any
intersection's property symbol to bugs/type_core.md. 


- tttt -b on TypeScript6/src reserves ~12 GB of address space on 16 cores (ast_alloc.c:83 panic under ulimit -v 12 GB; realworld.py's and clidiff.py's caps raised to 48: typescript-eslint's -b reserves 22-28 GB, the same at 4eadfcc74, varying run to run with the load): look at the per-thread/per-builder AST and arena reservation sizes, they scale with the core count.

- Missing TS2322 (2026-10-21, IntersectionState audit): `function f<X extends string>(m: Uppercase<X>) { let y:
  Uppercase<X> & { z?: 1 } = m; }`: tsc fails `Uppercase<X>` against the weak member (its chain: `Uppercase<string>`
  has no properties in common with `{ z?: 1 }`), tttt accepts it (before and after the audit).
- Chain (2026-10-21): `C<X>` (conditional) to `D<X> & { z?: 1 }` where D<X> fails: tsc's chain goes through the
  default constraint's union members against the whole intersection and repeats `C<X>` to `D<X>` inside it; tttt
  reports the D<X> member directly (test/cases/ttttIntersectionStateNotPassedToConditionalBranches.ts, as an
  assignment instead of the overload)

- Source sweep policy (2026-10-08 idea, unexplored): if checking every `tsconfig*.json` turns out noisy, mine each
  repository's own `tsc` invocations from its package.json scripts instead.

- Loop partial evaluations, leftovers after the loop-pass diagnostics (2026-10-22, worker cor): (1) tsc's types
  baseline prints `someFrom(isSome(null) ? result.some : makeSome())` in typeGuardsAsAssertions' null-arg mutant as
  `{ some: a; }` (an uninstantiated return: the nested resolution under the flow loop leaves resolvedSignature as it
  was), tttt `{ some: any; }`; (2) `while (cond) { const q = result.some; result = someFrom(q); }` is TS7022 on q in
  tsc, nothing in tttt (the circularity through the loop's assigned value). (3) Call argument errors (TS2345...) of a
  nested call resolution under a loop in progress aren't reported from the partial pass (call.c resolves without
  errs under flow_uncached): wire them like expr_property_access_errors() if an oracle shows one.

- K-dependence hole of the loop-pass diagnostics (`ty_flow_partial_reports()`, 2026-10-22): a partial evaluation
  reports only in the checker checking the expression's file. If another file's check evaluates that loop first (an
  inferred return type: `export function f(a: number[]) { let y; while (a.length) { y = a[y] } return y }` in a.ts and
  `import { f } from "./a"; const r = f([])` in b.ts checked first) its partial pass is silent, and at K1 the same
  checker later reads finished memos for a.ts (`flow_expr_type_in` returns a done memo, no rerun), so a.ts's TS2538 could
  be missing at K1 and present at K16. Not seen by checkerdiff/kdiff yet; if it shows, rerun the partial pass for the
  own file (or record foreign partial errors per file and replay them when that file is checked).
