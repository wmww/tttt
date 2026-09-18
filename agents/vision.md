# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal.

## State (2026-09-18, first insights session)
- ~17k LoC of C in `src/` (parser 4.2k, type.c 1.9k, display 1.3k, expr/statement ~0.9k each). Inherited from a
  solo human project halted in 2025, recently ported onto the cleat stdlib.
- `./fasttest.sh`: 987 / 11962 tests fully pass (829 before the first parser-gaps session). Per component: errors
  3695, symbols 5172, types 1974 pass (of 11236 that run; the other 726 are explained in `measurements.md`).
- The test harness only honours `@filename`, `@strict: true`, `@noImplicitAny: true` and `@lib`. Every other
  compiler option (target, module, strictNullChecks individually, jsx, allowJs, multi-value options like
  `@strict: true,false`...) is ignored.
- Measurement exists now: `./failstats.py` (why tests fail, in aggregate) and `./bench.sh` (vs tsc and tsgo), with
  results in `measurements.md`. Headlines: **a quarter of the suite still fails in the parser** (valid syntax
  rejected, which also switches off typechecking for the test; was 40%), now mostly member syntax (object literal
  and class members), generators/await, decorators and spread; no crashes;
  generic inference is the loudest "not implemented" in the checker. Perf on the (easy, synthetic) corpus tttt can
  check: 5x faster than JS tsc, between single- and multi-threaded tsgo, with worse memory than either. That is
  with tttt doing far less work than they do, so there is no perf lead yet - only no evidence of a deficit.

## Architecture assessment
What exists is a clean, small, direct-style checker: hand-written backtracking parser straight from `char*`
(no tokenizer, fail cache for the ambiguous cases), an AST of plain structs, a name resolution phase using a
transient `ScopeStack`, then typechecking driven by `LazyType` (memoised, cycle-detecting per-node types).

Things I believe are fine: C, cleat, the phase structure, LazyType as the memo/cycle mechanism, the parser
approach (fast in principle; watch error-recovery fidelity since tsc's parse errors are part of the oracle).

Things I believe are **foundationally wrong for the stated goal** (evidence is a skim, see plan 01 for the
session that must confirm and design the fix):
1. **Types are value-semantic trees** (`Type` struct, `type_clone`/`type_drop`, `TYPE_REF` borrowed pointers,
   "sometimes owned by AST" rules). There is no type identity. tsc/tsgo intern types and give them ids; this is
   what makes union dedup, relation caches, instantiation caches, recursion-depth ("maybe") tracking and
   `symbols`/`types` baseline printing tractable. Here, generic instantiation is clone-and-substitute
   (`generic_degenericize_type`, `replace_named_types_with`), and ownership is a manual discipline with ~90
   `type_clone` call sites.
2. **The relation cache is keyed by a string rendering of both types** (`type_pair_cache.h`, `hash_key` display
   mode). That is a symptom of (1), and is hot-path string building - the opposite of beating tsgo.
3. **No control flow analysis at all.** No narrowing by typeof/instanceof/truthiness/discriminants/assignment, no
   reachability, no definite assignment. A huge share of real-world TS and of the test suite depends on it. It
   needs a flow graph (or equivalent) built at bind time, plus per-reference-per-flow-node caching - which in turn
   wants cheap type identity.
4. **Scopes are discarded after name resolution**; there's no persistent symbol table. Declaration merging lives
   in `Scope` during resolution. Probably workable, but the symbols baseline, flow analysis, `this`/class member
   lookup and later language-service-ish needs all lean on persistent symbols. Needs a look.

At 17k LoC and 7% pass rate, replacing the type core now is cheap. At 60k LoC it will not be. So the direction
is: **move to interned, arena-allocated, id-carrying types with id-keyed relation/instantiation caches before
adding significant checker features.** The planning session may refine or, with real evidence, overturn this,
but the default is that it happens. Expect the pass count to dip or stall during the migration; that is
acceptable. Do not grind out test passes on top of the old representation in the meantime, beyond work that
is independent of it (parser, harness, tooling, module resolution).

## Risks / concerns
- Test-chasing: 11k tests invite special-casing message text. Prefer fixes that move whole categories.
- Exact-match oracle: `types`/`symbols` baselines require tsc's type *printing* (alias preservation, union
  ordering by type id, truncation). Union order by creation id is something an interned design can mimic and a
  tree design cannot. Keep this in mind when designing ids.
- Perf claims without measurement. Get a benchmark early; keep it running in CI-ish fashion per session.
- Single-threaded design. tsgo's win is largely parallel parse/bind + multiple checkers. Arena/interning design
  must not preclude per-file parallel parse/bind and ideally sharded checking. Plan 01 must address this.
- JS files, JSX, JSDoc, tsconfig/project handling, module resolution (node16/bundler), declaration emit: all
  absent. Declaration/JS emit is out of scope for now (typechecker only, `--noEmit` semantics).
- readme.md still says development is halted; update when convenient.

## Where this is going (rough order)
1. ~~Measurement tooling~~ done. Parser gaps (cheap, large, independent of the type core) slot in alongside 2.
2. Type core redesign (plan, then execute in stages).
3. Persistent symbols + binder-time flow graph, then narrowing.
4. Harness fidelity (compiler options, multi-config tests), module resolution.
5. Broad feature fill driven by the aggregation tool: classes, overloads, inference, mapped/conditional/template
   types, enums, namespaces, decorators, JSX, JS/JSDoc.
6. Parallelism and perf tuning against the benchmark.
