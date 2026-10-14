# Plan 63: the AST's allocation (struct diet first, then per-file arenas)

Written 2026-10-14 by todo entry 5 (planning). It replaces plan 60 (deleted: its checker-side leftovers are in
bugs/perf_leads.md "Memory", its corpus table in measurements.md "Memory breakdown"). Numbers are this machine's (4 vCPU,
GCC 15.2), K1 = `--checkers 1 --threads 1`.

## The measurement (what decided the plan)
`scripts/memtrack.sh` (now with `MEMTRACK_STOP=<phase>` to exit after a phase's dump, and realloc churn counted) and
callgrind on webpack, at 89f761ff:

| | vscode | webpack |
|---|---|---|
| live malloc blocks after load+parse | 8.82M | 1.01M |
| requested / chunk bytes (malloc's header + size-class rounding) | 1896 / 2001 MB (**+5.5%**) | 229 / 241 MB (+5%) |
| free bytes inside malloc's heap (mallinfo `fordblks`) | 3 MB | 0 MB |
| all-zero words in the live blocks | **962 MB (51%)** | 116 MB (51%) |
| parse churn: malloc'd in total / realloc'd away / freed outright | 3603 / 1465 / 330 MB | 486 / 189 / 79 MB |
| malloc+realloc+free instructions, share of `parse_file` | - | **11.2%** (of 2.49 G) |
| huge pages (`reexec_with_huge_pages`) vs `TTTT_NO_REEXEC=1`, K1 wall | parse 1.88 vs 2.24 s, names 1.36 vs 1.45, check 9.85 vs 10.27 | - |

**malloc's per-block overhead is small** (~105 MB on vscode, nothing fragmented): arenas would not move peak RSS by more
than ~5%. What they buy is **time**: ~11% of parsing's instructions (14.4M calls on vscode, 3.5M of them reallocs of
growing lists), the huge-page effect without re-exec'ing (parse -16% wall on vscode), and freeing a program in
O(chunks): `drop_source_files()` frees ASTs node by node, "a fifth of a big program's time on one" thread, paid per
project by `-b`. **The memory is in the representation**: half of every AST block is zero. So stage 1 is struct work,
stage 2 the arenas, gated on an audit.

Where the zero words are (vscode after load+parse; struct sizes from `ptype /o` on the -g build):
| block | count | MB | zero | what |
|---|---|---|---|---|
| PropertyExpr 184 | 1.68M | 309 | 47% | two whole `Expr`s (`expr`, `prop`: 144 B); `prop` is a name, its IdentifierExpr fields (target/symbol/flow) unused; `declares`, `context`, the private/for-in union rare |
| CallExpr 128 | 1.10M | 140 | 56% | `enclosing_class`, `unique_symbol`, `declares` rare; 4 bools + 4-byte hole |
| InitializedBinding 200 | 342k | 68 | **82%** | lists of one (`vec_initialized_binding_reserve`) |
| import/export statement 400 + its 240-byte box | 123k + 124k | 49 + 30 | 70 / 80% | `parse_import_export_statement` |
| BinaryOpExpr 168 | 329k | 55 | 52% | |
| 144-byte box from `parse_success < parse_expr_right_side` | 334k | 48 | 45% | identify (ternary / as-type?) |
| one-element `VecExpr` (72) | 543k | 39 | 49% | call arguments of one |
| ObjectExprItem 184 (lists of 2-4) | 165k lists | 80 | 50% | |
| FunctionArg 256 | ~120k lists | 50 | **75%** | parameters |
| boxed TypeNode 72 (`parse_type_inner`) | 425k | 31 | 71% | |
| Statement 136 (lists) | 190k lists | 26+ | 55% | |
Also: the source text (159 MB, `cstr_resize`) and a 32 MB `ResolvedModule` hash table (224-byte values inline, triage).
Binder arena (`scripts/arenasites.sh`, vscode): `scope_new` 175 MB (Scope 136 B, 1.28M), `binder_new_symbol` 144 (Symbol
152 B), `symbol_add_decl` 68 (SymbolDecl 72), `symbol_table_insert` 50, `scope_rare_new` 21, flow nodes 46.

Per-corpus baseline (K1, `--time` resident after load+parse / after names, `/usr/bin/time` peak MB): tsc 109 / 151 / 225,
webpack 237 / 323 / 545, vue 120 / 164 / 342, effect 203 / 280 / 704, excalidraw 153 / 207 / 379, **vscode 1917 / 2517 /
4403**. Bun's whole vscode run is 1.56 GB at 4 threads (measurements.md): the struct work below doesn't get us there.
That would take 32-bit node references (indices or offsets into a per-file arena, what Bun's Zig port does), which
stage 2's arenas make possible but which remains out of scope (vision.md decides if it's ever worth it).

## Stage 1: the struct diet (one session)
Plan 43's pattern, which worked: rare fields behind one owned side struct (`*_rare()` reads a shared all-null static,
`*_rare_mut()` allocates on first write), bools as bit fields packed into holes, optional children as null pointers.
In order of zero bytes:
1. **PropertyExpr** (-145 MB of zeros): first find what reads `prop` as a full Expr (walkers, the types/symbols
   baselines print a type and symbol for the name, the checker caches `lazy_type` on it). If the name can be a small
   struct (Span + LazyType + symbol, ~32 B) behind accessors, PropertyExpr drops to ~120 B; `declares`, `context` and the
   private/for-in union into a rare part (~96 B). Measure who sets `context`/`declares` on vscode before choosing.
2. **CallExpr** 128 -> ~96 (`enclosing_class`, `unique_symbol`, `declares` rare; plan 60 measured ~-17 MB).
3. **InitializedBinding** (82% zero) and **FunctionArg** (75%): look at their layout with `ptype /o` and move what is
   zero for ordinary `const x = e` / `(a: T)` behind a rare part.
4. **Import/export statements** 400 + 240 (70-80% zero): different shapes in one struct; split by form or rare part.
5. **IdentifierExpr 40 -> 32** (Expr 72 -> 64, ~8M Exprs: ~-65 MB): one of target/symbol/flow/the union goes (plan 60
   stage 2 has the options: `target` only differs from `symbol` for aliases; `flow` could share the union).
6. **Binder**: Scope 136 (1.28M on vscode: count how many are empty walk-only scopes that survive), Symbol 152,
   SymbolDecl 72: rare parts, expected ~-100 MB on vscode.
Use `scripts/memtrack.sh --by sizef` and the zero scan after each item (scan.py's Span column is stale since Span went
to 16 bytes: fix or drop it). Stop items whose readers make the change a churn of hundreds of sites for < 15 MB.

**Target**: vscode resident after load+parse 1917 -> **<= 1650 MB**, after names 2517 -> **<= 2150**, peak 4403 ->
**<= 4000**; the other corpora down in proportion (compare as a set: peak moves +-30 MB with where an intern table
grows). Instructions (`scripts/instrcount.sh`) within +0.5% on every corpus.

## Stage 2: per-file parse arenas (one session, after an audit that may stop it)
### 2a. Audit (first hour; write the findings into this file and stop if any answer is "no")
- **Who frees or replaces AST memory after parsing?** grep the 150 `*_drop(` calls in parser.c (failure paths), and
  outside the parser: the binder/resolver mutating AST vecs or boxing into nodes (`function_impl_rare_mut` 15 MB and
  `unused_register` 39 MB are malloc'd during resolve_names on vscode), JSDoc application, `function_ctx_leave`, the
  checker. Each must either allocate in the file's arena (only if one thread owns the file at that time: resolve
  threads walk whole files, checkers don't) or keep using malloc with an owner that frees it.
- **Speculation**: `parser(&p)` / `parse_fail(&p)` nest strictly, so a mark taken in `parser()` can rewind the arena in
  `parse_fail()`, *if* nothing allocated inside a failed child survives it (shared error lists, the fail cache and
  `assigned_name`-style writes into outer nodes are malloc'd or plain stores: check). The 330 MB freed outright on
  vscode is the upper bound of what rewinding must recover; without rewind it stays as arena waste.
- **Lists**: built on a per-parser scratch stack (inner lists finish before outer ones, so it stays LIFO) and copied
  out exact into the arena (removes `list_push`/`shrink_list()` and their 1465 MB of realloc churn). Find lists that
  are pushed to after their parse function returns (they need a capacity or stay malloc'd).
- **Strings in nodes** (`cstr`: cooked string literals, template texts, `_cstr_init` 17 MB): arena slices
  (`csview`) are fine for readers that never mutate; count the mutating ones.
### 2b. Build
`Arena` in each `SourceFile` (the binder's `arena_alloc` with its huge-page `madvise`, chunk size tuned for small
files: ~11k files on vscode, many tiny); parser boxes (`c_new`/`c_safe_memcpy` sites, ~115) and lists through it;
drop functions keep only the non-arena frees; `drop_source_files()` becomes an arena drop per file. Then measure
`TTTT_NO_REEXEC=1` against the re-exec on vscode, webpack, effect and tsc: if the whole run is within ~2% wall,
delete `reexec_with_huge_pages` (the checker's remaining malloc users, intern tables and LazyMemo pages, may need their
own `madvise`d mmap first). Delete `shrink_list()`'s hacks.md entry with it.

**Target**: `parse_file` instructions -10% on webpack (callgrind `--toggle-collect=parse_file`), vscode K1 parse wall
<= 1.75 s without re-exec, `-b` on tsc-src faster by the drop time, peak RSS not above stage 1's.

## Validation (both stages)
Suite (`scripts/fasttest.sh --summary`: 0 lost), `scripts/oracles.py`, `scripts/threadcheck.sh` and
`scripts/checkerdiff.py` (byte-identical outputs: these are layout changes only), valgrind clean on a few suite tests
(`scripts/test.sh -f NAME -d`), `scripts/instrcount.sh`, the K1 table above rerun into measurements.md.

## Not doing
- An index-based / 32-bit-reference AST (above), lazily instantiated members: architecture-wide, tsgo does neither.
- Freeing per-file data after checking: declarations are read across files until the end.
