# Plan 78: deferred function bodies (the memory lever)
Brief by the insights of 2026-10-16 (after the eighteenth window), from the human's decision that memory comes first
(vision.md "Memory target"). **Entry 1 of the window is a planning session that turns this brief into stages with a
prototype gate; entries 2-4 execute them.** Numbers are vscode's at one checker (plan 67/75 census): AST ~1370 MB
(8.2M Exprs alone 590 MB), binder arena ~505 (flow nodes ~110, scopes ~67, symbols 162), text ~170, program ~2.2 GB
of a 2.55 GB peak; bun's whole run is ~1.4 GB.

## The idea
Function bodies are most of the AST and most of the binder's flow nodes and scopes, and almost every body is needed
by exactly one checker: the one checking its file. Parse a body to its brace span only, materialize it (parse, bind,
resolve) when a checker needs it, drop it with the file when that checker is done. The parse-time peak becomes
declarations + text + binder-without-bodies; the check-time peak adds the in-flight files' bodies per checker.
Expected: vscode's program ~2.2 -> ~1.1-1.2 GB; K1 peak near bun's 1.4 GB; at K24 the checker budgets dominate
(plan 79). It is also the restructuring a real watch mode would need (plan 76 W3: resolve scoped per body).

## What exists
- `skip_typed_body()` / `skip_balanced_braces()` (parser.c): `-b` brace-skips the bodies of annotated functions of
  referenced projects' sources (`SourceFile.skip_typed_bodies`), never resolved. It must become: skip every body
  when the skip is unambiguous, else parse (regex vs division, templates with nested braces, JSX text with braces,
  `}` in comments and strings: the skipper must be exactly as right as the parser or give up).
- Per-file AST arenas (`ast_alloc.h`, plan 63): a drop is O(1) per arena; a materialized body in an arena of its
  own (owned by the materializing checker's thread) drops alone. Dropped addresses are never reused, so stale
  `links`/memo entries keyed by node address can't alias; they must still be purged or they leak until recycle.
- The resolve walk shards per file (plan 45); JavaScript files replay assignment declarations serially.
- LazyType slots are a process-wide counter (`lazy_slot_new()`, hacks.md): per-body slot ranges released on drop
  would free the memo pages too (the ~6% RSS of sparse pages, and the counter watch/-b would exhaust).

## Which bodies
Every function body at any nesting, deferred as a unit from the outermost deferred one: declarations, methods,
accessors, constructors, function expressions, block-bodied arrows. Not: class bodies (member signatures are
declarations), module-level statements and initializers, expression-bodied arrows. JavaScript files stay eager at
first (expandos and `this.x =` inside bodies are declarations). Whether a materialized body re-defers its nested
functions is a measurement (a parent is checked whole, so probably not).

## What the planning session must answer (the prototype gate)
1. **The upper bound**: skip every body at parse (no materialization, `--listFilesOnly`-style run) and measure RSS
   after parse and after names on vscode, effect, webpack, drizzle; and how many bodies the skipper gives up on.
   **Gate: the program drops >= 600 MB on vscode**, else stop and say so.
2. **Cross-file demand**: instrument the checker to count bodies read while checking *another* file (unannotated
   return types, `this` types, contextual signatures through inferred returns, declaration emit's pseudo types):
   how many, which kinds, how often the same body from several checkers. Decides whether bodies demanded by other
   files are kept after their file's check or re-materialized (first version: re-materialize, measure).
3. **Per-body bind and resolve**: what the body-level walk needs from the file (the enclosing scope chain at the
   function, `FunctionCtx` flags, strict mode, the flow container), what escapes a body in TypeScript (nothing
   hoisted; `arguments`? labels? `suggestion_count_order()`'s "Did you mean" budget already models deferred bodies),
   where body-local symbols/scopes/flow nodes go (a body-owned binder arena, dropped with it), how errors from a
   checker thread reach the file's ErrorCtx (per-body lists merged, or a lock; output is sorted per file anyway)
   and the once-per-file memos (`Error.once_key`).
4. **The checker's entry points**: every reader of `FunctionImpl.body` goes through an accessor that materializes
   (the compiler finds them once the field is renamed); the file's check materializes all its bodies up front
   (simplest) or lazily per function (measure); the after-check pass (unused, declaration diagnostics, emit) runs
   before the drop; `TTTT_CHECK_ONLY`, the suite harness (symbols/types traversal: materialize everything), `-b`'s
   `skip_typed_bodies` folded in.
5. **Stages** (three execution sessions): (1) skip + materialize-at-check with whole-file semantics, byte-identical
   output everywhere, instrcount; (2) drop after check, per-body slots and memo pages, the memory table; (3)
   cross-file demand policy, JavaScript, `-b`, the harness, kdiff/recycle oracles, perfsweep.

## Validation
Every oracle byte-identical (`scripts/oracles.py`, `scripts/kdiff.py`, `scripts/checkerdiff.py`, `threadcheck.sh`,
scenarios, dtsdiff), the suite 0 lost; `scripts/instrcount.sh` within +3% on every corpus (the perf session of the
window pays it back); measurements.md "Plan 78": RSS after parse / after names / peak at K1 and T4 on vscode, effect,
webpack, drizzle, type-fest before and after, next to bun's.
