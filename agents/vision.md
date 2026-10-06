# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-10-05, insights after the ninth window)
- **Suite 13099 / 13540** fully pass (errors 13396, symbols 13410, types 13197). +346 in the window, 0 lost. Failing
  components: types 343, errors 144, symbols 130. Checker log_error output: none.
- **Corpora** (all clean under tsc 5.8): tsc `src/compiler`, type-fest, valibot, react, nest, webpack, zod, xstate,
  drizzle, vue, tsc-src and typescript-eslint (`-b`), **vscode** (1.39M lines, pinned this window: 129 -> 0) at 0;
  **effect** (299k lines, pinned this window: 6300 -> 7; three need tsc's base-type resolution stack, one TS2590);
  zod-root at tsc's level. **Breadth sweep** (plan 44, this window): 8579 / 8595 DefinitelyTyped packages match tsc
  5.8's (file, line, code) set exactly, no crash, nothing slower than tsc. **`$ExpectType`** (plan 44, 2026-10-06): 68610 of DT's
  68640 type assertions print as tsc 5.8 prints them (2026-10-06 fan-out); the 30 left are relation side effects on
  creation order, tuple identity by label declarations and a few alias choices (plan 44).
- **Flag flips** (plan 46 stage 1, 2026-10-06): the corpora rechecked under 13 flag sets give 47k real tsc
  diagnostics; tttt misses 103, adds 68 and reports 17 under another code (was 603 / 1460 / 1029 before the first
  drain). The non-strict modes and exactOptionalPropertyTypes were the weak spots.
- **Perf**: callgrind flat over the window except where tsc caches were added (valibot -24%, effect -12%). vscode
  2.2 s / 2.1 GB vs tsgo 6.75 s / 3.7 GB; 1.5-7x faster than multi-threaded tsgo elsewhere. **Memory** below tsgo's
  peak RSS on every corpus (plan 43: -20 to -42%). Name resolution is parallel for TypeScript files (plan 45 stage 1).
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, mapper instantiation, tsc's inference flow (lazy fixing, contextual instantiation, intra-expression sites,
  calls resolved once), one circularity stack (plan 39), contextual typing, conditional/mapped/indexed/template/
  substitution types, deferred references, flow graph + narrowing, module resolution + tsconfig, iteration protocol,
  generators, `this` types, mixins, decorators, the missing type, union origins, elaboration, JSX, tsc's type printer
  and symbol chains, JSDoc + CommonJS + expandos, parallel checking, loading and resolving, declaration diagnostics
  (plan 40), project references / `-b` (plan 41), tsc's laziness and resolution barriers (plan 42), tsc's deferred
  checks (checkNodeDeferred: functions, class expressions, JSX, `void`). ~113k lines of C.
- **Absent or approximated tsc mechanisms**: contextual instantiation (push-down, not tsc's leaf-wise pull; type_core.md); permissive
  instantiation; TS7056 and TS5088; member identity (members aren't Symbols; the symbols *harness* rebuilds tsc's
  merges for display, decided acceptable). Out of scope: emit (JS and `.d.ts` text), watch mode, the language service.

## Course correction (insights of 2026-10-05, end of the ninth window)
- **Health is good; no redesign.** Ninth clean window: all ten entries landed, 0 lost, callgrind flat, two corpora
  pinned and drained the same session each, the sweep built and drained to 99.8%. src grew ~6k lines; hacks.md
  725 -> 770 lines after a re-check.
- **Last window's bet paid off**: widening the oracles (effect, vscode, the sweep) found more real bugs per session than
  the suite tail did. Now those are drained too, and **all of them only test one direction**: clean code can only show
  spurious diagnostics. The blind spot is *missing* diagnostics on real code, the thing a user replacing tsc relies on.
- **This window: oracles for missing diagnostics** (plan 46). Prototyped in this session: rerunning tsc's own sources
  with `--noUncheckedIndexedAccess` gives 1233 real tsc errors (tttt: 25 missing, 5 spurious) and with
  `--exactOptionalPropertyTypes` 747 (tttt never reports TS2412/TS2375, the flag's own codes). Flag flips over every
  corpus are the cheap first stage (and non-strict modes, which no corpus runs); mutation testing is the planned
  second. Also `$ExpectType` (plan 44 stage 3 item 2): a *types* oracle at breadth (69.6k assertions).
- **One mechanism to plan rather than patch**: speculative checks (plan 47). Planned 2026-10-06 and answered no: the
  feared memo side effects of checking a literal under a trial contextual type weren't real (a literal's memo goes by
  its contextual type); what was missing were two of tsc's skip-round rules (regular object literals, nested generic
  calls skipped). The hack is gone and nothing replaced it. Lesson: prototype the deletion before designing the
  mechanism.
- **Effect's last 7 and the plan 35 queue stay open; the suite tail gets one fan-out.**

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
- **Binder**: declare and link are serial and write shared tables; the resolve walk shards per file for TypeScript
  (plan 45: JavaScript files and merged type parameters still walk serially; `hacks.md`, "Parallelism contract"). JS support will add declarations from assignments; design it with that in mind.
- **Threading**: checking is threaded (stage 3; nothing in the checker had to change once stage 2's audit fixes were
  in, which is evidence the "AST read-only while checking" contract holds). Any new checker-wide memo that *reports*
  must be tagged `Error.once_key`, and anything written to AST/binder state during checking is a race (the frozen
  guards log it; TSan on the corpora is the net: `plans/36_parallelism.md` has the build line). Loading is
  threaded too (a cache-warmer pool beside the unchanged sequential loader: `src/load_pool.c`; the shared state is the
  resolver's locked caches). Name resolution shards per file for TypeScript (plan 45 stage 1); JavaScript files and declare/link are serial.

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
8. ~~Declaration diagnostics (40), project references (41) with tsc-src and typescript-eslint, laziness planned (42)
   and stages 1-2, memory like-for-like, the types/symbols long tail~~ done.
9. ~~Wider oracles (effect, vscode, the breadth sweep of plan 44), plan 42 stages 3-4, AST memory (43), parallel
   name resolution (45 stage 1)~~ done.
10. **Now**: oracles for missing diagnostics (46: flag flips, then mutations), `$ExpectType` at breadth (44 stage 3),
   speculative checks (47: not needed), two new corpora (a JSX application, an inference-heavy router/RPC library), a perf session.
11. Then: plan 45 stages 2-3 (JS resolve, declare/link), the check phase's load balance and file-to-checker affinity,
   a token cache / node arenas in the parser, compact instantiated Props / the `Signature` split.
