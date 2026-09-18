# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal.

## State (2026-09-18, first insights session)
- ~17k LoC of C in `src/` (parser 4.2k, type.c 1.9k, display 1.3k, expr/statement ~0.9k each). Inherited from a
  solo human project halted in 2025, recently ported onto the cleat stdlib.
- `./fasttest.sh`: 1084 / 11962 tests fully pass (829 before the parser-gaps sessions, 987 after the first). Per
  component: errors 4396, symbols 5939, types 2146 pass (of 11236 that run; the other 726 are explained in
  `measurements.md`).
- The test harness only honours `@filename`, `@strict: true`, `@noImplicitAny: true` and `@lib`. Every other
  compiler option (target, module, strictNullChecks individually, jsx, allowJs, multi-value options like
  `@strict: true,false`...) is ignored.
- Measurement exists now: `./failstats.py` (why tests fail, in aggregate) and `./bench.sh` (vs tsc and tsgo), with
  results in `measurements.md`. Headlines: **the parser is no longer the main blocker**: 10% of the suite (1204 tests) still gets
  a syntax error tsc doesn't report (was 40%, then 25%), and what's left is a flat tail, much of it error-recovery
  fidelity. The newly parsing tests now fail in the checker and the harness instead: the old type core **blows up on
  recursive generics** (38 GB before a depth-limit band-aid, see `hacks.md`), and the ES5-only lib set makes
  `Symbol`/`Promise`/`Generator` unknown names in hundreds of tests; no crashes;
  generic inference is the loudest "not implemented" in the checker. Perf on the (easy, synthetic) corpus tttt can
  check: 5x faster than JS tsc, between single- and multi-threaded tsgo, with worse memory than either. That is
  with tttt doing far less work than they do, so there is no perf lead yet - only no evidence of a deficit.

- Type core migration (plan 02): **stage 1 of 5 done.** `src/ty/` has the interned, arena-allocated `Ty` core
  (identity, lazy members, mapper instantiation, lowering from the AST, tsc-style printer) running in shadow mode
  over the whole suite: no crashes, 0.4% rendering mismatches against the legacy display (mostly the new core being
  right), 73 us per program. Nothing is switched over yet; the legacy checker still produces every result.

## Architecture assessment
What exists is a clean, small, direct-style checker: hand-written backtracking parser straight from `char*`
(no tokenizer, fail cache for the ambiguous cases), an AST of plain structs, a name resolution phase using a
transient `ScopeStack`, then typechecking driven by `LazyType` (memoised, cycle-detecting per-node types).

Things I believe are fine: C, cleat, the phase structure, LazyType as the memo/cycle mechanism, the parser
approach (fast in principle; watch error-recovery fidelity since tsc's parse errors are part of the oracle).

Things that are **foundationally wrong for the stated goal**. Points 1, 2 and 5 were confirmed with measurements
by the planning session (evidence and the replacement design are in `plans/02-type-core.md`); 3 and 4 are still a
skim, plan 03 covers them:
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
5. **Checker state is stored in the AST** (`LazyType` memos inside nodes), and the test harness shares one lib AST
   between all test programs. Blocks arena-owned types and multiple checkers over one AST. Plan 02 moves all checker
   state into a per-`Program` `Checker` (side tables indexed by per-file slot), leaving the AST read-only after bind.

At 19k LoC and 9% pass rate, replacing the type core now is cheap. At 60k LoC it will not be. **Decision: migrate to
interned, arena-allocated, id-carrying types (`Ty`), split from syntax-only `TypeNode`s, with id-keyed relation
caches, mapper-based instantiation and tsc-style recursion handling, staged in place over ~6 sessions with the suite
runnable throughout (`plans/02-type-core.md`).** Expect the pass count to stall during the migration; that is
acceptable. Do not grind out test passes on top of the old representation in the meantime, beyond work that is
independent of it (parser, harness, tooling, module resolution). The main risk is the temporary `TYPE_NEW` bridge
between old and new representations costing more than it saves; there's a short insights checkpoint after stage 4a.

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
2. Type core redesign: ~~plan~~ done, execute `plans/02-type-core.md` stages 1-5.
3. Persistent symbols + binder-time flow graph, then narrowing.
4. Harness fidelity (compiler options, multi-config tests), module resolution.
5. Broad feature fill driven by the aggregation tool: classes, overloads, inference, mapped/conditional/template
   types, enums, namespaces, decorators, JSX, JS/JSDoc.
6. Parallelism and perf tuning against the benchmark.
