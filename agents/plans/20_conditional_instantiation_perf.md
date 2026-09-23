# Plan 20: conditional type instantiation cost (valibot is slower than tsgo)

## Why
valibot (`./realworld.py --corpus valibot`, `./bench.sh`) is the first corpus where tttt loses: 4.0 s / 1.7 GB vs tsgo
1.3 s / 0.8 GB, typecheck 5.4 s, and that with thousands of checks cut short. Callgrind (`measurements.md`, plan 17
entry): 93% under `resolve_call` -> `infer_from_signature_lists` -> `infer_from_returns` -> instantiating conditional
types (`conditional_from_root`, `compose_mappers`, `instantiate_inner`). valibot's `pipe()` overloads and
`InferOutput<...>` chains are what every schema library looks like, so this is the perf plan vision.md said a slow
second corpus would trigger.

## Suspects (verify, don't assume)
- No instantiation cache for conditional types. tsc (getConditionalTypeInstantiation) keys `root.instantiations` by the
  mapped values of the root's *outer type parameters* (the ones the conditional can actually mention), so the same
  conditional under a different-but-equivalent mapper is one lookup. tttt instantiates from the root every time and
  composes the mapper chain (`compose_mappers`) on every step, so mappers grow with nesting.
- Same question for anonymous object types / mapped types (tsc: `target.instantiations` keyed by outer type params).
- `infer_from` 19% self and `ty_is_deeply_nested` 4.4% self (the invokeOnce stack added in plan 17 scans linearly).
- Peak RSS 1.7 GB: arena growth from the above (types and mappers never freed).

## Approach
Planning-light: the execution agent profiles first (`valgrind --tool=callgrind ./release/tttt -q --keep-going -p
thirdparty/corpora/valibot/library`, ~5 min), finds the call sites that repeat work, reduces one to a probe that shows
the blowup (time vs overload count / nesting depth), and implements tsc's outer-type-parameter keyed cache. Be careful
that cached results don't depend on state outside the key (the instantiation depth/count limits: a TS2589-truncated
result must not be cached).

## Validation
valibot typecheck time and RSS down (target: below tsgo multi-threaded), same diagnostics on all three corpora
(`./realworld.py --corpus X` counts unchanged, or explained), suite nothing lost. Record in `measurements.md`.
Best done after plan 18 stage 3 at the earliest (templates change what valibot's tests instantiate), or before if a
stage turns out to be blocked on time.
