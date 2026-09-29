# Perf leads
Numbers are in `measurements.md`; this is what to try next. Profile with callgrind (`valgrind --tool=callgrind
./release/tttt -q files...`, then `callgrind_annotate [--inclusive=yes]`; no `perf` on the dev box). The release build
has no debug info: for line-level annotation (`callgrind_annotate --auto=yes`) build a copy with
`meson setup /tmp/relg --buildtype release -Dc_args=-g` (same code, same instruction counts). Compare
instruction counts across sessions, not wall times. `--time` prints the resident set after each phase and the
checker's arena size and type count. Memory: `valgrind --tool=massif` sees malloc only; the checker arena is mmap'd
(to attribute arena bytes, count `__builtin_return_address(0)` per call site in `arena_alloc` and `addr2line` the
`/tmp/relg` binary run under `setarch -R`, as the 09-28 perf session did).

## drizzle and react (2026-09-29 corpus session)
- drizzle went 265 -> 16.3 G instructions once its TS2589 runaways were fixed (object literal instances), but it is
  still the heaviest corpus (tsc's: 5.2 G) with 1.8 GB peak RSS at K=4: `./arenasites.sh -p
  thirdparty/corpora/drizzle/drizzle-orm/tsconfig.corpus.json`, then per file (pg select.ts and set-operators.ts
  were the heaviest). tttt measures a base class's variances for the derived-to-base check (PgColumnBuilder ->
  ColumnBuilder<..., TTypeConfig & {dialect}>) where tsc relates identical members structurally and measures nothing.
- react +4.6% from two tsc ports in relate.c (generic-source constraints pushing only the source, the conditional
  immediate constraint): react-hook-form's Path/FieldPathValue indexed-access chains are related level by level with
  uncached Maybe results, each variant ~12 times (hacks.md "Relations"; relating whole chains as tsc does is the fix).

## Memory (the weak side now: every corpus is faster than single-threaded tsgo, several use more memory)
- **Arena by call site**: `./arenasites.sh -p <corpus>` (a `-DTTTT_ARENA_SITES` build in /tmp). zod after the
  `Prop`/`PropDecl` split (09-28, 431 -> 298 MB): instantiated `Prop`s (80 bytes, `instantiate_members`) 86 MB, types
  52 MB (instantiated anonymous types 19, references 10), `intern_slot` keys 24, `builder_finish`'s member arrays 36,
  instantiated `Signature`s (192 bytes, same split possible: `instantiate_members`' second site) 14, scopes/symbols of
  name resolution ~38. Next steps if memory matters again: `Signature` split; `Prop` to 56 bytes (`write_type` into a
  side table, `mapped_type` into `PropDecl`, `type_state` a byte); instantiating props lazily per name (tsc doesn't).
- **Type counts are not the problem**: tsc makes 718k types on type-fest (tttt 578k), 589k on zod (tttt 361k), 332k
  on valibot (tttt 385k). The old "3.5x on deep-partial.test.ts" was the variance bug below, fixed.
- **The AST**: after the parser's shrink_to_fit (09-28), webpack's parse heap is ~198 MB live plus ~100 MB of malloc
  per-block overhead: millions of small `c_new` nodes. Node arenas (per file) would take the overhead and most of the
  malloc time (`_int_malloc` is ~9% of parsing), but every `*_drop` of the AST and every backtracking path frees
  nodes: the parser would need to stop freeing (failed attempts leak into the arena, bounded by the fail cache).
  Struct sizes: `Expr` 144 (two 24-byte `Span`s), `PropertyExpr` 368 (7 checker/binder pointers), `BinaryOpExpr`
  312, `Statement` 272, `FunctionArg` 568, `InterfaceMember` 536. A `Span` could be 12 bytes (u32 file-relative
  offset + length, the source from the file being checked).
- Name resolution adds ~60 MB on webpack (scopes 248 B, symbols 192 B).

## Time
- **Variance measurements** match tsc's reporting now (09-28: only where the marker occurs in the source, at tsc's
  four sites; Unmeasurable separate). Probe: zod with its `in`/`out` annotations stripped (sed over a /tmp copy of
  `packages/zod/src`, `.configs/` and `vitest.config.ts` alongside) costs 4.65 G vs 4.33 annotated (was 7.8). To compare
  variances with tsc, patch a copy of `thirdparty/tsc/lib` (`_tsc.js`, after `links.variances = variances;` in
  getVariancesWorker print `symbol.escapedName` and the flags; tsc's Unmeasurable is 8, Unreliable 16, tttt's
  `TTTT_DUMP_VARIANCES` the other way round). ~25 of ~250 zod variances still differ in reliability, both ways
  (ordering of cached comparisons; `ZodTransform`, `ZodTuple`: tsc unreliable, tttt not).
- **Parser** (13% of the tsc corpus, ~25% of webpack, half of nest): no single function stands out. What's left is
  structural: a `Parser` copy per attempt, identifiers scanned again by each alternative that fails, one malloc per
  node. A real token (scan the identifier or punctuator once per position) and node arenas.
- **Name resolution** 78 ms on webpack, 89 ms on the tsc corpus, ~1 IPC (cache misses walking the AST): every
  identifier walks the scope chain (locals + container exports per level, value then type meaning on failure),
  `scope_check_strict_identifier` compares each name with 9 reserved words, `scope_declares_symbol` rehashes the name
  per scope in the loops of `unused_mark_referenced`/`check_static_type_parameter_reference`/`expr.c` (hash once, or
  keep the hash in `Symbol`). Declare + resolve are two AST walks.
- **Flow** (~12% of webpack): `type_at_flow_node` follows tsc; each variable read asks once now (TS2454's check
  reused the walk until 09-28).
- **Checker hot spots** (tsc corpus): `check_related` setup, `ty_union` rebuilding small unions (`access`,
  `identifier_flow_type`), `intern_slot`, `checker_atom` (`ty_member_name` atomizes every member name of every
  resolved declaration: ~3% of webpack; atomize at bind time instead).
- **zod** (4.3 G): re-profile. valibot: `infer_from_returns` over schema objects' `~run`/
  `~standard` members and `ty_conditional_resolve`'s `infer X` walks.
- `find_prop`'s intersection memo caches whatever depth it was computed at (`depth > 8` gives "not found").
  `ty_intersection` distributes over non-primitive unions with no size limit (tsc: TS2590 at 100k).
- **Stack depth**: the parser and checker recurse once per nesting level; binderBinaryExpressionStress needs 4-8 MB
  with GCC 15 -O3 (passes at the default 8 MB). Logical chains (`&&`, `??`, `||`) still recurse per operand in
  `expr_lookup_names_logical`, the checker, `expr_compile_test_info_inner` and `expr_drop`. Threads get smaller
  stacks: matters for parallelism.
- The suite: the slowest tests left are the React-typings JSX ones (~1 s each: react16.d.ts per test).
- **Thread startup per program** (plan 36 stage 3): `baseline_tests --checkers 4 --threads 4` takes 38 s against 24 s
  at `--threads 1` (same output): ~1 ms per program for starting and joining 3 threads (8 MB stacks, glibc arenas).
  Irrelevant for one CLI run; a persistent pool (stage 1's load pool) fixes it for a watch/LSP mode or a threaded
  harness. With fewer checked files than checkers, the empty checkers still build their global types (~0.2 ms each).

- **valibot +2.5% instructions** since the corpus session (todo entry 8): generic homomorphic mapped types get index
  signatures from their constraint, eagerly instantiated. tsc resolves them lazily; a lazy IndexInfo value (or the
  mapped type's members computed only when index infos are asked for) would take that back.

## Pathologies
- **webpack loaded twice: 89 s** (tsc: 11 s on the same program). Run from a worktree whose `thirdparty/corpora` is a
  symlink (`cd /tmp/wt; tttt -p thirdparty/corpora/webpack`), the root files keep the symlinked path and
  `node_modules/webpack -> ..` realpaths to the other copy: two webpacks, 130 diagnostics (tsc gets 24 of its own).
  realworld.py now passes the real path, but some relation between the two copies' structurally-identical types
  runs away in tttt; a callgrind of that run would show which (probably the relation budget/recursion identity).

## Base constraints of expect-type's conditional constraints
In valibot's `*.test-d.ts`, `ty_base_constraint` of expect-type's `Expected` (constrained by
`StrictEqualUsingTSInternalIdenticalToOperator<Input, Expected> extends true ? unknown : MismatchInfo<...>`) took
5-10 ms per type parameter (about 250M instructions over the corpus, 5%) when constrained type variable reduction asked
for it; `primitive_constraint_of` (checker.c) now skips deferred declared constraints because of it. Worth a look: tsc
computes the same constraint without it showing up.
