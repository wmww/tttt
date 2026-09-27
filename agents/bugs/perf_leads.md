# Perf leads
Numbers are in `measurements.md`; this is what to try next. Profile with callgrind (`valgrind --tool=callgrind
./release/tttt -q files...`, then `callgrind_annotate [--inclusive=yes]`; no `perf` on the dev box). Compare
instruction counts across sessions, not wall times.
- **Parser** (~20% of instructions on the tsc corpus, 116 of react's 300 ms, most of nest's time): backtracking
  sub-parsers (`parse_fail` ~7 M calls from `parse_type_compat_literal`, `parse_expr_right_side`, `parse_type`,
  `parse_left_unary_op`...; `hop_start_if_ends_on_word_break` 1.4% self). A first-char dispatch (or a real token) in
  `parse_expr`, `parse_type` and the keyword probes is the lever; the binary operator switch alone didn't move wall time.
- **Checker hot spots** (tsc corpus, 2026-09-27): `is_simple_related` 6.3% self, 3 M of 4.5 M calls in the enum branch
  (literal vs literal of one enum: a same-enum shortcut before the generic checks); `ty_union` 4.5% self (small unions
  rebuilt over and over: `access`, `identifier_flow_type`, `lower`); `infer_from` 5% self (1.3 M calls, per-call
  prologue/visited scans); `ty_declared_type_of` -> `intern_slot` (cache the enum type on the member literal?).
- valibot: inference (`infer_from_signature_lists` -> `infer_from_returns` over schema objects' `~run`/`~standard`
  members) and `ty_conditional_resolve`'s `infer X` walks. type-fest: `test-d/opaque.ts` (`type_core.md`, circularity).
- **Name resolution** 77 ms on the tsc corpus (plan 04 wanted 40): every identifier walks the scope chain (locals +
  container exports per level, value then type meaning on failure); declare + resolve are two AST walks. Ideas: one
  table probe per scope for all meanings, skip empty block scopes, merge the walks.
- **Memory**: peak RSS ~120 bytes per source byte. Suspects: AST node size, per-node allocations, `DEBUG_ALLOCATOR`
  (always defined in `prelude.h`, even in release: measure with it off). Long term, arenas.
- `find_prop`'s intersection memo caches whatever depth it was computed at (`depth > 8` gives "not found").
  `ty_intersection` distributes over non-primitive unions with no size limit (tsc: TS2590 at 100k).
- **Stack depth**: the parser and checker recurse once per nesting level; binderBinaryExpressionStress needs 4-8 MB
  with GCC 15 -O3 (passes at the default 8 MB). tsc walks binary chains iteratively; logical chains (`&&`, `??`,
  `||`) still recurse per operand in `expr_lookup_names_logical`, the checker, `expr_compile_test_info_inner` and
  `expr_drop` (tsc overflows at ~5k `&&` too). Threads get smaller stacks: matters for parallelism.
