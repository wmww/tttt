# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-20 (triage session, todo entry 6 of the twenty-third window). mikro-orm's `string[]` against
`OperatorMap<T> & FilterObject<T>` was IntersectionState.Target leaking into the generic mapped target's template
comparison (fixed; the fallback cut is wide again, mikro-orm's probe 258k types against tsgo's 393k, its chain
identical; the general stickiness is hacks.md "IntersectionState is sticky"). never_reduced_literal_prop() honours
HasNeverType (fixed). The combined import's member symbols went to bugs/binder_modules.md, the apparently-any
intersection's property symbol to bugs/type_core.md. Plan 86's done stage 1 condensed.


- tttt -b on TypeScript6/src reserves ~12 GB of address space on 16 cores (ast_alloc.c:83 panic under ulimit -v 12 GB; realworld.py's cap raised to 24): look at the per-thread/per-builder AST and arena reservation sizes, they scale with the core count.
