# Vision

## Goal
A complete TypeScript typechecker in C that is faster than typescript-go (tsgo). Completeness first, but never
by building on a foundation that can't be fast or can't be complete. The tsc baseline tests (errors / symbols /
types components) are the oracle, not the goal; real projects that are clean under tsc are the second oracle.

## State (2026-09-29, insights after the window of plan 38)
- **Suite 12040 / 13540** fully pass (errors 12730, symbols 13003, types 12768); JS tests 898 / 1049. **+1497** in the
  ten sessions since the last insights (10543), the largest window yet, all committed, nothing lost. Almost all of it
  came from sessions fanning out to four Opus sub-agents by failstats/displaydiff/jsstats/syntaxstats bucket (checker
  bugs +263, JS +444, parser +198, types component +490, triage +78). Plan 38 stages 1-2 (tsc's inference flow) landed
  as planned: +13 suite, xstate 394 -> 19 with the corpus sessions.
- **Where the 1500 failures are**: types 772, errors 810, symbols 537 (mean line match 99.5-99.7%: nearly all are
  one or two lines off). Errors: 237 same codes and positions but other text (plan 34), 162 only missing, 149 tttt
  reports nothing, 148 missing+spurious, 102 only spurious; 46 with a spurious TS1xxx. 45 fail on declaration-emit
  diagnostics (TS2742, TS4xxx, isolatedDeclarations TS9xxx), which tttt doesn't have at all (plan 40).
- **Corpora** (all clean under tsc 5.8): tsc `src/compiler`, valibot, react, nest, type-fest, zod **0**; webpack
  **6**; xstate **19** (plan 19 "xstate"); **zod-root** at tsc's 55. They are out of signal again: new ones next.
- **Perf**: 4 checkers on up to 4 threads, output independent of the thread count, parallel loading (plan 36 stage 1).
  Last `./bench.sh` (09-28): tsc corpus 0.48 s (tsgo 1.26), zod 0.45 (2.92). **Instructions crept ~8% since** (tsc
  corpus 4.82 -> 5.21 G single-threaded) across ~20 sessions of fixes with no profile in between. Serial parts: load
  (now pooled), name resolution; the check phase is bounded by the heaviest file.
- **Built and holding**: hand-written backtracking parser (JSX, tsc's lexical diagnostics and regex checks),
  tsc-shaped binder with persistent symbols and merging, interned `Ty` core with id-keyed relations and tsc's identity
  relation, mapper instantiation, tsc's inference flow (lazy fixing, top-level contextual instantiation, intra-expression
  sites), contextual typing, conditional/mapped/indexed/template/substitution types, deferred references, flow graph +
  narrowing, module resolution + tsconfig, iteration protocol, generators, `this` types, mixins, decorators, the missing
  type, union origins, elaboration, JSX, tsc's type printer and symbol chains, JSDoc + CommonJS + expandos, parallel
  checking and loading. ~94k lines of C. `documentation.md` describes it, `hacks.md` lists what is approximated.
- **Absent or approximated tsc mechanisms**: resolve-once (a call is resolved several times, a replay record stands in:
  plan 38 stage 3), one circularity stack (plan 39), declaration-emit diagnostics (plan 40), project references (plan
  41), lazy reverse mapped types, permissive instantiation, intersection reduction by constraint. (The five plan-sized hacks of the
  sixth window's list were all ported 2026-09-29; none needed a plan.)
  Out of scope: emit (JS and `.d.ts` text), watch mode, the language service.

## Course correction (insights of 2026-09-29, end of the sixth window)
- **Health is very good; no redesign.** Sixth clean window, and the biggest. Fan-out sessions (four Opus sub-agents in
  worktrees, one failstats bucket each, 0 lost enforced at merge) are the engine for the long tail: keep one or two per
  window. The long tail is now thin (failing tests are 99.5% line-matched), so pass-count growth will slow whatever we do.
- **The work shifts from "the suite" to "what a real user runs"**: the remaining gaps that matter most are whole
  features rather than per-test bugs. This window plans three of them: declaration-emit diagnostics (every library
  with `declaration: true` gets TS2742/TS4xxx from tsc; plan 40), project references (monorepos run `tsc -b`; plan
  41, with TypeScript's own `src/tsconfig.json` as the corpus), and the circularity stack (plan 39). Plus plan 38
  stage 3 (resolve once), which may also be a perf win. Each gets a planning session that prototypes and may say no.
- **New corpora**: every pinned corpus is at 0 or nearly. Pinning xstate took it 394 -> 19 and found real inference
  bugs; do it again with two corpora of kinds not yet covered (a type-level heavy builder library, a large
  class/overload-heavy application package).
- **Perf needs attention before it becomes a trend**: +8% instructions in a window with no perf session on the checker
  (the window's perf session was the load pool). Profile and claw back before the new features add more. Instruction
  counts at every session end stay mandatory (the triage session caught and gated +1.9% that way).
- **Sub-agent sessions add hacks**: hacks.md went 811 -> 985 in this window before the triage halved it. The plan-sized
  ones get a session of their own (todo entry 5), done as tsc mechanism ports, not more patching.

## Decisions still in force (from earlier course corrections; details in git history of this file)
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
7. **Now**: two new corpora, a perf claw-back, the plan-sized hacks, and planning for circularity (39), resolve once
   (38 stage 3), declaration diagnostics (40) and project references (41).
8. Then: executing 40 and 41, parallel name resolution, the check phase's load balance, the plan 34/35 queues, a
   token cache / node arenas in the parser, memory (the `Signature` split).
