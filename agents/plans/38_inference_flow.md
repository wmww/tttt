# Plan 38: tsc's inference flow in call resolution (brief)
Brief by the insights of 2026-09-28 (end of the fifth window). **A planning session turns this into the real plan**
(prototype in a worktree, measure, rank stages, write them up here), the way plan 37's planning did.

## Why
call.c's inference is the one area where tttt stacks approximations on top of each other instead of doing what tsc
does, and every recent session that touched it ended with "needs tsc's X":
- the fixing gate and its probes (`fixed_by_argument` / `fixed_by_expr` / `fixed_by_type` / `fixed_by_function`,
  hacks.md "`infer_type_arguments` second pass"): tttt decides up front, by probing shapes, which type parameters an
  argument fixes; tsc fixes lazily, exactly what instantiating a contextual signature touches;
- "a type parameter without candidates stays itself only where it would be `unknown`" (hacks.md "Second pass
  contextual types"), which stands in for tsc's getApparentTypeOfContextualType going through constraints;
- the second pass instantiates the whole parameter type with the inferences so far; tsc's instantiateContextualType
  replaces only instantiable leaves, with the return mapper's inferences;
- the first pass skips context-sensitive arguments entirely; tsc checks them with functions replaced by
  anyFunctionType and still infers from the non-inferrable literal;
- the contextual return type is memoized (`KEY_CONTEXT_RETURN`) and computed eagerly; tsc fixes a function's return
  type at its first contextual check.
bugs/type_core.md "Inference" has five bullets with probes that name this root (defaults leaking into
context-sensitive callbacks, eager return types, eager contextual instantiation, fixing without candidates, the first
pass), and it is **webpack's largest remaining cause** (~9 of its 30 TS2322s on `.then` chains, plan 19 "webpack").
Plan 37 measured the naive gate removal at +1 / -8 and zod's `Array.from({length: n}, (_, i) => ...)` must stay clean:
the pieces only work together, so this is a port, not a fix.

## What the planning session decides
- The stages and their order (candidates: (1) a precise "mentions type parameter P" walk, which `collect_free_params`
  lacks for functions and permissive instantiation needs too; (2) first pass with anyFunctionType arguments; (3)
  leaf-wise instantiateContextualType; (4) lazy fixing through `Checker.contextual_fixing_mapper` and deleting the
  probes, the gate and the "only where unknown" stand-in; (5) return types fixed at the first contextual check).
- Whether the memoization tttt relies on (`LazyType` per node, `KEY_CONTEXT_*`) is compatible with tsc's
  check-twice-and-keep-the-last model, or needs a per-call-resolution scope. This is the risk; answer it with the
  prototype, not by reading.
- Measure with the suite (`37_prototypes/fixing_gate_lost.txt` is the loss list to beat), webpack, zod, valibot and the
  inference-heavy corpus the window's first session pins; `perf stat` on zod/valibot.

## JS: a JSDoc `@template` is fixed by any nested JSDoc function type
`38_prototypes/jsdoc_template_fixed_by_nested_function_type.ts` (todo entry 1 of the sixth window): in JS, tsc's
isTypeParameterPossiblyReferenced walks up from a JSDoc type node and never meets the `@template` tag, so R counts as
referenced by `s: () => void` in `@param {(i: number, s: () => void) => R} setup`. Checking the context-sensitive
setup arrow instantiates that parameter type with the fixing mapper, which fixes R before it has candidates: the JS
default `any`. webpack depends on it: MultiCompiler.js `_runGraph` (SetupResult is any: TS18048 x2, TS2345) and
FileSystemInfo.js `_readContext` (T is any: TS2345 at 3859). Lazy fixing (stage 4 above) plus this quirk in the "mentions
P" walk (stage 1) reproduce it; without the quirk those four stay.

## Validation (of the whole plan)
The HACK entries above deleted from hacks.md, the five type_core.md bullets fixed or rewritten with a named reason,
webpack's `.then` TS2322s gone, zod/valibot/the new corpus unchanged or better, 0 suite tests lost per stage.
