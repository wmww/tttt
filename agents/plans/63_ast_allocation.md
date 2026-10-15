# Plan 63: the AST's allocation (struct diet first, then per-file arenas)

**Folded into plan 67 (2026-10-14):** stage 1b below is plan 67 stage 2's list (after checker recycling, plan 67
stage 1); stage 2 (per-file parse arenas) stays here as a *time* lever, after plan 67 stage 2. The compact AST this
plan ruled out is costed in plan 67 (~-400..-500 MB on vscode, deferred to insights).

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

## Stage 1: the struct diet (done 2026-10-14, todo entry 6; targets missed, see 1b)
Plan 43's pattern: rare fields behind one owned side struct (`*_rare()` reads a shared all-null static, `*_rare_mut()`
allocates on first write), bools as bit fields packed into holes, optional children as null pointers. Done, vscode K1
MB saved after parse / after names (commits e681ab78..HEAD):
- **PropertyExpr**: a dot access's name is a Span (`property_name()`), not a 72-byte literal Expr with a malloc'd
  cooked cstr; the access is allocated short (`property_dot_new()`, 120 bytes, the 128 malloc class; a bracket one
  keeps its `index` Expr at 184). `declares`/private class/for-in loop in `PropertyRare`. The dot key's literal type is
  memoized in a LazyMemo slot (`name_slot`): recomputing it from the name (atom + literal interning, ~460 instructions)
  cost +1% instructions, memoized it's -0.3..-0.9%. -120 / -120.
- **CallExpr** 128 -> 112: `CallRare` (enclosing_class, unique_symbol, declares). -17.
- **ObjectExprItem** 184 -> 160: shorthand default, its `=`, JSDoc type in `ObjectExprItemRare`. -17.
- **Binding.explicit_type** boxed (`binding_type()`, `binding_set_type()`, `binding_take_type()`): 428k of vscode's
  700k bindings have no annotation (-56 each), 272k do (+24). -16.
- **PortStatement** 400 -> 288 (`PortStatementRare`; `target_path` moves into the ModuleRef instead of being cloned),
  **PortItem** 208 -> 192 (bools regrouped), **ModuleRef** 240 -> 112 (`ModuleRefRare`: failure paths/hints). -36.
- Binder: **Scope** 136 -> 96 (locals table allocated on first declaration, `scope_locals()`/`scope_locals_mut()`;
  flags as bit fields), **SymbolDecl** 72 -> 64. Names -23 (the arena shrank more than resident did).
- Resolve: per-file **unused candidates** trimmed after the walk and 32 -> 16 bytes. Names -24.

Result (K1, resident after load+parse / after names / `/usr/bin/time` peak MB, before -> after):
tsc 109/151/225 -> 104/144/219, webpack 237/323/545 -> 224/307/529, vue 120/164/342 -> 116/157/330, effect
203/280/704 -> 192/265/688, excalidraw 153/207/379 -> 146/197/363, **vscode 1917/2517/4403 -> 1724/2277/4158**
(targets were <= 1650 / 2150 / 4000). Output byte-identical (suite, threadcheck, checkerdiff, oracles).

### Stage 1b: what is left (in order of MB)
**Closed 2026-10-15 (plan 67 stage 2)**: the cheap items done there (PortItem, PortStatement, Statement,
ObjectExprItem: -67 MB); what is below is < 120 MB together and isn't scheduled (measurements.md "Plan 67 stage 2
items 2-3"). PropertyExpr: to drop a malloc class (128 -> 112) it needs 16 bytes; its name span is derivable from the
source text after the object, but ty/lower.c's synthesized accesses name keys from elsewhere.
- **Expr 72 -> 64** (the only lever of the size the targets need, ~8M Exprs on vscode, but malloc classes eat part
  of it: PropertyExpr stays in the 128 class, CallExpr 112 -> 104 and BinaryOpExpr 168 -> 152 drop a class, lists
  shrink 11%; estimate -50 MB at parse). The union is 40 because of IdentifierExpr (target, symbol, flow, 8 bytes of
  flags, an 8-byte union). Analysis this session: `flow` can't join the union (`this` needs this_function_, `x[i] = v`
  needs evolving_index_, any `x[k]` object needs element index: all need flow too); dropping `target` means
  recomputing `symbol_for_meaning()` (alias resolution) on hot checker paths. The workable design: `symbol` joins
  the union when it differs from `target` (aliases, the TS2301/TS2693 cases; a 2-bit kind says SAME / NONE / UNION /
  SIDE), and when the union is taken (an element object or extends entity naming an import) it goes to a per-file
  side table written by the file's resolve thread; `identifier_symbol()` reads it (54 readers). The union is filled
  in different orders (parser: module_ref_, extends_entity_; resolve: element index before the object's lookup;
  flow builder: evolving_index_ after it), so every writer goes through a setter that migrates. Also needs the
  flags to fit 8 bytes with the kind (they do: 26 bits of bools + 4 u8). A session of its own.
- **Binding element-only fields** (parent, parent_item/index, default_value, element span offsets: 32 bytes) behind a
  `BindingElement*`: 37k of 700k bindings are pattern elements. ~-18 MB, ~110 readers.
- **Symbol** 152 -> 144 (bools as bit fields, ~950k on vscode, -8 MB): `is_assigned`, `is_definitely_assigned`,
  `is_assigned_in_closure` are written by resolve threads and must stay whole bytes; `is_unresolved` (alias
  resolution) and `exported` (expando declarations) need an audit of which phase writes them first.
- **ResolvedModule** cache (one 32 MB hash table on vscode, 224-byte values with 7 cstrs): the failure hints behind a
  pointer, ~-10 MB; churn in module_resolution.c.
- Not worth it (measured): BinaryOpExpr (rare parts can't leave the 176 malloc class), Statement (sized by variable
  declarations, which would need a box each), ClassItem (sized by methods' inline FunctionImpl).

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
