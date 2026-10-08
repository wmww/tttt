# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-10-07, insights after the eleventh window)
- **Target: TypeScript commit 4d4f005c** (6.0.3 + stable type ordering, what tsgo 7.0.2 implements; plan 48, done in
  one window). **Suite 14829 / 14969** (99.07%; errors 14903, symbols 14920, types 14872), 0 lost. The 140 failing
  tests are a thin tail: no code is missing in more than 7 tests, 27 tests fail errors on a single code.
- **Corpora**, re-pinned at current upstreams and clean under the commit's tsc: tsc `src/compiler`, type-fest,
  valibot, react, nest, webpack (checkJs), zod, xstate, drizzle, vue, tsc-src and typescript-eslint (`-b`), vscode,
  excalidraw, tanstack-router all **0**; effect 3 (dtslint TS2578s, plan 19); zod-root = tsc's 53. **Sweep** 8594 /
  8595, **`$ExpectType`** 68628 / 68640, **flagflip** missing / spurious / other 18 / 10 / 4 (of 117k tsc
  diagnostics), scenarios 240 / 0, fresh mutation seeds ~1% productive.
- **Perf** (2026-10-07, same configs): 3.0x (vscode, 6.2 s / 5.8 GB vs 18.3 s / 8.7 GB) to 13.9x (type-fest) faster
  than multi-threaded tsgo 7.0.2, less memory everywhere; work volume within ~15% of tsc's. Weakest: vscode, where the
  four checkers redo ~24% of the type work and the AST is ~600 bytes a line.
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, tsc 6's stable type ordering, mapper instantiation, tsc's inference flow, one circularity stack, contextual
  typing, conditional/mapped/indexed/template/substitution types, deferred references, flow graph + narrowing, module
  resolution + tsconfig, iteration protocol, generators, `this` types, mixins, decorators, union origins, elaboration,
  JSX, tsc's type printer and symbol chains, JSDoc + CommonJS + expandos, parallel loading / resolving / checking,
  declaration diagnostics, project references / `-b`, tsc's laziness and deferred checks. ~124k lines of C.
- **Absent or approximated tsc mechanisms**: the lazy reads of a function's contextual signature (its return type and
  `this` read "the contextual type of the last check", where tsc goes through the call's resolved signature: plan 51
  stages 2-3; the push-down contextual typing itself is tsc's two contextual chains evaluated eagerly, and
  ContextChecked is tsc's rule since plan 51 stage 1); tsc's symbol clones for `export =` / CommonJS (plan 50); permissive
  instantiation; TS7056 and TS5088; member identity (members aren't Symbols; the symbols *harness* rebuilds tsc's
  merges for display, decided acceptable). Out of scope: emit (JS and `.d.ts` text), watch mode, the language
  service, 7.0's removal of deprecated options (until 7.x has a JS-free test oracle).

## Course correction (insights of 2026-10-07, end of the eleventh window)
- **The retarget landed in one window, cleanly**: suite 13303/13540 (5.8) -> 14829/14969 (6.0+ordering), every
  oracle regenerated, every corpus re-pinned and back to 0, creation-order mimicry deleted. No redesign needed.
- **The suite has stopped being the lever.** At 99.07% the remaining 140 tests cost a hack each, and hacks.md keeps
  growing (770 -> 826 -> 870 lines over three windows) while the pass count barely moves. **Decision: no more
  per-test suite drains for their own sake.** Failing tests are fixed by mechanism ports, or picked up by triage when
  cheap; a fix that needs a new special case goes to bugs/ instead.
- **Pay down the debt where it concentrates**: hacks.md's "Calls and inference" and "Expressions and contextual
  typing" (~140 lines) are mostly side channels for one approximation, tttt's push-down contextual instantiation and
  its "contextual type of the last check" (KEY_CONTEXT_*). Two triage agents in a row patched it again. Plan 51
  prototypes tsc's model (ContextChecked, instantiateContextualType, the resolved-signature lookup) and deletes the
  side channels, plan-47 style: prototype the deletion first, measure what breaks (2026-10-08: the push-down turned out
  to be tsc's instantiation evaluated eagerly, ContextChecked landed; the lazy reads are stages 2-3). A second session audits every
  hacks.md entry for hacks whose reason is gone (many predate plans 38, 39, 42, 48).
- **The next oracle is real source code at breadth** (plan 52). The pinned corpora go quiet once drained, and every
  new project found tens to hundreds of bugs; the DefinitelyTyped sweep is declarations plus small tests. A sweep of
  ~100+ real TypeScript repositories (their own tsconfigs, installed dependencies, tsc 4d4f005c's diagnostics cached,
  strict flag flips for the missing direction) is the source-code analogue of plan 44. It also exercises tsconfig/CLI
  shapes the corpora don't. (2026-10-08 prototype, 22 repos / 655 projects: only 344 match tsc, ~2700 missing and
  ~2600 spurious checker diagnostics plus config-level ones; the premise holds.)
- **Perf stays a session per window**: vscode's 3.0x is the margin to defend (checkers redoing type work, AST bytes
  per line).

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
11. ~~Retarget to TypeScript 6.0 + stable ordering (plan 48): suite, oracle caches, re-pinned corpora, perf against
   tsgo 7~~ done.
12. **Now**: contextual-check state (plan 51), export= clones (plan 50), a source-code breadth sweep (plan 52), a
   hacks audit, a perf session (check-phase sharing / load balance on vscode, AST bytes per line).
13. Then: plan 45 stage 3 (declare/link), a token cache / node arenas in the parser, compact instantiated Props / the
   `Signature` split, whatever plan 52's sweep ranks first.
