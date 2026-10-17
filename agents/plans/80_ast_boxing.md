# Plan 80: AST boxing, spans, source text (the mechanical memory items)
Brief by the insights of 2026-10-16 (after the eighteenth window; vision.md "Memory target"). One session (entry 5):
main plus Opus agents in worktrees for the mechanical parts (the compiler finds every site once a field is renamed or
boxed). Gate per item: **>= 50 MB on vscode after names, or trivially cheap**; measure `sizeof` and
`scripts/memtrack.sh -p corpora/vscode/src` (after parse, after names) before and after each. bugs/perf_leads.md
"Memory" has the site counts.

1. **Expr 72 -> 40**: box `IdentifierExpr` and `Value` (146 `.identifier` and 78 `.literal` sites; the identifier box
   carries its name span, since Exprs move while vectors grow); the union becomes one pointer; the rare union's 21k
   users to a side table if cheap. Net ~-100..-140 MB. Measure instructions: one pointer chase per identifier.
2. **Other fat unions**: TypeNode 72 (the literal Value + span variant; copied by value, so audit owners),
   ObjectExprItem 224 -> ?, Binding element-only fields behind a pointer (37k of 700k), PropertyExpr 128 -> 112,
   ModuleRef 184 (three failure-only strings). Take what passes the gate.
3. **Span 16 -> 8**: `Span` is `{ char const* buf; i32 size; u32 file; }`. An AST node can find its file from its
   own address (`ast_owns()` / the per-file arena ranges): a `{ u32 start; u32 len; }` span for AST-resident nodes,
   the text through the file. Checker-side copies (Prop, Signature, SymbolDecl, TypeNodes copied by value) keep a
   12-byte `{ u32 file; u32 start; u32 len; }` or stay 16. Estimated 60-120 MB; ~650 `.sv`/`.buf` sites, mechanical
   through an accessor. The human suggested a context struct passed everywhere as the alternative for the file;
   address lookup needs none. Decide by the measured saving after items 1-2 (every 8 bytes count in the arena).
4. **Source text**: `fs_read_file()` -> `mmap(MAP_PRIVATE)` of the file (program.c's three sites), a malloc copy
   only where the parser needs a terminator the mapping can't give (a page-multiple size; check what `at_eof()`
   relies on). It cuts anonymous memory (~170 MB on vscode), not the RSS the benchmarks print; do it because it is
   cheap and evictable, say so in measurements.md.
5. **Per-file LazyType slots** if plan 78 hasn't taken them (per-body is better; this is the fallback).

Validation: every oracle byte-identical, `scripts/instrcount.sh` flat (react and nest are the sensitive ones), the
after-names heap table in measurements.md "Plan 80".
