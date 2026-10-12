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

## Stage 1: done (2026-10-12, todo entry 4)
K1 peak (`/usr/bin/time`): tsc 256 -> 244, type-fest 800 -> 711, valibot 283 -> 276, webpack 604 -> 573, zod 464 ->
446, drizzle 473 -> 468, vue 370 -> 357, effect 771 -> 743, excalidraw 421 -> 400, **vscode 5189 -> 4839** (-6.7%; the
target was 4.7 GB). K4: vscode 5966 -> 5438 (target 5.3), tsc 323 -> 293, type-fest 921 -> 823, effect 891 -> 828,
excalidraw 529 -> 494, the rest -14..-38 MB. Instructions: measurements.md "Plan 60 stage 1". What landed:
- **LazyMemo 24 -> 12 bytes**: pages are 96% used on vscode (10M memos), so the memo size counts, not page density.
  `evaluated`/`context` are type ids, looked up in a checker-wide id -> type table (`LazySlots.types`, two levels, filled
  by ty_alloc()); flags are bit fields. -107 MB vscode, +0.3% instructions (the lookup).
- **CallResolution 112 -> 48** (failure candidate and diagnostics behind `rare`), **PropDecl 120 -> 96** (`overloads`,
  `overload_count`, `name_type` into PropDeclRare; `asserted` stays: 13% of vscode's literal props have one), **Members
  96 -> 72** (`members_rare()`: expansions, generic bases).
- **Interned unions/intersections/references** key their lists by pointer and point their TyLists into the key
  (`intern_key_list()`) instead of copying: vscode arena -44 MB.
- **Node links** (`ty_link_get()`/`ty_link_set()`, `Checker.node_links`): 11 two-word key kinds of `links` (call
  resolutions, function types/return memos, flow reachability, literal contexts and anchors, fresh literals...) in a
  table with the key inline (16-byte entries, no arena block), growing by half. vscode: links 5.58M -> 2.63M entries,
  3.26M node links.
Not done, with why:
- **The rest of `links`** (2.6M entries on vscode, 4M slots + 24-byte blocks): its kinds hand out slots that callers keep
  across further interning (`void const** slot = intern_slot(...)`, ~50 sites), which an inline-key table can't. A kind
  moves to node links once every use of it is a plain get/set: worth it for the next big ones (KEY_FLOW_CALL 400k and
  KEY_FLOW_EXPR 245k are three words: a second inline table with two-word subjects, or pack (node, id)).
- **8-byte intern entries** (hash fragment + compressed block pointer): growing has to rehash from the key blocks, a
  cache miss per entry (vscode ~12M over the run). Not worth it.
- **1.5x growth for intern tables**: +1.2% zod instructions (longer probes, more rehashes) for a few MB either way; only
  node links grow by half. Peak RSS moves +-30 MB with where the last grow lands (vscode was 4866 vs 4892 between two
  growth policies): compare corpora as a set, never one.
- **Instantiated Prop 56 -> 48** (no `decl` pointer): mapped type props get their PropDecl after the Prop
  (`expand_mapped()`'s `prop->decl = arena_new(...)` paths), so it can't be found next to the Prop by a flag. Would need a
  side lookup or allocating mapped props declared up front: ~-25 MB vscode.
- **Signature's declaration fields behind a pointer**: saves 72 bytes per instantiation but costs 8 per declared one,
  vscode ~-11 MB. Not worth the churn alone.
- **A compact declaration for object literal props** (1.2M on vscode, Prop 56 + PropDecl 96): PropDecl's sources
  (`node`/`lazy`/`function`/`symbol`) as one tagged pointer would take PropDecl to ~72, but `decl->node` & co have
  ~250 readers. -30 MB vscode. Same for `namespace_export_prop()` (effect).
- Unexplained: `/proc` VmRSS sampled at the end of typecheck reads ~116 MB below `/usr/bin/time`'s peak on vscode, and
  strace shows no allocation after typecheck. Probably the kernel's per-CPU RSS counters lagging; use `/usr/bin/time`.

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
