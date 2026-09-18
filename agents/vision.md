# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal.

## State (2026-09-18, after type core stage 5)
- ~28.7k LoC of C in `src/` (parser 5.0k, type core `src/ty/` 9.5k, display 1.4k). Inherited from a solo human
  project halted in 2025, ported onto the cleat stdlib, then had its type core replaced (below).
- `./fasttest.sh`: **1434 / 13165 tests fully pass** (errors 5513, symbols 7797, types 3360; `./passcount.py`).
  History, like for like denominators and what each session moved are in `measurements.md` and the git log (829 before
  the parser-gaps sessions, 1084 before the type core migration, 1137 -> 1362 was the suite growing to one test per
  `@option: a,b` configuration).
- **The type core migration is finished.** Every type is a `Ty` (`src/ty/`): interned, arena-allocated, id-carrying,
  lazily resolved members, mapper instantiation, tsc-shaped relations (id-keyed caches, maybe-stack, variances),
  calls/overloads/two-pass inference, contextual typing through one `ty_check_expr(expr, contextual)`, conditional/
  mapped/indexed types, functions, namespaces and merged declarations. The value-semantic `Type` trees, `TYPE_NEW`,
  legacy views, clone-and-substitute generics, shadow mode are deleted; `Type` is now `TypeNode`, syntax only. It took
  8 sessions, every one of which raised pass counts, made the suite faster and removed `log_error` noise; nothing in
  `Ty` itself had to be redesigned. `documentation.md` describes the result, `hacks.md` what's unfinished in it.
- What did *not* get deleted: the bind-time links (`TYPE_LAZY`, `TYPE_REF`, binding `TYPE_INDEXED`, `MergedType`) that
  say where a `Declaration`'s type comes from are still `TypeNode`s, and the statement/expression walk still finds
  its checker through a thread local. Both are plan 03 material (symbols), not more type core work.
- The test harness parses every compiler option into one `CompilerOptions` (`src/compiler_options.h`, tsc's defaults
  resolved, also behind `tttt --target/--lib/--strict...`), runs multi-value directives as separate configurations
  with tsc's baseline names, and picks the default lib by target like tsc. The *checker* still only reads four of the
  options; harness-only directives (`@libFiles`, `@noImplicitReferences`, tsconfig files) are ignored. ~900 tests are
  JS files (`@allowJs`), which tttt parses as TypeScript: the largest source of junk diagnostics, needs its own plan.
- Measurement: `./failstats.py` (why tests fail, in aggregate) and `./bench.sh` (vs tsc and tsgo), results in
  `measurements.md`. Headlines: 15% of tests (2017) still get a spurious syntax error (a flat tail, much of it JS files
  and error-recovery fidelity); the most missed diagnostics are TS2322 and TS2339 (narrowing, freshness/excess
  properties, honest property-not-found), the most spurious TS2304 (JS/module syntax, missing module resolution).
  Perf on the (easy, synthetic) corpus tttt can check: **0.55 s vs tsc 3.3 s and tsgo 0.45 s (1.0 s single-threaded)**,
  RSS between the two. Typecheck is 87 ms of that; **parse (175 ms) and name resolution (166 ms) now dominate**, and
  tttt still does far less checking work than they do, so there is no perf lead yet - only no evidence of a deficit.

## Architecture assessment
What exists is a clean, small, direct-style checker: hand-written backtracking parser straight from `char*`
(no tokenizer, fail cache for the ambiguous cases), an AST of plain structs, a name resolution phase using a
transient `ScopeStack`, then typechecking driven by `LazyType` (memoised, cycle-detecting per-node types).

Things I believe are fine: C, cleat, the phase structure, LazyType as the memo/cycle mechanism, the parser
approach (fast in principle; watch error-recovery fidelity since tsc's parse errors are part of the oracle).

Things that were or are **foundationally wrong for the stated goal**. 1, 2 and 5 are fixed (the type core
migration, plan 02, now deleted: see git history for the plan and `documentation.md` for the result); 3 and 4 are
next, plan 03 covers them:
1. ~~Types are value-semantic trees with no identity (clone/drop, clone-and-substitute generics)~~ **Fixed**: `Ty`.
2. ~~The relation cache is keyed by a string rendering of both types~~ **Fixed by plan 02 stage 3**: relations run
   on `Ty` with id-keyed caches; `type_pair_cache.h` and the `hash_key` display mode are deleted.
3. **No control flow analysis at all.** No narrowing by typeof/instanceof/truthiness/discriminants/assignment, no
   reachability, no definite assignment. A huge share of real-world TS and of the test suite depends on it. It
   needs a flow graph (or equivalent) built at bind time, plus per-reference-per-flow-node caching - which in turn
   wants cheap type identity.
4. **Scopes are discarded after name resolution**; there's no persistent symbol table. Declaration merging lives
   in `Scope` during resolution. Probably workable, but the symbols baseline, flow analysis, `this`/class member
   lookup and later language-service-ish needs all lean on persistent symbols. Needs a look.
5. **Checker state is stored in the AST** (`LazyType` memos inside nodes), and the test harness shares one lib AST
   between all test programs. Blocks arena-owned types and multiple checkers over one AST. **Fixed by plan 02 stage
   2** (memos in per-`Checker` slots); the `has_cache` fields of legacy type operators went in 4b2.

**Lessons from the type core migration worth keeping** (8 sessions, staged in place with the suite runnable
throughout, scaffolding = a `TYPE_NEW` tag + one-layer legacy views, all deleted again):
- Staging in place worked: flips of one kind of thing at a time were cheap, and the scaffolding never cost more than it
  saved. The expensive part each time was what the new, correct rule *stopped forgiving* in the remaining
  approximations (weak inference, no contextual typing, no narrowing). Expect the same when narrowing lands.
- Don't transliterate weak algorithms onto a new foundation; write them the way tsc observably behaves.
- When a checker feature "doesn't work" on lib types, look at what the lib *parsed* to first (`new (): T`).
- Threading (not built, not precluded): all checker state is in `Checker`, the AST is read-only while checking, so N
  checkers over one AST is "make N `Checker`s and partition files". Type ids are per checker (printed union order
  could depend on the partition; keep it deterministic). Parse is per file. Bind still mutates shared scopes: plan 03.

## Risks / concerns
- Test-chasing: 11k tests invite special-casing message text. Prefer fixes that move whole categories.
- Exact-match oracle: `types`/`symbols` baselines require tsc's type *printing* (alias preservation, union
  ordering by type id, truncation). Union order by creation id is something an interned design can mimic and a
  tree design cannot. Keep this in mind when designing ids.
- Perf claims without measurement. Get a benchmark early; keep it running in CI-ish fashion per session.
- Single-threaded design. tsgo's win is largely parallel parse/bind + multiple checkers. Arena/interning design
  must not preclude per-file parallel parse/bind and ideally sharded checking (the type core doesn't, see above;
  plan 03 must cover bind). Parse + bind are now 4x the typecheck phase on the bench: they're where the time is.
- JS files, JSX, JSDoc, tsconfig/project handling, module resolution (node16/bundler), declaration emit: all
  absent. Declaration/JS emit is out of scope for now (typechecker only, `--noEmit` semantics).
- readme.md still says development is halted; update when convenient.

## Where this is going (rough order)
1. ~~Measurement tooling~~ done. Parser gaps (cheap, large, independent of the type core) slot in alongside 2.
2. ~~Type core redesign~~ done (8 sessions).
3. Persistent symbols + binder-time flow graph, then narrowing.
4. ~~Harness fidelity (compiler options, multi-config tests)~~ done; module resolution, JS files (`allowJs`/`checkJs`).
5. Broad feature fill driven by the aggregation tool: classes, overloads, inference, mapped/conditional/template
   types, enums, namespaces, decorators, JSX, JS/JSDoc.
6. Parallelism and perf tuning against the benchmark.
