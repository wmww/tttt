# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal.

## State (2026-09-22, after plan 09)
- **Suite: `./fasttest.sh` 4107 / 13163 fully pass**. Plan 07 (types baseline underlines) made the types component
  a real measurement of type *printing*; plans 08-10's corpus fixes moved the suite by +611 on the side.
- **Real corpus: `./realworld.py`** (tsc's own `src/compiler` via its tsconfig, 223 files incl. `@types/node`, clean
  under tsc): **0 parse, 0 name, 95 type diagnostics** (1751 at the start of plan 08, flat for three plans before;
  plan 09 took the iteration bucket, 203 -> 109, plan 10 stage 1 the `this` cascades, -> 95). What is left is mostly
  two category-sized gaps (higher-order generic function inference, fresh/regular literals: plan 08 "Progress") plus
  a tail of ~25 singles.
- `this` types (plan 10) landed in stage 1 (+172 full passes); contextual `this` (stage 2) is next.
- ~31k LoC of C in `src/`. Done and holding: parser (hand-written, backtracking, no tokenizer; parses the whole
  corpus), binder (tsc-shaped symbols, persistent scopes, aliases, lib overlay: plan 04), the `Ty` type core
  (interned, id-keyed relations, mapper instantiation, two-pass inference, contextual typing, conditional/mapped/
  indexed types: 8 sessions), flow graph + narrowing (plan 05), module resolution + tsconfig (plan 06), compiler
  options in one struct, multi-config tests, JS gate (`allowJs`/`checkJs`), diagnostics without a file.
  `documentation.md` describes the architecture, `hacks.md` what is approximated in each part.
- Absent: JS/JSDoc semantics (~900 tests now fail quietly behind the gate), JSX, contextual `this`, reverse mapped types,
  decorators checking, declaration emit (out of scope). The iteration protocol, unique symbols and generators landed
  in plan 09 (`ty/iterate.c`).
- History: every foundation plan (type core, binder, flow, modules) moved the suite by a whole category in one go and
  made it faster. Per-plan numbers are in `measurements.md` and the git log.

## Course correction (insights, 2026-09-22)
- **The corpus is stalling while the suite rises.** 1750 / 1752 / 1751 diagnostics across plan 05, plan 06 and the
  triage. That's the pattern the previous course correction (below) warned about. The suite work was not wasted
  (module resolution was needed), but the corpus is now the next thing: plan 08 works it down root cause by root
  cause, with the suite as a regression check, and ends with the first bench/profile where tttt does all of the
  work tsgo does. Until then the perf headline stays "no evidence of a deficit", not "faster".
- **Plan 07 (underlines)** done in one session (1971 -> 3496 full), within budget: the reuse rule is fitted to
  probes, not a port of tsc's node builder (`hacks.md`).
- The spread of the corpus count is smaller than it looks: rank buckets by root cause, not by code (one mapped type
  with an `as` clause = 810 TS7006). The same goes for `failstats.py`.

## Course correction (insights, after the type core migration)
- **tttt had never parsed a real program** until plan 03 (see State). The test suite barely contains regex literals,
  `?.()`, braceless `if/else`, comma operators, so `failstats.py` ranked them as a flat tail and we walked past them
  for 15 sessions while the perf headline came from a synthetic corpus of tests tttt already passes. **Second oracle
  from now on: `./realworld.py` must reach zero diagnostics on a real project, and the benchmark is that project**
  (`./bench.sh`, second table). Every insights session should look at its numbers next to the pass count; a rising
  pass count with a flat real-world count means we're test-chasing.
- The audit vision.md asked for: `hacks.md` is honest and detailed (it reads as a gap list of the new core, which is
  what it should be), the migration did not drift, and the "AST-side walk" section is an acceptable starting point
  for the symbols plan. No redesign of `Ty` is called for.
- **Symbols before flow analysis, as two plans.** The old todo had one planning session for both. The binder
  (`scope.c`, `declaration.c`, ports: ~1.5k lines) is the last legacy foundation: declarations are *copied* for
  imports/aliases/lib sharing (so identity needs `canonical_declaration`, and a copied interface is a second `Ty`),
  same-name namespaces don't merge (177 tests, `Intl`), scopes are thrown away, name resolution is as slow as parsing
  and mutates shared state (blocks parallel bind). Narrowing keys its caches on reference identity -> symbol identity,
  so it can't be built on copies. Plan 04 = symbols/binder (replace, don't patch; it's small), plan 05 = flow graph +
  narrowing on top of it.
- JS files (~900 tests parsed as TS) have the cheap gate (skip without `allowJs`, no semantic errors without
  `checkJs`, done); real JS/JSDoc support waits until TS itself works on real code.

## Architecture assessment
What exists is a clean, small, direct-style checker: hand-written backtracking parser straight from `char*`
(no tokenizer, fail cache for the ambiguous cases), an AST of plain structs, a binder (symbols, persistent scopes: declare, link, resolve), then typechecking driven by `LazyType` (memoised, cycle-detecting per-node types).

Things I believe are fine: C, cleat, the phase structure, LazyType as the memo/cycle mechanism, the parser
approach (fast in principle; watch error-recovery fidelity since tsc's parse errors are part of the oracle).

Things that were or are **foundationally wrong for the stated goal**. 1, 2 and 5 are fixed (the type core
migration, plan 02, now deleted: see git history for the plan and `documentation.md` for the result); 3 and 4 are
fixed too (symbols = plan 04, flow = plan 05, see the course correction above):
1. ~~Types are value-semantic trees with no identity (clone/drop, clone-and-substitute generics)~~ **Fixed**: `Ty`.
2. ~~The relation cache is keyed by a string rendering of both types~~ **Fixed by plan 02 stage 3**: relations run
   on `Ty` with id-keyed caches; `type_pair_cache.h` and the `hash_key` display mode are deleted.
3. ~~No control flow analysis at all~~ **Fixed by plan 05** (4 stages, plan deleted: `documentation.md` "Control
   flow" describes the result, `hacks.md` "Narrowing" what is left out): tsc's flow graph built in the resolve walk,
   `getFlowTypeOfReference` with loops/closures/type facts, type predicates end to end, assertions, `never` calls,
   typed reachability, discriminants, switch, `instanceof`, `in`, TS2454. Real corpus 7777 -> 1626 diagnostics. Stage 4
   took TS2339 / "possibly undefined" out from behind its legacy gate (corpus 1750 now: the +124 are real bugs the gate
   hid, mostly aliased conditions and weak inference, see `triage.md`) and profiled: three algorithmic blowups (none
   in the flow engine itself) took the corpus typecheck from 778 to **352 ms, wall 0.57 s vs tsgo 0.79 s**.
4. ~~Scopes are discarded after name resolution; there's no persistent symbol table~~ **Fixed by plan 04 stage 1**
   (class/interface members are still not symbols, see the plan).
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
  could depend on the partition; keep it deterministic). Parse is per file. Bind still writes to shared tables and one arena: `hacks.md`, "Parallelism contract audit".

## Risks / concerns
- Test-chasing: 11k tests invite special-casing message text. Prefer fixes that move whole categories.
- Exact-match oracle: `types`/`symbols` baselines require tsc's type *printing* (alias preservation, union
  ordering by type id, truncation). Union order by creation id is something an interned design can mimic and a
  tree design cannot. Keep this in mind when designing ids.
- Perf: on real code tttt is at 0.6-0.7 s next to tsgo's 0.72 s, parse + bind healthy. **Not a result yet**: tttt
  still fails ~1750 checks early (a failed call skips work), tsgo does all the work and reports nothing. Fix correctness on the corpus first, re-profile
  when the error count is down (the first profile paid for itself at once), and don't trust the synthetic 0.6 s
  headline for anything.
- Parser robustness: error recovery paths are barely exercised under valgrind (plan 03's heap corruption turned out
  to be the error *printer*, `span_line_start` on a span starting at a newline, not the parser).
- Single-threaded design. tsgo's win is largely parallel parse/bind + multiple checkers. Arena/interning design
  must not preclude per-file parallel parse/bind and ideally sharded checking (the type core doesn't, see above;
  plan 04 must cover bind). Parse + bind are now 4x the typecheck phase on the bench: they're where the time is.
- JS files, JSX, JSDoc, project references, declaration emit: all absent (tsconfig and module resolution exist
  since plan 06). Declaration/JS emit is out of scope for now (typechecker only, `--noEmit` semantics).

## Where this is going (rough order)
1. ~~Measurement tooling~~ done. Parser gaps (cheap, large, independent of the type core) slot in alongside 2.
2. ~~Type core redesign~~ done (8 sessions).
3. ~~Real-world corpus parses and runs end to end~~ done (plan 03); it is the pinned benchmark and second oracle.
3b. ~~Persistent symbols / new binder (plan 04)~~ done, then bind-time flow graph + narrowing (plan 05).
4. ~~Harness fidelity (compiler options, multi-config tests)~~ done; ~~module resolution~~ done (plan 06); ~~JS gate,
   diagnostics without a file~~ done (triage). ~~Types baseline underlines (plan 07)~~ done (+1525 full passes).
4b. **Real corpus to zero diagnostics (plan 08)**, then bench + profile it: the first honest perf comparison. What
   plan 08's loop can't do is category-sized and planned: ~~iteration protocol + unique symbols + generators (plan
   09)~~ done, `this` types (plan 10), fresh literals + higher-order generic inference (plan 11, a sketch).
5. Broad feature fill driven by the aggregation tool: classes, overloads, inference, mapped/conditional/template
   types, enums, namespaces, decorators, JSX, JS/JSDoc.
6. Parallelism and perf tuning against the benchmark. Single-threaded tttt must beat single-threaded tsgo first.
