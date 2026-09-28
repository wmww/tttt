# Perf leads
Numbers are in `measurements.md`; this is what to try next. Profile with callgrind (`valgrind --tool=callgrind
./release/tttt -q files...`, then `callgrind_annotate [--inclusive=yes]`; no `perf` on the dev box). The release build
has no debug info: for line-level annotation (`callgrind_annotate --auto=yes`) build a copy with
`meson setup /tmp/relg --buildtype release -Dc_args=-g` (same code, same instruction counts). Compare
instruction counts across sessions, not wall times. `--time` prints the resident set after each phase and the
checker's arena size and type count. Memory: `valgrind --tool=massif` sees malloc only; the checker arena is mmap'd
(to attribute arena bytes, count `__builtin_return_address(0)` per call site in `arena_alloc` and `addr2line` the
`/tmp/relg` binary run under `setarch -R`, as the 09-28 perf session did).

## Memory (the weak side now: every corpus is faster than single-threaded tsgo, several use more memory)
- **Instantiated `Prop`s are 200 bytes** and most of that is the declaration source (node, lazy, function,
  overloads, ctx, write_node, spans, asserted...), which an instantiation never uses (it follows `target`). zod's
  deep-partial.test.ts makes 1.56 M of them (297 MB of its 607 MB arena). Split `Prop` into the core (name, flags,
  parent, target, mapper, type state, type) and a `PropDecl const*` for declared ones: ~200 references to the
  declaration fields, mostly `ty/members.c`. Then `Signature` the same way (`instantiate_members`' second site).
- **tttt makes 3.5x the types tsc does** on deep-partial.test.ts (587k vs 168k) and type-fest has 575k for 0.3 s of
  work: find which kinds (a histogram of `ty_alloc` by kind and caller) and which tsc doesn't create (tsc's
  instantiations of anonymous types are cached by the outer type parameters they mention; members resolve lazily).
- **The AST**: after the parser's shrink_to_fit (09-28), webpack's parse heap is ~198 MB live plus ~100 MB of malloc
  per-block overhead: millions of small `c_new` nodes. Node arenas (per file) would take the overhead and most of the
  malloc time (`_int_malloc` is ~9% of parsing), but every `*_drop` of the AST and every backtracking path frees
  nodes: the parser would need to stop freeing (failed attempts leak into the arena, bounded by the fail cache).
  Struct sizes: `Expr` 144 (two 24-byte `Span`s), `PropertyExpr` 368 (7 checker/binder pointers), `BinaryOpExpr`
  312, `Statement` 272, `FunctionArg` 568, `InterfaceMember` 536. A `Span` could be 12 bytes (u32 file-relative
  offset + length, the source from the file being checked).
- Name resolution adds ~60 MB on webpack (scopes 248 B, symbols 192 B).

## Time
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
- **zod** (9.1 G): with deep-partial fixed, re-profile. valibot: `infer_from_returns` over schema objects' `~run`/
  `~standard` members and `ty_conditional_resolve`'s `infer X` walks.
- `find_prop`'s intersection memo caches whatever depth it was computed at (`depth > 8` gives "not found").
  `ty_intersection` distributes over non-primitive unions with no size limit (tsc: TS2590 at 100k).
- **Stack depth**: the parser and checker recurse once per nesting level; binderBinaryExpressionStress needs 4-8 MB
  with GCC 15 -O3 (passes at the default 8 MB). Logical chains (`&&`, `??`, `||`) still recurse per operand in
  `expr_lookup_names_logical`, the checker, `expr_compile_test_info_inner` and `expr_drop`. Threads get smaller
  stacks: matters for parallelism.
- The suite: the slowest tests left are the React-typings JSX ones (~1 s each: react16.d.ts per test).

- **valibot +2.5% instructions** since the corpus session (todo entry 8): generic homomorphic mapped types get index
  signatures from their constraint, eagerly instantiated. tsc resolves them lazily; a lazy IndexInfo value (or the
  mapped type's members computed only when index infos are asked for) would take that back.

## Pathologies
- **webpack loaded twice: 89 s** (tsc: 11 s on the same program). Run from a worktree whose `thirdparty/corpora` is a
  symlink (`cd /tmp/wt; tttt -p thirdparty/corpora/webpack`), the root files keep the symlinked path and
  `node_modules/webpack -> ..` realpaths to the other copy: two webpacks, 130 diagnostics (tsc gets 24 of its own).
  realworld.py now passes the real path, but some relation between the two copies' structurally-identical types
  runs away in tttt; a callgrind of that run would show which (probably the relation budget/recursion identity).
