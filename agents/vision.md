# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-10-07, insights after the tenth window)
- **Switched (plan 48 stage 1, todo entry 2 of the eleventh window): suite 13683 / 14969** against TypeScript commit
  4d4f005c (errors 14588, symbols 14858, types 13883); the remaining gap is mostly member order (stage 2) and
  strict-by-default exposure (stage 3). The sweep / `$ExpectType` / flagflip caches are still tsc 5.8's until stage 4.
  The 5.8 numbers below are history.
- **Suite 13303 / 13540** fully pass (errors 13455, symbols 13490, types 13355) against **TypeScript 5.8.2**. +204 in
  the window, 0 lost. 237 tests fail somewhere. Checker log_error output: none.
- **Corpora** (all clean under tsc 5.8, pinned at their last commits before TypeScript 6): tsc `src/compiler`,
  type-fest, valibot, react, nest, webpack, zod, xstate, drizzle, vue, tsc-src and typescript-eslint (`-b`), vscode
  (1.39M lines), and this window's **excalidraw** (JSX app) and **tanstack-router** at 0; **effect** 7 -> 3 (a
  TaggedClass cycle, plan 19); zod-root at tsc's level. **Sweep** 8589 / 8595 DefinitelyTyped packages exact, also under
  nuia and eopt. **`$ExpectType`** (built this window) 68619 / 68640. **Flag flips** (built this window, 182 cells, 53k
  tsc diagnostics): missing / spurious / other code 603 / 1460 / 1029 -> 51 / 45 / 10. **Mutation testing** (built this
  window): fresh seeds now ~1% (single-file) and ~2% (multi-file/JS) productive.
- **Perf**: callgrind flat over the window except tsc shortcuts added in the perf session and triage (drizzle -37%,
  vue -13%). vscode 2.2 s / 2.1 GB vs tsgo 6.75 s / 3.7 GB; 1.5-7x faster than multi-threaded tsgo elsewhere, memory
  below tsgo's everywhere. Caveat: tsgo is 7.0, a newer language (see below).
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, mapper instantiation, tsc's inference flow (lazy fixing, contextual instantiation, intra-expression sites,
  calls resolved once, skip-round rules), one circularity stack (plan 39), contextual typing, conditional/mapped/
  indexed/template/substitution types, deferred references, flow graph + narrowing, module resolution + tsconfig,
  iteration protocol, generators, `this` types, mixins, decorators, the missing type, union origins, elaboration, JSX,
  tsc's type printer and symbol chains, JSDoc + CommonJS + expandos, parallel checking, loading and resolving,
  declaration diagnostics (plan 40), project references / `-b` (plan 41), tsc's laziness and resolution barriers (plan
  42), tsc's deferred checks. ~120k lines of C.
- **Absent or approximated tsc mechanisms**: contextual instantiation (push-down, not tsc's leaf-wise pull;
  type_core.md); permissive instantiation; TS7056 and TS5088; member identity (members aren't Symbols; the symbols
  *harness* rebuilds tsc's merges for display, decided acceptable). **Everything since TypeScript 5.8** (5.9, 6.0's
  new defaults and deprecations, 7.0). Out of scope: emit (JS and `.d.ts` text), watch mode, the language service.

## Course correction (insights of 2026-10-07, end of the tenth window)
- **Health is good; no redesign.** Tenth clean window: all ten entries landed, 0 lost, two oracle tools built and
  drained (flag flips, mutations) plus `$ExpectType`, two corpora pinned and drained. hacks.md 770 -> 826 lines (the
  triage wrote leftovers up rather than leaving them in triage.md, fine).
- **The target is stale: retarget to TypeScript 6.0/7.0 (plan 48), this window.** Every oracle is tsc 5.8.2
  (2025-02). 5.9, 6.0 (2026-03: new defaults, deprecations, the last JavaScript tsc) and 7.0 (2026-07: tsgo, now
  `typescript@latest`) have shipped; our corpora are pinned at "the last commit before TypeScript 6" because their
  upstreams moved on; tsgo, the competitor, checks a newer language and rejects two of our corpus configs. The suite at
  v6.0.3: 11.7k test case files changed (mostly options made explicit and code fixed for strict-by-default), 3080
  `.types` / 2436 `.symbols` / 2113 `.errors.txt` baselines changed, 245 new tests. At 98.2% on 5.8, each further
  5.8-parity session buys less than moving to the language users write today. Expect a dip in the pass count and a
  window spent on the switch, the oracles' caches and re-pinned corpora.
- **Decided (plan 48 planning, 2026-10-07): the oracle is TypeScript commit 4d4f005c**, the `tsgo-port` commit tsgo
  7.0.2 pins: 6.0.3 plus `stableTypeOrdering` on by default (tests/cases identical to 6.0.3's; 650 `.types` differ by
  member order only). tsc 6's `compareTypes` and tsgo's `CompareTypes` are the same rule, so tttt **adopts the stable
  ordering** and deletes its creation-order mimicry. Prototypes: the switch reaches 92.3% of the new 14969-test suite
  with defaults, libs and deprecations alone. Stable ordering landed 2026-10-07 (suite 14269 / 14969, a cached
  per-type sort key, +0.9-2.1% instructions); session B deletes the creation-order machinery. tsgo's own baselines are not an oracle (they skip 1861 configs,
  rework JS, keep source quotes). Deprecated options (es5, amd, outFile...) stay supported: 6.0 still tests them.
- **The missing-diagnostic oracles are drained to their noise floor**; rerun them after the switch rather than
  mining them further on 5.8.

## Decisions still in force (from earlier course corrections; details in git history of this file)
- **Fan-out sessions** (four Opus sub-agents in worktrees, one failstats/displaydiff/jsstats bucket each, disjoint
  test lists, 0 lost enforced at merge) are the engine for the long tail: one or two per window. **Instruction counts
  at every session end** (`./instrcount.sh`): creep is silent otherwise.
- **Whole features before per-test bugs** now that the tail is thin: a planning session prototypes and may say no
  (plan 47: prototype the deletion of a hack before designing the mechanism to replace it).
- **Oracles must test both directions**: clean corpora only show spurious diagnostics; flag flips and mutations (plan
  46) show missing ones. Rerun all of them after checker changes.
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
- **Binder**: declare and link are serial and write shared tables; the resolve walk shards per file, JavaScript too
  (plan 45: a JS file's assignment declarations replay in program order; merged type parameters still walk serially;
  `hacks.md`, "Parallelism contract"). JS support will add declarations from assignments; design it with that in mind.
- **Threading**: checking is threaded (stage 3; nothing in the checker had to change once stage 2's audit fixes were
  in, which is evidence the "AST read-only while checking" contract holds). Any new checker-wide memo that *reports*
  must be tagged `Error.once_key`, and anything written to AST/binder state during checking is a race (the frozen
  guards log it; TSan on the corpora is the net: `plans/36_parallelism.md` has the build line). Loading is
  threaded too (a cache-warmer pool beside the unchanged sequential loader: `src/load_pool.c`; the shared state is the
  resolver's locked caches). Name resolution shards per file (plan 45 stages 1-2); declare/link are serial.

## Risks / concerns
- Exact-match oracle: `types`/`symbols` baselines require tsc's type printing (alias preservation, union order,
  origins, truncation). Union order is tsc 6's compareTypes now (`ty/order.c`); type ids still break its last ties
  (fresh/regular literals, same-symbol anonymous types, conditional roots), so creation order still matters a little.
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
8. ~~Declaration diagnostics (40), project references (41) with tsc-src and typescript-eslint, laziness planned (42)
   and stages 1-2, memory like-for-like, the types/symbols long tail~~ done.
9. ~~Wider oracles (effect, vscode, the breadth sweep of plan 44), plan 42 stages 3-4, AST memory (43), parallel
   name resolution (45 stage 1)~~ done.
10. ~~Oracles for missing diagnostics (46: flag flips, mutations), `$ExpectType` at breadth (44 stage 3), speculative
   checks (47: not needed), two new corpora (excalidraw, tanstack-router), a perf session~~ done.
11. **Now**: retarget to TypeScript 6.0/7.0 (plan 48): the suite, the oracles' caches, re-pinned corpora, perf
   against tsgo 7 like for like.
12. Then: plan 45 stages 2-3 (JS resolve, declare/link), the check phase's load balance and file-to-checker affinity,
   a token cache / node arenas in the parser, compact instantiated Props / the `Signature` split.
