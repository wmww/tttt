# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-10-08, insights after the twelfth window)
- **Target: TypeScript commit 4d4f005c** (6.0.3 + stable type ordering, what tsgo 7.0.2 implements), the last JS
  TypeScript there will be: about to move to 7.x (below). **Suite 14835 / 14969** (99.11%; errors 14905, symbols
  14922, types 14877), 0 lost all window.
- **Corpora**: tsc `src/compiler`, type-fest, valibot, react, nest, webpack (checkJs), zod, xstate, drizzle, vue,
  tsc-src and typescript-eslint (`-b`), vscode, excalidraw, tanstack-router all **0**; effect 3 (plan 19); zod-root =
  tsc's 53. **Sweep** 8594 / 8595, **`$ExpectType`** 68631 / 68640, **flagflip** 17 / 11 / 4, scenarios 244 / 0.
  **Source sweep** (plan 52, new this window): 174 repos, 3359 projects, **3003 match, 71 repos fully** (2241 / 47
  when it landed).
- **Perf** (2026-10-07/08): 3.0x (vscode) to 13.9x (type-fest) faster than multi-threaded tsgo 7.0.2, less memory
  everywhere. Sharing types between checkers is not a lever (measured: 4 checkers cost ~2% more CPU than one); the
  source sweep found real pathologies instead (karakeep 55 s -> 0.6 s).
  **A second competitor (2026-10-11): Bun 1.4.3's `bun check`**, a Rust port of tsgo claiming 7.0.2 conformance.
  On a 12-core box it ties tttt's default on wall time (tttt ahead on small projects, bun on vscode/effect/drizzle/vue
  at 24 threads vs our 4) and uses less memory on 10 of 14 corpora (**up to 2.9x**, vscode 2.8x); at 12 threads tttt is fastest on every corpus but uses
  ~3.5x bun's memory on vscode. tttt's thread default (capped at 4) and its memory (the AST, per-checker redundant
  types) are now the competitive gap, not single-threaded speed (measurements.md).
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, tsc 6's stable type ordering, mapper instantiation, tsc's inference flow and ContextChecked/context anchors
  (plan 51), one circularity stack, conditional/mapped/indexed/template/substitution types, deferred references, flow
  graph + narrowing, module resolution + tsconfig, iteration protocol, generators, `this` types, mixins, decorators,
  union origins, elaboration, JSX, tsc's type printer and symbol chains, JSDoc + CommonJS + expandos (the export=
  merge in place, plan 50), parallel loading / resolving / checking, declaration diagnostics, project references /
  `-b`, tsc's laziness and deferred checks. ~124k lines of C.
- **Absent or approximated tsc mechanisms**: tsc's namespace-import clones (cloneTypeAsModuleType: predicates,
  hacks.md) and a few clone-ordering quirks (bugs/binder_modules.md); permissive instantiation; TS7056 and TS5088;
  member identity (members aren't Symbols; the symbols harness rebuilds tsc's merges for display, decided
  acceptable). **Not tsc's CLI**: own output format, exit codes, progress chatter, a 5000-diagnostic cap (plan 53
  stage 1). Out of scope until the human says otherwise: emit (JS and `.d.ts` text), watch mode, the language
  service.

## Course correction (insights of 2026-10-08, end of the twelfth window)
- **Last window's debt paydown worked.** Plan 51 replaced the contextual-typing side channels with tsc's model in
  three stages (six KEY_CONTEXT_* channels deleted, 0 lost); the hacks audit took hacks.md 895 -> 798 lines; plan 50
  merged CommonJS export= in place. The "no per-test drains" decision held and the suite still rose (14829 -> 14835)
  as a by-product. hacks.md growth has reversed for the first time in four windows: keep it that way.
- **The source sweep (plan 52) is the best oracle we have built**: 3359 real projects with their own configs found
  config-level bugs no other oracle could (the TS6305 cascade under `-p`), perf pathologies (55 s files) and ~1000
  checker mismatches, drained to 3003 in two sessions. Its cache has a fidelity problem (26% of projects aren't what
  `tsc -p --noEmit` prints: it checks past option errors and doesn't pass noEmit), which the 7.x switch fixes by
  caching the CLI's output.
- **New direction, the human's request: TypeScript 7 as the target and tttt as a drop-in `tsc`** (plan 53). This is
  the right call independently: 4d4f005c is the last JS TypeScript, users are moving to 7, and every month on a
  frozen oracle makes our tests less representative. Decisions (confirming plan 53's proposals, with one change):
  - **Pin TypeScript `main` at a 7.1 nightly now, not 7.0.2.** Only `main` has the native test tree
    (`tsc/testdata`, promoted from the 6.0 suite, no `.diff`s); 7.0's release branch keeps the old submodule layout,
    so pinning 7.0.2 means writing the harness twice. 7.1 has no npm beta yet; `typescript@7.1.0-dev.YYYYMMDD` is
    built from `main`, so binary and tests pin to the same commit. Re-pin to 7.1.0 stable after 2026-11-24 (one
    session, drift measured at ~2%). The 7.1 language delta (source phase imports, ambient-module attributes, es2026)
    then comes with the oracle.
  - **No dual 6/7 mode; delete what 7 deleted** (es5/es3, amd/umd/system/none, node10/classic, baseUrl, outFile,
    downlevelIteration, the removed JS/JSDoc features) with the switch, plan-48 style. Stage 2 (7.0's option errors)
    lands *with* the switch: on the 6.0 oracle it would only lose tests.
  - **CLI parity (stage 1) goes first** and is worth doing on its own: tsc's `--pretty false` format, exit codes, no
    chatter, tsgo's flag names. The scripts move to the tsc format with it, so one parser reads both sides.
  - **The oracle tooling is the risk, not the checker.** On TS code 7.0 matches our oracle on 10 of 13 corpora and
    76% of the source sweep; what's expensive is regenerating seven caches with tsgo and replacing the JS-API tools
    (`tscbaseline.sh`/`filetest.sh`/`mutate.py` need baselines for arbitrary files; `sweep_tsc.js --expect` needs
    `getTypeAtLocation`). **Prototyped (plan 54)**: the Go route works without touching the checkout (`go build
    -overlay` maps our Go files into tsgo's tree: the test runner gives any file's baselines in 45 ms, an overlay
    main calls the checker), and the native suite through tttt's harness is already 87.3% with a lib swap and three
    harness changes, nearly every failure a baseline 7 changed or a test 7 added. **Done through stage 2**: the
    suite is tsgo's, and every tool runs tsgo (`test/oracle`: the probe, `tttt-oracle`); only the caches are 6.0's.
  - **Expect the suite to fall to ~90% at the switch** (mostly JS, where 7 removed features), then recover by fan-out
    on the 7.x mechanisms. These are mechanism changes, not per-test drains: the no-special-cases rule stands.
  - **Open question for the human** (in report.md): does emit (`.d.ts` and/or JS) enter scope? "Drop-in" for most
    users means emit; tttt checks only. Until answered: out of scope, readme.md says so. Watch mode likewise.
- **Perf stays a session per window**, now against tsgo 7.1 (which has had perf work since 7.0.2): re-measure the
  margin after the switch rather than assume it.

## Decisions still in force (from earlier course corrections; details in git history of this file)
- **Fan-out sessions** (four Opus sub-agents in worktrees, one failstats/displaydiff/jsstats bucket each, disjoint
  test lists, 0 lost enforced at merge) are the engine for the long tail: one or two per window. **Instruction counts
  at every session end** (`scripts/instrcount.sh`): creep is silent otherwise.
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
- **Prefer fixes that move whole categories** (`scripts/failstats.py`, `scripts/displaydiff.py`, the corpora); 13k exact-match
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
  guards log it; TSan on the corpora is the net: documentation.md "Typecheck" has the build line). Loading is
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
- The 7.x switch moves the oracle from a JS program we can patch and instrument (memory: "instrument a tsc copy")
  to a Go binary. Keep that ability: build tsgo from source at the pinned commit, so a patched copy is one
  `go build` away. tsgo is order dependent in places tttt isn't (bugs/include_order.md) and 7.1 changes the stable
  ordering rules (bigint literals, same-named aliases, mapped types, variance and `infer` order): `ty/order.c` gets
  re-ported, not patched.
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
11. ~~Retarget to TypeScript 6.0 + stable ordering (plan 48): suite, oracle caches, re-pinned corpora, perf against
   tsgo 7~~ done.
12. ~~Contextual-check state (plan 51), the source-code breadth sweep (plan 52), a hacks audit, plan 50, a perf
   session~~ done.
13. **Now (the human's request, 2026-10-08)**: TypeScript 7 as the target and tttt as a drop-in `tsc` (plan 53: CLI
   parity, then the switch to `main`'s native suite and tsgo-generated caches at a pinned 7.1 nightly, deletion of
   what 7 removed, the 7.x deltas by fan-out, 7.1's syntax; re-pin to 7.1.0 stable after 2026-11-24).
14. Then: parallel declare/link (bugs/perf_leads.md "Parallelism"), a token cache / node arenas in the parser, compact instantiated Props / the
   `Signature` split, whatever plan 52's sweep ranks first.
