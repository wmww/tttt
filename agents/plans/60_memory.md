# Plan 60: memory diet, round 2 (checker side tables and structs, then the AST)

Written 2026-10-12 by todo entry 3 (planning), which also landed the first lever (below). Numbers are this machine's
(4 vCPU, GCC 15.2), `--time` and `/usr/bin/time` peak RSS, tsgo = the pinned 7.1.

## Where we stand
| corpus | tttt K1 RSS / bound / arena MB | tttt K4 RSS / arena sum | tsgo 1 thread "Memory used" | Bun-4 RSS (other machine) |
|---|---|---|---|---|
| tsc | 254 / 165 / 63 | 320 / 94 | - | 221 |
| type-fest | 797 / 61 / 695 | 920 / 806 | 3274 | 555 |
| valibot | 274 / 90 / 160 | 333 / 196 | - | 158 |
| webpack | 596 / 357 / 185 | 685 / 236 | 805 | 356 |
| zod | 464 / 132 / 287 | 551 / 360 | - | 325 |
| drizzle | 470 / 144 / 286 | 665 / 442 | 760 | 347 |
| vue | 369 / 179 / 149 | 429 / 179 | 457 | 224 |
| effect | 767 / 310 / 401 | 890 / 467 | 1065 | 307 |
| excalidraw | 416 / 230 / 155 | 527 / 228 | - | 266 |
| vscode | 5220 / 2812 / 1818 | 6017 / 2132 | 5941 (parse+bind alone: 2908) | 1557 |
"bound" = resident after name resolution (AST + binder + libs). tttt is at 0.6-0.9x tsgo everywhere, Bun at ~0.45x
tsgo: Bun is 1.5-3x below us at 4 threads. Its gap is mostly representation (Bun is a Zig port of tsgo; our AST
is tsgo-sized: vscode parse+bind 2.81 GB vs tsgo's 2.91). **Closing all of it would take an index-based compact AST,
which is not worth it now** (correctness is still the main work, and our speed lead is 1.1-2.7x over Bun at equal
threads). This plan takes the struct-level waste that is left, aiming at **-20% peak RSS** on the big corpora.

### Done in the planning session (e043e9e2)
type-fest's peak was IntRange's tuple targets: each target of arity n made n synthetic type parameters (Ty + 96-byte
TyParamState), each resolved tuple reference n element Props with a 120-byte PropDecl for `name_is_value` alone.
Now element props share `ty_prop_decl_value_name` and all tuple targets share one checker-wide list of element type
parameters (`Checker.tuple_params`). type-fest K1 arena 1906 -> 695 MB, RSS 2057 -> 797 MB (Bun 835 at defaults),
types 4.78M -> 217k. Every oracle unchanged.

## Where the rest goes (vscode K1, 5.2 GB)
- **AST** 2.17 GB live malloc after parsing (159 MB of source text; 50% all-zero words, 19% non-null Spans):
  `PropertyExpr` boxes (216 B, 1.68M: 362 MB), `span_make` 144-byte boxes from `parse_expr` (1.09M: 158 MB),
  statement sequences 150 MB, `parse_success` 83 MB, call-argument `ObjectExprItem`s 72+49 MB (224 B),
  `InitializedBinding` 216 B (70 MB), `BinaryOpExpr` 200 B (66 MB). `Expr` is 88 bytes and embedded everywhere
  (Span 24, tag+bits 8, `paren_before/after` 8, union 40 (IdentifierExpr), LazyType 8).
- **Binder** ~0.6 GB: `scope_new` 174 MB (Scope 136 B), `binder_new_symbol` 151+19 (Symbol 160 B), `symbol_add_decl`
  83 (SymbolDecl 88 B), `symbol_table_insert` 50, `scope_rare_new` 21.
- **Checker malloc** (outside the arena, per checker, so 4x at K4): `lazy_type_evaluate_inner`'s LazyMemo pages
  **222 MB** (36k zeroed pages of 24-byte memos), the intern tables **202 MB** (3 tables, 16-byte entries, load
  0.375-0.75, a grow holds old and new at once), relation caches.
- **Checker arena** 1.82 GB, diffuse: types 290 MB (2.56M, ~113 B), intern keys 244 MB (KEY_STRUCTURAL 75: object
  literal keys of 23.5 words; KEY_CALL_RESOLUTION 27), object literal Props with a full PropDecl
  (`ty_object_literal_alternative`) **212 MB**, instantiated Props (`instantiate_members`, 56 B) 129+29 MB,
  `CallResolution` (112 B) **127 MB**, `builder_finish` member arrays 104+50+40, `ty_list_copy` duplicate member lists
  44, plain signatures 44 + instantiated signature copies 31 (Signature 176 B, its spans copied into each instance).
  effect adds `namespace_export_prop` 49 MB (a 176-byte Prop+PropDecl per namespace export, only `symbol`,
  `name_span`, `ctx` used); drizzle `expand_mapped` + its `prop_decl_rare_mut` 27 MB.
- **K4 duplication**: arena sum K1 -> K4 +17% on vscode but +55% drizzle, +47% excalidraw, +49% tsc (lib and
  shared declaration types made by every checker). Not in this plan (bugs/perf_leads.md "Memory": a cost model for
  deal_files() or a shared lib checker).

## Stage 1: checker side tables and structs (one session)
Each item: measure the site before and after with `scripts/arenasites.sh` / `scripts/memtrack.sh --phase typecheck`,
keep it only if output stays byte-identical (suite, `scripts/oracles.py --quick`, checkerdiff).
1. **LazyMemo 24 -> 16 or 8 bytes** (`src/lazy_type.h`): five bools and `hits` into one byte; `context` is it set on
   most expressions? If rare, move it to the checker's links keyed by the slot and keep `evaluated` + flags in 8 bytes
   (low pointer bits or a parallel byte page). Measure how dense the pages are first (LAZY_PAGE_BITS): if most pages
   are sparsely touched, smaller pages beat smaller memos. Expected vscode -75..-150 MB per checker.
2. **Intern tables**: 16-byte entries at 37-75% load. Options: 8-byte entries (32-bit hash fragment + 32-bit index
   into a per-table block vector, the block pointer looked up), a higher max load with robin hood probing, or growth
   by 1.5x. Expected -50..-100 MB per checker on vscode; keep `intern_find`'s instruction count (callgrind).
3. **Object literal and namespace export props**: an audit of which PropDecl fields each creator sets (members.c's
   `prop_new_declared` callers), then either a compact declaration for literal props or PropDecl 120 -> ~64 with the
   declared-member-only fields (`node`, `lazy`, `overloads`/`overload_count`, `ctx`, `asserted`) moved into a second
   side struct. Expected vscode -100 MB, effect -25 MB.
4. **CallResolution 112 -> ~56**: `failure_sig`/`failure_mapper`/`param_types` and the ErrorCtx only on failure or
   when not empty (a rare pointer). vscode -65 MB.
5. **Instantiated Prop 56 -> 48** (plan 42's idea: `decl` is always `ty_prop_decl_none` for instantiations; a flag
   instead of the pointer means every `prop->decl` read goes through an accessor, ~300 sites: maybe a sub-agent's
   mechanical job) and **Signature's declaration fields** (two Spans, class_instance, declared_by) behind a pointer so
   instantiated signatures stop copying spans. vscode -80 MB, drizzle/effect -15 MB each.
6. Optional: `ty_list_copy`'s duplicate of interned member lists (the key already holds the ids: drizzle 29, effect 34,
   vscode 44 MB).
Validation: vscode K1 peak 5.2 -> <= 4.7 GB, K4 6.0 -> <= 5.3; drizzle/effect K1 arena -10%; instructions flat
(`scripts/instrcount.sh` within 1%; smaller structs usually pay for themselves).

## Stage 2: the AST (one session, sub-agents for the mechanical parts)
1. Count Exprs and Spans per corpus first (a counter in a debug build, or memtrack's scan by struct size) to confirm
   the estimates below.
2. **Span 24 -> 16**: `{ char const* buf; u32 size; u32 file; }` with files indexed (the program's file vector)
   instead of `SourceFile*`, `.sv` reads through `span_sv()`. ~650 `.sv` uses and every `.source` use: mechanical,
   one sub-agent, then the suite. 19% of AST bytes are non-null Spans: vscode -140 MB direct, more in null ones.
3. **Expr 88 -> 64**: Span 16, `paren_before/after` out of the node (a `parenthesized` bit + a per-file side table:
   parentheses are rare), IdentifierExpr 40 -> 32. Expr is most of the expression bytes (two inline in every
   PropertyExpr and BinaryOpExpr): estimated vscode -250 MB.
4. The `span_make` 144-byte boxes from `parse_expr` (1.09M on vscode): find what they are (CallExpr is 144) and whether
   CallExpr's fields are mostly zero (memtrack's zero-word scan per size).
Validation: vscode "load+parse files" resident 2174 -> <= 1750 MB, webpack bound 357 -> <= 300; suite, every corpus
byte-identical, parser instructions flat or better (nest is half parser).

## Stage 3 (after the next insights, optional): the binder
Scope 136 B (vscode 1.28M: 174 MB), Symbol 160 B (~1M: 151 MB), SymbolDecl 88 B: plan 43's pattern (rare fields
behind a side struct, spans as offsets). Expected vscode -100 MB.

## Not doing
- An index-based compact AST or lazily instantiated members (resolve a property by name without materializing the
  instantiation's whole member list): the levers that would reach Bun, both architecture-wide; tsgo does neither.
- Freeing per-file data after checking: declarations are read across files until the end.
