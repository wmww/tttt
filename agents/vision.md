# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-10-04, insights after the seventh window)
- **Suite 12354 / 13540** fully pass (errors 13113, symbols 13033, types 12836). +314 in the window (12040), all
  committed, nothing lost; smaller than the sixth on purpose: four of ten sessions planned or measured. Failing
  components: types 704, symbols 507, **errors 427** (was 810: suite checker bugs 7 took the "right codes, other text"
  bucket 236 -> 39). Of the errors failures: 121 missing+spurious, 101 tttt reports nothing, 98 only missing, 36 only
  spurious, 29 tttt-only; 40 have a spurious TS1xxx. Checker log_error output: none.
- **Corpora** (all clean under tsc 5.8): tsc `src/compiler`, valibot, react, nest, type-fest, zod, **vue 0** (pinned
  this window, was 107); **drizzle 6** (pinned, was 693); webpack 6; xstate 12; zod-root at tsc's 55.
- **Perf**: the +8% creep was clawed back and more (tsc corpus 5.23 -> 4.53 G instructions, react -29%, vue -17%), by
  porting missing tsc caches, not by reverting. Resolve-once (plan 38 stage 3) took another 2-9%. `./bench.sh`
  (09-29): faster than multi-threaded tsgo on every corpus, 2.8-11x. **Memory** is the weak number: peak RSS above
  single-threaded tsgo's on most corpora (drizzle 1.8 GB vs 1.05).
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, mapper instantiation, tsc's inference flow (lazy fixing, contextual instantiation, intra-expression sites,
  calls resolved once), one circularity stack (plan 39), contextual typing, conditional/mapped/indexed/template/
  substitution types, deferred references, flow graph + narrowing, module resolution + tsconfig, iteration protocol,
  generators, `this` types, mixins, decorators, the missing type, union origins, elaboration, JSX, tsc's type printer
  and symbol chains, JSDoc + CommonJS + expandos, parallel checking and loading. ~96k lines of C.
- **Absent or approximated tsc mechanisms**: declaration-emit diagnostics (plan 40, planned), project references
  (plan 41, planned), **laziness** (members, reverse mapped types, intersection reduction, contextual instantiation
  and return types are computed eagerly; plan 42), resolvingSignature and the rest of the circularity stack (blocked
  on laziness), permissive instantiation. Out of scope: emit (JS and `.d.ts` text), watch mode, the language service.

## Course correction (insights of 2026-10-04, end of the seventh window)
- **Health is good; no redesign.** Seventh clean window. Every planned item landed: two corpora pinned and taken to
  0/6, the perf creep reversed, plans 38 stage 3 and 39 stages 1-2 executed, all five plan-sized hacks ported, plans
  40 and 41 written. hacks.md grew moderately (540 -> 625 lines).
- **Execute 40 and 41 now.** They are what a real user with `declaration: true` or a monorepo hits first, and they
  make the perf comparison honest (tsc does declaration diagnostics on four of our corpora; tttt doesn't yet). They
  take half the window.
- **The pattern this window: eagerness.** The leftovers of plans 38 and 39, five hacks.md entries, three type_core
  bugs and two perf leads all end in "tttt computes X when it's made, tsc when it's asked". Each was patched locally.
  Stop patching: plan 42 inventories the sites and decides on a port (likely lazily resolved members of instantiated
  types, guarded by plan 39's stack). It may also be the memory fix. Until then, a fix that adds another eager
  workaround in this area should go to plan 42's list instead.
- **The long tail moves to types and symbols.** Errors is no longer the biggest failing component; the window's
  fan-outs point at displaydiff's buckets first.
- **Memory gets attention**: the perf session looks at peak RSS, not only instructions.

## Decisions still in force (from earlier course corrections; details in git history of this file)
- **Fan-out sessions** (four Opus sub-agents in worktrees, one failstats/displaydiff/jsstats bucket each, disjoint
  test lists, 0 lost enforced at merge) are the engine for the long tail: one or two per window. **Instruction counts
  at every session end** (`./instrcount.sh`): creep is silent otherwise.
- **Whole features before per-test bugs** now that the tail is thin: a planning session prototypes and may say no.
- **Every todo window has corpus sessions** (the corpora were once left alone for ~30 sessions), and todo.md has a
  fixed session budget that overruns come out of; ending a turn ends the session, so commit before long jobs.
- **Parser: no rewrite** (plan 27 looked; recovery is one list/commit-point framework plus patching). Revisit only
  with a profile. **JS must lower into the same `Ty` machinery**, no parallel JSDoc type world.
- **Real projects are the second oracle.** A rising pass count with flat corpus counts means we're test-chasing.
  Rank corpus diagnostics by root cause, not by code (one mapped type with an `as` clause was 810 TS7006). Every
  insights session looks at the corpus numbers next to the pass count. Corpus root causes that are type-core rules go
  ahead of suite plans.
- **A clean corpus only finds spurious diagnostics**: code tttt doesn't check at all (decorators) looks perfect there;
  missing checks are found by the suite. Pick corpora for the front ends and features they exercise.
- **A relation that is too lenient hides inference bugs**: when relations get stricter, expect a cluster of latent
  inference bugs (plan 19 R1, the callback-parameter rule). Budget for them.
- **Prefer fixes that move whole categories** (`./failstats.py`, `./displaydiff.py`, the corpora); 13k exact-match
  tests invite special-casing message text.
- **Staging in place works** (the type core migration: 8 sessions, suite runnable throughout, scaffolding deleted).
  The expensive part each time is what the correct rule stops forgiving in the remaining approximations. Don't
  transliterate weak algorithms onto a new foundation; write them the way tsc observably behaves. When a checker
  feature "doesn't work" on lib types, look at what the lib *parsed* to first.
- **A small perf session every few plans**: every check costs a little (plan 15's ~4%). Single-threaded tttt must
  stay ahead of single-threaded tsgo before parallelism is worth building.

## Architecture assessment
A clean, direct-style checker: hand-written backtracking parser straight from `char*` (no tokenizer, fail cache for
the ambiguous cases), an AST of plain structs, a binder (persistent symbols and scopes, merging like tsc's), then
checking driven by `LazyType` (memoised, cycle-detecting per-node types) over the interned `Ty` core. All checker
state is in `Checker`; the AST is read-only while checking.

The five foundational problems the first insights found are all fixed: value-semantic types (-> interned `Ty`),
string-keyed relation caches (-> id-keyed), no control flow analysis (-> plan 05), scopes discarded after resolution
(-> plan 04), checker state in the AST (-> per-`Checker` slots). What's left to watch:
- **Parser**: error-recovery fidelity is essentially done (plan 27: 34 tests still differ, 2026-09-29; the framework held, no rewrite needed), unbounded recursion depth (binary chains need ~8 MB of
  stack; tsc walks them iteratively), backtracking cost.
- **Binder**: still writes to shared tables and one arena (`hacks.md`, "Parallelism contract audit"), the one phase
  that doesn't shard per file. JS support will add declarations from assignments; design it with that in mind.
- **Threading**: checking is threaded (stage 3; nothing in the checker had to change once stage 2's audit fixes were
  in, which is evidence the "AST read-only while checking" contract holds). Any new checker-wide memo that *reports*
  must be tagged `Error.once_key`, and anything written to AST/binder state during checking is a race (the frozen
  guards log it; TSan on the corpora is the net: `plans/36_parallelism.md` has the build line). Loading is
  threaded too (a cache-warmer pool beside the unchanged sequential loader: `src/load_pool.c`; the shared state is the
  resolver's locked caches). The serial resolve-names phase is next.

## Risks / concerns
- Exact-match oracle: `types`/`symbols` baselines require tsc's type printing (alias preservation, union order by
  type id, origins, truncation). The interned design can mimic creation order; keep it that way when adding types.
- Perf: deep generic code is where tsc's caches and shortcuts matter (the 2026-09-27 check-up's wins were all tsc
  caches tttt lacked: key-property maps, reverse mapped types, containsType). Checks accumulate cost silently between
  perf sessions (+7% over 09-25..09-27). Wall times drift with the machine: compare callgrind counts across sessions.
- JS/JSDoc is a second front end on top of the checker: the risk is a parallel type world for JSDoc. It must lower
  into the same `Ty` machinery.
- Parser rewrites are tempting and expensive; plan 27 looked and found no case for one. Revisit only with a profile.

## Where this is going (rough order)
1. ~~Tooling, type core, first corpus, binder, flow, modules, underlines, corpus to zero, second corpora, plan 19
   R1-R9, suite plans 13-16, 21-24, JSX~~ done.
2. ~~Deferred references (29), substitution types (30), fourth corpus (31), JS stage 2, calls resolve once (28)~~ done.
3. ~~Checkjs corpus (33), JS stage 3, error text tool (34), parser perf, permissive grammar (27), zod corpus~~ done.
4. ~~Mixins (32), silent type differences (35 stages 1-2), parallelism planning (36), perf, lexical diagnostics~~ done.
5. ~~Parallel checking (36 stages 2-3), tsc mechanism ports (37 A-F), webpack and zod-root~~ done.
6. ~~xstate corpus, tsc's inference flow (38 stages 1-2), parallel loading (36 stage 1), the long tail with sub-agents
   (checker bugs, JS, parser, types component)~~ done.
7. ~~Two new corpora (drizzle, vue), perf claw-back, plan-sized hacks, circularity stack (39), resolve once (38
   stage 3), planning declaration diagnostics (40) and project references (41)~~ done.
8. **Now**: executing 40 and 41 (with tsc-src and typescript-eslint as corpora), planning laziness (42), memory, the
   types/symbols long tail.
9. Then: laziness stages, parallel name resolution, the check phase's load balance, a token cache / node arenas in
   the parser.
   token cache / node arenas in the parser, memory (the `Signature` split).
