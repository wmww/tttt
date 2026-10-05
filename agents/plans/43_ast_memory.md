# Plan 43: AST and binder memory (nest and webpack below tsgo's peak RSS)

## Goal
nest and webpack are the two corpora whose peak RSS at one checker is above single-threaded tsgo's (measurements.md,
10-05: nest 254 vs 212 MB, webpack 396 vs 303). Both are parse + name resolution heavy. Target: nest <= 200 MB and
webpack <= 300 MB at `--checkers 1 --threads 1`, no corpus worse, `./instrcount.sh` not worse.

## Measurements (10-05, planning session; tools in `43_prototypes/`)
`memtrack.c` is an LD_PRELOAD live-heap tracker (size, `malloc_usable_size`, caller) dumped at the end of each phase
through `main_hooks.patch`; `an.py`/`gcd.py` group the live blocks by allocating function and size, `scan.py` counts in
each live block the non-null `Span`s (a word pointing at a `SourceFile`, then a pointer, then a small length) and the
all-zero words. Binder and checker arenas are mmapped (invisible to it): `./arenasites.sh` covers those (copy it with
another `B=` build directory if other worktrees use `/tmp/tttt-arenasites`).

Where the peak comes from (`--time` resident; heap live at the end of name resolution; arenas by arenasites):
| | nest | webpack |
|---|---|---|
| resident after parse / names / end | 172 / 233 / 254 | 217 / 285 / 396 |
| malloc heap live (requested) | 172 MB in 354k blocks | 217 MB in 472k blocks |
| of it source text (`cstr_reserve`) | 13 | 12 |
| of it `line_starts` | 10.4 (4 KB minimum per file) | 5.6 |
| binder arena (names phase) | ~60: scopes 26, symbols 17, table entries 9, `SymbolDecl`s 7 | ~68: scopes 29, symbols 18, decls 8, tables 7, flow 5 |
| checker arena | 13 | 79 |

**Malloc overhead is not the problem.** perf_leads.md said ~100 MB of malloc overhead on webpack: that was massif's
`mem_heap_extra_B`, which is wrong for glibc. Chunks (`usable + 8`) minus requested is 4.3 MB on nest and 5.8 MB on
webpack (~3%), and `mallinfo2` has no free fragmentation (fordblks 0). Per-file node arenas would save ~5 MB: they are a
*time* lever (glibc's malloc/free/realloc are 11.7% of nest's instructions, callgrind), not a memory one.

**The AST is two thirds zeros.** 66% (nest) and 62% (webpack) of the live heap's 8-byte words are all-zero after name
resolution: null pointers, empty vecs, null spans, inline optional children that aren't there. Non-null Spans are 12% /
16% of the bytes (840k / 1.46M spans). The nodes are fat because:
- **Unions sized by their rarest variant**: `InterfaceMember` is 584 bytes because `key` holds the mapped-type (304) and
  index-signature (272) variants inline while a plain `name: T` needs ~32; `Statement` (280) carries a 208-byte union
  sized by `TypeDecl`/`decl` while most statements are an expression or a pointer; `Expr`'s union is 80 bytes for
  `IdentifierExpr` while 14 of its 19 variants are one pointer.
- **Optional children inline by value**: `InitializedBinding` (440) holds `Expr value` (144) that a parameter almost
  never has, `Binding` (232) a `TypeNode explicit_type` (72) and its span, `InterfaceMember` a `write_type` (72),
  `FunctionArg` (584) `jsdoc_span`, `question_span`, `rest_span` (mostly null).
- **Language-mode and checker-side fields in every node**: `FunctionImpl` (744) has ~150 bytes of JSDoc/JavaScript-only
  fields, class-only and flow fields; `PropertyExpr` (400) has 11 context pointers the resolve walk sets
  (`enclosing_class`, `this_function`, `containing_function`, `flow_container`, `for_in_loop`...), mostly the same for
  every access in a function; `IdentifierType` (232) has import-type, JSDoc and namespace fields for what is mostly a
  bare `Foo` or `Foo<T>` (90k of them on nest: 21 MB).
- **Duplicated spans**: `Expr` has `inner_span` and `outer_span` (48 bytes); on webpack 948 of 274k expressions are
  parenthesized. An identifier's `IdentifierExpr.name` is also `inner_span` (`expr_identifier()`), 96k of them.
- **List slack**: the first push reserves 4 elements; `shrink_list()` gives the rest back in some parsers but not in
  `if` chains (a 1120-byte statement vec and 576-byte condition vec per `if`), type parameter and argument lists,
  import/export specifiers, unions/intersections, template substitutions, array literals, arrow expression bodies.

Biggest live blocks by struct (MB; counts from size patterns per allocating function):
| | nest | webpack |
|---|---|---|
| `InterfaceMember` lists (584 each) | 22.8 | 18.7 |
| `FunctionArg` lists (584) | ~23 | ~22 |
| `IdentifierType` (232) | 21.4 (90k) | 16 (68k) |
| `FunctionImpl` (744; method signatures, function types, functions) | ~14 | 14.4 |
| `ClassItem` lists (920) | 10.6 | 7.1 |
| `Statement` lists (280) | ~13 | ~30 |
| `PropertyExpr` (400) | 1.2 | 20.0 (50k) |
| `CallExpr` (232), `BinaryOpExpr` (328) | small | 7.5, 6.4 |
| `InitializedBinding` (440) | 1.7 | 8.0 |
| `Expr` lists (args, arrays, conditions, templates) | small | ~12 |
| generic decls / type args / import items / union lists | 5.6 / 6.3 / 6.3 / 6.7 | 2.3 / 3.5 / 1.2 / 4.0 |
`Scope` is 296 bytes (~90k scopes on either corpus), `Symbol` 224, `SymbolDecl` 96. `Expr` 144 is embedded by value
everywhere (274k on webpack: 39 MB of the table above).

## Prototypes
- **Exact `line_starts`** (committed with this plan, `source_file_make()`): RSS nest 252 -> 244, webpack 398 -> 393
  (best of 3, MiB), instructions unchanged, suite and corpora identical.
- **`shrink_list()` everywhere a list is finished** (`43_prototypes/shrink_lists.patch`: `if` chains, type parameters,
  type arguments, import items, unions/intersections in `parse_type()`, templates, arrays, arrow bodies): together with
  the above RSS nest 252 -> 231, webpack 398 -> 374; suite 0 lost, all corpora byte-identical. **But instructions +1.1%
  on nest, +0.4% webpack, +0.3% tsc** (callgrind: glibc's realloc splitting chunks and merging the remainders,
  `_int_malloc`/`realloc`/`_int_free_merge_chunk`), so it was not committed.

## Levers, ranked (estimates are live bytes: count x bytes saved)
| # | lever | nest | webpack | effort | notes |
|---|---|---|---|---|---|
| 1 | Finished lists at exact size (the patch) | -21 | -24 | small | costs 0.3-1% instructions as is; free with lever 7's scratch lists |
| 2 | Declaration structs: box rare variants and optional children (`InterfaceMember` 584 -> ~200, `FunctionArg`/`InitializedBinding`/`Binding` -> ~250, `FunctionImpl` 744 -> ~300 with JSDoc/JS/class/flow side structs, `ClassItem` 920, `IdentifierType` 232 -> ~80 (simple vs full, the `PERF:` comment on it)) | -50 | -40 | medium, per struct | `.d.ts`-heavy corpora (nest, drizzle, vue) gain most |
| 3 | `Expr` 144 -> ~96: `outer_span` only when parenthesized (a flag plus side storage or start/end deltas), drop `IdentifierExpr.name` (= `inner_span`), rare identifier fields out | -3 | -15 | medium (`outer_span`: ~290 uses behind an accessor, `identifier.name`: 70) | cascades into every struct embedding `Expr` |
| 4 | `Statement` 280 -> ~170 (box `TypeDecl`, `NamespaceDecl`, `labeled`; follows 3) | -4 | -10 | small-medium | |
| 5 | `PropertyExpr` 400 -> ~200: the 11 resolve-time context pointers become one pointer to a context shared by every access in the same walk context (like `AccessSite`) | -1 | -8 | medium | |
| 6 | Binder: `Scope` 296 -> ~120 (walk-only state in the walker, not in every scope), `Symbol` 224 -> ~150 (rare alias/module/JS fields to side structs), `SymbolDecl` 96 | -20 | -20 | medium | first check what keeps function/block scopes alive after the walk (`SymbolDecl.block`); if nothing else, they need not persist |
| 7 | Per-file node arenas with lists built in a scratch stack and copied in exact | -5 | -6 | large | a time lever (malloc is ~12% of nest's instructions); the parser frees on backtracking (cumulative malloc during parse is 2.3x what stays live), failed attempts would leak into the arena: build lists in scratch, measure the leak |
| 8 | `Span` 24 -> 16 (pointer + u32 length + u32 file index; `.sv` through an accessor, ~650 uses, `.source` ~250) | -10 | -15 | large, mechanical | also shrinks `Prop`, `Signature`, `SymbolDecl`; only if 1-6 aren't enough |

1-6 add up to ~-100 MB on both corpora (nest ~150, webpack ~290), below tsgo on both. Stages 2-5 also remove zero
words that each walk touches (cache misses in name resolution, ~1 IPC per perf_leads.md "Time"), so they should not cost
instructions; lever 1 does, so land it with or after a struct diet that pays for it, or accept <= 1% explicitly.

## Recommended staging
1. **Struct diet, declarations first** (lever 2), one struct per commit, each measured: `InterfaceMember`,
   `IdentifierType`, `FunctionArg`/`InitializedBinding`/`Binding`, `FunctionImpl`, `ClassItem`. Pattern: what's rare
   goes behind one owned pointer (`InterfaceMember.key.index`/`.mapped`, `FunctionImpl`'s JSDoc block, an
   `IdentifierType` import/namespace extension), absent optional children become null pointers rather than empty
   inline nodes, bools become bit fields. The parser fills them; drop functions free them; nothing else should notice
   behind accessors. Re-run `gdb -batch -ex 'ptype /o struct X' <-g build>` to see holes and union padding.
2. **Lists at exact size** (lever 1, the patch), in the same session as stage 1 so the combined instrcount is not
   worse.
3. **Expr and Statement** (levers 3, 4), then **PropertyExpr context** (lever 5).
4. **Binder** (lever 6).
5. Re-measure. Only if a corpus is still above tsgo: `Span` (lever 8). Node arenas (lever 7) belong to a parser *time*
   plan (with perf_leads.md "Parser"), not to this one.

Not recommended: a tsgo-style rewrite of the AST into index-based per-file node pools. Levers 1-6 get under tsgo with
local changes; revisit only if the parser's time work wants it anyway.

## Validation
- Per stage: `./fasttest.sh --summary` 0 lost (save with `-s`), the corpora's output unchanged (`./realworld.py --corpus
  X` for all, or md5 of `tttt --keep-going -p` against the previous build), `./instrcount.sh` not worse than the
  session start beyond layout noise (~0.2%; compare per function with callgrind when in doubt).
- Peak RSS best of 3, `/usr/bin/time -f %M ./release/tttt --checkers 1 --threads 1 -q --keep-going -p <corpus>`
  (KiB / 1024) on all ten corpora, and the K=4 default; record in measurements.md next to tsgo `--singleThreaded`
  (nest 212, webpack 303). Done when nest <= 200 and webpack <= 300 with no corpus worse.
- `memtrack` before/after: the zero-word share (62-66% now) is the diet's progress meter; aim below 40%.
