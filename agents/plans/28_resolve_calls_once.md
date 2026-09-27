# Plan 28: resolve each call once

Written by the perf check-up of 2026-09-27. Status: not started, needs a short investigation before the code (step 1).

## Why
Every call expression is resolved twice: once when its type is computed (`ty_call_return_type()` -> `resolve()`,
`errs == nullptr`) and again by the error-reporting walk (`expr_typecheck()` in `src/expr.c`, case `EXPR_CALL` ->
`ty_call_typecheck()` -> `resolve()` with `errs`). The second run redoes overload selection, inference (both passes)
and the argument relations. On the tsc corpus that second run is **7.7% of all instructions** (callgrind, 471 M of
5.91 G at the end of the perf check-up; it was 9.7% before that session's inference caches). tsc resolves once
(`getResolvedSignature` caches `links.resolvedSignature`) and reports the errors of that one resolution.

The two runs can also disagree in principle (the second sees caches the first filled), which is a correctness smell
of its own: an error pass that re-derives instead of reporting what was computed.

## What
Cache, per `CallExpr` (and per JSX element / tagged template / decorator if they share `resolve_with_signatures()`),
the outcome of the resolution done with the call's final contextual type: the chosen signature, the return type and
whether any diagnostic would have been produced. The typecheck pass then:
- reuses it when resolution succeeded without diagnostics (the common case: the tsc corpus has zero);
- re-resolves with `errs` only when the cached one would report something (keeps today's error paths untouched).

## Steps
1. **Investigate what `errs` does on success paths.** `resolve_call()`/`resolve_with_signatures()` report some
   things even when a signature is chosen (constructor accessibility `ty_constructor_accessible()`, abstract classes
   TS2511, deprecations if any, excess property / implicit-any elaborations inside arguments?). List every
   `error_ctx_error*` reachable with a successful resolution. Each needs either to move to a place that runs once
   (the typecheck walk, from the cached chosen signature) or to set a "would report" bit in the cached outcome.
2. **Find the right key and validity.** The type pass may resolve the same call several times under different
   contextual types (inference of an enclosing call, `skip_sensitive` passes, `outer_inference`). Only the resolution
   under the final context (what `lazy_type_state_peek(...)->context` holds when the lazy type is done, which is what
   the typecheck walk passes today) may be reused. Check how `ty_check_expr`'s memo decides "final" and cache at the
   same point, or key the cache by (CallExpr, contextual type id) and let the walk look up with the memo's context.
3. Implement: a side table in the checker (`KEY_CALL_RESOLUTION` in the links intern table, or a field on
   `CallExpr` if the checker already writes per-node state) holding `Resolution` + a `clean` bit.
4. Make `ty_call_typecheck()` use it; keep the re-resolve fallback for unclean ones.

## Validation
- `./fasttest.sh --summary`: no lost component (this is a pure refactor; gains would be suspicious and worth a look).
- `./realworld.py` and the three `--corpus` runs: same diagnostics as before.
- callgrind on the tsc corpus (`valgrind --tool=callgrind ./release/tttt -q --keep-going -p
  thirdparty/tsc/src/compiler`): `ty_call_typecheck` inclusive should drop to near nothing; total instructions
  -5% or better. Record in `measurements.md`.

## Risks
- Hidden side effects of the second resolution that later checks rely on (it assigns contextual signatures to
  function arguments: `assign_contextual_signature()`; it may also be what checks arguments that the type pass
  skipped). If something only happens in the second run, move it, don't keep the double resolution for it.
- If step 1 finds many success-path diagnostics interleaved with inference, stop and write up why instead of forcing
  it; a narrower version (cache only for non-generic single-signature callees) still gets part of the win.
