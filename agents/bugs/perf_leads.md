# Perf leads
Numbers are in `measurements.md`; this is what to try next. Profile with callgrind (`valgrind --tool=callgrind
./release/tttt -q files...`, then `callgrind_annotate [--inclusive=yes]`; no `perf` on the dev box). The release build
has no debug info: for line-level annotation (`callgrind_annotate --auto=yes`) build a copy with
`meson setup /tmp/relg --buildtype release -Dc_args=-g` (same code, same instruction counts). Compare
instruction counts across sessions, not wall times.
- **Parser** (after the first-char dispatch session, 2026-09-27: parse is 13% of the tsc corpus, 43% of nest, 20% of
  webpack): no single parser function is above ~2.5% self any more. What's left is structural: every attempt makes a
  `Parser` (the `parser()` copy is the top line), identifiers are scanned again by each alternative that fails, and
  AST nodes are separate mallocs (`_int_malloc` is 5% of nest). Next levers: a real token (scan the identifier or
  punctuator once per position, reuse it across alternatives), arenas for AST nodes. nest's remaining parse time is
  .d.ts member lists (`parse_class_item_inner` -> `parse_type`).
- **Checker hot spots** (tsc corpus, 2026-09-27, after the same-enum shortcuts): `type_at_flow_node` 4.6% self,
  `check_related` 3.8% (2.3 M calls, 1.5 M of them now answered by the same-enum test before the RelationState setup),
  `ty_union` 3.1% (small unions rebuilt over and over: `access`, `identifier_flow_type`, `lower`), `intern_slot` 3%,
  `is_simple_related` 2.7% (flag tests only now); `infer_from` (1.3 M calls, per-call prologue/visited scans);
  `ty_declared_type_of` -> `intern_slot` (cache the enum type on the member literal?).
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
