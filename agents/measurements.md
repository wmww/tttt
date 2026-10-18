# Measurements
The latest of each kind of measurement. **Replace a section when re-measuring** (keep its date and commit); don't
append history, git has it (this file before the 2026-09-27 triage holds every entry since plan 05).

## Machine and caveats
AMD EPYC-Turin, 4 vCPUs = 2 cores x 2 SMT threads (KVM), 31 GB; Ubuntu 26.04, GCC 15.2, node 22.22. The oracle (tsc)
is tsgo 7.1.0-dev fed0bf24 (`thirdparty/typescript`, plan 54; every cache since 2026-10-09). `scripts/bench.sh` compares against the pinned tsgo only (since 2026-10-12). Wall times drift by ~15% between days (a shared machine) and anything running alongside skews
them: compare walls only within one session. Callgrind instruction counts (`valgrind --tool=callgrind ./release/tttt
-q --keep-going -p DIR`) are the stable measure across sessions. Neither side is clean on webpack, drizzle, vue, effect and
vscode (exit 2): the same (file, line, code) on both (realworld.py), so equal work, but webpack's and drizzle's
elaboration text differs (`scripts/clidiff.py --corpora`). **The 12-core machine** (the human's
Framework laptop, Ryzen AI 9 HX 370, 12 cores / 24 threads) is measured only when the human says a session runs
there (`nproc` 24): its section below; it is the one to read for core scaling.

## Perf session: serial declare, error merges, shared warm state (2026-10-18, todo entry 8)
- webpack declare (`TTTT_RESOLVE_TIMES`, defaults): 40 -> 18 ms (the CommonJS export walk at parse time); link 7,
  walk 52 unchanged.
- Error merges (`error_ctx_merge()`, one pass): inbox-zero 1.29 -> 0.85 s (bun 0.86), its resolve step's shard merge
  525 -> 36 ms; inbox-zero/apps/web 1.28 -> 0.86 s (bun 0.97). Both report ~22k errors (tsc too).
- Shared warm state: perf_leads.md "Shared warm state at the defaults" (K1 / K4 / K2-off / K4-off / bun on dokploy,
  monkeytype, karakeep; five policies prototyped, all reverted).
- Bench: plan 67 "Where it stands" (tttt 3-5% above the 2026-10-17 table there, machine state: start and end commits
  time the same interleaved). Instructions (callgrind) all within 2% of test/instrcount.txt (drizzle +1.34% as at
  entry 7, vue +0.47, effect -0.36, vscode -0.07), not re-saved. Oracles at the window's starting point (suite 13826,
  corpora 0/0 but vue 0/2, DT 8595, `$ExpectType` 68639, flagflip 18/8/3, source sweep sample 874/878, dts 1245/1274,
  kdiff 1 (inbox-zero), recycle 4 / 3634).

## Refresh 2, declaration-emitting libraries and checkJs projects (2026-10-18, todo entry 7)
25 repositories by a third rule (repos.txt's "Refresh 2" comment, candidates from `scripts/reposearch.py`): 16
TypeScript libraries whose root tsconfig.json emits declarations, 9 JavaScript projects with checkJs (rarer: 19 topics
searched for 9); 3 dropped for their installs. 68 projects, 41 of them dtsdiff projects. **Day one (35d49066,
before any fix): source sweep 66 / 68 (97.1%), 20 / 22 repos; dtsdiff 34 / 41 projects, 4007 / 4100 files** (the
mismatching files mostly the checkJs projects' JavaScript emit, plus vest). Root causes on the sweep, all JavaScript:
`export default async x =>` parsed with an `async` modifier (TS1042), `this.f = this.f.bind(this)` declaring a
member beside the method (TS7008), JSDoc-typed `export default {...}` related without elaboration (TS2322 position),
and an order-dependent TS7031 (bugs/context_on_demand.md). **After: 67 / 68**, the whole sweep 3626 / 3634 (the old 7
plus rocket), dtsdiff 1245 / 1274. Both refreshes say the same: the checker generalizes to new TypeScript (>= 96%);
JavaScript is where new code finds bugs (3 of 4 root causes here). Instructions flat (tsc 4.483, type-fest 10.469,
webpack 9.237, drizzle 6.512 +1.3%, effect 9.351, vscode 88.314 G; all within 2% of test/instrcount.txt); every other
oracle at the window's starting point.

## Deferred bodies: drops leak nothing, single-body re-materialization (2026-10-18, todo entry 1)
- Drop leak (malloc a block_drop() walk would still free at exit, vscode K1 T1): 12.6 -> 0.7 MB after sealing
  literals/templates/object keys and moving the walk's lists into the arena; valgrind on vscode K1 then ~62 KB
  (class member names, binding keys, namesakes: fixed). Live malloc at exit 169.3 -> 151.3 MB. No peak change from
  this alone (freed malloc isn't returned).
- Single-body re-materialization (`TTTT_DEFER_STATS`, vscode): defaults 1276 files re-materialized, 43k bodies, 0.80 s
  CPU -> 3.4k single bodies, 34 completions, 0.16 s; K1 16k -> 1.2k bodies. Of 18.5 MB of re-materialized body source
  at the defaults 3.5 MB was demanded, 18.4 MB of it in `singly` files.
- Session start (538aaee5) -> now, `/usr/bin/time`: vscode defaults 6.17 / 1253 MB -> **6.10 / 1221**; K1 (default
  threads) 11.55 / 1516 -> **11.32 / 1475**. K1 T1 resident after load+parse / after names / peak MB, then defaults
  wall and peak: vscode 765/839/1402 -> 769/843/1369, defaults 6.18 s 1220 -> 6.11 s 1192; effect 133/175/504 ->
  133/176/507, 0.61 s 332 -> 0.60 s 329; webpack 218/269/515 -> 220/271/517, 0.63 s 419 -> 0.62 s 420; drizzle
  91/108/389 -> 90/108/389, 0.60 s 354 -> 0.59 s 345; type-fest 43/50/418 -> 43/50/417, 0.48 s 411 -> 0.49 s 413.
- `--checker-memory 0` recycles after every file again (the warm-up rule had stopped it in small programs): the
  suite at -m 0 drops 6.1k materializations (111 re-materialized, 136 single bodies), and `scripts/oracles.py`'s
  recycle line is 4 / 3566 (inbox-zero, sequelize x2's TS7022, dokploy flaky), the same with single bodies off.
- Oracles: suite 13792 (--no-defer-bodies, -m 0: 0 differ), corpora 0/0 (vue 0/2), DT 8595, `$ExpectType` 68639,
  flagflip 18/8/3, source sweep sample 856/860, dts 1193/1233, kdiff 1 (inbox-zero), threadcheck identical,
  `TTTT_AST_POISON=1 --checker-memory 0` and K1 -m 0 identical to the defaults on 12 corpora.
- Instructions (callgrind, saved): vscode 89.70 -> **88.41 G (-1.45%)**, the rest within +-0.5% (valibot +0.44, nest
  +0.28: the string seal's copy).

## Perf session paying for the budget (2026-10-17, todo entry 8)
The 4-vCPU box. **Instructions** (`scripts/instrcount.sh`, callgrind, K1 T1; now saved in `test/instrcount.txt`, which
the script compares against and flags changes over 2%) b4129d18 -> now: tsc 4.609 -> **4.479**, type-fest 10.992 ->
**10.483**, valibot 3.469 -> **3.276**, react 1.407 -> **1.356**, nest 1.079 -> **1.056**, webpack 9.578 -> **9.231**,
zod 5.172 -> **4.851**, xstate 2.681 -> **2.530**, drizzle 9.773 -> **6.418 (-34%)**, vue 6.521 -> **6.216**, effect
9.803 -> **9.330**, vscode 93.606 -> **89.707 (-4.2%)**, excalidraw 7.683 -> **7.344**, tanstack-router 5.273 ->
**4.978 G**. From: arena_alloc()'s bump, ty_instantiate()'s early exit and ty_members()' resolved case inline (they
paid a full frame per call: 6M, 9M, 17M calls on zod/drizzle), instantiated members reusing their source's by-name
order, eight hot (kind, pointer) caches moved from `links` to `node_links`, a two-operation intern hash. **drizzle's
+50%** (6.48 G at entry 4, 9.77 at entry 6) was 966d0334: an intersection target's combined properties didn't see the
intersection as `this`, so a variance measurement related ever longer intersections (PgColumnBuilder 14.4k
comparisons, tsgo 1.2k, now 1.5k); fixing it needed tuples to take a this-argument too (a spurious TS2352 on vueuse
otherwise). drizzle single-threaded 1.24 -> 0.91 s, its relation cache 413k -> 218k pairs.
**The budget's cost at the defaults**: corpora sum 11.43 s at the session's start against 9.99 with
`--checker-memory off` (+14%: re-derivation, perf_leads.md "What the recycle budget costs"); now 11.02 s / 4604 MB against 9.74 / 9887 (+13%). drizzle at the defaults 0.75 -> 0.59 s, peak 343 -> 361 MB (19 recycles instead of 24: each budget now lasts more files, so fewer doublings settle a little higher).
Oracles unchanged (suite 13791 / 13828 with two new tttt tests, corpora, DT 8595, `$ExpectType` 68639, flagflip
18 / 8 / 3, source sweep 3559 / 3566, kdiff and recycle 1 / 3566, dts 1193 / 1233); the perfsweep's worst types ratio
2.31 (better-auth's drizzle-adapter, the same bug) is 1.62 again. Bench: plan 67 "Where it stands".

## Checker work stealing (2026-10-17, todo entry 7)
Plan 72 "Checker balance: work stealing". Defaults on this box (4 checkers, best of 3, wall s, without -> with):
webpack 0.68 -> 0.62, zod 0.43 -> 0.38, vue 0.42 -> 0.36, effect 0.67 -> 0.61, tanstack-router 0.36 -> 0.32, drizzle
0.81 -> 0.78, valibot 0.27 -> 0.26, excalidraw 0.57 -> 0.55, vscode 6.42 -> 6.28, type-fest 0.49 -> 0.50, xstate
0.21, **tsc 0.29 -> 0.31** (SMT: see plan 72). Peak RSS equal or lower but webpack 394 -> 408 MB (tanstack-router 264
-> 226). Source sweep's 285 projects with a K1 arena over 40 MB (`perfsweep.py --defaults-only --min-arena 40`, one at
a time, `TTTT_NO_STEAL=1` for the base): wall total -5.2% (102.5 -> 97.2 s), median -5.6%; peak RSS +0.4% total,
median -0.6%, p90 +10% (keeping the thief's checker: -7.1% wall, +3.8% peak). Every oracle unchanged (flagflip 18 / 8
/ 3, one run 18 / 9 / 3: the known TS5115 flicker). Instructions (K1, no stealing) flat: vscode 93.606 G, the rest
within 0.05% of the hacks audit's.

## Hacks audit (2026-10-17, todo entry 6)
Eleven checker-side guesses replaced by tsc's rule (merged from four worktrees): statement errors at function/namespace
names, `alias_is_local()`, `super(...)` constructor arity filtering, ContainsSpread as a literal flag, `f(x.push)`
evolving arrays, `Awaited<T>` where tsgo has it, any-constructor bases' index signature (`ty_has_unresolved_base()`
gone), check_access's generic mapped gates, `has_inferable_index` (CommonJS `module`), IntersectionState.Source into
members. Suite 13773 / 13812 -> 13789 / 13826 (0 lost, 11 new tttt tests), every oracle unchanged (corpora, DT 8595,
`$ExpectType` 68639, flagflip 18 / 8 / 3, source sweep 3559 / 3566). Instructions (callgrind, K1): tsc 4.609 G,
type-fest 10.987, valibot 3.469, react 1.406, nest 1.079, webpack 9.576, zod 5.172, xstate 2.681, drizzle 9.772, vue
6.522, effect 9.803, vscode 93.618 (93.954 at entry 5), excalidraw 7.683, tanstack-router 5.273.

## Gated runs without the restart (2026-10-17, todo entry 5)
A run gated by options or syntax diagnostics (1397 of the source sweep's 3566 projects have TS1xxx/TS5xxx/TS6xxx under
tsc) parsed and resolved everything twice (the restart without deferral); it now parses only the deferred bodies for
syntax errors. Source sweep, summed per-project wall at `-j` default (same load both): gated projects 301 -> 227 s
(-25%), all projects 738 -> 661 s (-10%); angular-cli (TS5108) 1.44 -> 0.24 s. Outputs unchanged (sweep 3559 / 3566,
`clidiff --emit` 400 / 410 at both builds). Instructions flat (the corpora aren't gated): vscode 93.954 -> 93.953 G.

## Plan 81 stage 2, cheaper recycles (2026-10-17, todo entry 4)
The 4-vCPU box, against 38452d50 (stage 1) run alternately. **Two density fixes, merged.** `import * as ns` had a
namespace type of its own per import (make_gather_target()) whose members were rebuilt per import: effect made 3068
namespace types, 317k export props (255 imports of Schema's 423 exports), the 42 MB `namespace_export_prop` line; they
now return the module namespace type's members. Mapper lists go through a 1024-entry content-checked cache
(`mapper_list()`): effect's copied lists 37 -> 8 MB (headers stay, 22.6 MB: a shared mapper would hit the active-mapper
cache where tsc's fresh one doesn't). Effect's K1 arena 359 -> 279 MB; K1 uncapped peak effect 691 -> 612, zod 456 ->
434 MB, wall -3% / 0. At K1's 256 MB budget peak is the budget (effect 537 / 539). Defaults, best of 3, wall / peak /
recycles: effect 0.69 / 342 / 26 -> 0.69 / 327 / 24, zod 272 -> 258 MB, xstate 193 -> 184, webpack 401 -> 397, others
within noise (vscode 1148 -> 1170, 131 -> 124 recycles; drizzle 317 -> 312, type-fest 439 -> 436). K1 peaks
(budgeted, best of 2): effect 537 -> 539, drizzle 400 -> 402, webpack 537 -> 529, vscode 1454 -> 1461, walls equal; the
phases before checking unchanged. **The tail (174 projects, defaults): wall -0.4%, recycles
1525 -> 1479, summed peak -0.7%**: density barely moves it.
**What a recycle costs is the rebuild, not the recycle**: `checker_recycled()` itself (drop, new, global types) is ~1 ms
(hop-protocol 39 recycles 33 ms, renovate 40 / 35, mikro-orm 51 / 62, summed over 4 threads). What costs is the next
segment re-deriving a shared graph: react-select's tests rebuild csstype/React (~17-20 MB, ~120 ms) in the first file
that touches it, often not the segment's first file (so the warm-up guard misses it). Same tail at
`--checker-memory 256` (the old 4 x 96): mikro-orm 5.56 -> 4.78 s, renovate 1.38 -> 1.15 (50 -> 4 recycles, 528 -> 805
MB), hop-protocol 1.42 -> 1.13, react-select's tests 0.68 -> 0.27.
**Tried, not merged** (a policy point on stage 1's frontier, the insights' call): the warm-up as the most any one file of
the segment added (not just its first file's footprint). Tail -5.8% wall, recycles 1525 -> 1119, summed peak +6%;
corpora wall -0-5%, peaks drizzle 315 -> 347 and excalidraw 265 -> 288 (both above bun's then), type-fest 443 -> 496,
tsc 220 -> 241, effect/zod/xstate/vue down 5-17 MB. About stage 1's "16 g4 b32" point.
Instructions (`scripts/instrcount.sh`, K1) against stage 1: tsc 4.607, type-fest 10.952 (+0.1%), valibot 3.458, webpack
9.573, zod 5.176 (-3.3%), drizzle 6.484 (-2.2%), effect 9.795 (-3.2%), vscode 93.954 G (-0.4%). Oracles unchanged.

## Plan 81 stage 1, the budget at several checkers against bun (2026-10-17, todo entry 3)
The 4-vCPU box, 4 checkers, bun 1.4.3 at its defaults (`scripts/perfsweep.py --defaults-only --bun PATH`, which now
runs `bun check` from an empty directory: in a corpus whose package.json has a `check` script, `bun check` runs the
script). Corpora best of 2, wall s / peak MB / recycles; "16 b16" = 16 MB each, a checker's budget doubling after a
segment of fewer than 16 files (up to the split default, 96 MB); "g4" = the warm-up guard at 4x instead of 2x:
| | 96 (before) | 32 | 24 | 16 | 16 b16 (**chosen**) | 16 g4 b32 | bun |
|---|---|---|---|---|---|---|---|
| drizzle | 0.55 / 511 | 0.68 / 301 | 0.71 / 321 | 0.73 / 321 | 0.67 / 313 | 0.61 / 476 | 1.00 / 329 |
| effect | 0.68 / 604 | 0.69 / 363 | 0.69 / 346 | 0.71 / 336 | 0.69 / 339 | 0.69 / 422 | 1.37 / 311 |
| excalidraw | 0.47 / 467 | 0.52 / 316 | 0.56 / 290 | 0.60 / 263 | 0.59 / 258 | 0.56 / 298 | 0.83 / 274 |
| nest | 0.12 / 150 | 0.12 / 149 | 0.12 / 152 | 0.11 / 149 | 0.11 / 151 | 0.12 / 151 | 0.14 / 177 |
| react | 0.12 / 175 | 0.12 / 174 | 0.13 / 167 | 0.15 / 157 | 0.15 / 158 | 0.12 / 169 | 0.37 / 292 |
| tanstack-router | 0.34 / 391 | 0.34 / 266 | 0.37 / 254 | 0.38 / 262 | 0.38 / 261 | 0.36 / 257 | 0.74 / 330 |
| tsc | 0.30 / 273 | 0.30 / 277 | 0.30 / 239 | 0.30 / 218 | 0.30 / 220 | 0.31 / 230 | 0.47 / 215 |
| type-fest | 0.50 / 499 | 0.50 / 487 | 0.50 / 442 | 0.50 / 448 | 0.50 / 442 | 0.49 / 511 | 2.03 / 555 |
| valibot | 0.25 / 310 | 0.26 / 210 | 0.26 / 181 | 0.28 / 150 | 0.27 / 158 | 0.27 / 186 | 0.47 / 151 |
| vscode | 5.90 / 1527 | 6.40 / 1161 | 6.73 / 1072 | 7.17 / 1049 | 6.71 / 1134 | 6.59 / 1251 | 7.91 / 1531 |
| vue | 0.41 / 390 | 0.42 / 274 | 0.41 / 233 | 0.43 / 203 | 0.43 / 217 | 0.43 / 210 | 0.57 / 217 |
| webpack | 0.64 / 616 | 0.66 / 417 | 0.67 / 398 | 0.69 / 366 | 0.70 / 401 | 0.69 / 416 | 1.11 / 353 |
| xstate | 0.19 / 271 | 0.20 / 233 | 0.22 / 202 | 0.24 / 193 | 0.24 / 195 | 0.21 / 253 | 0.29 / 164 |
| zod | 0.39 / 490 | 0.43 / 272 | 0.47 / 251 | 0.48 / 210 | 0.44 / 274 | 0.45 / 264 | 0.70 / 311 |
| **sum** | 11.3 / 7184 | 12.2 / 5191 | 12.7 / 4822 | 13.4 / 4624 | 12.7 / 4865 | 12.4 / 5448 | |
| at or below bun (of 14) | 4 | 7 | 7 | 10 | 9 | 7 | |
(zod-root aside: 0.45 / 510 -> 0.55 / 344, bun 0.78 / 340; 64 and 48 MB: sums 11.6 / 6441 and 11.8 / 5748.) The source
sweep's tail (174 projects whose K1 arena is over 64 MB, one run each; the default rerun moved -0.6%):
| | wall vs before | ratio median / p90 / worst | faster than bun | peak at or below bun | peak sum | recycles |
|---|---|---|---|---|---|---|
| 96 (before) | 79 s | 1.00 | 169 (97%) | 33 | 64.6 GB | 155 |
| 32 | +17% | 1.09 / 1.30 / 2.47 | 167 | 53 | -16% | 1067 |
| 16 | +28% | 1.14 / 1.52 / 4.43 | 163 (94%) | 87 | -25% | 2264 |
| 16 g4 | +14% | 1.10 / 1.36 / 4.33 | 168 | 77 | -21% | 1647 |
| 16 b8 | +17% | 1.13 / 1.42 / 2.84 | 166 | 83 | -24% | 1778 |
| 16 b16 (**chosen**, as built) | +14% | 1.09 / 1.40 / 2.88 | 167 (96%) | 76 | -22.5% | 1525 |
| 16 b32 | +11% | 1.08 / 1.35 / 2.33 | 167 | 71 | -21% | 1387 |
| 16 g4 b32 | +8% | 1.07 / 1.27 / 2.35 | 167 | 65 | -19.5% | 1254 |
| 24 b32 | +10% | 1.09 / 1.31 / 1.92 | 167 | 62 | -18% | 1042 |
**The gate (10 / 14 corpora below bun, faster everywhere, tail within +10%) is missed by every policy**: the corpora
want low budgets and the tail wants few recycles. What the tail pays for: the guard's "2x the segment's first file"
protects almost nothing at 16 MB (the first file mostly holds under 1 MB: react-select's tests build their ~20 MB of
React types over several files, then recycle after 2-5 files, ~100 ms per recycle, 0.28 -> 1.25 s at plain 16).
Doubling a checker's budget after a short segment helps those (react-select 1.25 -> 0.81 s) but lets drizzle and
type-fest grow back. Chosen: 16 b16 (corpora 9 / 14, valibot and tsc within 5%; tail +14%, 96% faster than bun),
the Pareto middle; stage 2 (cheaper recycles) is what moves the tail. Not faster than bun at it (7): inbox-zero and
its apps/web (1.45 s vs 0.9, as slow before: not the budget), webcmd and solid (likewise), react-select's tests (0.28
-> 0.81 vs bun 0.25, the budget's worst), mikro-orm/packages/decorators and ethereumjs-monorepo (ties).
K1 is unchanged (`program_set_checker_memory_shared()` only applies to several checkers, and only when
`--checker-memory` isn't given: every oracle passing it explicitly runs as before).
`scripts/bench.sh --runs 3 --bun` (`/usr/bin/time` peaks, no Python parent): plan 67 "Where it stands", 9 / 14 at or
below bun at the defaults. Peak at the defaults before -> after: vscode 1560 -> 1161, effect 605 -> 350, webpack 621 ->
406, drizzle 511 -> 321, type-fest 501 -> 445 MB; K1 and the phases before checking unchanged. Instructions
(`scripts/instrcount.sh`, K1): tsc 4.608, type-fest 10.939, valibot 3.460, webpack 9.601, zod 5.352, drizzle 6.628,
effect 10.121, vscode 94.348 G (K1 doesn't run the new code). Oracles unchanged but flagflip 18 / 6 / 3 -> 18 / 8 / 3:
one more TS5115 in xstate's two non-strict cells, the depth-limit error reported once per fresh checker (triage.md).

## Plan 81 planning: the checker's density census, bun's thread scaling, the budget frontier (2026-10-17, todo entry 2)
The 4-vCPU box, ad4e1701, no product code. **Census at one checker without recycling** (`scripts/arenasites.sh -p P
--checker-memory 100000`; its site table is process-wide, so binder_new_symbol/scope_*/symbol_* lines are the
program's, not the checker's). Arena MB at the end / types; the biggest checker sites, MB:
| | valibot | webpack | zod | excalidraw | effect | vscode |
|---|---|---|---|---|---|---|
| arena / types | 137 / 224k | 182 / 320k | 276 / 369k | 140 / 151k | 359 / 533k | 1482 / 2.39M |
| `ty_alloc` (types, ~108 B avg) | 22.6 | 35.9 | 40.7 | 16.8 | 57.3 | 277.5 |
| instantiated Props (`instantiate_members`, 56 B) | 22.9 | 22.9 | 63.3 | 11.2 | 30.8 | 134.9 |
| instantiated Signatures (160 B) | 2.9 | 5.5 | 14.3 | 2.4 | 13.4 | 27.9 |
| `builder_finish` (Members 72 B, props_by_name, arrays) | 13.6 | 20.1 | 32.6 | 12.2 | 31.5 | 148.8 |
| object literal Prop+PropDecl (152 B) | 8.3 | 12.9 | 3.7 | 27.6 | 5.8 | 183.5 |
| namespace export Prop+PropDecl (152 B) | - | 3.6 | 7.4 | - | **42.1** | 18.2 |
| mappers (24 B header + both lists copied) | ~8 | ~4 | ~16 | ~6 | **~60** | ~95 |
| intern keys (`intern_slot`) | 18.1 | 23.1 | 24.6 | 20.6 | 32.7 | 173.4 |
| PropDeclRare (160 B) | 5.5 | ~1 | 10.9 | 7.3 | 3.6 | 14.9 |
| CallResolution (`call_return_type`) | 1.3 | 2.9 | ~1 | 2.7 | 5.0 | 54.5 |
Mappers on effect (ty_list_copy and ty_mapper_new inlined in a scratch build to attribute them): ~990k mappers, 22.6
MB of headers + 2 x 18.5 MB of copied lists; `inference_mapper_of` 27 MB of it. The checker's malloc on top (memtrack
`--phase typecheck`, and a scratch `--time` line per checker): intern tables + node links + relation caches + LazyMemo
pages are 5-17 MB per checker at the defaults (zeroed_table: 56 MB on effect at K1 without recycling), materialized
bodies 10-15 MB per program.

**Priced density items** (arena MB saved at K1, mid estimates): instantiated Prop 56 -> 40 B (drop `parent`/`decl`:
valibot 6.5, webpack 6.6, zod 18, excalidraw 3.2, effect 8.9, vscode 39); instantiated Signature 160 -> ~64 B (1.7,
3.3, 8.6, 1.4, 8, 17); object literal PropDecl compact (~-35%: 2.9, 4.5, 1.3, 9.7, 2, 64); namespace export props
without a PropDecl (0, 2.2, 4.4, 0, 25-42, 11-18); mappers sharing stable lists, arrays inline (4, 2, 8, 2, 20, 30);
instantiated members' props_by_name from the source's permutation (3.3, 5.4, 9.5, 3.5, 5.8, 30); intern keys -20%
(3.6, 4.6, 5, 4, 6.5, 35); per-kind Ty layouts -15% (3.4, 5.4, 6, 2.5, 8.6, 42). **All eight: valibot 18%, webpack
19%, zod 22%, excalidraw 19%, effect 24%, vscode 18% of the arena**, less of arena-plus-malloc, ~10-13% of the peak at
the defaults; most of them touch hundreds of readers (`prop->name`, `prop->decl`, Signature fields). Under the 25% gate.

**Per-type cost is bun's already at one thread** (`/usr/bin/time` peak MB, wall s, user s; tttt K1 = `--checkers 1`
at its 256 MB budget):
| | tttt K1 | bun T1 | bun T2 | bun T4 | tttt K4 (defaults) |
|---|---|---|---|---|---|
| valibot | 276 / 0.45 / 0.48 | 285 / 1.03 / 0.95 | **150** / 0.65 / 1.23 | 159 / 0.48 / 1.72 | 317 / 0.25 / 0.73 |
| effect | 556 / 1.29 / 1.50 | 380 / 3.19 / 3.05 | **278** / 1.83 / 3.52 | 324 / 1.41 / 5.17 | 616 / 0.68 / 2.06 |
| excalidraw | 392 / 0.77 / 0.95 | 369 / 1.51 / 1.39 | **263** / 1.12 / 2.06 | 282 / 0.83 / 2.87 | 483 / 0.52 / 1.50 |
bun's memory *falls* from one thread to two while its CPU time rises 15-50% (T4: +70-110%): at more than one thread
it re-derives instead of keeping (its T1 RSS climbs steadily through the check, its T4 RSS is flat). tttt's K4 holds 4
budgets' worth. **Duplication across K** (sum of the 4 checkers' arenas at the defaults, no recycle, against K1's
whole arena): valibot 174 vs 137 (+27%), webpack 239 vs 182 (+31%), excalidraw 209 vs 140 (+49%): a shared frozen
declared layer saves at most that slice (37-69 MB), less than the budget does.

**The budget frontier at the defaults** (4 checkers, `--checker-memory MB` each; wall s / peak MB, one or best of 2):
| | 96 (now) | 48 | 32 | 24 | 16 | bun T4 |
|---|---|---|---|---|---|---|
| valibot | 0.24 / 318 | 0.25 / 281 | 0.27 / 214 | 0.26 / 186 | 0.28 / **151** | 0.48 / 159 |
| effect | 0.68 / 622 | 0.68 / 436 | 0.67 / 373 | 0.71 / 357 | 0.70 / 351 | 1.41 / 324 |
| excalidraw | 0.47 / 480 | 0.50 / 382 | 0.52 / 327 | 0.56 / 293 | 0.60 / **278** | 0.83 / 282 |
| webpack | 0.65 / 629 | 0.65 / 484 | 0.68 / 430 | 0.68 / 407 | 0.70 / 381 | 1.09 / 365 |
| drizzle | 0.57 / 527 | 0.61 / 359 | 0.69 / **312** | | 0.73 / 325 | 0.99 / 332 |
| zod | 0.39 / 500 | 0.42 / 326 | 0.43 / **287** | | 0.49 / 220 | 0.69 / 322 |
| type-fest | 0.50 / 513 | 0.49 / 435 | 0.50 / 500 | | 0.50 / 464 | 2.02 / 578 |
| vscode | 6.07 / 1573 | 6.33 / **1270** | 6.49 / 1168 | | 7.17 / 1077 | 7.81 / 1552 |
(bun T4 column: plan 67 "Where it stands" except valibot/effect/excalidraw, measured here.) At 32 MB tttt is at or
below bun on 5 of these 8 and within 1.18x on the rest, every one still faster than bun (vscode 6.49 vs 7.81 s). Not
measured here: the source sweep's 169 biggest, where entry 7 found 64 MB +9% wall (p90 +34%), and bun on them.
A side finding (triage.md): in a worktree whose `thirdparty/corpora` is a symlink, tttt loads ~300 of effect's files
twice (3165 errors; tsc 5).

## Plan 77, fresh repositories (2026-10-17, todo entry 1)
28 repositories by a second rule (repos.txt's "Plan 77" comment: GitHub topics game, webgl, electron, discord-bot, cli,
angular, nestjs, data-visualization, monorepo, vscode-extension; 300-3000 stars; the first 3 per topic with a root
lockfile; 30 picked, 2 dropped for their installs: everroom's pnpm git dependency, thorium-reader's npm >= 12), 203
projects. **Day one (abef93ee, before any fix): 195 / 203 projects match (96.1%), 26 / 28 repos**, 0 crashes, 0
timeouts, 8 spurious, 0 missing. Two root causes, both checker, no config-level (TS5xxx/TS6xxx, module resolution)
mismatch at all: +TS2503 (5 ethereumjs projects: `import type { debug } from "debug"`, an `export =` property, then
`debug.Debugger`: tsc's lookup goes past the import to the UMD global) and +TS2344 x ~800 (3 hop-protocol projects,
@aws-sdk's `extends $Command<I, O, C>`: an expression's type arguments were checked without the missing ones'
defaults). **After: 203 / 203**; the whole sweep 3559 / 3566 (the old 7, unchanged). By plan 77's scale (>= 90%) the
checker generalizes: the next refresh is cheap and needs no fan-out. Perf on them (perfsweep, 59 projects tsgo checks
with >= 1000 types): check median 0.37, worst types ratio 1.49 (rpg-js vue-gui, 1886 vs 1262 types, tiny), RSS median
0.64. The sweep's "slower than tsc" rows there were load (8.6 on 4 cpus) and webcmd's committed `.tsbuildinfo` (tsc
skips checking: triage.md). Instructions (instrcount) unchanged from entry 9 to 0.01 G on every corpus (vscode
94.346 G); every oracle unchanged.

## Perf session paying for the window (2026-10-17, todo entry 9)
The 4-vCPU box. **Instructions** (`scripts/instrcount.sh`, callgrind, K1 T1) at the window's start (83923e4d) -> entry 8
(5a01c278) -> now: tsc 4.546 -> 4.624 -> **4.606**, type-fest 14.099 -> 14.086 -> **10.939 (-22%)**, valibot 3.470 ->
3.495 -> **3.460**, react 1.424 -> 1.416 -> **1.403**, nest 1.099 -> 1.089 -> **1.079**, webpack 9.677 -> 9.647 ->
**9.598**, zod 5.339 -> 5.375 -> **5.354**, xstate 2.690 -> 2.705 -> **2.665**, drizzle 6.628 -> 6.644 -> **6.628**, vue
6.488 -> 6.532 -> **6.512**, effect 10.127 -> 10.243 -> **10.120**, vscode 92.440 -> 95.517 -> **94.343** (+2.1% over
the window's start: deferred bodies' re-materialization ~1.7% and skip scan ~0.9%, perf_leads.md "Deferred bodies"),
excalidraw 7.658 -> 7.709 -> **7.688**, tanstack-router 5.300 -> 5.325 -> **5.263 G**. From: tuples' members (weak and
string-index-only checks skip them, props_by_name merged from cached runs, packed target keys, whole splices) and the
parser's whitespace hop leaving at an ASCII byte. **Memory** (`--time` RSS MB after parse / after names / peak
`/usr/bin/time` / wall s; K1 = `--checkers 1` at 4 threads): vscode 790 / 846 / 1499 / 11.75, defaults 788 / 847 /
1559 / 5.80; effect 139 / 185 / 555 / 1.24, defaults 139 / 184 / 616 / 0.68; webpack 226 / 284 / 551 / 1.08, defaults
226 / 290 / 641 / 0.64; drizzle 97 / 117 / 410 / 0.80, defaults 95 / 119 / 523 / 0.54; type-fest 48 / 63 / 453 / 0.86,
defaults 48 / 65 / 513 / 0.49. vscode's defaults peak is ~50 MB above entry 8 (1512 -> 1560 alternating with its
binary) and type-fest's K1 ~40 MB: the recycle sawtooth's phase, not a cost (vscode without recycling makes 81 MB
*less* arena, 1564 -> 1483 MB; 28 -> 26 recycles at the defaults, type-fest 2 -> 1). **The memory bench** against
bun 1.4.3 and tsgo: plan 67 "Where it stands" (vscode at the defaults 1560 MB vs bun 1552, single 1388 vs 1414; less
than tsgo on every corpus; less than bun on 7 of 14 single-threaded, 3 of 14 at the defaults). Perfsweep median check
0.23, worst types ratio 1.62 (date-fns docs, unchanged); scaleprobe every kind linear and faster than tsgo. Oracles all
unchanged (source sweep 3356 / 3363, kdiff and recycle 1 / 3363).

## The K1 budget, LazyType slot blocks (2026-10-17, todo entry 8)
The 4-vCPU box. **The K1 budget** (`--checkers 1`, 4 threads, `--checker-memory MB`; wall s / peak MB / recycles,
best of 2; 256 is the default at one checker):
| corpus | 256 | 192 | 128 | 96 |
|---|---|---|---|---|
| vscode | 11.95 / 1450 / 9 | 11.96 / 1317 / 13 | 12.13 / **1202** / 20 | 12.35 / 1125 / 28 |
| effect | 1.26 / 542 / 1 | 1.27 / 476 / 2 | 1.28 / 408 / 3 | 1.32 / 371 / 5 |
| webpack | 1.08 / 544 / 0 | 1.08 / 519 / 1 | 1.10 / 446 / 1 | 1.08 / 415 / 2 |
| drizzle | 0.79 / 399 / 1 | 0.85 / 342 / 2 | 0.89 / 293 / 3 | 0.98 / 251 / 5 |
| type-fest | 1.07 / 399 / 2 | 0.99 / 404 / 3 | 1.07 / 479 / 4 | 1.06 / 289 / 6 |
| zod / zod-root | 0.64 / 396 ; 0.74 / 424 | 0.62 / 327 ; 0.73 / 356 | 0.65 / 274 ; 0.77 / 297 | 0.67 / 229 ; 0.81 / 259 |
| all 15 -p corpora | 20.97 s / 6246 | 20.94 / 5807 | 21.40 / 5286 (+2% / -15%) | |
The source sweep's 169 biggest (`--checkers 1 --threads 1`, alternating): 256 116.5 s / 49.2 GB summed peak (28
recycles), 192 120.0 / 48.2 (+3% / -2%), 128 123.5 / 45.0 (+6% / -8.5%, 136 recycles; folo renderer 3.02 -> 4.90 s,
mikro-orm/tests 5.74 -> 6.85, folo/apps/mobile 1.17 -> 1.77, react-select +36%). Over the window's 3%: K1 stays at 256.
vscode's K1 peak is the checker's sawtooth (RSS ~1050 MB after each recycle, ~1450 before the next, sampled every
0.25 s), so the budget is its lever, but only on programs whose files don't share a big graph. At the defaults the
sweep's 169 run 4 checkers (165) and never see the K1/K2 budget: 128 total changed nothing there.
**LazyType slot blocks** (slots per thread in memo-page blocks, a fresh one per file): memo pages touched on vscode
(summed over recycles) 63.4k -> 57.8k at K1, 88.7k -> 67.0k at 4 checkers (~10 -> ~7 MB a checker; pages ~60% used
now, 33-57% before). Peaks within noise (vscode defaults 1486 -> 1478 MB, K1 1447 -> 1448; effect, webpack, drizzle,
type-fest +-8), walls unchanged (best of 3, alternating). **ClassItem** census (vscode): 80k methods, 49k fields, 11k
constructors (63 MB at 448 B); boxing the FunctionImpl would grow it (methods are the biggest variant), exact sizes
save ~7 MB: declined. Oracles all unchanged. `scripts/instrcount.sh` (callgrind): tsc 4.624, type-fest 14.086,
valibot 3.495, react 1.416, nest 1.089, webpack 9.647, zod 5.375, xstate 2.705, drizzle 6.644, vue 6.532, effect
10.243, vscode 95.517, excalidraw 7.709, tanstack-router 5.325 G (plan 80's within 0.03%).

## The checkers' budget at the defaults (2026-10-17, todo entry 7; floor 128 -> 96 MB)
The 4-vCPU box, 4 threads, 4 checkers (`--checker-memory MB` = MB each below 128). Wall s / peak MB / recycles,
best of 2, `scripts/perfsweep.py --corpora --defaults-only --defaults-args "--checker-memory MB"`:
| corpus | 128 (before) | 96 (now) | 80 | 64 |
|---|---|---|---|---|
| vscode | 5.80 / 1690 / 18 | 5.90 / **1492** / 28 | 5.97 / 1444 / 34 | 6.01 / 1333 / 45 |
| type-fest | 0.55 / 621 / 3 | 0.55 / 631 / 5 | 0.56 / 552 / 5 | 0.56 / 521 / 6 |
| effect | 0.69 / 705 / 2 | 0.69 / **613** / 3 | 0.69 / 550 / 5 | 0.69 / 483 / 6 |
| zod-root | 0.45 / 609 / 1 | 0.46 / 506 / 2 | 0.47 / 479 / 4 | 0.49 / 408 / 6 |
| webpack | 0.65 / 630 / 0 | 0.65 / 630 / 0 | 0.66 / 601 / 1 | 0.65 / 526 / 3 |
| zod | 0.37 / 534 / 0 | 0.39 / 491 / 2 | 0.40 / 438 / 3 | 0.40 / 388 / 5 |
| drizzle | 0.51 / 599 / 2 | 0.56 / 511 / 4 | 0.55 / 468 / 4 | 0.59 / 401 / 8 |
| excalidraw | 0.47 / 467 / 0 | 0.48 / 469 / 0 | 0.47 / 470 / 0 | 0.49 / 458 / 2 |
| tanstack-router | 0.35 / 403 / 0 | 0.34 / 400 / 0 | 0.34 / 399 / 0 | 0.35 / 380 / 1 |
| all 15 -p corpora | 11.24 s / 7855 | 11.40 / 7343 (+1.4% / -6.5%) | 11.50 / 6997 (+2.3 / -11) | 11.61 / 6496 (+3.3 / -17) |
vue, valibot, tsc, xstate, react, nest: unchanged at every budget (no recycle above 64). The source sweep's 169
projects whose K1 arena is over 64 MB (`--defaults-only --min-arena 64`, one run each; two runs at 128 agree within
0.1 s): total wall / summed peak 73.2 s / 64.1 GB at 128, 75.5 / 62.2 at 96 (+3% / -3%, p90 project +14%), 76.6 /
60.5 at 80, 79.7 / 59.3 at 64 (+9% / -7.5%, p90 +34%, monkeytype/frontend 1.9x). The tail is dokploy-like: one
file grows a fresh checker by ~70 MB (a drizzle schema behind tRPC) in ~100 ms, so a recycle frees ~25 MB for
~100 ms. drizzle (`TTTT_FILE_TIMES=1`, which now prints each checker's holding and its recycles) re-derives its
graph spread over many files (db.ts, session.ts, select.ts, count.ts: +20-27 MB, 20-30 ms each in a fresh
checker), ~365 ms of K1's ~1 s at 64 MB. **A warm-up guard on the largest single-file growth** (recycle only past
2x or 3x it, instead of 2x the first file's holding) moved along the same wall/memory frontier on the sweep (2x at
64 MB = 80 MB today's guard: 76.8 s / 60.5 GB) and let type-fest's checkers grow to 767 MB (its big file blocks
every recycle): dropped. A time-based guard would see the same thing and make the cuts depend on timing. 96 is
the floor (vscode +1.7% for -200 MB, inside the window's 3%; 80 costs the sweep +5%). `program_set_checker_memory_cap()`
(1536 MB together, 64 each at least) now binds only past 16 checkers (12-core K24: 64 each), unmeasured here.

## Plan 79, a copying checker recycle: survivors and the budget (2026-10-17, todo entry 6; closed, declined)
The 4-vCPU box. **Survivors** (`scripts/survivors.sh -m MB -p P`: at every recycle of one checker at `--threads 1`,
a conservative mark of the arena from the declaration-level roots: links of symbols that aren't function locals,
the checker's fields, identity entries (interned types, by-node types, signature instances) kept while their value
is; `--lazy` roots every LazyType memo too): arena MB summed over the recycles, survivors' share without / with
`--lazy`:
| corpus | budget | recycles | arena | survivors | per recycle |
|---|---|---|---|---|---|
| vscode | 64 | 47 | 2640 | **35%** (921 MB) | 10-27 MB, 18-49% |
| dokploy/apps/dokploy | 64 | 8 | 1157 | **37% / 57%** | 61-96 MB late |
| drizzle | 64 | 9 | 548 | 13% / ~20% | 5-13 MB |
| type-fest | 64 | 5 | 860 | 3% / 5% | |
vscode's survivors are mostly the declared world a segment touched: anonymous types 78 MB (of 214 made), atoms 22
(877k of 1.25M), literals 27, declared 24, references 13 (sum over 47 recycles). What dies: KEY_STRUCTURAL object
literals (83 MB), conditional instances, flow calls, the `instantiate_members` lists of expression-level types.
**Re-derivation per recycle** (vscode `--checkers 1 --threads 1`, s / peak MB): off 13.09 / 4178, 128 MB 13.81 /
1152 (20 recycles: ~36 ms each), 64 MB 14.41 / 1062 (47: ~22 ms each over 128's), 32 MB 15.40 / 1062 (109: ~16
ms), 16 MB 17.69 / 1062 (269: ~14 ms). A precise copy of ~20 MB (~1.5M objects, the intern tables rehashed) costs
about as much as that, and at K1 the peak is already the program's (1062 MB at 64 MB and below).
**The budget at the defaults** (4 checkers, `--checker-memory MB` = MB each below 128; s / peak MB, best of 2-3):
| corpus | 256 (128 each, today) | 96 each | 64 each | 32 each |
|---|---|---|---|---|
| vscode | 5.80 / 1747 | 5.87 / **1537** | 6.04 / 1362 | 6.45 / 1207 |
| effect | 0.68 / 722 | 0.68 / 620 | 0.67 / 495 | |
| drizzle | 0.49 / 615 | 0.54 / 525 | 0.57 / 414 | |
| type-fest | 0.55 / 636 | 0.54 / 646 | 0.55 / 537 | |
| webpack | 0.64 / 648 | 0.64 / 649 | 0.65 / 540 | |
| zod | 0.36 / 547 | 0.38 / 506 | 0.39 / 399 | |
bun at the defaults (plan 67 "Where it stands", an older tttt): vscode 1572, effect 315, drizzle 342, type-fest 563,
webpack 362, zod 321. The per-checker budget is the defaults' lever: vscode -210 MB at +1% (96) or -385 at +4% (64);
drizzle and zod pay for it (+10-18% at 64), effect, type-fest and webpack don't.

## Plan 80, source text mapped and released, ResolvedModule cache dropped (2026-10-17, todo entry 5)
The 4-vCPU box. `--time` RSS MB after parse / after names / peak (`/usr/bin/time`) / wall s, best of 3, 8116490e
against now (same session, alternating); K1 = `--checkers 1` (4 threads), K4 = the defaults:
| corpus | K | before | after |
|---|---|---|---|
| vscode | 1 | 767 / 1029 / 1584 / 11.71 | 790 / **843 / 1447** / 11.86 |
| vscode | 4 | 766 / 1028 / 1852 / 5.68 | 788 / **848 / 1706** / 5.79 |
| effect | 1 / 4 | 136 / 202 / 544 / 1.25 ; 136 / 201 / 707 / 0.69 | 139 / 186 / 539 / 1.25 ; 139 / 186 / 705 / 0.68 |
| webpack (JS) | 1 / 4 | 220 / 307 / 538 / 1.07 ; 220 / 315 / 635 / 0.64 | 222 / 283 / 541 / 1.08 ; 224 / 283 / 634 / 0.64 |
| drizzle | 1 / 4 | 89 / 126 / 405 / 0.79 ; 89 / 126 / 605 / 0.50 | 95 / 119 / 403 / 0.80 ; 95 / 115 / 601 / 0.50 |
| type-fest | 1 / 4 | 49 / 70 / 403 / 1.06 ; 47 / 68 / 623 / 0.54 | 48 / 63 / 399 / 1.07 ; 50 / 65 / 623 / 0.54 |
vscode wall +1.3% (K1) / +1.9% (defaults): re-faulting text a recycle dropped (~+20k minor faults) and an open+mmap
per file. After parse is a few MB higher (mappings are whole pages); the peaks of the small corpora are their
checkers'. Mapping without releasing was *worse* than read() (vscode K1 1640 MB): the gain is the release. Releasing
per file (11k madvises a recycle, a TLB shootdown each) cost +2.7% at the defaults; per 256 MB segment +0.9%. Census
(vscode, after names): 1.32M Exprs, AST ~470 MB, source text ~175 MB, ResolvedModule table 33 MB (plan 80).
Oracles all unchanged; scenarios 211 / 0, clidiff `--emit` 387 / 396. `scripts/instrcount.sh` (callgrind) stage 3 -> now: tsc
4.634 -> 4.624, type-fest 14.105 -> 14.086, valibot 3.500 -> 3.494, react 1.429 -> 1.418, nest 1.102 -> 1.089, webpack
9.679 -> 9.648, zod 5.383 -> 5.375, xstate 2.710 -> 2.706, drizzle 6.653 -> 6.643, vue 6.546 -> 6.531, effect 10.226 ->
10.244, vscode 95.684 -> 95.495, excalidraw 7.718 -> 7.708, tanstack-router 5.329 -> 5.325 G (no copy of the text).

## Plan 78 stage 3, huge-page materialization chunks, TSX deferral (2026-10-17, todo entry 4)
The 4-vCPU box. `--time` RSS MB after parse / after names / peak (`/usr/bin/time`) / wall s (best of 3), stage 2
(6cae4848) against stage 3; K1 = `--checkers 1` (4 threads), defaults = 4 threads, 4 checkers:
| corpus | K | stage 2 | stage 3 |
|---|---|---|---|
| vscode | 1 | 767 / 1028 / 1566 / 11.83 | 766 / 1027 / 1579 / **11.63** |
| vscode | defaults | 766 / 1028 / 1875 / 5.73 | 768 / 1032 / 1845 / **5.66** |
| effect | 1 / defaults | 136 / 203 / 545 / 1.26 ; 136 / 204 / 706 / 0.69 | 136 / 201 / 542 / 1.26 ; 137 / 200 / 707 / 0.68 |
| webpack (JS) | 1 / defaults | 220 / 310 / 541 / 1.07 ; 222 / 315 / 637 / 0.63 | 222 / 311 / 542 / 1.07 ; 222 / 312 / 634 / 0.64 |
| drizzle | 1 / defaults | 92 / 130 / 408 / 0.78 ; 93 / 128 / 597 / 0.49 | 91 / 129 / 407 / 0.79 ; 91 / 126 / 607 / 0.49 |
| type-fest | 1 / defaults | 47 / 68 / 400 / 1.07 ; 49 / 71 / 626 / 0.54 | 47 / 71 / 403 / 1.06 ; 49 / 69 / 624 / 0.54 |
| excalidraw | 1 / defaults | 134 / 183 / 387 / 0.77 ; 132 / 181 / 474 / 0.46 | **109 / 155** / 387 / 0.76 ; 110 / 156 / 475 / 0.46 |
| tanstack-router | 1 / defaults | 77 / 108 / 349 / 0.60 ; 77 / 107 / 398 / 0.33 | **61 / 88** / 348 / 0.58 ; 59 / 89 / 405 / 0.33 |
vscode against eager (`--no-defer-bodies`, same binary): K1 T4 11.53 s (deferred +1.1%), defaults 5.55 (+2.0%), K1 T1
13.08 (deferred 13.47, +3%; stage 2 13.72). Where stage 2's +4% went: huge pages for the materialization slabs (sys
time 0.66 -> 0.49 s, page faults 636k -> 307k at T1), given back by 2 MB chunk when all their materializations are
dropped. Carrying owner-made materializations across recycles kept their chunks (K1 T1 peak 1543 -> 1697 MB, live
chunks 350 MB for 188 MB of live materializations), hence the lasting small-page slab for cross-made ones (carried
alone: 743 re-materializations instead of 737). Without any carry-over: 13.49 s, 1528 MB, 1110 re-materializations.
The skip scan (table-driven): tsc corpus 85.6 -> 56.9 M instructions (12 -> 8 per skipped byte). Materializing costs
~1.69 s single-threaded on vscode against the eager parse and walk's ~1.38, re-materializing 0.23 s (perf_leads.md).
TSX: excalidraw skips 3.98 MB of bodies (1.75 before), tanstack-router 1.43 MB; peaks there are the checkers'.
Source sweep `TTTT_DEFER_STATS`: 0 parse failures, 745 of 3363 projects restart (681 before .tsx deferral): gated by
options errors (a second parse, by design) or signature collisions in projects with not-found names. The suite with
deferral (now the harness default) 13.3 -> 13.6 s. Oracles all unchanged. `scripts/instrcount.sh` stage 2 -> 3: tsc
4.664 -> 4.634, type-fest 14.104 -> 14.105, valibot 3.510 -> 3.500, react 1.429, nest 1.103 -> 1.102, webpack 9.680 ->
9.679, zod 5.400 -> 5.383, xstate 2.716 -> 2.710, drizzle 6.654 -> 6.653, vue 6.559 -> 6.546, effect 10.251 -> 10.226,
vscode 96.154 -> 95.684, excalidraw 7.695 -> 7.718 (+0.3%: its .tsx bodies skip-scanned now), tanstack-router 5.304 ->
5.329 G (+0.5%). `scripts/scaleprobe.py` body kinds (stmts, locals, calls, inferfn, funcs, jsx and the new jsxbody: a
.tsx function returning N elements) linear at 8000 / 32000, every one faster than tsgo.

## Plan 78 stage 2, materializations dropped with the last checker incarnation (2026-10-17, todo entry 3)
The 4-vCPU box. RSS MB after parse / after names / peak (`/usr/bin/time`) / wall s (best of 3), stage 1 (826d28fc)
against stage 2; K1 = `--checkers 1` (4 threads: materialize-ahead helpers), defaults = 4 threads, 4 checkers:
| corpus | K | stage 1 | stage 2 |
|---|---|---|---|
| vscode | 1 | 768 / 1027 / 2490 / 11.35 | 766 / 1026 / **1574** / 11.82 |
| vscode | defaults | 766 / 1024 / 2643 / 5.56 | 768 / 1028 / **1829** / 5.79 |
| effect | 1 / defaults | 136 / 203 / 545 / 1.26 ; 136 / 202 / 716 / 0.68 | 136 / 204 / 544 / 1.26 ; 136 / 204 / 704 / 0.69 |
| webpack (JS) | 1 / defaults | 220 / 311 / 542 / 1.07 ; 220 / 307 / 629 / 0.64 | 222 / 309 / 540 / 1.07 ; 222 / 313 / 634 / 0.64 |
| drizzle | 1 / defaults | 93 / 130 / 415 / 0.78 ; 91 / 127 / 608 / 0.49 | 91 / 127 / 405 / 0.79 ; 93 / 129 / 597 / 0.49 |
| type-fest | 1 / defaults | 45 / 67 / 412 / 1.07 ; 47 / 70 / 638 / 0.54 | 47 / 68 / 401 / 1.06 ; 47 / 69 / 621 / 0.54 |
| tsc | 1 / defaults | 55 / 84 / 240 / 0.45 ; 57 / 84 / 286 / 0.29 | 59 / 87 / 228 / 0.46 ; 59 / 90 / 281 / 0.30 |
vscode single-threaded (K1 T1): 2477 -> 1545 MB, 13.1 -> 13.7 s. Bun 1.4.3 on this box (plan 67): vscode 1443 MB at one
thread, 1590 at four. The checkers' own budget (plan 67) is now most of what's left: vscode's program is ~1.0 GB after
names. `TTTT_DEFER_STATS=1` vscode K1 T1: 8165 files materialized, 737 again after a drop (1110 without the carry-over
of cross-demanded files at a recycle), 9014 drops; materializing 1.8 s, again 0.36 s, dropping 0.08 s (a
`block_drop()` walk instead: 0.44 s). The defaults: 475 again. Never dropping (an experiment): 2511 MB, +1.8% wall at
+0.35% instructions over stage 1 (the 4 KB pages: huge pages -0.15 s). Callgrind vscode K1 T1: 94.04 -> 96.14 G
(+2.2%: the re-materializations' parse and name lookups). `scripts/instrcount.sh` stage 1 -> 2: tsc 4.637 -> 4.664,
type-fest 14.097 -> 14.104, valibot 3.502 -> 3.510, react 1.424 -> 1.429, nest 1.103 -> 1.103, webpack 9.680 -> 9.680,
zod 5.385 -> 5.400, xstate 2.712 -> 2.716, drizzle 6.641 -> 6.654, vue 6.545 -> 6.559, effect 10.216 -> 10.251, vscode
94.036 -> 96.154, excalidraw 7.685 -> 7.695, tanstack-router 5.304 -> 5.304 G. Oracles all unchanged (suite 0 lost with and without deferral, `--defer-bodies
--checker-memory 0` 0 of 15952 outputs differ, kdiff/recycle 1 / 3363), TSan clean on effect, drizzle, tsc, vue at K2
recycling after every file and K1 with helpers, `TTTT_AST_POISON=1` clean on every corpus at `--checker-memory 0`.

## Plan 78 stage 1, deferred bodies materialized at check (2026-10-17, todo entry 2)
The 4-vCPU EPYC machine (default threads 4). `/tmp/memtable.sh`-style runs, `--time` RSS MB after parse / after names /
peak (`/usr/bin/time`) / wall s, deferral (the CLI's default now) against `--no-defer-bodies`:
| corpus | K | deferred | eager |
|---|---|---|---|
| vscode | 1 | 764 / **1025** / 2482 / 11.30 | 1644 / 2161 / 2483 / 11.77 |
| vscode | defaults (4) | 766 / 1026 / **2651** / 5.60 | 1647 / 2164 / 2730 / 5.65 |
| effect | 1 | 136 / 198 / 544 / 1.25 | 189 / 270 / 541 / 1.27 |
| effect | defaults | 138 / 203 / 707 / 0.68 | 191 / 271 / 719 / 0.68 |
| webpack (JS) | 1 / defaults | 222 / 314 / 545 ; 220 / 308 / 629 | 222 / 311 / 541 ; 222 / 309 / 631 |
| drizzle | 1 / defaults | 93 / 128 / 413 ; 93 / 129 / 607 | 99 / 135 / 401 ; 99 / 134 / 594 |
| type-fest | 1 / defaults | 49 / 70 / 410 ; 49 / 69 / 634 | 47 / 70 / 397 ; 47 / 68 / 612 |
Nothing is dropped yet (stage 2), so peaks barely move: the bodies come back during checking. Small programs pay ~10-20
MB of per-thread materialization arenas and AST slabs (drizzle, type-fest). vscode (`TTTT_DEFER_STATS=1`): 95.9k bodies
skipped (92 MB of source), 5.3k parsed after all (IIFEs, constructors of classes with fields), 90.6k materialized in 8165
files, 10 cross-file materializations at K1 (202 at K4), 0 failures or restarts on every corpus. Walls (vscode, 3 runs):
K1 T4 11.3 (eager 11.6; 12.6 before the materialize-ahead helpers, 14.2 with a 32-file window), K2 6.8 (7.1), K1 T1 13.0
(13.1), defaults 5.5 (5.6); effect K1 1.25 (1.27). Suite with `--defer-bodies`: 21.6k bodies skipped, 92 tests rerun
without deferral (syntax errors in bodies, 111 signature collisions: error tests), 0 components lost, also at
`--threads 4 --checkers 4`. Oracles all unchanged, kdiff 1 / 3363 again after the assignment-marks fix (hono and
linkwarden had differed), TSan clean on effect, drizzle, vue, tsc at K1 and K2 (it found a pre-existing race:
expr.c's `checked_callee`).
Instructions (`scripts/instrcount.sh`, callgrind, K1 T1) 83923e4d -> stage 1: tsc 4.546 -> 4.637 (+2.0%), type-fest
14.099 -> 14.097, valibot 3.470 -> 3.502, react 1.424, nest 1.099 -> 1.103, webpack 9.677 -> 9.680, zod 5.339 -> 5.385,
xstate 2.690 -> 2.712, drizzle 6.628 -> 6.641, vue 6.488 -> 6.545, effect 10.127 -> 10.216, vscode 92.440 -> 94.036
(+1.7%), excalidraw 7.658 -> 7.685, tanstack-router 5.300 -> 5.304 G (the skip scan reads every body's text once more).

## Plan 78 planning, deferred bodies' gate (2026-10-16, todo entry 1, cb3c529b)
On the 24-thread machine (`nproc` 24; a Blender build ran during part of it: RSS is what counts). Probe:
`agents/plans/78_probe.patch`. **Upper bound** (every TypeScript body brace-skipped, stopped after names, `--threads 4`,
RSS MB after parse / after names, eager -> skipped): vscode 1644 / 2161 -> 671 / **898 (-1263)**, effect 187 / 266 ->
126 / 194 (-72), drizzle 101 / 143 -> 91 / 128 (-15), type-fest 49 / 74 -> 47 / 68, webpack (JavaScript, eager) 220 /
309 unchanged. At the default 24 threads: vscode 1685 / 2236 -> 704 / 976. vscode parse 655 -> 345 ms (T4), single
thread parse 1550 -> 800 ms and names 1220 -> 390 ms. Bodies with a parameter default kept eager: vscode 928, effect
193, drizzle 134. **Skipper**: vscode 100.7k outermost attempts, 402 give-ups (`import` 384, `require` 18, 1 regex
newline) with the paren-head rule (without it +467 `/` after `)`); verified against the parser on vscode (419k bodies
incl. nested), effect, drizzle, tsc, vue, zod: 0 mismatches; suite 12, all in bodies with syntax errors.
**Cross-file demand** (outermost bodies read by a checker not checking their file; reads are 4161 inferred returns,
1706 context sensitivity, 695 anchors, 504 predicates on vscode K1): vscode K1 1856 bodies / 962 KB source in 591 files
(9.5 MB of bodies), K4 1965 / 631 files, K24 2092; effect K1 87 (18 files), K4 147; drizzle K1 58 (36 files), K4 85;
webpack 0 (JavaScript). Checker types point into the bodies they walked: drop only at the incarnation's recycle.

## Perf + real code (2026-10-16, todo entry 9)
Instructions (`scripts/instrcount.sh`, callgrind, `--checkers 1`): window start (plan 71) -> session start (7f462498)
-> end: tsc 4.723 -> 4.577 -> **4.546**, type-fest 23.420 -> 23.517 -> **14.099 (-40%)**, valibot 3.463 -> 3.563 ->
**3.470**, react 1.512 -> 1.464 -> **1.424**, nest 1.189 -> 1.110 -> **1.098**, webpack 10.051 -> 9.873 -> **9.674**,
zod 5.426 -> 5.406 -> **5.338**, xstate 2.771 -> 2.723 -> **2.690**, drizzle 6.917 -> 6.800 -> **6.627**, vue 6.635 ->
6.556 -> **6.488**, effect 10.612 -> 10.225 -> **10.130**, vscode 96.780 -> 94.641 -> **92.390**, excalidraw 7.930 ->
7.847 -> **7.658**, tanstack-router 5.412 -> 5.389 -> **5.300 G**. An empty esnext+dom project 460.7 -> 412.5 M. From:
keyof of an object type remembered per type (every corpus -1..-4%; folo renderer's constants/copy.ts 3.53 -> 0.40 G,
its worst file 11 -> 0.6 s), index atoms per checker and a typed member sort (type-fest 22.66 -> 15.36), intersection
dedup by a set (type-fest -> 14.11). `scripts/oracles.py`: suite 13764 / 13803 (+8 own tests of the mutation agent),
every other line unchanged, perfsweep median check ratio 0.21 (0.22 at the start), worst types ratio 1.62 (date-fns
docs). Scaleprobe: every kind linear as on 2026-10-14. Mutation seed 81 (6000 mutants): 16 of 2909 differing -> 7.

## The 12-core machine: tttt, tsgo, bun, tsc-rs (2026-10-18, 2d1ea533)
The human's laptop (below), "balanced" on AC, idle. Each `-p` corpus as `scripts/bench.sh` does, but plain `--noEmit`
for every tool (tttt without `--keep-going`), run from an empty directory; median of 3 runs: wall s / CPU s (user +
sys) / peak RSS MB (`/usr/bin/time`). Defaults: tttt 24 threads and its budget, tsgo 4 checkers, bun every core, tsc-rs
0.7.0 4 checkers (`--fast`: one per core, up to 16). `-1t` = `--singleThreaded` / bun `--threads=1`. tsc-rs
(pingdotgg/ts-rust) is a Rust port of tsgo **at our pin fed0bf24**; `scripts/bench.sh --tscrs PATH` now runs it.
| corpus | tttt | tttt-1t | tsgo | tsgo-1t | bun | bun-1t | tscrs | tscrs-fast | tscrs-1t |
|---|---|---|---|---|---|---|---|---|---|
| TypeScript6 | 0.29 / 1.65 / 467 | 0.51 / 0.50 / 217 | 0.64 / 3.09 / 499 | 1.17 / 2.47 / 394 | 0.43 / 2.45 / 342 | 1.11 / 1.10 / 213 | 0.46 / 1.23 / 332 | 0.46 / 1.25 / 326 | 1.02 / 1.01 / 280 |
| type-fest | 0.30 / 2.23 / 919 | 0.78 / 0.77 / 421 | 4.20 / 20.78 / 5150 | 5.96 / 13.81 / 4399 | 1.14 / 6.41 / 839 | 4.29 / 4.27 / 1009 | 2.36 / 4.85 / 3134 | 1.59 / 4.81 / 3284 | 3.62 / 3.60 / 2304 |
| valibot | 0.16 / 2.33 / 481 | 0.44 / 0.43 / 253 | 0.72 / 5.03 / 782 | 1.71 / 3.55 / 621 | 0.24 / 2.96 / 274 | 1.22 / 1.21 / 279 | 0.46 / 1.79 / 486 | 0.24 / 2.37 / 645 | 1.15 / 1.15 / 400 |
| bulletproof-react | 0.15 / 0.51 / 285 | 0.19 / 0.18 / 145 | 0.45 / 2.68 / 440 | 0.59 / 1.29 / 292 | 0.29 / 1.65 / 372 | 0.60 / 0.59 / 175 | 0.22 / 0.64 / 311 | 0.21 / 0.65 / 300 | 0.41 / 0.40 / 235 |
| nestjs-boilerplate | 0.10 / 0.58 / 266 | 0.18 / 0.17 / 132 | 0.15 / 1.46 / 249 | 0.36 / 0.72 / 205 | 0.12 / 1.02 / 225 | 0.38 / 0.38 / 154 | 0.12 / 0.54 / 221 | 0.11 / 0.53 / 237 | 0.28 / 0.28 / 188 |
| webpack | 0.33 / 4.42 / 697 | 1.15 / 1.14 / 519 | 1.20 / 8.07 / 1184 | 2.58 / 5.20 / 933 | 0.60 / 5.93 / 511 | 2.17 / 2.17 / 371 | 0.69 / 2.50 / 841 | 0.49 / 4.70 / 1207 | 1.77 / 1.75 / 716 |
| zod | 0.25 / 3.59 / 743 | 0.63 / 0.63 / 388 | 1.07 / 7.24 / 1310 | 2.16 / 5.52 / 908 | 0.37 / 4.04 / 512 | 1.46 / 1.45 / 388 | 0.56 / 2.19 / 789 | 0.35 / 2.61 / 866 | 1.14 / 1.13 / 587 |
| xstate | 0.20 / 2.36 / 550 | 0.33 / 0.32 / 192 | 0.43 / 2.97 / 464 | 0.85 / 1.71 / 359 | 0.20 / 1.51 / 244 | 1.04 / 1.03 / 175 | 0.28 / 0.95 / 321 | 0.26 / 1.02 / 319 | 0.67 / 0.66 / 261 |
| drizzle | 0.39 / 6.37 / 895 | 0.86 / 0.85 / 391 | 1.52 / 10.53 / 1623 | 2.16 / 5.46 / 932 | 0.49 / 5.83 / 592 | 2.18 / 2.17 / 376 | 0.66 / 2.39 / 997 | 0.58 / 6.26 / 1685 | 1.65 / 1.63 / 595 |
| vue | 0.20 / 2.58 / 530 | 0.67 / 0.66 / 301 | 0.88 / 5.65 / 753 | 1.87 / 3.55 / 683 | 0.31 / 3.20 / 338 | 1.54 / 1.54 / 280 | 0.52 / 1.80 / 495 | 0.34 / 2.87 / 675 | 1.28 / 1.27 / 417 |
| effect | 0.30 / 4.36 / 766 | 1.28 / 1.27 / 506 | 1.52 / 11.33 / 1553 | 3.33 / 6.03 / 1291 | 0.56 / 8.47 / 497 | 3.65 / 3.64 / 369 | 1.36 / 5.32 / 1261 | 0.89 / 8.69 / 1577 | 4.18 / 4.15 / 1048 |
| excalidraw | 0.30 / 3.97 / 671 | 0.82 / 0.81 / 363 | 1.50 / 10.43 / 1763 | 2.22 / 4.11 / 884 | 0.38 / 4.78 / 417 | 1.55 / 1.54 / 357 | 0.72 / 2.84 / 616 | 0.82 / 10.23 / 951 | 1.56 / 1.55 / 474 |
| tanstack-router | 0.23 / 2.50 / 584 | 0.60 / 0.59 / 301 | 1.22 / 7.40 / 945 | 2.11 / 4.40 / 675 | 0.45 / 4.29 / 466 | 1.39 / 1.38 / 325 | 0.58 / 1.89 / 536 | 0.58 / 2.34 / 527 | 1.48 / 1.46 / 420 |
| vscode | 2.65 / 43.31 / 1863 | 12.24 / 12.17 / 1368 | 8.25 / 62.01 / 7876 | 20.49 / 35.71 / 6528 | 3.01 / 58.19 / 2111 | 20.10 / 20.05 / 1414 | 5.24 / 22.07 / 5494 | 3.39 / 36.74 / 6368 | 14.54 / 14.47 / 4925 |

Geometric means against tsgo at its defaults (wall / CPU / RSS): tttt 0.26 / 0.41 / 0.56, bun 0.41 / 0.60 / 0.41,
tsc-rs 0.59 / 0.31 / 0.64, tsc-rs `--fast` 0.47 / 0.44 / 0.77; single-threaded tttt 0.66 / 0.10 / 0.30, tsc-rs 1.38 /
0.21 / 0.51, bun 1.64 / 0.25 / 0.31, tsgo 1.88 / 0.59 / 0.74 (tsgo's CPU above its wall single-threaded: GC threads).
- **Wall**: tttt fastest on all 14 at the defaults and all 14 single-threaded (vscode 2.65 s vs bun 3.01, tsc-rs
  `--fast` 3.39, tsgo 8.25).
- **CPU**: single-threaded tttt is 2-2.5x below bun and tsc-rs. At the defaults tsc-rs (4 checkers) spends the least
  CPU on 11 / 14 (vscode 22 s vs tttt 43, bun 58): tttt's 24 threads buy wall with CPU.
- **Memory**: at the defaults bun is lowest on 10 / 14, tttt usually 1.2-1.8x bun (vscode 1.86 vs 2.11 GB, tttt
  lower); single-threaded tttt and bun split 7 / 7. tsc-rs is near tsgo on type-heavy code (type-fest 3.1 GB, vscode
  5.5 GB).
- **Output**: tsc-rs byte-identical to tsgo on 13 / 14 (effect: its built-in Effect plugin diagnostics, TS377xxx, extra
  work). tttt byte-identical on 11 / 14: vue 2 spurious TS7006 (known, bugs/sourcesweep_checker.md), webpack and drizzle
  the same (file, line, code) but other elaboration text (`scripts/clidiff.py --corpora`). bun prints its own format,
  differs on the five corpora with errors and reports none on vscode (exit 0).

## The 12-core machine: checkers per K, bun, tsgo (2026-10-16, f760ecdc)
**Framework laptop**: AMD Ryzen AI 9 HX 370, 12 cores / 24 threads, 62 GB, GCC 16.2, "balanced" power profile on
AC, load < 0.3. `scripts/bench.sh --runs 3 --checkers "4 8 12 16" --bun` (bun 1.4.3); best wall s / peak RSS MB.
tttt's default here is 24 threads and 24 checkers (one per 64 KB caps none of these but react/nest); K = `--checkers K`
at 24 threads; single = `--singleThreaded` / bun `--threads=1`. Exit codes as on the other box (webpack, drizzle, vue,
effect, vscode 2 under tttt and tsgo).
| corpus | tttt (K24) | K4 | K8 | K12 | K16 | tttt single | tsgo | tsgo single | bun | bun single |
|---|---|---|---|---|---|---|---|---|---|---|
| TypeScript6 compiler | 0.27 / 487 | 0.25 / 363 | 0.23 / 409 | 0.23 / 433 | 0.24 / 465 | 0.49 / 217 | 0.61 / 498 | 1.13 / 406 | 0.34 / 350 | 0.76 / 212 |
| type-fest | 0.38 / 1007 | 0.75 / 657 | 0.48 / 884 | 0.38 / 951 | 0.42 / 965 | 1.46 / 380 | 3.87 / 5697 | 5.95 / 4317 | 1.01 / 850 | 3.36 / 1009 |
| valibot | 0.13 / 492 | 0.18 / 373 | 0.16 / 412 | 0.16 / 442 | 0.14 / 464 | 0.39 / 252 | 0.73 / 829 | 1.51 / 627 | 0.22 / 281 | 1.33 / 276 |
| react | 0.11 / 256 | 0.09 / 284 | 0.10 / 314 | 0.08 / 341 | 0.09 / 383 | 0.16 / 147 | 0.38 / 448 | 0.54 / 283 | 0.27 / 378 | 0.36 / 176 |
| nest | 0.08 / 241 | 0.08 / 235 | 0.08 / 254 | 0.08 / 268 | 0.08 / 269 | 0.15 / 130 | 0.13 / 259 | 0.30 / 207 | 0.10 / 232 | 0.26 / 154 |
| webpack | 0.30 / 1006 | 0.47 / 714 | 0.38 / 808 | 0.35 / 857 | 0.37 / 925 | 1.13 / 525 | 1.16 / 1159 | 2.46 / 961 | 0.57 / 526 | 2.25 / 377 |
| zod | 0.21 / 935 | 0.28 / 628 | 0.24 / 707 | 0.22 / 784 | 0.22 / 838 | 0.62 / 382 | 1.04 / 1360 | 2.06 / 949 | 0.37 / 515 | 1.73 / 391 |
| xstate | 0.14 / 561 | 0.15 / 350 | 0.13 / 400 | 0.13 / 450 | 0.14 / 490 | 0.31 / 214 | 0.41 / 477 | 0.79 / 368 | 0.18 / 244 | 0.68 / 177 |
| drizzle | 0.28 / 1161 | 0.38 / 701 | 0.32 / 871 | 0.31 / 962 | 0.32 / 1066 | 0.75 / 387 | 1.44 / 1629 | 2.02 / 921 | 0.48 / 599 | 1.46 / 382 |
| vue | 0.23 / 646 | 0.30 / 480 | 0.25 / 522 | 0.22 / 552 | 0.23 / 574 | 0.65 / 330 | 0.88 / 745 | 1.76 / 683 | 0.31 / 346 | 1.06 / 280 |
| effect | 0.29 / 1092 | 0.49 / 825 | 0.40 / 960 | 0.33 / 1004 | 0.33 / 1046 | 1.25 / 528 | 1.46 / 1545 | 3.11 / 1303 | 0.58 / 502 | 2.79 / 370 |
| vscode | 2.50 / 6459 | 4.43 / 3089 | 3.11 / 3810 | 2.81 / 4511 | 2.59 / 5118 | 11.45 / 2485 | 7.95 / 7880 | 19.50 / 6710 | 2.92 / 2138 | 18.21 / 1413 |
| excalidraw | 0.26 / 867 | 0.32 / 552 | 0.29 / 653 | 0.26 / 727 | 0.26 / 772 | 0.79 / 372 | 1.44 / 1822 | 2.15 / 898 | 0.40 / 416 | 1.90 / 357 |
| tanstack-router | 0.19 / 646 | 0.25 / 472 | 0.23 / 527 | 0.21 / 562 | 0.21 / 578 | 0.58 / 336 | 1.16 / 988 | 2.00 / 659 | 0.47 / 477 | 1.14 / 327 |
`-b TypeScript6/src`: tttt 0.53 / 683, tsgo -b 1.28 / 675 (single 2.41 / 526).
- **Speed**: at the defaults tttt is 1.2-2.5x faster than bun (vscode 2.50 vs 2.92) and 1.6-10x faster than tsgo;
  single-threaded 1.6-3.4x faster than bun (vscode 11.45 vs 18.21). Scaling K4 -> K12 is 1.2-2.0x (vscode 1.58x,
  type-fest 2.0x, effect 1.5x); **past 12 almost nothing**: K12 -> K24 gains 0-11% on vscode / webpack / effect, 0 on
  the rest, and type-fest's K16 is slower than K12. Mid-size corpora stop gaining at K8-12.
- **Memory**: each checker past 4 adds ~10-30 MB on mid-size corpora and **~170 MB on vscode** (K4 3.1 GB -> K24 6.5
  GB). At the defaults bun uses less on 13 of 14 (vscode 3.0x, effect 2.2x, drizzle 1.9x, webpack 1.9x; tttt less on
  react only).
- **The floor isn't the libraries**: a fresh checker on a one-line program (esnext + dom libs) holds a 7 MB arena
  (13k types, process RSS 90 MB). The K-proportional growth is each checker filling its recycle budget (128 MB) plus
  its malloc side (~25-30 MB per checker on vscode at B=32: (3639 - 2267 resident after names) / 24 = 57 MB per
  checker). Plan 67's "no" to a shared frozen lib layer stands; the lever is the budget.
- **Recycle budget per K** (`--checker-memory B`; best of 3 wall s / peak MB):

| corpus | K12 B256 | K12 B128 | K12 B64 | K12 B32 | K16 B128 | K16 B64 | K16 B32 | K24 B128 | K24 B64 | K24 B32 |
|---|---|---|---|---|---|---|---|---|---|---|
| vscode | 2.78 / 4518 | 2.67 / 4545 | 2.88 / 3622 | 2.97 / 3037 | 2.61 / 5160 | 2.81 / 3919 | 2.84 / 3283 | 2.50 / 6501 | 2.56 / 4750 | 2.65 / 3639 |
| type-fest | 0.40 / 949 | 0.44 / 961 | 0.45 / 870 | 0.45 / 757 | 0.46 / 976 | 0.47 / 855 | 0.47 / 820 | 0.40 / 996 | 0.44 / 937 | 0.45 / 912 |
| effect | 0.32 / 1001 | 0.34 / 1012 | 0.35 / 1004 | 0.33 / 735 | 0.34 / 1044 | 0.35 / 1044 | 0.33 / 876 | 0.31 / 1103 | 0.29 / 1099 | 0.31 / 1033 |
| drizzle | 0.30 / 969 | 0.29 / 970 | 0.30 / 883 | 0.37 / 632 | 0.30 / 1059 | 0.32 / 1030 | 0.37 / 714 | 0.29 / 1136 | 0.29 / 1138 | 0.32 / 867 |
| zod | 0.20 / 769 | 0.21 / 772 | 0.20 / 768 | 0.21 / 619 | 0.22 / 833 | 0.21 / 842 | 0.22 / 744 | 0.22 / 934 | 0.21 / 934 | 0.20 / 850 |
| webpack | 0.34 / 864 | 0.33 / 857 | 0.33 / 854 | 0.34 / 777 | 0.37 / 923 | 0.35 / 919 | 0.31 / 879 | 0.33 / 1000 | 0.32 / 996 | 0.31 / 981 |
  vscode K24 B64 is -1.75 GB for +2.4% wall, K24 B32 -2.86 GB for +6% (still faster than bun's 2.92 s, 1.7x its RSS);
  mid-size corpora barely move below B=64 (they don't fill it) and gain 100-300 MB at B=32 for 0-25% wall
  (drizzle's +25% the worst). A total budget (e.g. ~1.5 GB split across K, floored at 32-64 MB) and a K cap around
  12-16 are the two policy knobs for plan 75; both are cheap on this machine.
- **Plan 72's lines** (`TTTT_RESOLVE_TIMES=1 TTTT_LOAD_POOL_STATS=1 --time`, 24 threads, ms): vscode libs 63, files
  389 (11035 paths, 543 parsed by the loader, the loader **waited 6 ms**, workers busy 6414 of 10839 thread-ms = 59%),
  declare 76 + link 54 = **130 (5% of the 2.5 s wall)**, walk 237, typecheck 1424 (24 checkers 746-1443 ms each:
  max/mean 1.3x, five recycled once), startup + exit ~250. webpack: libs 23, files 24 (waited 8), declare 39 + link 6
  = **45 (15% of 0.30 s)**, walk 34, check 171. effect: libs 19, files 16, declare 9.5 + link 3.9 = 13 (4%), walk 29,
  check 199. So: the loader thread is still the load phase's critical path (59% worker busy) but the phase is only
  16% of vscode's wall here; declare+link is under plan 72's 10% bar on vscode, above it on webpack (its serial
  `declare`, plan 72 says look at that first). Load discovery (the pre-scan) stays unjustified: 6-8 ms waited.

## Plan 75, the memory endgame (2026-10-16, todo entry 7)
**Checker budget policy** (this box, 4 vCPU; best wall s / peak MB): one checker at 128 MB instead of 256 is free on
vscode (12.3 s, -146 MB) and type-fest (+2%, -120 MB) but drizzle +12% (0.81 -> 0.90 s), so a lone checker keeps 256.
At 64 MB vscode K1 is +4%. Policy shipped: the CLI's default (no `--checker-memory`) caps the checkers at 1.5 GB
together, each at least 64 MB (`program_set_checker_memory_cap()`): 128 MB each up to K12 as before, 96 at K16, 64 at
K24. vscode `--checkers 24` on 4 threads: 6.13 s / 4897 MB (uncapped) -> 6.21 / 3561, output byte-identical; K4
unchanged (6.15 / 2791). The 12-core section's K24 B64 row is what the cap gives there (-1.75 GB, +2.4%).
**Compact AST census** (vscode after names, `scripts/memtrack.sh`, one checker): AST 1366.9 MB with 557.0 MB zero
words, diffuse: the top site (CallExpr-sized boxes from parse_expr, 1.1M) holds 48.5 MB of zeros, PropertyExpr dots
38, InitializedBinding vectors 25, BinaryOpExpr 22, TypeNode 15, call arguments 15; no site past 50 MB. Exprs (ast
walk, not counting those inside types): 8.19M x 72 B = 590 MB: identifiers 2.59M (877k with symbol != target, 21k
using the rare union), literals 1.14M (number 251k, string 644k), implicit any 310k, the boxed variants 4.46M using 8
of the union's 40 bytes. Exact-size variants remove ~165 MB at most (4.46M x 32 + the empty and literal slots); boxing
identifiers to get Expr to 40 B costs back ~60-100 MB. **No-go** under the gate (>= 400 MB): the zero words are
mostly sparse optional fields and empty vectors over every node kind, not union padding. Estimates of the separate
items: 12-byte spans ~60 MB (alignment eats most of the 4 bytes), 8-byte spans ~120 MB but need the file from context
at ~570 `.buf` sites, 32-bit node references ~40 MB. No prototype branch was built (the census answered the gate).

## Plan 63 stage 2, per-file AST arenas and huge pages (2026-10-16, todo entry 6)
**Audit (2a)**: nothing stopped it. One parser entry (`parse_file()`); later phases do mutate the AST (resolve's rare
structs and `AccessSite`s, module resolution's ModuleRef rares, JSDoc moves), so instead of forbidding that the arena
is told apart by address (`ast_owns()`, one load): a free of arena memory is a no-op after the parse, a realloc copies
out to malloc. Speculation needs no rewind: freed blocks are reused during the parse (vscode: 1.32 GB bumped, 331 MB
of freed blocks reused, 42 MB still free at the files' ends). Strings (`cstr`) stay malloc, so the drop is still a
walk (bugs/perf_leads.md "Memory").
**Huge pages** (this machine: 11 days up, 17 GB page cache, ~600 MB free in order >= 9 blocks): madvising every arena
chunk made vscode **9.39 s, 13.9k compaction stalls a run**; none 6.17 s; the old glibc re-exec cost the same way
(base 8.1-8.4 s vs 7.4 with `TTTT_NO_REEXEC`). Mid-size corpora gain 3-8% from them, without stalls. Now: chunks are
madvised while a `/proc/buddyinfo` budget lasts (`huge_pages_take()`), and arenas prefault ahead with
`MADV_POPULATE_WRITE` (vscode K1 parse -14%, names -4%, check -2.5%).
Base 9459b02c (as shipped: re-exec) -> now, default threads, best of 3, wall s / peak MB: tsc 0.29 / 273 -> 0.28 /
274, type-fest 0.82 / 609 -> 0.82 / 616, webpack 0.68 / 631 -> 0.64 / 634, zod 0.36 / 521 -> 0.35 / 526, drizzle
0.50 / 587 -> 0.49 / 592, effect 0.70 / 719 -> 0.69 / 719, **vscode 8.14 / 2779 -> 5.91 / 2727**. vscode K1 phases
(load+parse / names / check ms, resident after parse): base 3030 / 1979 / 13638, 1665 MB (without the re-exec 1950 /
1370 / 11900) -> 1603 / 1349 / 10675, 1615 MB. `-b` tsc-src: 1.13 s / 460 MB -> 1.12 / 390 (dropped projects give
their pages back), its 13 drops 166 -> 119 ms. Callgrind `parse_file` on webpack 2.106 -> 1.946 G (-7.5%, target was
-10%: the allocator went from ~280 M to ~100 M instructions, the rest is parsing). Instructions (callgrind, K1): tsc
4.648 -> 4.574, webpack 10.019 -> 9.868, vue 6.629 -> 6.552, effect 10.363 -> 10.209, vscode 95.76 -> 94.58 G (-1.2..
-1.6%); at the end, every corpus: tsc 4.577, type-fest 23.515, valibot 3.563, react 1.464, nest 1.110, webpack 9.873,
zod 5.406, xstate 2.723, drizzle 6.800, vue 6.557, effect 10.224, vscode 94.640, excalidraw 7.845, tanstack-router
5.389 G. Suite, every oracle, threadcheck and checkerdiff unchanged; suite and both `-b` corpora pass under
`TTTT_AST_POISON=1`.

## Plan 64 stage 6, JavaScript declaration emit (2026-10-16, todo entry 4)
Suite **13746 / 13787** (+1 own test, 0 lost; TS dts 1478 / 1502 unchanged, **JS dts 0 -> 320 / 346** apart),
**dtsdiff 1116 -> 1166 / 1233 projects, 95824 -> 96584 / 96726 files** (0 written that tsgo doesn't), clidiff `--emit`
309 -> 369 / 396, corpora all 0/0 but vue 0/2, DT 8595 / 8595, `$ExpectType` 68639, flagflip 18 / 6 / 3, source sweep
3356 / 3363, perfsweep median 0.22 (worst types ratio 1.62, date-fns docs), kdiff 1 / 3363, recycle 3 / 3363.
Instructions (callgrind, `--noEmit`; JS emit only runs when emitting) flat against entry 3's end: tsc 4.648, type-fest
23.523, valibot 3.596, react 1.494, nest 1.146, webpack 10.022, zod 5.434, xstate 2.754, drizzle 6.830, vue 6.628,
effect 10.356, vscode 95.798, excalidraw 7.931, tanstack-router 5.422 G.

## Plan 64 stage 6 finished (2026-10-16, todo entry 5)
Main + one agent. `scripts/oracles.py`: suite **13756 / 13798** (dts 1834 / 1859 with JavaScript tests folded in, those
351 / 352), corpora all 0/0 but vue 0/2, DT 8595, `$ExpectType` 68639, flagflip 18 / 6 / 3, source sweep sample 818 /
822 (full 3356 / 3363 mid-session), perfsweep median 0.22, worst types ratio 1.62; **dtsdiff 1193 / 1233 projects,
96627 / 96726 files** (from 1166 / 96584; no file tsgo writes that tttt doesn't). Instructions (callgrind, `--noEmit`):
tsc 4.648, type-fest 23.519, valibot 3.595, react 1.494, nest 1.145, webpack 10.020, zod 5.435, xstate 2.754, drizzle
6.829, vue 6.630, effect 10.358, vscode 95.800, excalidraw 7.933, tanstack-router 5.419 G: flat against entry 3.

## Real-code fan-out 2 (2026-10-16, todo entry 3)
Three agents (A source sweep diagnostics, B dtsdiff "type text", C dtsdiff's emitter buckets: import, member order,
comments, maps), ~55 picks, 0 lost at every merge, 0 conflicts, 0 duplicates. `scripts/oracles.py`: suite **13745 /
13783** (dts 1478 / 1502; +45 full passes, ~40 of them own tests), corpora all 0/0 but vue 0/2, DT 8595 / 8595,
`$ExpectType` 68639, flagflip 18 / 6 / 3, source sweep **3356 / 3363** full (from 3352: arktype x3, tanstack-table,
nextra, base-ui; tiptap/demos' 9 spurious TS2345 gone too; sample 818 / 822), perfsweep median 0.22, worst types ratio
1.62 (date-fns docs, as at the start), **dtsdiff 1116 / 1233 projects, 95824 / 96726 files** (from 1077 / 95611; import
22 -> 7, comment 3 -> 0, most map buckets), kdiff 1 / 3363, recycle 3 / 3363 (sequelize's TS7022 shown to be tsgo's own
checker-count dependence). Instructions (callgrind, `--noEmit`) against the session start: tsc 4.648 (+0.1%), type-fest 23.517 (+0.4%), valibot 3.597 (+1.4%), react 1.495,
nest 1.145, webpack 10.015, zod 5.433, xstate 2.755, drizzle 6.830 (+0.3%), vue 6.630 (+0.5%), effect 10.357, vscode
95.804 (+0.1%), excalidraw 7.933 (+0.4%), tanstack-router 5.420 G (+0.4%). Mid-session bisects:
+1.2% tanstack-router (contextual intersection distribution recomputed per property lookup, cached), +1.1% valibot
(9e42832a, tsgo's isDeclarationContainedBy offset quirk in inherited member order: kept, the cost is relations
visiting properties in tsgo's order, not the check; a cheaper check measured the same).

## Plan 72 stage 1, the loader's serial work (2026-10-16, todo entry 1)
vscode `load+parse files` (`--listFilesOnly`, 3 runs, HEAD f760ecdc against stage 1): T1 2525 -> 2310 ms, T4 940 ->
805; effect T4 76 -> 45, webpack T4 100 -> 95. The loader's own work at T4 (`TTTT_LOAD_POOL_STATS=1`) 630 -> ~385 ms,
non-parse at T1 ~415 -> ~225. `newfstatat` 194k -> 43k. Callgrind `--listFilesOnly --threads 1` vscode 16.88 -> 15.68
G (non-parse load work 2.46 -> 1.30 G). Peak RSS vscode (full check) T1 2540 -> 2548 MB, T4 2806 -> 2830. Every
oracle as at the window's start (suite 13698, corpora 0/0 but vue 0/2, sweep 8595, `$ExpectType` 68639, flagflip
18/6/3, source sweep 3352 / 3363, dts 1077 / 1233, kdiff 1, scenarios 211 / 0 failed), threadcheck identical.
Instructions (`scripts/instrcount.sh`, callgrind; HEAD f760ecdc -> stage 1): tsc 4.643 -> 4.640, type-fest 23.427 ->
23.422, valibot 3.463 -> 3.426, react 1.512 -> 1.490, nest 1.189 -> 1.143, webpack 10.048 -> 9.953, zod 5.425 ->
5.410, xstate 2.772 -> 2.761, drizzle 6.835 -> 6.787, vue 6.633 -> 6.608, effect 10.401 -> 10.340, vscode 96.763 ->
95.564, excalidraw 7.928 -> 7.891, tanstack-router 5.411 -> 5.397.
The pre-stage split per thread count, declare+link, checker duplication and imbalance: plans/72_core_scaling.md.

## Plan 73, one table of a class's member symbols (2026-10-16, todo entry 2)
Suite 13699 / 13741 (+1: ttttLateBoundAccessorPairs), every `scripts/oracles.py` line unchanged, dtsdiff 1077 / 1233
projects (95611 / 96726 files) unchanged. Instructions (callgrind): tsc 4.643, effect 10.346, drizzle 6.808, vue 6.599,
xstate 2.762, type-fest 23.420 G: flat against plan 74's.

## Plan 74, one reporting run where tsc has one (2026-10-16, todo entry 2)
Suite 13698 / 13740 unchanged (complexRecursiveCollections kept without failed_log, by the interface heritage check's
single run), every `scripts/oracles.py` line unchanged. Instructions (callgrind) before -> after: tsc 4.640 -> 4.642,
effect 10.340 -> 10.346, drizzle 6.786 -> 6.803, vue 6.608 -> 6.597, xstate 2.761 -> 2.763, type-fest 23.422 ->
23.421 G: flat. relationComplexityError 1.21 -> 0.57 s (its TS2859 explained from the cached overflow).

## Plan 71, the hacks.md audit (2026-10-15, todo entry 7)
Suite **13697 / 13739** (lost noImplicitAnyUnionNormalizedObjectLiteral1 and
declarationEmitAugmentationUsesCorrectSourceFile, argued in tsgo_divergences.md; +1 own test). `scripts/oracles.py`:
corpora 0 / 0 but vue 0 / 2, DT 8595 / 8595, `$ExpectType` 68639, flagflip 18 / 6 / 3, source sweep sample 817 / 822,
perfsweep median 0.21, worst types ratio 1.62, dts projects 1077 / 1233 (files 95611 / 96726), kdiff 1 / 3363.
Instructions (callgrind, `--noEmit`): tsc 4.723, type-fest 23.420, valibot 3.463, react 1.512, nest 1.189, webpack
10.051, zod 5.426, xstate 2.771, drizzle 6.917, vue 6.635, effect 10.612, vscode 96.780, excalidraw 7.930,
tanstack-router 5.412 G (flat against entry 5). Plan 74's relation counts: the reporting run is ~0.5% of relation
calls on the suite without relationComplexityError (which alone reruns 5.25M calls to TS2859) and < 0.5% on the corpora.

## Real-code fan-out (2026-10-15, todo entry 6)
Four agents (A import specifiers / painting, B the declaration writer's reused-node text, C checker types in `.d.ts`,
D source sweep diagnostics), ~60 picks, 0 lost at every merge. `scripts/oracles.py`: suite **13698 / 13735** (dts
1440 / 1465; from 13625 / 13675, +60 own tests), corpora all 0/0 but vue 0/2 (argued plan-sized), DT **8595 / 8595**
(from 8594), `$ExpectType` 68639, flagflip 18 / 6 / 3, source sweep **3352 / 3363** full (from 3344, 0 lost; sample
817 / 822), perfsweep median 0.21, worst types ratio 1.63, **dtsdiff 1077 / 1233 projects, 95611 / 96726 files** (from
932 / 94352; "type text" 197 -> ~45 projects, JS sources 77 of the 156 left), kdiff 1 / 3363, recycle 4 / 3363.
Instructions (callgrind, `--noEmit`), flat against the session start: tsc 4.722, type-fest 23.451, valibot 3.465
(-1.3%), react 1.513, nest 1.189, webpack 10.052, zod 5.426, xstate 2.770, drizzle 6.915, vue 6.636, effect 10.613,
vscode 96.778 (+0.25%), excalidraw 7.916, tanstack-router 5.409 G. One mid-session +1.7% drizzle / +1.2% nest (the
declaration-diagnostics pass searching every module per unresolvable import lookup, under --noEmit too) was fixed
with tsgo's program-wide container index.

## Plan 64 stage 5, declaration maps (2026-10-15, todo entry 5)
Suite **13625 / 13675** (dts **1372 / 1410**: the dts component compares the `.js.map` baselines' `.d.ts.map`
sections, 13 tests gained, own test `ttttDeclarationMaps`), 0 lost. dtsdiff's manifests regenerated with the configs'
declarationMap (96726 files, 20435 of them maps in 509 projects): **maps 19884 / 20435 byte-identical (97.3%)**, 385 /
509 projects all maps identical, 12 projects differ by maps alone; dtsdiff **932 / 1233 projects, 94352 / 96726 files**
(942 / 1233 and 74463 / 76294 before maps were compared). `clidiff.py --emit`: **309 / 396** identical (164 + 145
maps-only before), declarationMap 2, file text 51, missing files 29 (JS), diagnostics 2, text 3. `dtsdiff.py --kdiff 16
--sample 4` 3 / 292 (as before, no map among them). Every other oracle as in entry 4 (corpora 0 / 0 but vue 0 / 2, DT
8594 / 8595, `$ExpectType` 68639, flagflip 18 / 6 / 3, source sweep sample 814 / 822, kdiff 1 / 3363, recycle 4 / 3363,
perfsweep median 0.20, worst types ratio 1.62). **Maps' cost** (`--checkers 1 --threads 1 --checker-memory off`,
`--no-write`, best of 3; check / emit / emit + maps): drizzle 800 / 813 / 846 ms, effect 1489 / 1607 / 1599, valibot
459 / 470 / 482. **Instructions** (`--noEmit`; TypeNode.pos is only recorded with declarationMap): tsc 4.722,
type-fest 23.398, valibot 3.509, react 1.510, nest 1.190, webpack 10.050, zod 5.428, xstate 2.768, drizzle 6.895, vue
6.641, effect 10.625, vscode 96.537, excalidraw 7.921, tanstack-router 5.413 G (entry 4's +0.00..0.17%: storing the
position unconditionally cost nest +0.3%, a bigger TyDisplay nothing).

## Plan 67 stage 2 items 2-3, the struct diet's end and the program against bun (2026-10-15, todo entry 4)
**Diet** (plan 63 stage 1b, the cheap items): PortItem 192 -> 120 (import-alias path behind a pointer,
modifiers/clause a union, flags packed), PortStatement 288 -> 216, Statement 136 -> 112 (VariableStatement 72 -> 48:
`await` derived from the span), ObjectExprItem 160 -> 152. vscode malloc heap after load+parse 1690 -> **1623 MB
requested** (1802 -> 1733 chunks), 48% zero words (782 MB). Resident (K1 `--time`): after parse 1727 -> 1662, after
names 2255 -> **2191**; peaks (`/usr/bin/time`): vscode K1 2610 -> **2549**, T4 2874 -> 2840; webpack 540 / 643,
effect 543 / 740, type-fest 388 / 624 (K1 / T4: recycling's peaks wobble +-10 MB). Output identical: suite 0 lost,
every oracle as in the entries below (recycle 4 / 3363), threadcheck identical. **Instructions** flat: tsc 4.721,
type-fest 23.399, valibot 3.508, react 1.509, nest 1.188, webpack 10.047, zod 5.428, xstate 2.766, drizzle 6.890, vue
6.637, effect 10.619, vscode 96.520, excalidraw 7.916, tanstack-router 5.415 G.

**vscode's memory, tttt K1 against bun 1.4.3 at one thread** (bun's RSS sampled every 0.5 s: it climbs to ~1.29 GB
when its load ends and stays at 1.32-1.38 GB for the whole check):
| | tttt | bun |
|---|---|---|
| program (after parse / names) | 1662 / **2191 MB** | ~**1300** (parse+bind, load end) |
| AST malloc heap after parse | 1623 MB requested, 48% zero words; source text 174, ResolvedModule table 33 | - |
| binder (arena, names phase) | ~480 MB (Symbol 162, flow nodes ~110, SymbolDecl 60, tables 57, Scope ~51) | - |
| check on top of the program | **~360 MB** (`--checker-memory 256`, 9 recycles) | ~80 MB |
| peak | 2549 MB | ~1380-1440 MB |
The gap is 1.8x: ~900 MB in the program (the AST representation: the zero words alone are 782 MB, malloc's headers
and size classes ~110) and ~280 MB in the check (the recycling budget; 128 MB costs +2-4% wall, plan 67 stage 1).
What struct work is left (plan 63 stage 1b, after this): Expr 72 -> 64 (~-30..50 MB: boxed Exprs stay in malloc's
80-byte chunks), Binding element fields (-17, ~110 readers), PropertyExpr to the 112 chunk (-27, needs 16 bytes:
its name span is derivable from the text but ty/lower.c synthesizes accesses), Symbol bit fields (-8),
ResolvedModule hints (-10): **under 120 MB together, ~5%. The diet is done**; the remaining lever is the
representation (plan 67 "Not doing": compact AST).

## Plan 67 stage 2 item 1, binder scopes (2026-10-15, todo entry 3)
**vscode's program at K1**: resident after names 2281 -> **2255 MB** = malloc heap 1736 (the AST: memtrack, 38%
zero words; top: c_safe_memcpy 251, property_dot_new 201, source text 174, parse_append_statement_sequence 147,
vec_expr 128, object literal items 115) + binder arena ~480 (arenasites, pops subtracted now: Symbol+SymbolAlias 162,
flow nodes ~110, SymbolDecl 60, symbol tables 57, Scope 67 -> ~51, ScopeRare 17) + libs. Scopes kept 747k -> 533k of
1.43M made. Peaks (`/usr/bin/time`): vscode K1 2642 -> 2610 MB, T4 2874 (2858 in stage 1's table: recycling
peaks wobble), webpack K1 545 / T4 640 (names 309). Suite 13611 / 13677, 0 lost; every oracle as in stage 4's entry
below but dts 943 / 1233 projects (74464 files); threadcheck identical; output byte-identical under
`-DTTTT_POISON_SCOPES` (suite, vscode, type-fest, effect, webpack). **Instructions** flat: tsc 4.725, type-fest
23.393, valibot 3.510, react 1.510, nest 1.190, webpack 10.053, zod 5.428, xstate 2.768, drizzle 6.893, vue 6.641,
effect 10.620, vscode 96.584, excalidraw 7.922, tanstack-router 5.418 G.

## Plan 64 stage 4, the CLI emits declarations (2026-10-15, todo entry 2)
Suite **13611 / 13677 with the dts component counted** (dts 1358 / 1409; 13660 / 13672 before, without it), 11 gained,
0 lost. Corpora 0 / 0 but vue 0 / 2, DT sweep 8594 / 8595, `$ExpectType` 68639, flagflip 18 / 6 / 3, source sweep
sample 814 / 822, kdiff 1 / 3363 (inbox-zero), recycle 4 / 3363, scenarios 211 / 0 (+1). Declaration emit:
`dtsdiff.py` (now the CLI's writes) 942 / 1233 projects, 74463 / 76294 files; `clidiff.py --emit` on the 396
`emitDeclarationOnly` source sweep projects: 164 match, 145 declarationMap only, 28 missing files (JS), 52 file text,
4 diagnostics + 3 text (as under --noEmit); `dtsdiff.py --kdiff 16 --sample 4` 3 / 292 (member order, see plan 64).
**Emit's cost** (`--checkers 1 --threads 1 --checker-memory off`, best of 3, against `--noEmit --declaration`):
drizzle 823 -> 834 ms (+1.3%, 519 files), effect 1511 -> 1568 (+3.8%, 950), valibot 474 -> 495 (+4.4%, 1105).
**Instructions** (callgrind, `--noEmit` now explicit, unchanged from stage 67's): tsc 4.724, type-fest 23.400, valibot
3.510, react 1.509, nest 1.188, webpack 10.052, zod 5.427, xstate 2.767, drizzle 6.893, vue 6.639, effect 10.617,
vscode 96.588, excalidraw 7.921, tanstack-router 5.414 G.

## Plan 67 stage 1, checker recycling (2026-10-15, todo entry 1)
Default `--checker-memory 256` (split between the checkers, at least 128 MB each). Peaks: vscode K1 4190 -> **2575**
MB (+2.4% wall), T4 4786 -> **2858** (+3.6%); type-fest K1 738 -> 380 (+16% wall), T4 857 -> 603; effect T4 797 ->
717; drizzle T4 648 -> 589; zod K1 443 -> 382; webpack, vue, tsc unchanged (table in plans/67). Suite 13660 / 13672,
0 lost; every oracle as in the triage below except dtsdiff files 74466 -> 74462 (vscode's undefinedProperties order),
plus the new `recycle` oracle (fresh checker per file): source sweep 4 / 3363 differ (inbox-zero, lucide, sequelize
x2). **Instructions (callgrind, `--checkers 1`, so recycling at 256 MB)**: tsc 4.724, type-fest **23.403 (+21%)**,
valibot 3.510, react 1.510, nest 1.188, webpack 10.047, zod **5.426 (+6.5%)**, xstate 2.767, drizzle **6.893
(+14%)**, vue 6.640, effect **10.616 (+2.2%)**, vscode **96.585 (+7.3%)**, excalidraw 7.921, tanstack-router 5.413 G.
That's the fresh checkers' warm-ups, in the corpora that recycle (type-fest 2 recycles, drizzle 1, vscode 9). Compare
instructions with `--checker-memory off` to see the checker's own work.

## Triage (2026-10-15, todo entry 10)
Suite **13660 / 13672** (+3 own tests: an unused redeclaration, the binary marker as a token), 0 lost. Every other
oracle identical to entry 9 below (corpora all 0/0 but vue 0/2, DT 8594 / 8595, `$ExpectType` 68639, flagflip 18 / 6
/ 3, source sweep sample 814 / 822, perfsweep median 0.21, worst types ratio 1.62, dtsdiff 940 / 74466). Instructions
flat (+0.0..+0.4%): tsc 4.727, type-fest 19.337, valibot 3.513, react 1.512, nest 1.188, webpack 10.068, zod 5.094,
xstate 2.769, drizzle 6.037, vue 6.647, effect 10.387, vscode 90.037, excalidraw 7.929, tanstack-router 5.417 G.

## Plan 66 stage 4, the second fan-out (2026-10-15, todo entry 9)
Suite **13657 / 13669** (tsgo's 13556 / 13568, 12 left; from 13618 / 13660), 0 lost. `scripts/oracles.py`: corpora
all 0/0 but vue 0/2 (webpack 0/1 -> 0/0, effect 3/0 -> 0/0, vue 0/3 -> 0/2), DT 8594 / 8595, `$ExpectType` 68639
(from 68637), flagflip 18 / 6 / 3 (from 42 / 6 / 3), source sweep sample 814 / 822 (from 811), perfsweep median 0.19,
worst types ratio 1.62, dtsdiff 940 / 1233 projects, 74466 / 76294 files, kdiff 1 / 3363 (inbox-zero, as before).
Instructions (callgrind): tsc 4.716 (+0.4%), type-fest 19.333 (+0.4%), valibot 3.509 (+1.4%), react 1.505, nest
1.182, webpack 10.049 (+0.4%), zod 5.087 (+0.3%), xstate 2.763 (+1.2%), drizzle 6.029 (+0.6%), vue 6.636 (+0.6%),
effect 10.369 (+1.3%), vscode 89.903 (+0.6%), excalidraw 7.916 (+0.8%), tanstack-router 5.410 G (+0.4%). Half of
valibot/effect/xstate's is the return mapper rule (b502608d, +0.75%: same call counts, arguments checked against
tsgo's contexts), the rest ~0.1-0.25% per fix (plan 65's in-flight bookkeeping, tuple `this` methods...).

## Plan 64 stages 2-3 (2026-10-15, todo entry 7): declaration emit with comments
Suite dts component: **1345 / 1408** (95.5%, from 1192). `scripts/dtsdiff.py`: **922 / 1233 projects, 74279 / 76294
files** byte-identical (from 402 / 49020; comments were 23448 of the mismatching files, now ~180; JS sources are 693 of
the remaining ~2000, bugs/dts_emit.md). Corpora files: 14617 / 14846. Everything else identical to the session start:
suite 13618 / 13663, 0 lost; corpora (webpack 0/1, vue 0/3, effect 3/0), DT 8594 / 8595, `$ExpectType` 68637,
flagflip 42 / 6 / 3, source sweep sample 811 / 822, perfsweep median 0.22 (load), worst types ratio 1.62.
Instructions (callgrind, flat: comments are recorded only where declarations are emitted): tsc 4.696, type-fest
19.261, valibot 3.460, react 1.504, nest 1.185, webpack 10.009, zod 5.076, xstate 2.732, drizzle 5.994, vue 6.597,
effect 10.239, vscode 89.350, excalidraw 7.853, tanstack-router 5.390 G.

## Plan 64 stage 1 + most of stage 2 (2026-10-14, todo entry 6): the declaration writer
Suite dts component (not in the headline count): **1192 / 1408** TS tests (344 with JS inputs apart), from 0.
`scripts/dtsdiff.py`: **402 / 1233 projects, 49020 / 76294 files** byte-identical to tsgo (comments are 23448 of the
mismatching files). Everything else identical to the session start: suite 13618 / 13663, 0 lost; corpora (webpack 0/1,
vue 0/3, effect 3/0), DT 8594 / 8595, `$ExpectType` 68637, flagflip 42 / 6 / 3, source sweep sample 811 / 822,
perfsweep median 0.18 (load), worst types ratio 1.62. Instructions (noEmit, flat): tsc 4.699, type-fest 19.265,
valibot 3.456, react 1.506, nest 1.185, webpack 10.000, zod 5.075, xstate 2.726, drizzle 6.001, vue 6.596, effect
10.210, vscode 89.360, excalidraw 7.858, tanstack-router 5.392 G. Emit's own cost isn't measured yet (stage 4).

## tttt vs bun on this machine, checker recycling (2026-10-14, plan 67 planning, 70ffccdc)
Bun 1.4.3 runs here (`npm pack @oven/bun-linux-x64@1.4.3`). Peak MB / wall s, `/usr/bin/time`; B = the prototype's
recycle budget (the prototype's arena-only measure; stage 1's results are above). The full table (B=32..256, T4) is in
plans/67_memory_against_bun.md.
| corpus | tttt K1 | K1 B=128 | tttt T4 | T4 B=128 | bun-1 | bun-4 |
|---|---|---|---|---|---|---|
| vscode | 4261 / 13.67 | 2513 / 13.38 | 4900 / 5.82 | 3027 / 5.98 | 1443 / 20.3 | 1590 / 7.76 |
| type-fest | 734 / 1.41 | 263 / 1.67 | 854 / 0.77 | 560 / 0.82 | 1035 / 3.72 | 577 / 2.03 |
| effect | 709 / 1.48 | 454 / 1.49 | 804 / 0.73 | 769 / 0.73 | 381 / 3.13 | 317 / 1.37 |
| drizzle | 457 / 0.80 | 290 / 0.98 | 656 / 0.50 | 637 / 0.51 | 385 / 1.62 | 340 / 1.01 |
| webpack | 541 / 1.35 | 478 / 1.36 | 641 / 0.71 | 642 / 0.71 | 383 / 2.29 | 362 / 1.11 |
| zod | 450 / 0.68 | 274 / 0.74 | 533 / 0.37 | 530 / 0.37 | 402 / 1.40 | 320 / 0.68 |
| vue | 338 / 0.76 | 334 / 0.76 | 390 / 0.41 | 390 / 0.41 | 292 / 1.21 | 228 / 0.56 |
| tsc | 224 / 0.57 | 225 / 0.57 | 277 / 0.31 | 277 / 0.30 | 219 / 0.83 | 224 / 0.46 |
vscode K1 phases: 1727 MB resident after load+parse, 2281 after names, checker arena 1534 MB / 2.41M types; T4 arena
sum 1819 MB. RSS sampled during the check: tttt +150-170 MB/s throughout, bun flat at 1.36 GB from the end of its
2.7 s load. Malloc heap after names 1736 MB, 38% zero words.

## Plan 62 stages 2-3 (2026-10-14, todo entry 4): output independent of K, K follows the cores
kdiff (1 vs 16 checkers): source sweep 4 -> **1** / 3363 (inbox-zero's undefinedProperties order, tsgo's too), DT
0 / 8595, suite dumps 0 / 39870, every corpus byte-identical (`scripts/checkerdiff.py -k 16`, now exact), threadcheck
identical. Suite 13615 -> 13618 / 13660 (three own tests), 0 lost; `scripts/oracles.py` otherwise identical: corpora
(webpack 0/1, vue 0/3, effect 3/0), DT 8594 / 8595, `$ExpectType` 68637, flagflip 42 / 6 / 3, source sweep sample
811 / 822, perfsweep median 0.21, worst types ratio 1.62. New default `--checkers`: one per thread, at most one per
64 KB of checked non-lib source. A twelfth of the source sweep (281 projects, `--threads K --checkers K`, medians):
below 64 KB the check takes ~4 ms and K=4 costs +9% RSS for nothing; 64-256 KB K=4 checks in 0.7x K=1's time (a few
ms) at +11-13% RSS; above 1 MB 0.6-0.7x at +15-23%. On this box only react (2 checkers: 0.11 -> 0.12 s, 196 -> 165 MB)
and nest (3) change. Instructions (callgrind): tsc 4.700, type-fest 19.258, valibot 3.457, react 1.506, nest 1.186,
webpack 10.008, zod 5.076, xstate 2.726, drizzle 5.999, vue 6.598, effect 10.212, vscode 89.372, excalidraw 7.853,
tanstack-router 5.391 G (flat; an eager signature_resolve() for the return-type message code had cost drizzle +16%,
6.000 -> 6.962, before it was limited to reports).

## Content mappers (2026-10-14, todo entries 2-3, plan 56) and the stage 1 fan-out's rescue
Suite 13604 -> 13615 / 13660 (`contentMapper*` 15 / 15, ttttMissingInitializerImplicitAny), scenarios 211 / 0 failed
(both content-mapper scenarios now run). `scripts/oracles.py` identical to the session start: corpora (webpack 0/1,
vue 0/3, effect 3/0, rest 0/0), DT sweep 8594 / 8595, `$ExpectType` 68637, flagflip 42 / 6 / 3, source sweep sample
811 / 822, perfsweep median 0.21 (load), worst types ratio 1.62. Instructions (`scripts/instrcount.sh`, callgrind):
tsc 4.700, type-fest 19.256, valibot 3.458 (3.456 at 65aa0d23), react 1.506, nest 1.186, webpack 10.006, zod 5.077,
xstate 2.726, drizzle 6.000, vue 6.598, effect 10.215, vscode 89.370, excalidraw 7.853, tanstack-router 5.391 G.
Everything content-mapper is gated on the option: no corpus uses it.

## Perf session (2026-10-15/16, todo entry 9)
`scripts/bench.sh --runs 3 --bun` (bun 1.4.3: `npm pack @oven/bun-linux-x64@1.4.3`; best wall s / peak RSS MB;
defaults = every core, single = `--singleThreaded` / bun `--threads=1`):
| corpus | tttt | tsgo | bun | tttt single | tsgo single | bun single |
|---|---|---|---|---|---|---|
| TypeScript6 compiler | 0.30 / 271 | 0.98 / 536 | 0.46 / 216 | 0.58 / 217 | 1.41 / 418 | 0.84 / 213 |
| type-fest | 0.84 / 613 | 7.47 / 5368 | 2.05 / 563 | 1.66 / 379 | 8.21 / 4162 | 3.79 / 1012 |
| valibot | 0.24 / 308 | 1.30 / 831 | 0.48 / 154 | 0.45 / 251 | 1.97 / 655 | 1.05 / 278 |
| react | 0.12 / 169 | 0.69 / 455 | 0.36 / 299 | 0.19 / 145 | 0.66 / 296 | 0.39 / 177 |
| nest | 0.10 / 144 | 0.25 / 236 | 0.13 / 177 | 0.17 / 127 | 0.35 / 192 | 0.22 / 156 |
| webpack | 0.70 / 628 | 2.10 / 1163 | 1.12 / 362 | 1.34 / 525 | 3.33 / 971 | 2.17 / 380 |
| zod | 0.38 / 522 | 2.10 / 1445 | 0.71 / 321 | 0.72 / 383 | 2.69 / 957 | 1.38 / 389 |
| xstate | 0.19 / 272 | 0.71 / 466 | 0.29 / 162 | 0.36 / 215 | 0.99 / 347 | 0.59 / 174 |
| drizzle | 0.52 / 595 | 2.74 / 1683 | 1.03 / 342 | 0.87 / 387 | 2.72 / 910 | 1.66 / 376 |
| vue | 0.41 / 386 | 1.46 / 839 | 0.57 / 220 | 0.76 / 330 | 2.27 / 661 | 1.21 / 285 |
| effect | 0.72 / 722 | 2.78 / 1555 | 1.39 / 315 | 1.47 / 531 | 4.23 / 1229 | 3.18 / 375 |
| vscode | 6.13 / 2778 | 15.81 / 7982 | 7.99 / 1572 | 13.67 / 2485 | 26.85 / 6647 | 20.59 / 1417 |
| excalidraw | 0.48 / 467 | 2.83 / 2038 | 0.81 / 277 | 0.92 / 373 | 2.72 / 852 | 1.50 / 358 |
| tanstack-router | 0.34 / 391 | 2.00 / 970 | 0.73 / 333 | 0.65 / 333 | 2.55 / 657 | 1.22 / 325 |
| TypeScript6 `-b` | 1.11 / 445 | 1.97 / 558 | - | - | 2.97 / 476 | - |
2.5-8.9x faster than tsgo at the defaults, 2.0-4.9x single-threaded, at 0.11-0.6x its RSS; **1.3-3x faster than bun**
at the defaults (vscode and nest 1.3x), 1.3-2.3x single-threaded, but **1.6-2.3x bun's memory at the defaults**
(effect 2.3x, valibot 2.0x, vscode 1.77x) and ~1-1.75x single-threaded (plans/67 "Where it stands").
- perfsweep (quiet machine): check median **0.21x** tsgo, types median 0.64, worst 1.62 (date-fns/pkgs/docs, as
  before), peak RSS median 0.65, worst 0.80; one project above 1 on check time (lit/packages/labs/nextjs, 8 vs 6 ms).
- **Recycling's warm-ups**: redone shared work, not overhead (bugs/perf_leads.md "Recycling's cost"); a tail rule
  was tried and dropped (no time at T4, +60..90 MB).
- **Emit under `--noEmit`**: nothing but the declaration diagnostics walk, which tsc's CLI computes too when there are
  no other errors (drizzle 1.3%); it now stops once a checker reports a diagnostic sure to be reported (gated).
- **scaleprobe `--emit`** (new): six quadratic paths fixed (union of objects 57 -> 0.09 s at 32k, object literal 3.4 ->
  0.05, inferred return 4.7 -> 0.06, class 1.1 -> 0.06, namespace 0.47 -> 0.08, overloads 2.3 -> 0.07; overloads
  under `--noEmit` 1.03 -> 0.07 too).
- Oracles unchanged: suite 13697 / 13739 (0 lost), corpora 0 / 0 but vue 0 / 2, DT 8595, `$ExpectType` 68639, flagflip
  18 / 6 / 3, source sweep **3352 / 3363** full (sample 817 / 822), dtsdiff 1077 / 1233 projects, 95611 files, kdiff 1 / 3363, recycle 4 / 3363.
- **Instructions** (callgrind, `--noEmit`), session start -> end: tsc 4.723 -> **4.643** (-1.7%), type-fest 23.420 ->
  23.427, valibot 3.463 -> 3.464, react 1.512, nest 1.189, webpack 10.051 -> 10.048, zod 5.426 -> 5.425, xstate 2.771
  -> 2.772, drizzle 6.917 -> **6.835** (-1.2%), vue 6.635 -> 6.633, effect 10.612 -> **10.401** (-2.0%), vscode 96.778
  -> 96.763, excalidraw 7.930 -> 7.928, tanstack-router 5.412 -> 5.411 G.

## Source sweep + suite tail fan-out (2026-10-14, todo entry 7)
4 agents (A source sweep relations + the TS5115 bucket, B source sweep calls/inference/flow, C suite errors tail, D
suite symbols/types tail + module/declaration sweep projects), then second tasks (B scenarios/clidiff/DT sweep, C
mutation seeds 61/62, D flagflip/`$ExpectType`, A its untouched items); 71 picks, 0 lost components at every merge,
2 duplicates caught as pick conflicts (peer-dependency package ids A/D, never-before-any intersections A/B).
- suite 13464 -> **13515 / 13629** (+22 own tests); errors 13547 -> 13584, symbols 13560 -> 13592, types 13509 -> 13547.
- source sweep (full, match-set diff vs the session start) 3310 -> **3332 / 3363**, 0 lost (one transient loss,
  react-router `__tests__`, came with B's forceTuple commit and went with its fix). The 13-project -TS5115 bucket is
  plan 65 (a prototype matched all of them but made zod 0 -> 20 spurious).
- corpora unchanged (webpack 0/1, vue 0/3, effect 3/0, vscode 0/0, rest 0/0); DT sweep 8594 / 8595 (types/ember/v2
  written up, binder_modules.md); `$ExpectType` 68631 -> **68637 / 68640**; flagflip 46 / 8 / 3 -> **42 / 6 / 3**
  (a vscode/eopt TS2375->TS2322 regression found by the final run, fixed b46696dc); scenarios 209 -> **210 / 0 failed /
  14 skipped**; clidiff `--sample 4` 824 -> 827 / 839; mutants seed 61 11 -> 5, seed 62 11 -> 8; kdiff 4 / 3363
  projects differ at K=16 (inbox-zero, mermaid, openapi-typescript, redux-toolkit); perfsweep worst types ratio 1.62
  (date-fns docs, unchanged).
- Instructions (callgrind): tsc 4.766, type-fest 19.258, valibot 3.450, react 1.525, nest 1.183, webpack 10.898, zod
  5.065, xstate 2.711, drizzle 6.019, vue 6.601, effect 10.153, vscode 90.442, excalidraw 7.940, tanstack-router 5.420
  G: +0.2..+0.6% creep from ~70 commits; type-fest +1.2%, +0.84 of it TS2574's generic variadic element check
  (a03f29d0, tsgo runs it too; perf_leads.md has where its cost goes). Mid-session bisects: nest +3.1% (peer
  dependency lookup per resolution, fixed by D).

## Plan 63 stage 1 (2026-10-14, todo entry 6)
- K1 (`--checkers 1 --threads 1`) resident after load+parse / after names / `/usr/bin/time` peak MB, f67e4fa7 -> now:
  tsc 109/151/225 -> 104/144/219, webpack 237/323/545 -> 224/307/529, vue 120/164/342 -> 116/157/330, effect
  203/280/704 -> 192/265/688, excalidraw 153/207/379 -> 146/197/363, **vscode 1917/2517/4403 -> 1724/2277/4158**.
- Oracles unchanged: suite 13464 (0 lost), corpora (webpack 0/1, vue 0/3, effect 3/0), DT sweep 8594 / 8595,
  `$ExpectType` 68631, flagflip 46 / 8 / 3, source sweep sample 4 806 / 822, perfsweep worst types ratio 1.62;
  threadcheck and checkerdiff identical. scenarios 207 / 213, as at f67e4fa7 (triage.md).
- Instructions (callgrind; perf unavailable): tsc 4.744, type-fest 19.034, valibot 3.432, react 1.518, nest 1.179,
  webpack 10.853, zod 5.043, xstate 2.704, drizzle 5.997, vue 6.582, effect 10.089, vscode 90.136, excalidraw 7.927,
  tanstack-router 5.409 G. vscode -1.2%, most within 0.2%; react +0.6% and nest +0.8% (over the plan's 0.5%): small
  lib-dominated projects pay a malloc per annotated lib parameter (boxed annotation) and a separate locals table per
  scope that declares something.

## Plan 61 stage 3 (2026-10-13, todo entry 4)
- Suite 13463 -> **13464** (1 own test); corpora unchanged (webpack 0/1, vue 0/3, effect 3/0), DT sweep 8594 / 8595,
  `$ExpectType` 68631, flagflip 46 / 8 / 3, sourcesweep sample 4 805 -> **806** / 822 (socket.io), kdiff 5 / 3363,
  perfsweep worst types ratio 1.62 (date-fns/pkgs/docs; mikro-orm/packages/seeder 444k -> 392k types).
- Instructions (callgrind): tsc 4.761, type-fest 19.033, valibot 3.427, react 1.509, nest 1.170, webpack 10.875, zod
  5.051, xstate 2.698, drizzle 5.995, vue 6.581, effect 10.100, vscode 91.217, excalidraw 7.952, tanstack-router
  5.401 G (all within 0.2% of stages 1-2).

## Plan 62 stage 1 (2026-10-13, todo entry 2)
- kdiff (`scripts/kdiff.py`) 19 -> **5** / 3363 at K=16 (babel-traverse, inbox-zero, mermaid, openapi-typescript,
  redux-toolkit), **2** at K=4 (inbox-zero, redux-toolkit); suite dump at K=16 still 0 files differ.
- The sweeps now run at the default K: source sweep **3309 / 3363** (K=1 was 3307), DT sweep 8594 / 8595,
  `$ExpectType` 68631, flagflip 46 / 8 / 3, corpora unchanged; suite 13458 -> **13460** (2 own tests).
- perfsweep: check median 0.21x tsgo (load), worst types ratio 1.56. Instructions (callgrind): tsc 4.763, type-fest
  18.992, valibot 3.435, react 1.503, nest 1.169, webpack 10.842, zod 5.048, xstate 2.688, drizzle 6.020, vue 6.580,
  effect 10.086, vscode 91.142, excalidraw 7.964, tanstack-router 5.337 G (all within 0.1%).

## Checker count (2026-10-13, todo entry 1, plan 62)
Output at `--checkers 16` against 1: suite dump 0 / 39636 files differ, corpora 16 / 16 identical, DT sweep 0 / 8595,
source sweep 19 / 3363 (13 already at K=4; tsgo 1/4/16 identical on all 19 but inbox-zero's undefinedProperties).
Types / arena / RSS per K for six corpora and the FENNEL prototype (worse): plan 62 (done, deleted 2026-10-15: `git show 7c7c13db:agents/plans/62_checkers_follow_cores.md`).
Times: suite K=1 13 s, K=16 21 s; `scripts/kdiff.py` source sweep 7 min, `--dt` 4.5 min (4 jobs).

## Triage (2026-10-13, todo entry 10)
3 agents (own tests + vscode excludes; lib checking + two JSX/elaboration items; variance accuracy + feathers +
sweep load averages), ~12 picks, 0 lost at every merge. GCC 16.0 / clang 22.1 builds: warning-free in src/, suite and
corpus output byte-identical to GCC 15's, user times within noise.
- suite **13440 -> 13458 / 13583**: 18 own tests now run (test/cases, all pass); tsgo's suite unchanged at 13440 / 13568.
- corpora unchanged (webpack 0/1, vue 0/3, effect 3/0; vscode now checks its 3 formerly excluded files, 0/0). DT sweep
  8593 -> **8594 / 8595**, `$ExpectType` 68631, flagflip 46 / 8 / 3 (vscode cells regenerated for the overlay), source
  sweep sample 4 804 / 822 (full: 3307 / 3363 by agent B).
- perfsweep: worst types ratio 2.50 -> **1.56** (knex 18287 -> 4832 types, tsgo 7324), types 70.3M vs tsgo 128.0M
  (+3M: default lib files are checked now), check median 0.16x tsgo, RSS median 0.65x, none above tsgo's.
- Instructions (callgrind): tsc 4.763, type-fest 18.988, valibot 3.433, react 1.503, nest 1.169, webpack 10.844, zod
  5.048, xstate 2.688, drizzle 6.016, vue 6.579, effect 10.089, excalidraw 7.966, tanstack-router 5.336 G (type-fest
  +1.6%, webpack +3.6%: lib checking, ~0.3 G per esnext+dom program where tsgo spends ~4x; the rest -0.4..+0.5%).
  vscode not measured.

## Suite/JS tail fan-out (2026-10-12, todo entry 9)
4 agents by test list (A/B the 80 non-JS errors-failing tests split alphabetically, C the 89 types/symbols-only, D the
63 JS tests), then second tasks (B source sweep, C mutation seeds 41/42, D corpora/flagflip missing); ~85 picks, 0 lost
components at every merge, 0 duplicates, 0 conflicts.
- suite **13330 -> 13440 / 13568** (99.1%); errors 13455 -> 13511, symbols 13489 -> 13526, types 13431 -> 13474.
  Failing JS tests 63 -> **22** (1143 / 1165).
- corpora: webpack 0/5 -> **0/1**, drizzle 0/3 -> **0/0**, vue 0/3, effect 3/0, vscode 0/0, the rest 0/0.
  DT sweep 8592 -> **8593 / 8595**, `$ExpectType` 68631, flagflip 132 / 16 / 3 -> **46 / 8 / 3**, source sweep sample 4
  800 -> **804 / 822** (full: 3295 -> 3304 / 3363 by agent B's runs), mutants seed 41 10 -> 7, seed 42 13 -> 5.
  clidiff `--sample 4` 820 / 839, scenarios 209 / 213.
- perfsweep: worst types ratio 1.56 -> 2.50 (feathers/packages/knex 4518 -> 18287 types, tsgo 7324, from the variance
  port d93172d5); total types 67.1M vs tsgo 128.0M, check median 0.20x. Fixed after the fan-out (agent C, generic
  relation keys, bugs/variance_accuracy.md): knex **4832** types (0.66); `--sample 4` worst types ratio **1.56**
  (lightweight-charts lwc-plugin-hlc-area-series, 1624 vs 1044 types), median 0.66.
- Instructions (callgrind): tsc 4.774, type-fest 18.686, valibot 3.433, react 1.497, nest 1.167, webpack 10.468, zod
  5.022, xstate 2.680, drizzle 6.007, vue 6.601, effect 10.130, vscode 91.288, excalidraw 7.971, tanstack-router
  5.339 G (+0.3..+0.6% creep from ~85 commits; drizzle +0.96%, 0.77 of it a GCC layout shift from 6c3c5419 whose new
  path never runs there, checked by agent D; two real regressions bisected and fixed mid-session: excalidraw +0.7%
  union accessibility, valibot/zod +0.8% marker-reference search).

## isolatedDeclarations from the pseudo tree (2026-10-12, todo entry 8, plan 55 stage 1b)
TS 6.0's syntactic builder in `declaration_diagnostics.c` replaced by tsgo's serializeTypeForDeclaration /
pseudoTypeToNode reporting. Suite 13328 -> **13330** (isolatedDeclarationsAddUndefined,
isolatedDeclarationErrorsExpressions; 29/30 isolatedDeclaration tests, the JS one left). Every other oracle unchanged
(corpora, DT sweep 8592, `$ExpectType` 68631, flagflip 132 / 16 / 3, source sweep sample 800 / 822, perfsweep median
0.19 / worst types 1.56).
- New probe, `--declaration --isolatedDeclarations` flipped against tsgo (TS9xxx lines): 400 tsc-clean source sweep
  projects, missing / spurious 4944 / 7352 (old builder) -> 568 / 1, of which 487 missing are in arktype, redux-toolkit
  and trpc/react-query where tttt has unrelated type errors (the CLI then drops declaration diagnostics); zod, xstate,
  nest exact (old: 82/77, 9/11, 6/6).
- Instructions: tsc corpus 4.690 -> 4.774 G (+1.8%: it has isolatedDeclarations, every declaration now gets the
  equivalence check and Inferred parts print), valibot 3.431, others within 0.1% (type-fest 18.600, react 1.502,
  nest 1.161, webpack 10.439, zod 4.994, xstate 2.670, drizzle 5.950, vue 6.573, effect 10.083, vscode 90.917,
  excalidraw 7.938, tanstack-router 5.324 G).

## Pseudochecker (2026-10-12, todo entry 7, plan 55 stages 1-3 but 1b)
`src/ty/pseudo.c` and its printing at tsgo's serializeTypeForDeclaration/serializeReturnTypeForSignature sites, the
old display slices deleted. Suite 13318 -> **13328** / 13568 (accessor pairs, `this`-returning literals, deeper
function recursion, `as const` tuples, Array<error> members). Everything else unchanged: corpora (webpack 0/5, drizzle
0/3, vue 0/3, effect 3/0, the rest 0/0), DT sweep 8592 / 8595, `$ExpectType` 68631, flagflip 132 / 16 / 3, source sweep
3293 / 3363 (full run), perfsweep median check ratio 0.19, worst types ratio 1.56.
- Instructions (callgrind): tsc 4.690, type-fest 18.598, valibot 3.430, react 1.502, nest 1.161, webpack 10.439, zod
  4.992, xstate 2.670, drizzle 5.952, vue 6.572, effect 10.095, vscode 90.923, excalidraw 7.938, tanstack-router 5.323 G
  (-0.1..+0.1% against entry 6: printing only).

## Source sweep fan-out (2026-10-12, todo entry 6)
4 agents by repo list (A config/module resolution/references/parser/perf, B relations+inference in application code,
C type-level libraries, D contextual typing/narrowing/calls; then second tasks: A and B their own leftovers, B plan 61,
D the tail of C's list), ~85 picks, 0 lost components at every merge. Source sweep **3132 -> 3293 / 3363** projects,
repos fully matching 93 -> 129 / 174, no project that matched at the start lost (match-set diff). Leftovers per agent
in bugs/sourcesweep_checker.md; plan 61 (constraint functions) written.
- suite 13317 -> **13318** / 13568; corpora unchanged (webpack 0/5, drizzle 0/3, vue 0/3, effect 3/0, vscode 0/0); DT
  sweep 8592 / 8595, `$ExpectType` 68631, flagflip 132 / 16 / 3 (6 -> 3 other-code), source sweep sample 4 779 -> 799
  / 822, perfsweep median check ratio 0.19, worst types ratio 1.56.
- Caught at merge time: a +15.6% react (C's `T & P` reduction asking a distributive base constraint: fixed with a
  hacks.md perf gate), +0.8% nest (A: realpath on relative names), 11 source sweep projects lost (B's
  SkipGenericFunctions in conditional arguments, found only by a full-sweep match-set diff), flagflip webpack -31
  TS6196 and xstate +7 TS5115 (D: found only by flagflip against a session-start run in a base worktree).
- Instructions (callgrind): tsc 4.687, type-fest 18.611, valibot 3.429, react 1.502, nest 1.161, webpack 10.432, zod
  4.990, xstate 2.671, drizzle 5.949, vue 6.568, effect 10.093, vscode 90.864, excalidraw 7.932, tanstack-router 5.325 G
  (against the session start: valibot -2.0%, zod +0.8%, the rest +0.0..+0.5%; D's last commit moving cold code out of
  the hot try_resolve_indexed alone took drizzle -0.8% and 0.2-0.4% elsewhere: watch GCC inlining there).

## Perf at breadth after plan 59 stage 2 (2026-10-12, the fan-out's merges up to a43aaa00)
`scripts/perfsweep.py -j 3` with agents running (types and RSS ratios are exact; time from the end-of-session run
below). 1779 projects:
| ratio tttt / tsgo | median | p90 | worst | > 1 |
|---|---|---|---|---|
| types | 0.68 (was 0.76) | 0.94 (1.07) | 1.55 (16.40) | 92 (322) |
| peak RSS | 0.69 (0.71) | 0.79 (0.81) | 0.95 (2.48) | 0 (28) |
Total types 71.3M vs tsgo's 127.9M (was 96.0M). Each stage 1 outlier (types ratio before -> after): date-fns/pkgs/docs
16.4 -> 0.94, feathers typebox 7.0 -> 1.08, nitro 5.18 -> 0.79, rxjs 2.62 -> 0.45 (RSS 1943 -> 475 MB), nuxt 2.57 ->
0.84, lit ssr-react 2.43 -> 0.64, dokploy x3 2.3 -> 0.69, mcp-sdk codemod 2.24 -> 0.89, quill 1.82 -> 0.57,
immutable-js 1.77 -> 0.45, vitest/examples/lit 1.62 -> 0.45; arktype/ark/docs 1.51 -> 0.92 (a 5M-instantiation
runaway, 300k TS2589 hits), linkwarden/apps/mobile one file 1.54 -> 0.14 s. New worst: lightweight-charts plugins
(~1.5k types, 1.3-1.55), karakeep/packages/plugins 1.40 (bugs/perf_leads.md). Callgrind on the corpora (base e273be72
-> 2nd checkpoint): drizzle -17.5%, xstate -9.3%, react -2.8%, tanstack-router -2.8%, zod -2.2%, effect -2.0%, the
rest -0.4..-1.0%, none up. Time, from `scripts/oracles.py`'s perfsweep at the end (-j 4, nothing else running, the
same setting as stage 1's baseline): check time median 0.16 (stage 1: 0.21), p90 0.29 (0.35), 2 projects > 1 (5); total
check 153 s vs tsgo's 1159 s (0.13). Its *wall* column was inflated in the load/parse phase (82 projects > 1, nest
11 s) right after the oracle run's sweeps; rerun alone, those projects were faster than at stage 1 (nest 0.19 vs 0.28 s,
renovate 2.7 vs 4.5 s): don't read perfsweep walls taken right after `oracles.py`'s other oracles. tsgo's side is cached in
`test/perfsweep/tsgo.jsonl.gz`, each incremental project measured cold (its own `--tsBuildInfoFile`).

## Memory breakdown (2026-10-12, plan 60 research, e043e9e2)
Peak RSS per corpus at K1/K4 with the resident after name resolution, checker arenas, and tsgo 7.1 `--singleThreaded`
"Memory used" (Bun-4 from the 12-core machine's 2026-10-11 run, `git show f760ecdc:agents/measurements.md`):

| corpus | tttt K1 RSS / bound / arena MB | tttt K4 RSS / arena sum | tsgo 1 thread "Memory used" | Bun-4 RSS (other machine) |
|---|---|---|---|---|
| tsc | 254 / 165 / 63 | 320 / 94 | - | 221 |
| type-fest | 797 / 61 / 695 | 920 / 806 | 3274 | 555 |
| valibot | 274 / 90 / 160 | 333 / 196 | - | 158 |
| webpack | 596 / 357 / 185 | 685 / 236 | 805 | 356 |
| zod | 464 / 132 / 287 | 551 / 360 | - | 325 |
| drizzle | 470 / 144 / 286 | 665 / 442 | 760 | 347 |
| vue | 369 / 179 / 149 | 429 / 179 | 457 | 224 |
| effect | 767 / 310 / 401 | 890 / 467 | 1065 | 307 |
| excalidraw | 416 / 230 / 155 | 527 / 228 | - | 266 |
| vscode | 5220 / 2812 / 1818 | 6017 / 2132 | 5941 (parse+bind alone: 2908) | 1557 |
"bound" = resident after name resolution (AST + binder + libs). Before plan 60 stages 1-2; plan 63 has today's AST numbers.
vscode's split then (AST 2.17 GB, binder ~0.6, checker malloc
~0.45: LazyMemo pages 222 MB, intern tables 202 MB; arena 1.82 GB; the top arena sites: bugs/perf_leads.md "Memory").
The tuple fix (tuple targets share element type parameters, element props a read-only declaration): type-fest K1 arena
1906 -> 695 MB, RSS 2057 -> 797 MB, types 4.78M -> 217k (type counts on tuple-heavy code are now well below tsgo's,
which makes fresh parameters per target). No huge-page slack: RSS is the same with `TTTT_NO_REEXEC=1`.
- Instructions (callgrind, after it): tsc 4.665, type-fest 18.609 (19.687 before: -5.5%), valibot 3.486, react 1.488,
  nest 1.159, webpack 10.352, zod 4.933, xstate 2.662, drizzle 5.910, vue 6.547, effect 9.998, vscode 90.478,
  excalidraw 7.891, tanstack-router 5.285 G.

## Triage (2026-10-09..11, todo entry 10)
Suite **13316 / 13571** (0 lost); corpora 0 but webpack 0/5, drizzle 0/3, vue 0/3 (4 -> 3: tsgo's include-pattern
order), effect 3/0; DT sweep 8592 (3 missing); `$ExpectType` 68631; flagflip 132 / 16 / 6 (145 missing before);
source sweep sample 763 / 822. failstats: 13595 / 13846 by its own count (bugs/harness.md).
- Instructions (callgrind): tsc 4.699, type-fest 19.687, valibot 3.500, react 1.531, nest 1.166, webpack 10.425, zod
  5.041, xstate 2.933, drizzle 7.153, vue 6.597, effect 10.200, vscode 91.169, excalidraw 7.969, tanstack-router
  5.437 G (flat: +0.0..+0.4% against entry 9).

## Plan 54 stage 5, the 7.1 language and new tests fan-out (2026-10-09, todo entry 9)
4 agents by list over the 189 failing tests new in tsgo's tree (A source phase imports + module/config, B ambient-module
import attributes + content mappers + merging/JS, C regex/enums/strings/declaration diagnostics, D checker
regressions), then second tasks (A clidiff/scenarios, B source sweep, C DT sweep/`$ExpectType`/mutate, D flagflip);
~110 picks, 0 lost components at every merge. Failing new-tree tests **189 -> 43**; plan 56 (content mappers) written,
plan 57 (distributed type parameters) written and done.
- suite **13148 -> 13309 / 13568** (98.1%); errors 13309 -> 13444, symbols 13431 -> 13488, types 13322 -> 13418.
- corpora 0 / 0 but webpack 0 / 5, drizzle 25 -> **3**, vue 6 -> **4**, effect 3 / 3 -> **3 / 0**, vscode 0 / 0.
  DT sweep 8581 -> **8592 / 8595**, `$ExpectType` 68619 -> **68631**, flagflip 594 / 17 / 6 -> **145 / 17 / 6**,
  source sweep 3069 -> **3119 / 3363** (88 repos; sample 4 762 / 822), scenarios 203 -> **209 / 213**, clidiff
  `--sample 4` 758 -> 779 / 839.
- Instructions (callgrind): tsc 4.691, type-fest 19.677, valibot 3.496, react 1.525, nest 1.162, webpack 10.386, zod
  5.034, xstate 2.927, drizzle 7.135, vue 6.572, effect 10.186, vscode 91.013, excalidraw 7.942, tanstack-router
  5.414 G (valibot -9.3%, not bisected; +0.3..+0.9% elsewhere, mostly the distributed type parameters' accepted cost;
  a +0.8% type-fest from a relation shortcut was bisected and fixed mid-session).

## Plan 54 stage 5, the JS rework fan-out (2026-10-09, todo entry 8)
4 agents by CHANGES.md table (A expandos/JS classes, B CommonJS, C JSDoc tags and types, D calls/JS syntax/rest),
then second and third tasks (webpack, flagflip's webpack cells, JS mutants); ~75 picks, 0 lost components at every
merge. `src/` net **-2246 lines** (constructor functions, value-as-type, `@enum`, Closure syntax, CommonJS aliases
deleted). Failing JS tests **530 -> 79** (1086 / 1165 pass).
- suite **12681 -> 13148 / 13568** (96.9%); errors 13047 -> 13309, symbols 13083 -> 13431, types 12970 -> 13322.
- webpack 0 / 589 -> **0 / 5**; every other corpus unchanged (drizzle 0 / 25, vue 0 / 6, effect 3 / 3, vscode 0 / 10).
  flagflip 7818 / 106 / 6 -> **594 / 17 / 6** (webpack's JS cells at its default-flag misses). DT sweep 8581,
  `$ExpectType` 68619, source sweep 3057 -> 3069 / 3363 mid-session (sample 4: 752 -> 753 / 822), scenarios 203 / 213.
  JS mutants (`mutate.py --tests js -n 3000`, seeds 1-3): 83 / 28 / 31 differing -> 19 / 18 / 19.
- Instructions (callgrind): tsc 4.696, type-fest 19.608, valibot 3.856, react 1.515, nest 1.154, webpack 10.308, zod
  5.031, xstate 2.965, drizzle 7.069, vue 6.544, effect 10.143, vscode 90.823, excalidraw 7.910, tanstack-router
  5.417 G (flat: webpack -1.2%, the rest -0.4% .. +0.2%).

## Plan 54 stage 5, the TS checker deltas fan-out (2026-10-09, todo entry 7)
4 agents (A relation elaboration, B diagnostic code deltas, C types printer, D symbols/alias naming/union order), ~95
picks, 0 lost components at every merge. The failing non-JS tests that existed in 6.0's tree went **904 -> 182**
(JS 560 -> 530: entry 8; tests new in 7.1's tree 211 -> 174: entry 9).
- suite **11892 -> 12681 / 13568** (93.5%); errors 12629 -> 13047, symbols 12909 -> 13083, types 12585 -> 12970.
- corpora 0 / 0 but webpack 0 / 589, drizzle 41 -> **25**, vue 14 -> **6**, effect 3 / 5 -> **3 / 3**, vscode 0 / 10
  (TS2769 overload reporting). DT sweep 8553 -> **8581 / 8595** (0 spurious, 14 missing), `$ExpectType` 68619,
  flagflip 8139 / 113 / 222 -> **7818 / 106 / 6**, source sweep 3018 -> **3057 / 3363** (sample 4: 741 -> 752 / 822),
  scenarios 203 / 213.
- Instructions (callgrind): tsc 4.714, type-fest 19.613, valibot 3.858, react 1.515, nest 1.152, webpack 10.430, zod 5.035, xstate 2.968, drizzle 7.070, vue 6.557, effect 10.166, vscode 91.009, excalidraw 7.920, tanstack-router 5.423 G (flat: -0.9% .. +0.1%).

## Plan 54 stage 4: TypeScript 7's option errors, the removed options' code deleted (2026-10-09, todo entry 6)
- source sweep **2242 -> 3018 / 3363** projects, 30 -> **78 / 174** repos; sample 4 559 -> **741 / 822**. Three causes:
  tttt says TS5108/TS5102 and stops where tsgo does (+120 projects alone, 2362), **sweep.py and sourcesweep.py no longer
  pass `--keep-going`** (the caches are tsgo's CLI since entry 4, which stops at syntactic and options diagnostics:
  +618; realworld.py dropped it too, `--keep-going=options` is gone), and TS5090 on an extended config's `paths` (+38).
  What's left is 7.x checker deltas (+TS2345 87 projects, +TS2322 43, -TS2552 25 ...) and 12 projects' +TS6305.
- scenarios 185 -> **203 / 213**; suite 11860 -> **11892 / 13571** (+32: the `deprecatedCompilerOptions*` tests, the
  ES5iterable/downlevel ones whose baselines are just TS5102, incrementalInvalid...), 0 lost; corpora, DT sweep 8553,
  `$ExpectType` 68619, flagflip 8139 / 113 / 222 unchanged.
- Code: ~460 lines deleted (classic/node10 resolution, baseUrl lookups, the esModuleInterop=false paths, ES5 checks,
  outFile ordering and bundle names, downlevelIteration).
- `scripts/clidiff.py` on every source sweep project: **2918 / 3353** byte-identical with tsgo's `-p --noEmit` (342
  diagnostics differ, 93 text only).
- Instructions (callgrind): tsc 4.715, type-fest 19.642, valibot 3.874, react 1.518, nest 1.153, webpack 10.483, zod
  5.048, xstate 2.988, drizzle 7.132, vue 6.550, effect 10.171, vscode 91.199, excalidraw 7.930, tanstack-router 5.452 G (flat).

## Plan 54 stage 3: every cache regenerated with tsgo 7.1 (2026-10-09, thirteenth window's todo entry 5)
tsgo's time to regenerate (4 cores): DT sweep 553 s, its nuia / eopt variants 527 / 492 s, `$ExpectType` 87 s, source
sweep 704 s, flagflip (182 cells) 735 s, the 17 corpora ~2 min. Against the new caches:
- suite 11860 / 13571 (unchanged), scenarios (tsgo's tsbuild + tsc/{projectReferences,composite} baselines) **185 /
  213**, 11 skipped (failures: TS5107 where 7.0 says TS5108, stage 4).
- corpora, spurious / missing against tsgo's own diagnostics (`test/corpora.tsc.jsonl`, was a hand-kept count): 0 / 0
  on tsc, tsc-src, type-fest, valibot, react, nest, zod, **zod-root** (tsgo 7.1: the same 53 as tsc 6, in 3 s; 7.0.2 ran
  away), xstate, excalidraw, tanstack-router, typescript-eslint; webpack 0 / 589 (JS rework), drizzle 0 / 41
  (TS2416 14, TS2430 11, TS2578/2769 8 pairs), vue 0 / 14, vscode 0 / 10 (TS2345 7, TS2322 2, TS2416), effect 3 / 5
  (tttt's 3 TS2578, tsgo's TS2769 4 + TS2719).
- DT sweep **8553 / 8595** (spurious 1, missing 23, both 18): -TS2303 in types/webpack (16 packages), +TS2497 (12),
  -TS2769/-TS2578, +TS2300 in selenium-webdriver. `$ExpectType` **68619 / 68640** (12 tsc-any, 4 other, 4 we-any, 1
  union order).
- source sweep **2242 / 3363** projects (6.0: 3003 / 3359), 30 / 174 repos; sample 4: 559 / 822. Dominated by 7.0's
  removed options: tsgo stops at TS5108 (537 projects) / TS5102 (308), tttt says TS5107 / TS5101 and checks on (+TS2307
  23967, +TS7006 6603, ... in those projects). Stage 4's job.
- flagflip (es5 cell dropped, sloppy without `--alwaysStrict false`: both are TS5108 in 7.0): 168 cells, missing
  **8139**, spurious 113, other code 222; without webpack 928 / 23 / 210 (the corpora's own 7.1 deltas repeated per
  cell, elaboration codes TS2740->TS2344, TS2741->TS2345).
- Instructions (callgrind, the tsc corpus now `thirdparty/corpora/TypeScript6`, same commit): tsc 4.715, type-fest
  19.648, valibot 3.875, react 1.519, nest 1.154, webpack 10.484, zod 5.047, xstate 2.988, drizzle 7.129, vue 6.551,
  effect 10.175, vscode 91.240, excalidraw 7.935, tanstack-router 5.452 G (flat).

## Plan 54 stage 2: the oracle tools on tsgo (2026-10-09, thirteenth window's todo entry 4)
Probe (`scripts/tsgoprobe.sh`): 50 random suite tests in 0.18 s, 184 / 184 baselines byte-identical to the reference
(APISample_jsdoc differs: tsgo skips it). `tttt-oracle` = `built/tsc -p P --noEmit` on 30 DT packages and 30 source
sweep projects (diagnostics, order, exit codes). `mutate.py -n 400 --seed 3`: 1.6 s end to end. With the CLI now
always `--noEmit` (no `--as-emitting`) the 6.0 caches' emit-only codes (TS5011/5055/5056/5096) are filtered in
`sweep.compare` until stage 3: sourcesweep sample 736 / 822 (737 before the switch), DT sweep 8586, every other oracle
unchanged (suite 11860, corpora, `$ExpectType` 68631, flagflip 17 / 11 / 4, scenarios 244). `scripts/clidiff.py
--sample 12 --max-ms 10000` against tsgo 7.1: **176 / 261** byte-identical, 0 exit code mismatches after moving to 7.1's
exit codes (2 with diagnostics; 102 mismatched before), 81 checker differences (7.1 deltas), 4 text. Instructions:
tsc 4.716, type-fest 19.646, valibot 3.876, react 1.519, nest 1.154, webpack 10.485, zod 5.048, xstate 2.989, drizzle
7.128, vue 6.551, effect 10.176, vscode 91.235, excalidraw 7.934, tanstack-router 5.453 G (flat).

## Plan 54 stage 1: the switch to the 7.1 suite (2026-10-08, thirteenth window's todo entry 3)
Suite (tsgo's native tree, fed0bf24): **11860 / 13571** (errors 12597, symbols 12909, types 12585; 1721 skipped
configurations), 10.3 s wall (intersectionConstructorReductionCrash 63 s -> 0.2 s). Libs are 7.1's. Corpora unchanged
(zod-root 53, effect 3, the rest 0), `$ExpectType` 68631, flagflip 17 / 11 / 4, DT sweep 8594 -> 8586 (8 spurious:
7.1's `lib.dom` CloseWatcher/Serial vs DT packages, against 6.0's cache), sourcesweep sample 699 / 822 (36 "crashes"
are exit code 2 since plan 53, triage.md). Instructions: tsc 4.717, type-fest 19.648, valibot 3.875, react 1.519,
nest 1.154, webpack 10.486, zod 5.048, xstate 2.989, drizzle 7.129, vue 6.552, effect 10.174, vscode 91.246,
excalidraw 7.935, tanstack-router 5.453 G (flat).

## Plan 53 stage 1: CLI parity (2026-10-08, thirteenth window's todo entry 1)
`scripts/clidiff.py` (stdout byte for byte, exit codes): against tsc 6 (4d4f005c, `--sample 12 --max-ms 10000`) 243 /
261 source sweep projects identical, 175 of them with diagnostics; the 18 others are 12 checker differences and 6
message texts (triage.md), no ordering or exit code mismatch. Against tsgo 7.0.2 (`--tsgo --sample 4 --max-ms
30000`) 551 / 827 identical (368 with diagnostics), 255 differ in diagnostics (7.0's removed options and checker
deltas), 21 in text, 0 exit codes, 0 orderings. Every oracle unchanged: suite 14835, corpora, sweep 8594 / 8595,
`$ExpectType` 68631, flagflip 17 / 11 / 4, sourcesweep sample 737 / 822, scenarios 244. Instructions: tsc 4.715, type-fest 19.625,
valibot 3.873, react 1.516, nest 1.152, webpack 10.485, zod 5.045, xstate 2.986, drizzle 7.162, vue 6.550, effect
10.203, vscode 91.223, excalidraw 7.930, tanstack-router 5.456 G (flat; vscode -0.3%).

## Plan 51 stage 3 (2026-10-08, twelfth window's todo entry 9)
Every oracle unchanged against 5a04657f: suite 14835, corpora (zod-root 53, effect 3, the rest 0), sweep 8594 / 1
missing, `$ExpectType` 68631, flagflip 17 / 11 / 4, sourcesweep sample 737 / 822. Instructions (callgrind, single
checker) 5a04657f -> end: tsc 4.715 -> 4.717 G, type-fest 19.644 -> 19.645, valibot 3.874 -> 3.873, react 1.516 ->
1.515, nest 1.153, webpack 10.485 -> 10.484, zod 5.050 -> 5.048, xstate 2.990, drizzle 7.172, vue 6.559 -> 6.556,
effect 10.217 -> 10.209, vscode 91.523 -> 91.463, excalidraw 7.931 -> 7.930, tanstack-router 5.614 -> 5.612.

## Hacks audit (2026-10-08, twelfth window's todo entry 7, 4 sub-agents)
hacks.md 895 -> 784 lines (249 -> 209 entries), ~20 hacks deleted: suite 14833 / 14969 (0 lost), corpora 0 but effect 3
and zod-root 53, sweep 8594 / 8595, `$ExpectType` 68631, flagflip 18 -> 16 / 10 / 4, sourcesweep sample 4 555 -> 556
/ 822. Instructions (callgrind, vs the perf session's): tsc 4.689 (+0.15%), type-fest 19.548, valibot 4.163 (+0.19%),
react 1.557 (+0.84%: `base_constraint()` of an invalid indexed access is none like tsc's, react-hook-form paths),
nest 1.150, webpack 10.455, zod 4.996, xstate 3.199, drizzle 8.077, vue 6.752, effect 10.088, vscode 91.291 (+0.09%),
excalidraw 7.833, tanstack-router 5.889 G.

## Source sweep (2026-10-08, plan 52 stage 2 drain, twelfth window's todo entry 8)
`scripts/sourcesweep.py` (174 repos, 3359 projects compared; tttt now with `--keep-going=options`): **2997 / 3359 match (746
clean under tsc), 71 / 174 repos fully match**; spurious 277, missing 33, both 52, crash 0, timeout 0 (stage 1: 2241,
47 repos, 520 / 367 / 231). tttt 811 s single-threaded in total (1112 s at stage 1: `-p` no longer checks referenced
projects' sources), ~200-480 s wall depending on what else runs; tsc 11045 s of CPU. `--sample 4`: 737 / 822 (554).
Suite 14833 -> 14835 / 14969 (0 lost), corpora 0 but effect 3 and zod-root 53, sweep 8594 / 8595, `$ExpectType`
68631 / 68640, flagflip 16 / 10 / 4 -> 17 / 11 / 4 (one TS2488 moved, hacks.md `checker_defer_function_body`),
scenarios 240 -> 244 / 0 failing. Instructions (callgrind, vs the hacks audit's): tsc 4.715 (+0.6%), type-fest 19.645
(+0.5%), valibot 3.874 (-6.9%), react 1.516 (-2.6%), nest 1.153, webpack 10.485 (+0.3%), zod 5.050 (+1.1%), xstate
2.990 (-6.5%), drizzle 7.172 (-11.2%), vue 6.559 (-2.9%), effect 10.217 (+1.3%), vscode 91.523 (+0.3%), excalidraw
7.931 (+1.3%), tanstack-router 5.614 G (-4.7%). The drops are mostly ss-c's per-mapper instantiation caches (tsc's
activeTypeMappersCaches); the +1% ones weren't bisected.

## Plan 51 stage 2 (2026-10-08, twelfth window's todo entry 4)
Context anchors: suite 14832 / 14969 (0 lost), corpora unchanged (0 but effect 3, zod-root 53), sweep 8594 / 8595,
`$ExpectType` 68628 -> 68631 / 68640, flagflip 18 / 10 / 4. Instructions (callgrind, vs plan 50's): tsc 4.780
(+0.13%), type-fest 19.754, valibot 4.247 (+0.24%), react 1.544, nest 1.150, webpack 10.628 (+0.12%), zod 5.107
(+0.33%), xstate 3.207 (+0.72%: anchor bookkeeping, one more intern table doubling), drizzle 8.061, vue 7.068 (+0.41%),
effect 10.090 (+0.49%), vscode 92.853 (+0.42%), excalidraw 7.979 (+0.14%), tanstack-router 5.892 (+0.26%) G. The
unoptimised port was +12.6% on vue (`this` re-deriving through anchors) and +2.8% on tanstack-router (discriminating
huge unions per property): see plan 51's stage 2 notes for the caches.

## Plan 50 (2026-10-08, twelfth window's todo entry 3)
CommonJS export= merged in place at link time: suite 14829 -> 14832 / 14969 (0 lost), corpora unchanged (0 but
effect 3, zod-root 53), sweep 8594 / 8595, `$ExpectType` 68628 / 68640, flagflip 18 / 10 / 4. Instructions
(callgrind): tsc 4.774, type-fest 19.759, valibot 4.237, react 1.543, nest 1.150, webpack 10.615, zod 5.090, xstate
3.184, drizzle 8.055, vue 7.039, effect 10.041, vscode 92.469, excalidraw 7.968, tanstack-router 5.877 G: flat.

## Plan 51 stage 1 (2026-10-08, twelfth window's todo entry 1)
Every oracle identical to 55458150 (`scripts/oracles.py`): suite 14829 / 14969, corpora 0 but effect 3 and zod-root 53,
sweep 8594 / 8595, `$ExpectType` 68628 / 68640, flagflip 18 / 10 / 4. Instructions (callgrind, 81aa712b): tsc 4.770,
type-fest 19.766, valibot 4.240 (+0.14%), react 1.542, nest 1.149, webpack 10.611, zod 5.092, xstate 3.184, drizzle
8.062, vue 7.037, effect 10.046, vscode 92.435, excalidraw 7.967, tanstack-router 5.879 G: flat.

## Triage drain (2026-10-07, eleventh window's todo entry 10, 4 sub-agents)
Suite **14805 -> 14829 / 14969** (errors 14903, symbols 14920, types 14872), 0 lost. Corpora: every one 0 but effect 3
(plan 19) and zod-root = tsc's 53; **vscode 1 -> 0**. Sweep 8594 / 8595 (the lib-check one), `$ExpectType` 68621 ->
**68628 / 68640**, flagflip missing / spurious / other 42 / 12 / 5 -> **18 / 10 / 4**, scenarios 240 / 0, threadcheck and
checkerdiff identical. Mutation: seed 2 32 -> 13, seed 17 13 -> 7, multi/js seed 16 22 -> 11. Instructions (callgrind,
4d664405; vs entry 9): tsc 4.771 (-0.3%), type-fest 19.767 (+1.1%: tsc 6's tuple member order and label-keyed tuple
targets), valibot 4.234 (+0.4%), react 1.542, nest 1.150, webpack 10.607 (-0.7%), zod 5.091 (+0.4%), xstate 3.184,
drizzle 8.064, vue 7.032, effect 10.045 (+0.5%), vscode 92.442 (+0.3%), excalidraw 7.966, tanstack-router 5.879 G.

## Perf session on the re-pinned corpora (2026-10-07, eleventh window's todo entry 9, plan 48 stage 6)
No code changes; bench.sh now runs every corpus (effect added) on the same config for tsgo, tsc with a 16 GB heap,
and its obsolete synthetic corpus is gone. Instructions (callgrind, `--checkers 1`, 6650c15e) **identical to entry 8's
end**, vscode measured for the first time since its re-pin: tsc 4.786, type-fest 19.552, valibot 4.217, react 1.540,
nest 1.148, webpack 10.682, zod 5.069, xstate 3.178, drizzle 8.053, vue 7.021, effect 9.998, **vscode 92.151**,
excalidraw 7.956, tanstack-router 5.878 G. `scripts/bench.sh --runs 3` (quiet machine but for one idle agent session), best
wall / peak RSS, tttt at its defaults (4 checkers, 4 threads on 2 cores x 2 SMT):
| corpus | tttt | tsc 4d4f005c | tsgo 7.0.2 | tsgo 1 thread | tsgo / tttt |
|---|---|---|---|---|---|
| tsc `src/compiler` | **0.32 s** / 322 MB | 7.40 s / 616 | 1.36 s / 623 | 1.89 s / 481 | 4.3x |
| type-fest | **0.83 s** / 2307 MB | 27.66 s / 5386 | 11.52 s / 6090 | 12.58 s / 4448 | 13.9x |
| valibot | **0.26 s** / 336 MB | 8.55 s / 942 | 1.66 s / 948 | 2.46 s / 737 | 6.4x |
| react | **0.12 s** / 215 MB | 3.88 s / 605 | 0.93 s / 550 | 0.83 s / 296 | 7.8x |
| nest | **0.10 s** / 154 MB | 2.28 s / 380 | 0.36 s / 251 | 0.47 s / 212 | 3.6x |
| webpack | **0.72 s** / 697 MB | 16.62 s / 1572 | 2.73 s / 1303 | 3.97 s / 1046 | 3.8x |
| zod | **0.38 s** / 571 MB | 10.79 s / 1404 | 2.68 s / 1362 | 3.42 s / 1027 | 7.1x |
| xstate | **0.21 s** / 311 MB | 5.53 s / 620 | 0.95 s / 497 | 1.22 s / 360 | 4.5x |
| drizzle | **0.65 s** / 911 MB | 11.89 s / 1602 | 3.53 s / 1897 | 3.72 s / 1079 | 5.4x |
| vue | **0.44 s** / 444 MB | 11.44 s / 941 | 1.85 s / 894 | 2.75 s / 723 | 4.2x |
| effect | **0.72 s** / 881 MB | 19.38 s / 2112 | 3.86 s / 2060 | 5.91 s / 1623 | 5.4x |
| vscode (3.41M lines) | **6.16 s** / 5774 MB | 141.2 s / 9785 | 18.27 s / 8707 | 33.38 s / 7540 | 3.0x |
| excalidraw | **0.50 s** / 529 MB | 12.56 s / 1040 | 2.16 s / 1091 | 2.77 s / 734 | 4.3x |
| tanstack-router | **0.37 s** / 427 MB | 12.26 s / 1256 | 2.40 s / 1079 | 3.21 s / 786 | 6.5x |
| `-b` tsc `src` (14 configs) | **1.12 s** / 531 MB | 15.02 s / 758 | 2.41 s / 856 | 3.47 s / 572 | 2.2x |
tttt is 3.0x (vscode) to 13.9x (type-fest) faster than multi-threaded tsgo (single-threaded tsgo is faster than its threaded
run only on react) and uses less memory on every corpus. vscode phases (K4): load+parse 981 ms (2.16 GB resident after it), resolve
703 ms, typecheck 4.27 s (checkers 3.82-4.27 s, arenas 466-571 MB, 3.19M types); `--threads 1` load 1876 / resolve
1515 ms, so both scale ~2x on the 2 cores; `--checkers 1` typecheck 10.2 s (2.57M types, 1.73 GB arena), so the four
checkers redo ~24% of the type work (plan 36). Work volume against tsc's `--extendedDiagnostics` (tttt K1 vs tsc):
vscode types 2.57M vs 2.89M, assignable 960k vs 1008k, subtype 281k vs 277k, strict subtype 219k vs 258k, identity 38k
vs 42k (tsc check time 121 s); type-fest types 4.80M vs 5.08M (its 2.3 GB is IntRange's real work: tsc 5.3 GB). Stable
ordering's cost in the tsc corpus profile: `sort_by_id` 1.1% inclusive, `union_with_origin` 3.1% inclusive; the
profile is flat (top self cost `type_at_flow_node` 3.2%). Strict-by-default costs the corpora nothing: every corpus
config sets `strict` explicitly (nest: false).

## Corpora drained (2026-10-07, eleventh window's todo entry 8, 4 sub-agents)
`scripts/realworld.py` (4 checkers): every corpus **0** except effect 3 (known TS2578, plan 19), vscode **1** (8 -> 1),
zod-root 53 (= tsc); webpack 64 -> 0, type-fest 6 -> 0, valibot 4 -> 0, excalidraw 1 -> 0. Flagflip (182 cells,
116954 tsc diagnostics) missing 260 -> **42**, spurious 90 -> **12**, other code 10 -> **5**. `$ExpectType` 68620 ->
**68621 / 68640**, sweep **8594 / 8595**, scenarios **240 / 0**, suite 14804 -> **14805 / 14969** (0 components lost).
Mutation seeds drained: single-file 11, 13, 14 (4000 each, ~38 -> ~14 mismatches), multi/js 12 (3000, 31 -> 20).
Instructions (callgrind, `--checkers 1`), 909cf14e -> end: tsc 4.815 -> 4.786 (-0.6%), type-fest 19.455 -> 19.552
(+0.5%), valibot 4.176 -> 4.217 (+1.0%: TS2344 on deferred conditional arguments, a check tsc does), react 1.542 ->
1.540, nest 1.150 -> 1.148, webpack 10.708 -> 10.682, zod 5.040 -> 5.069 (+0.6%), xstate 3.177 -> 3.178 (a +8.4% from
the TS2344 check was fixed by giving non-generic classes/interfaces a recursion identity), drizzle 8.019 -> 8.053
(+0.4%), vue 7.053 -> 7.021, effect 9.984 -> 9.998, excalidraw 7.981 -> 7.956, tanstack-router 5.866 -> 5.878 G
(vscode not re-measured).

## Corpora re-pinned (2026-10-07, eleventh window's todo entry 7, plan 48 stage 5)
**New baselines: corpus numbers before this entry aren't comparable** (new pins and configs; vscode is now 3.41M
lines). `scripts/realworld.py` (4 checkers): tsc, tsc-src, react, nest, zod, xstate, drizzle, vue, tanstack-router,
typescript-eslint 0; excalidraw 1; effect 3; valibot 4; type-fest 6 (3 with `--checkers 1`); vscode 8; webpack 64;
zod-root 53 (= tsc). Flagflip regenerated: 182 cells, tsc 116954 diagnostics, tttt missing **260**, spurious **90**,
other code 10. Sweep 8594 / 8595, `$ExpectType` 68620 / 68640, suite 14804 / 14969: unchanged. Instructions
(callgrind, c30d6bd6; tsc, react, nest identical to the session start's 36cc5a5e): tsc 4.815, type-fest 19.455,
valibot 4.176, react 1.542, nest 1.150, webpack 10.708, zod 5.040, xstate 3.177, drizzle 8.019, vue 7.053, effect
9.984, vscode 92.239, excalidraw 7.981, tanstack-router 5.866 G (instrcount.sh gives valgrind a 64 MB main stack now:
type-fest's IntRange needs ~16 MB).

## Oracles on tsc 4d4f005c (2026-10-07, eleventh window's todo entry 6, plan 48 stage 4)
New caches: `scripts/sweep.py` (DefinitelyTyped 2026-10-02 pin) 8585 on the fresh oracle -> **8594 / 8595** match, 0 crash
(5.8's last: 8589); `scripts/expecttypes.py` 68574 -> **68620 / 68640** (5.8's last: 68619); `scripts/scenarios.py` 200 / 16 / 90
skipped -> **240 / 0 / 66**; `scripts/mutate.py` seed 1 (2000) **4** of 1030 that change tsc's codes, seed 2 (8000) **34** of
4051 (5.8: drained to 6). Flagflip not regenerated (waits for the re-pin, plan 48 stage 5). Suite **14804 / 14969**
unchanged, corpora unchanged (`scripts/realworld.py`: tsc, tsc-src, zod 0; type-fest, react, drizzle, vue, excalidraw,
typescript-eslint 1; nest 4; effect 5; tanstack-router 28; xstate 30; valibot 31; vscode 543; webpack 5297; vscode and
typescript-eslint identical to the session start's build). Instructions (callgrind, fa904e94 -> end) flat: tsc 4.816
-> 4.815, type-fest 3.139, valibot 4.522 -> 4.523, react 1.546 -> 1.545, nest 1.157, webpack 5.136 -> 5.135, zod
5.035, xstate 3.033 -> 3.035, drizzle 8.350 -> 8.353, vue 6.794 -> 6.796, effect 9.629 -> 9.626, vscode 33.168 ->
33.171, excalidraw 5.249 -> 5.248, tanstack-router 4.467 -> 4.468.

## Plan 48 stage 2 session B (2026-10-07, eleventh window's todo entry 5)
Creation-order deletion, then a 4-agent fan-out: suite 14712 -> **14804 / 14969** (errors 14890, symbols 14917,
types 14855; lost only `stableTypeOrdering(stabletypeordering=false)`, accepted). Corpora unchanged (`scripts/realworld.py`:
tsc, zod 0; type-fest, react, drizzle, vue, excalidraw 1; nest 4; effect 5; tanstack-router 28; xstate 30; valibot
31; webpack 5297). checkerdiff as before (vue's `--checkers 1` TS2345, webpack 4 inside quotes). Instructions
(callgrind, e1ff62a5 -> after the deletion -> end): tsc 4.804 -> 4.796 -> 4.816, type-fest 3.143 -> 3.132 -> 3.139,
valibot 4.500 -> 4.496 -> 4.522, react 1.544 -> 1.546, nest 1.155 -> 1.157, webpack 5.140 -> 5.105 -> 5.136, zod
5.004 -> 5.035, xstate 3.021 -> 3.033, drizzle 8.312 -> 8.304 -> 8.350, vue 6.755 -> 6.794, effect 9.624 -> 9.570 ->
9.629, vscode 33.117 -> 33.056 -> 33.168, excalidraw 5.233 -> 5.249, tanstack-router 4.447 -> 4.468 (fan-out creep
+0.2-0.6%, spread over ~75 picks; a +1.6% drizzle from one pick was fixed by its author).

## Plan 48 stage 3 fan-out (2026-10-07, eleventh window's todo entry 4)
Suite 14269 -> **14712 / 14969** (errors 14846, symbols 14905, types 14782; 0 lost). Corpora (`scripts/realworld.py`, 4
checkers): tsc, zod 0; type-fest, react, drizzle, vue, excalidraw 1; nest 4; effect 5; tanstack-router 28; webpack
5297; **xstate 0 -> 30 and valibot 5 -> 31, both now identical to the commit's tsc** (file, line, code; valibot's
extra one is TS5107, which `--ignoreDeprecations 6.0` hides on tsc's side). Instructions (callgrind, 25024b22 ->
end): tsc 4.744 -> 4.804 (+1.3%), type-fest 3.099 -> 3.143 (+1.4%), valibot 4.467 -> 4.500, react 1.535 -> 1.544,
nest 1.151 -> 1.155, webpack 5.133 -> 5.140, **zod 4.809 -> 5.004 (+4.1%)**, xstate 2.965 -> 3.021 (+1.9%, now
reporting 30 elaborated errors), drizzle 8.223 -> 8.312 (+1.1%), vue 6.867 -> 6.755 (-1.6%), effect 9.595 -> 9.624,
vscode 32.922 -> 33.117, excalidraw 5.193 -> 5.233, tanstack-router 4.432 -> 4.447. Most of zod/tsc/type-fest/xstate
is 9e63c0cb (merged generic interfaces instantiate every anonymous member type, as tsc's
isTypeParameterPossiblyReferenced does; triage.md has the possible relation fast path).

## Stable type ordering (2026-10-07, eleventh window's todo entry 3, plan 48 stage 2 session A)
Suite 13683 -> **14269 / 14969** (errors 14649, symbols 14876, types 14467; 0 lost). Corpora (`--all-errors`, 4
checkers): tsc, tsc-src, zod, xstate, typescript-eslint 0; type-fest, react, drizzle, vue 1 (vue 2 with `--checkers
1`: bugs/type_core.md "Variance measurement order"); excalidraw 3, nest 4, valibot 1 -> **5** (4 TS2344 in
keyof.test-d.ts the commit's tsc reports too), effect 5, tanstack-router 28, vscode 533, webpack 5297. threadcheck
identical (tsc, webpack, vue, effect); checkerdiff modulo quotes 0 everywhere but vue's case above. Instructions
(callgrind, 54214e94 -> end): tsc 4.703 -> 4.743 (+0.9%), type-fest 3.095 -> 3.098, valibot 4.385 -> 4.467 (+1.9%),
react 1.502 -> 1.534 (+2.1%), nest 1.144 -> 1.151, webpack 5.135 -> 5.133, zod 4.737 -> 4.796 (+1.2%), xstate 2.943
-> 2.965, drizzle 8.091 -> 8.222 (+1.6%), vue 7.007 -> 6.867 (-2.0%), effect 9.510 -> 9.561, vscode 32.728 -> 32.912,
excalidraw 5.113 -> 5.193 (+1.6%), tanstack-router 4.372 -> 4.430 (+1.3%). The prototype's cost (tsc +11.5%) was the
uncached comparator and an O(n^2) member sort. Checker arena +1% (`Ty.order_key`).

## Retarget to TypeScript 4d4f005c (2026-10-07, eleventh window's todo entry 2, plan 48 stage 1)
**Not comparable with the sections below** (new suite, libs, defaults). Suite **13683 / 14969** (errors 14588, symbols
14858, types 13883). Corpora with their current (5.x-era) pins, tttt only (tsc's numbers under 6.0.3 are in the plan 48 file, git history):
tsc, tsc-src, typescript-eslint, zod, xstate 0; type-fest, valibot, react, drizzle, vue, excalidraw 1 (the TS5101/5107
deprecation); nest 4 (TS6500 x2 and real strict ones); effect 5 (TS2578 x3 + deprecations); tanstack-router 28 (TS2591
x25: `types: []`); webpack 5297 (TS6203 / TS2300 / TS2451: @types/node twice) and vscode 533 (TS2882 x290, TS6203).
Sweep, `$ExpectType`, flagflip, mutation: caches still tsc 5.8's, not run. Instructions (callgrind, 546bbb87 -> end, new
libs and defaults so not a regression measure): tsc 4.662 -> 4.703 (now the commit's own src/compiler), type-fest 3.085
-> 3.095, valibot 4.375 -> 4.385, react 1.487 -> 1.502, nest 1.260 -> 1.144, webpack 3.896 -> 5.135 (the diagnostics
above), zod 4.719 -> 4.737, xstate 2.928 -> 2.943, drizzle 8.287 -> 8.091, vue 6.984 -> 7.007, effect 9.848 -> 9.510,
vscode 32.634 -> 32.728, excalidraw 5.090 -> 5.113, tanstack-router 4.518 -> 4.372. These are the baseline for stage 2's
"within ~1% of the switch" requirement.

## Triage (2026-10-07, tenth window's todo entry 10, four sub-agents)
Suite 13299 -> **13303** (errors 13455, symbols 13490, types 13355; 0 lost), corpora 0 but effect 3, sweep 8588 / 6 /
1 -> **8589 / 5 / 1**, flagflip 51 / 47 / 11 -> **51 / 45 / 10**, `$ExpectType` 68619, scenarios 214 / 0. Mutation
seeds 4 (single-file, 8000) 26 -> 21 and 5 (single-file, 8000) 29 -> 25. Instructions (callgrind, eab13296 -> end):
tsc 4.652 -> 4.662, type-fest 3.089 -> 3.085, valibot 4.254 -> **4.375 (+2.8%)**, react 1.490 -> 1.487, nest 1.259 ->
1.260, webpack 3.892 -> 3.896, zod 4.714 -> 4.719, xstate 2.921 -> 2.928, drizzle 10.834 -> **8.287 (-23.5%)**, vue
7.041 -> 6.984 (-0.8%), effect 9.832 -> 9.848, vscode 32.482 -> 32.634 (+0.5%), excalidraw 5.069 -> 5.090,
tanstack-router 4.532 -> 4.518. Bisected: 0a162dca (a deferred reference whose type parameters all map to non-generic
types isn't TYF_COULD_CONTAIN_TYPE_VARIABLES) is drizzle -24% and valibot +1.9%; 5dcc11f5 (dedupe_unions removed)
valibot +0.5%. Valibot's +1.9% not profiled (likely work the generic flag used to defer).

## Suite-tail fan-out (2026-10-06, tenth window's todo entry 9, five sub-agents)
Suite 13232 -> **13299** (errors 13413 -> 13455, symbols 13469 -> 13489, types 13306 -> 13352; 0 lost), corpora 0 but
effect 7 -> **3**, flagflip 67 / 57 / 11 -> **51 / 47 / 11**, `$ExpectType` 68610 -> **68619**, sweep 8588 / 6 / 1, scenarios
214 / 0, threadcheck effect identical. Mutation seeds 7 (single-file, 5000) 21 -> 12 (0.9% yield) and 5 (multi/js, 3000)
33 -> 27 (2.3%). Instructions (callgrind, 7f149fa9 -> end): tsc 4.619 -> 4.652 (+0.7%), type-fest 3.097 -> 3.089,
valibot 4.215 -> 4.254 (+0.9%), react 1.489 -> 1.490, nest 1.259 -> 1.259, webpack 3.860 -> 3.892 (+0.8%), zod 4.709 ->
4.714, xstate 2.920 -> 2.921, drizzle 10.789 -> 10.834 (+0.4%), vue 6.987 -> 7.041 (+0.8%), effect 9.797 -> 9.832,
vscode 32.353 -> 32.482 (+0.4%), excalidraw 5.016 -> 5.069 (+1.1%), tanstack-router 4.598 -> 4.532 (-1.4%). Creep from
~110 correct ports, not bisected per commit; the two big ones (+15% valibot / +9% drizzle from a homomorphic mapped type
over intersections composing mappers eagerly, +2.4% xstate from intersecting apparent types per object literal) were
caught by mid-session instrcounts and fixed by their authors.

## Perf session (2026-10-06, tenth window's todo entry 8)
Bisecting the window's growth (b66bb745 -> dd69f2c7: xstate +4.3%, vue +4.9%, zod +2.7%) found no single commit above
+0.6%; the two biggest steps were 37702442/ab972a46 (conditional instances named by their root's alias), whose cost
turned out to be tttt instantiating an instance's own alias arguments where tsc takes the root's through the
instance's mapper. Fixed that (871fdb11), moved two per-instantiation hash lookups into the root conditional type
(2f7835df, 4dffac78), then compared relation cache sizes with tsc's `--extendedDiagnostics` (tttt `--time` prints them
now) and ported three tsc shortcuts that the outlier, vue, lacked: same-index members of union pairs (c49283fb), an
intersection's key property in subtype reduction and object-to-primitive decided simply (the commit after). Plan 45
stage 2 by a sub-agent (6e216e42): webpack resolve names 63 -> 37 ms.
Callgrind (`scripts/instrcount.sh`), dd69f2c7 -> end: tsc 4.625 -> 4.619, type-fest 3.142 -> 3.097 (-1.4%), valibot 4.273
-> 4.215 (-1.4%), react 1.545 -> 1.489 (-3.6%), nest 1.261 -> 1.259, webpack 3.866 -> 3.861, zod 4.596 -> **4.709
(+2.5%**: same-index pairs order variance measurements like tsc: zod's variances differing from tsc's 19 -> 9),
xstate 3.003 -> 2.920 (-2.8%), drizzle 13.090 -> **10.800 (-17.5%)**, vue 8.071 -> **6.981 (-13.5%)**, effect 9.850 ->
9.797, vscode 32.485 -> 32.351, excalidraw 5.040 -> 5.016, tanstack-router 4.712 -> 4.598 (-2.4%). Suite 13232, corpora
(effect 7, the rest 0), sweep 8588 / 6 / 1, `$ExpectType` 68610, flagflip 67 / 57 / 11, mutants (3000, seed 1) 13
all flat. Relation cache sizes, tttt `--checkers 1` vs tsc 5.8 (assignable / subtype / strict subtype / identity):
vue 578k / 93k / 15k / 18k vs 541k / 86k / 15k / 28k (was 708k / 213k / 228k), tanstack-router 236k assignable vs
174k, type-fest 57k vs 111k, react 24k vs 55k; the other corpora within ~15%. Walls (best of 3, tttt default threads vs
tsgo 7.0.2 on the same tsconfig): webpack 0.28 vs 0.60 s, react 0.12 vs 0.22, nest 0.11 vs 0.32, zod 0.35 vs 2.47
(tsgo rejects the vue and drizzle corpus configs' `baseUrl`, TS5102, and checks nothing: bench.sh's file lists for
those).

## Two corpora pinned: excalidraw, tanstack-router (2026-10-06, tenth window's todo entry 7, two sub-agents)
excalidraw (JSX app, 149k lines; tsc 5.8 0 in 8 s) 9 -> **0**, tanstack-router (react-router + type tests, 41k lines +
101k .d.ts; tsc 0 in 17.5 s, tttt 0.43 s at one checker) 4 -> **0**; checkerdiff 1 vs 4 checkers identical on both.
Flagflip with their 26 new cells: 182 cells, 53212 tsc diagnostics; the old 156 cells 65 / 63 / 12 -> 67 / 57 / 11
(missing / spurious / other code; the new cells 4 / 5 / 4 at pinning -> 0 / 6 / 0, the 6 effect sloppy ones a side
effect, triage "Distributed conditionals"). Suite 13229 -> **13232** (one types component traded: bce92c5b), sweep
8588 / 6 / 1 flat, `$ExpectType` 68610 flat, other corpora flat (effect 7). Instructions (callgrind, session start ->
end): tsc 4.622 -> 4.625, type-fest 3.139 -> 3.142, valibot 4.275 -> 4.273, react 1.547 -> 1.545, nest 1.261 -> 1.261,
webpack 3.861 -> 3.866, zod 4.587 -> 4.596, xstate 2.976 -> 3.003 (+0.9%), drizzle 13.150 -> 13.090, vue 8.044 -> 8.071,
effect 9.846 -> 9.850, vscode 32.428 -> 32.485; new: excalidraw 5.040, tanstack-router 4.712. A mid-session drizzle
+25% (distributed conditionals named by their root's alias) was caught by the end-of-session instrcount and reverted.

## Flag sweeps and multi-file/JS mutants (2026-10-06, tenth window's todo entry 6; plan 46)
`scripts/sweep.py --flags nuia` 8585 -> **8588** / 8595, `--flags eopt` **8588** (both equal to the default sweep's 8588 /
6 / 1 but react-relay under nuia). `scripts/mutate.py --tests multi,js`: seed 1 (3000) 92 -> **12**, seed 2 (5000) 177 ->
**40**, fresh seed 3 (5000) 46 of 2325 productive. Suite 13228 -> **13229** (+ typeFromPropertyAssignment18's errors),
flagflip 65 / 63 / 12, `$ExpectType` 68610, scenarios 214 / 0, corpora (effect 7, the rest 0) all flat.
Instructions (callgrind, 8641e758 -> end): tsc 4.613 -> 4.622, type-fest 3.135 -> 3.139, valibot 4.270 -> 4.275,
react 1.544 -> 1.547, nest 1.258 -> 1.261, webpack 3.837 -> 3.861 (+0.6%), zod 4.576 -> 4.587, xstate 2.967 -> 2.976,
drizzle 13.140 -> 13.150, vue 8.033 -> 8.044, effect 9.802 -> 9.846 (+0.45%), vscode 32.363 -> 32.428.

## Plan 47 skip-round literals (2026-10-06, tenth window's todo entry 5)
Suite 13227 -> **13228** (mappedTypeInferenceErrors gained, jsxComplexSignatureHasApplicabilityError's types component
lost: bugs/display.md "Aliases"), sweep 8584 / 10 / 1 -> **8588 / 6 / 1**, flagflip **78** (HEAD re-measured) / 63 / 12
-> **65 / 63 / 12**, `$ExpectType` 68610 and corpora (effect 7, the rest 0) unchanged. Instructions (callgrind,
e48bd25d -> this commit): tsc 4.614 -> 4.613, type-fest 3.135, valibot 4.270, react 1.543 -> 1.544, nest 1.258, webpack
3.836 -> 3.837, zod 4.572 -> 4.576, xstate 2.932 -> 2.967 (+1.2%), drizzle 13.062 -> 13.140 (+0.6%), vue 7.890 ->
8.033 (+1.8%: more overloads rejected in the skip round, `call_param_shape` +88M; tsc does that work too), effect
9.785 -> 9.802, vscode 32.348 -> 32.363.

## `$ExpectType` fan-out (2026-10-06, tenth window's todo entry 4; oracle: plan 44 stage 3 item 2)
`scripts/expecttypes.py`: 68640 compared assertions in 2703 DefinitelyTyped packages, tttt ~27 s at -j4. The oracle now
checks every non-lib file before reading types (agent A; ~90 assertions had differed only by tsc's lazy creation
order). 68416 -> **68610 match**, 2637 -> 2683 packages all match. Five sub-agents, 0 tests lost: suite 13104 ->
**13227**, corpora unchanged (effect 7, the rest 0, vscode/typescript-eslint/tsc-src 0), sweep 8580 / 14 / 1 -> **8584
/ 10 / 1**, flagflip 81 / 63 / 12 -> **79 / 63 / 12**. Instructions (callgrind, `scripts/instrcount.sh`), 30f379db -> end:
tsc 4.638 -> 4.614, type-fest 3.114 -> 3.135 (+0.7%), valibot 4.120 -> 4.270 (+3.6%: +2.7% is 1e72625d, tsc's
forConstraint permissive check in distributive constraints, accepted as a cost tsc pays), react 1.596 -> 1.543,
nest 1.254 -> 1.258, webpack 3.842 -> 3.836, zod 4.533 -> 4.572 (+0.9%), xstate 2.897 -> 2.932 (+1.2%), drizzle
13.286 -> 13.062, vue 7.799 -> 7.890 (+1.2%), effect 9.758 -> 9.785, vscode 32.121 -> 32.348 (+0.7%). Two
regressions caught mid-session and fixed by their authors: +30% drizzle (ab972a46), +1.1% effect (a9ca5fdf). The
xstate/vue +1.2% are many small commits in the first half (not bisected; todo entry 8).

## Mutation drain (2026-10-06, tenth window's todo entry 2, plan 46 stage 2, five sub-agents)
`scripts/mutate.py`: seed 2 (8000 mutants) 94 -> 6 mismatches, seeds 3/4/5 67 -> 29, 50 -> 31, 43 -> 40. `scripts/flagflip.py`
103 / 68 / 17 -> **81 / 63 / 12**. Suite 13099 -> **13103** (0 lost), corpora unchanged (effect 7, the rest 0), sweep
8580 / 8595 flat. Instructions (callgrind, `scripts/instrcount.sh`), b05e2950 -> end: tsc 4.621 -> 4.639 (+0.4%), type-fest
3.110 -> 3.115, valibot 3.905 -> **4.097 (+4.9%**: template literal placeholders now checked; nearly all of it is one
`${IssueDotPath<TSchema>}` relation that tsc pays proportionally too, accepted), react 1.620 -> 1.597 (-1.4%, the
inferred true branch cached), nest 1.255 flat, webpack 3.845 -> 3.842, zod 4.489 -> 4.523 (+0.75%), xstate 2.893 ->
2.897, drizzle 13.278 -> 13.353 (+0.6%), vue 7.713 -> 7.797 (+1.1%), effect 9.750 -> 9.753, vscode 32.081 -> 32.119.
vue bisected: +0.55% at c762615d (relation error heads name a non-augmenting class as written: two locals and a
lookup in is_related_to, now gated on `report`, which only won 0.1%: probably GCC inlining/layout of is_related_to);
the other +0.5% vue and zod's +0.75% not bisected: todo entry 8.

## Flag flips (2026-10-06, tenth window's todo entry 1, plan 46 stage 1, four sub-agents)
`scripts/flagflip.py` (12 corpora x 13 flag sets, tsc 5.8 cached): 47455 tsc diagnostics; tttt missing / spurious / other
code 603 / 1460 / 1029 -> **103 / 68 / 17** (per flag: plan 46). Suite 13099 -> 13099 (0 lost), corpora at default
flags unchanged (effect 7, zod-root the same 55, the rest 0), sweep 8579 -> **8580** / 8595, vscode with
noUncheckedIndexedAccess / exactOptionalPropertyTypes / non-strict identical at `--checkers 1` and 4. Instructions
(callgrind, `scripts/instrcount.sh`), b66bb745 -> end: tsc 4.607 -> 4.621, type-fest 3.110 flat, valibot 3.869 -> 3.905
(+0.9%), react 1.618 -> 1.620, nest 1.254 -> 1.255, webpack 3.834 -> 3.845, zod 4.475 -> 4.489, xstate 2.880 ->
2.893, drizzle 13.280 -> 13.278, vue 7.692 -> 7.713, effect 9.719 -> 9.749, vscode 31.926 -> 32.081 (+0.5%): ~+0.3%
for ~25 checks tsc does too, not bisected per fix.

## Triage (2026-10-05, ninth window's todo entry 10, four sub-agents)
Suite 13097 -> **13099** (0 lost). Corpora unchanged (effect 7, the rest 0), scenarios 214 / 0 failing. Instructions
(callgrind, `scripts/instrcount.sh`), 6f877de4 -> end: tsc 4.606 -> 4.607, type-fest 3.106 -> 3.110, valibot 3.861 -> 3.868,
react 1.699 -> **1.618 (-4.8%**: deferred JSX checks change variance measurement order, perf_leads.md; -0.24% from
guarding ty_string_index_only_key), nest 1.254 flat, webpack 3.829 -> 3.834, zod 4.467 -> 4.475, xstate 2.873 ->
2.880, drizzle 13.291 -> 13.280, vue 7.674 -> 7.692 (+0.23%, lazy contextual name literals' second lookup), effect
9.709 -> 9.719, vscode 31.964 -> 31.926. The deep-nesting mapped-type unwrap (520ddc17) is most of the +0.1-0.2% on
valibot/zod/xstate (a correctness fix; tsc does the same work). webpack through a symlinked path (two copies of
types.d.ts): ~60 s -> 1.5 s.

## Types/symbols tail fan-out (2026-10-05, todo entry 9, four sub-agents)
Suite 12933 -> **13097** (0 lost); types component failing 488 -> 345, symbols 178 -> 131, errors 150 -> 147. Corpora
unchanged (effect 7, the rest 0). Sweep 8575 -> 8579 / 8595 match (ember v3 x4, immediate, macrotask fixed; 0 crash,
0 timeout). Instructions (callgrind, `scripts/instrcount.sh`), d33bf103 -> end: tsc 4.615 -> 4.606, type-fest 3.109 ->
3.106, valibot 3.867 -> 3.861, react 1.685 -> 1.699 (+0.83%: a29ce0b2 named union after subtype reduction +0.3%,
84b12f2b generic string-key index access +0.35%, 8619aacb and 5b06210a +0.2% each, possibly layout). Rechecked by reverting each on the next triage: 84b12f2b was real (+0.4%: ty_string_index_only_key called for
every deferred `T[K]`, 238k calls; an inline guard took back 0.24%), a29ce0b2 only 0.06%, nest 1.253 ->
1.254, webpack 3.834 -> 3.829, zod 4.475 -> 4.467, xstate 2.871 -> 2.873, drizzle 13.288 -> 13.291, vue 7.676 ->
7.674, effect 9.741 -> 9.709, vscode 32.042 -> 31.964. A mid-session +1.2% on effect (merged class/interface type
parameters by name, recomputed per type reference) was bisected and fixed by caching the arity per symbol (e32b54dc).

## Sweep and effect toward 0 (2026-10-05, todo entry 8, four sub-agents)
Sweep 8072 / 8208 -> **8575 / 8595** match (versioned dependencies added 387 packages: `test/sweep_links.py`, run by
`sweep.py`), 0 crash, 0 timeout, nothing slower than tsc (carbon-components-react 25 s -> ~2 s, d3-geo 1.8 -> 0.07 s).
Effect 31 -> 7, xstate 2 -> 0, the rest 0. Suite 12932 -> 12933, 0 lost. Every corpus identical at `--checkers` 1, 2,
3, 4, 7 (`scripts/checkerdiff.py`). Instructions (callgrind, `scripts/instrcount.sh`), 223f3d18 -> f2870a70: tsc 4.642 -> 4.616,
type-fest 3.239 -> 3.109, valibot 5.072 -> **3.866 (-23.8%)**, react 1.667 -> 1.685 (+1.1%: de52c3b4 +0.65%, the
intersection relation skipping the source's own property; 2b57049a +0.36%, a conditional's object-vs-primitives
shortcut; both tsc behaviour), nest 1.249 -> 1.254, webpack 3.872 -> 3.834, zod 4.488 -> 4.475, xstate 2.900 ->
2.869, drizzle 13.441 -> 13.256, vue 8.206 -> 7.671, effect 11.056 -> **9.740 (-11.9%)**, vscode 33.305 -> 32.042.

## Inference invokeOnce for conditional targets (2026-10-05, todo entry 8, effect agent)
Callgrind K1 (`scripts/instrcount.sh`), 0979dc97 -> the fix: effect 11.520 -> 9.728 G (-15.6%), type-fest 3.232 -> 3.111
(-3.7%), vue 7.828 -> 7.665 (-2.1%), valibot 3.884 -> 3.870, zod 4.488 -> 4.476, xstate 2.856 -> 2.864 (+0.3%),
the rest within 0.1%. The +4.3% effect regression of 3ec34a9e (inference restarting from a non-object apparent type)
was `any`/unions walked into recursive conditional aliases (Schema's `ElementsType<Tail, [...]>`: 112k infer_from
calls for one restart; tsc's invokeOnce stops it at two levels).

## Parallel name resolution of JavaScript (2026-10-06, plan 45 stage 2)
webpack `--time` resolve names, best of 5, default threads: 63.3 -> 36.1 ms, wall 0.30 -> 0.27-0.28 s. Callgrind K1
within +0.24% everywhere (nest; webpack +0.18%, vscode +0.12%); the numbers are in the plan.

## Parallel name resolution (2026-10-05, plan 45 stage 1, tenth window's todo entry 7)
`--time` resolve names, best of 3, default threads (4 = 2 cores x SMT), load ~3: vscode 593 -> 283 ms (the walk 510
-> ~200; declare 40 and link 28 stay serial), tsc `src/compiler` 77 -> 39, webpack 72 -> 63 (JavaScript walks
serially). Walls, best of 3: vscode 2.47 -> 2.23 s, tsc 0.338 -> 0.307 s, `-b` tsc `src` 1.10 -> 1.08 s (its programs
already ran on --build-jobs threads). Callgrind K1 (`scripts/instrcount.sh`) 1507bfb8 -> end: tsc 4.632 -> 4.642 G
(+0.22%), type-fest 3.236 -> 3.239 (+0.09%), valibot 5.067 -> 5.072 (+0.10%), react 1.664 -> 1.667 (+0.18%), nest
1.241 -> 1.249 (+0.64%), webpack 3.865 -> 3.872 (+0.18%), zod 4.479 -> 4.488 (+0.20%), xstate 2.894 -> 2.900
(+0.21%), drizzle 13.368 -> 13.441 (+0.55%), vue 8.200 -> 8.206 (+0.07%), effect 11.038 -> 11.056 (+0.16%), vscode
33.241 -> 33.305 G (+0.19%): the shard bookkeeping (a shard per file, a TLS check per binder allocation), spread thin.

## Errors-tail fan-out (2026-10-05, tenth window's todo entry 6, f93bac3f)
Callgrind K1 (`scripts/instrcount.sh`), session start dfde5fbc -> end: tsc 4.602 -> 4.632 G (+0.65%), type-fest 3.226 ->
3.236 (+0.31%), valibot 5.043 -> 5.067 (+0.48%), react 1.648 -> 1.664 (+0.97%), nest 1.230 -> 1.241 (+0.89%), webpack
3.820 -> 3.865 (+1.18%), zod 4.447 -> 4.479 (+0.72%), xstate 2.880 -> 2.894 (+0.49%), drizzle 13.343 -> 13.368
(+0.19%), vue 8.107 -> 8.200 (+1.15%, of which +0.45% is 65fe6860: tsc's inference from annotated parameters of nested
functions, kept; perf_leads.md "A big union against an intersection"), effect 10.974 -> 11.038 (+0.58%), vscode
32.926 -> 33.241 G (+0.96%). ~190 commits of new checks and fixes (suite 12768 -> 12932, sweep 8016 -> 8072); six
regressions of +0.1-0.6% were bisected and fixed; what's left is spread thinly (eg tsc's checkTypeParameter on
@types/node's defaults: webpack has no skipLibCheck, +0.16%).

## VS Code corpus pinned (2026-10-05, ninth window's todo entry 5)
VS Code's `src/` (1.39M lines, 4.5k files + node_modules types): the scale benchmark, in bench.sh. Idle machine, best
of 2-3 (`/usr/bin/time`, wall s / peak RSS MB):
| | tttt K4 | tttt K1 | tsgo | tsgo single-threaded | tsc 5.8.2 |
|---|---|---|---|---|---|
| wall | **2.50** | 4.55 | 6.75 | 12.10 | 38.2 |
| RSS | **2148** | 1920 | 3695 | 2896 | 3706 |
tttt phases (K4): load+parse 399 ms (845 MB resident after it: the AST is ~40% of the peak), resolve names 590 ms
(serial then; 283 ms since plan 45 stage 1, see above), typecheck 1410 ms (four checkers at 1.29-1.41 s, arenas 171-215 MB).
tsgo reports ~10 TS2300 of its own (its lib.dom has the WebGPU types @webgpu/types declares). The session's fixes
(vscode 129 -> 0, plus effect 63 -> 31, xstate 8 -> 2, drizzle 4 -> 0, webpack 6 -> 0, sweep 7969 -> 8016 match and
no crash; suite 12759 -> 12768, 0 lost) cost, callgrind K1 session start -> end: tsc -1.98%, type-fest +0.25%,
valibot +1.18%, react +0.67%, nest +0.08%, webpack -0.57%, zod +0.59%, xstate +1.12%, drizzle +0.43%, vue +0.78%,
effect -0.07%, vscode 32.99 G -> 32.93 G (-0.19%). Two agent commits were caught at +3.6% and +39% valibot (+4% zod)
by mid-session instrcount + bisect and fixed by their authors (47f83d6a, 955b5c2b).

## AST and binder memory (2026-10-05, plan 43, ninth window's todo entry 4)
Four Opus sub-agents (declaration structs, expressions/statements, binder, then the checker's tables) plus exact-size
parser lists. Suite 12759 -> 12759, 0 lost; every corpus byte-identical at K=1 and K=4 (threadcheck identical,
checkerdiff as before: effect's 7 known), `scripts/scenarios.py` 215/3 as before, the sweep unchanged (7969 match, 1 crash)
and 456 -> 376 s of tttt time. **Peak RSS** (MB, best of 3, `/usr/bin/time`; K1 = `--checkers 1 --threads 1` vs tsgo
`--singleThreaded`, K4 = tttt's default vs multi-threaded tsgo; tsgo from the 10-05 bench below):
| corpus | tttt K1 start -> end | tsgo 1 thread | tttt K4 start -> end | tsgo |
|---|---|---|---|---|
| tsc | 383 -> 242 | 537 | 419 -> 276 | 610 |
| type-fest | 509 -> 380 | 789 | 521 -> 391 | 793 |
| valibot | 421 -> 328 | 595 | 476 -> 373 | 806 |
| react | 231 -> 149 | 311 | 290 -> 200 | 543 |
| nest | 243 -> **142** | 212 | 260 -> 152 | 246 |
| webpack | 394 -> **253** | 303 | 436 -> 294 | 492 |
| zod | 537 -> 409 | 1004 | 620 -> 479 | 1426 |
| xstate | 316 -> 230 | 361 | 376 -> 285 | 472 |
| drizzle | 830 -> 657 | 1073 | 1242 -> 1026 | 1881 |
| vue | 573 -> 416 | 670 | 643 -> 474 | 866 |
| effect | 1058 -> 736 | - | 1167 -> 831 | - |
Callgrind (`scripts/instrcount.sh`) c6572d83 -> end: tsc 4.736 -> 4.695, type-fest 3.227 -> 3.219, valibot 5.002 -> 4.983,
react 1.685 -> 1.639, nest 1.274 -> 1.231, webpack 3.899 -> 3.846, zod 4.478 -> 4.421, xstate 2.879 -> 2.851, drizzle
13.330 -> 13.288, vue 8.120 -> 8.058, effect 11.064 -> 10.988 G. The exact-size lists alone cost +0.7% on nest (+0.2%
webpack); the smaller nodes paid it back (fewer bytes moved). nest's live malloc heap after name resolution 172 ->
88 MB, all-zero words 66% -> 45% (`scripts/memtrack.sh`). Biggest single wins: `InterfaceMember` 584 -> 176 bytes,
`FunctionImpl` 744 -> 296, `Expr` 144 -> 88, `Scope` 296 -> 136, `PropertyExpr` 400 -> 216, intern entries 24 -> 16
(drizzle -30 MB), a `Ty` allocated by its kind's variant (type-fest -39 MB).

## Breadth sweep (2026-10-05, plan 44 stage 1; `scripts/sweep.py`)
DefinitelyTyped ac3977d8, 8208 packages (8665 minus the generated gapi.client.*), 6968 clean under tsc 5.8. tttt
matches tsc's (file, line, code) set exactly on **7969**; spurious only 123, missing only 105, both 10, **crash 1**
(react-relay, SIGSEGV), timeout 0. 114 s wall at -j4; tttt 456 s summed single-threaded vs tsc 5.8 5314 s (one warm
process, libs parsed once). tttt's median package 41 ms (startup ~6 ms: libs aren't the cost). **Slower than tsc**:
carbon-components-react 25.4 s vs 5.9 s, d3-geo 1.85 s vs 0.62 s (both fixed the same day: 2.1 s and 0.07 s). Before the session's symlink fix ~30% of the sample had thousands of
duplicate-declaration errors (every file of a linked @types package loaded twice).

## Effect corpus pinned (2026-10-05, ninth window's todo entry 2)
Effect 6300 -> 63 (plan 19 "effect"); suite 12758 -> 12759 (+1 component more), 0 lost; the other corpora unchanged
(webpack 6, xstate 8, drizzle 6, rest 0). Callgrind (`scripts/instrcount.sh`, now with effect) c21bcd49 -> end: tsc 4.736 ->
4.736, type-fest 3.226 -> 3.227, valibot 5.003 -> 5.002, react 1.685 -> 1.685, nest 1.273 -> 1.274, webpack 3.898 ->
3.898, zod 4.477 -> 4.478, **xstate 2.772 -> 2.879 (+3.9%)**, drizzle 13.332 -> 13.328, vue 8.120 -> 8.120, effect
11.405 -> 11.064 G. xstate's cost is 7ea58a30 (a literal with a generic function in it is skipped in a candidate's
first check, so the candidate is inferred a second time with everything, as tsc does); bisected, the other commits
are within 0.1%. Effect: tsc 5.8.3 16.8 s / 1.8 GB; tttt ~1 s wall / 1.17 GB at K=4 (63 false errors, so not yet a
like-for-like number).

## Plan 42 (2026-10-05): stages 1-2 (todo entry 9), stages 3-4 (next window's entry 1)
Suite 12728 -> 12731 (stages 1-2) -> 12758 (3-4), 0 lost. Every corpus byte-identical throughout (webpack 6, drizzle
6, xstate 8, zod-root 58 lines, rest 0; tsc-src/typescript-eslint 0). Callgrind (`scripts/instrcount.sh`) f0fba718 -> stage
2 -> b8d36c64 -> stage 4: tsc 4.699 -> 4.727 -> 4.734 -> 4.736, type-fest 3.214 -> 3.225 -> 3.227 -> 3.226, valibot
4.991 -> 5.008 -> 4.995 -> 5.003, react 1.687 -> 1.681 -> 1.684 -> 1.685, nest 1.275 -> 1.269 -> 1.273 -> 1.273,
webpack 3.909 -> 3.890 -> 3.896 -> 3.898, zod 4.471 -> 4.463 -> 4.467 -> 4.477, xstate 2.747 -> 2.768 -> 2.768 ->
2.772, drizzle 13.214 -> 13.321 -> 13.327 -> 13.332, vue 8.027 -> 8.103 -> 8.112 -> 8.120 G. Stage 2's cost is
is_related_to reducing both sides (same call counts, ~3% more instructions in it on vue).

## Perf and memory session (2026-10-05, todo entry 8)
Suite 12728 -> 12728, 0 lost; corpora unchanged (webpack 6, xstate 8, drizzle 6, zod-root 55, rest 0); checkerdiff
and threadcheck identical. Callgrind, session start (02ca5d29) -> end: tsc 4.712 -> 4.699, type-fest 3.213 -> 3.213,
valibot 5.018 -> 4.992, react 1.695 -> 1.687 (base_constraint() memo), nest 1.274 -> 1.275, webpack 3.912 -> 3.909,
zod 4.479 -> 4.471, xstate 2.769 -> 2.747, drizzle 13.335 -> 13.214 (-0.9%: shorter interned keys), vue 8.105 ->
8.027 (arguments checked one at a time in overload resolution). Peak RSS of that session: contiguous runs of files
per checker instead of round robin (drizzle K=4 -25%), `Prop` 80 -> 56 bytes, `Signature` 200 -> 176, `Ty` 152 ->
136; superseded by plan 43's table above.

(Its 5.8-era bench table is superseded by the 2026-10-07 one above.)

## Todo entry 6: plan 40 stage 3, plan 41 stage 3 (2026-10-04)
Suite 12712 -> 12728, 0 lost (TS2742 +14, tsc-src fixes +2). Corpora: **tsc-src 12 -> 0**, new corpus
**typescript-eslint** (`-b` at its root solution, 38 projects) 66 -> **0**; webpack 6, xstate 8, drizzle 6, zod-root
55, rest 0. `scripts/scenarios.py` 199 -> **208** pass / 10 fail / 83 skipped. typescript-eslint: tttt -b 3.03 s / 650-705
MB (machine loaded by other agents; `--build-jobs 1` 3.4-3.8 s / 400 MB); **tsc 5.8 -b 20.7-22.9 s / 1.5-2.3 GB** (from
scratch on a /tmp copy); tsgo 7.0.2 -b fails it (TS 7 differences). tsc-src `-b` 1.34 s. Threads 1/4 byte-identical
(`threadcheck.sh` now runs the `-b` corpora with `-b`). Callgrind c0da8315 -> end: tsc 4.712 -> 4.712, type-fest 3.222
-> 3.213, valibot 5.051 -> 5.018, **react 1.634 -> 1.695 (+3.7%**: distributive constraints of `T & {}` check types,
base_constraint() has no memo, triage), nest 1.265 -> 1.274 (+0.7%, alternative containing modules and specifiers in
the declaration walk), webpack 3.913 -> 3.912, zod 4.466 -> 4.479, xstate 2.788 -> 2.769, drizzle 13.257 -> 13.335
(+0.6%: the same, plus intern_get's per-call cost from a different memory layout at equal call counts), **vue 7.840 ->
8.105 (+3.4%**: 9444083d, a widened object literal is no longer exempt from a weak target's optional properties, as in
tsc; unions that reduced before stay wider; perf_leads).

## Plan 41 stage 2: `-b` perf (2026-10-04, todo entry 4)
Suite 12377 -> 12377, 0 lost. Corpora unchanged (webpack 6, xstate 12, drizzle 6, zod-root 55, tsc-src 12, rest 0);
`scripts/scenarios.py` 199 / 19 / 83 skipped. **`-b thirdparty/tsc/src`** (best of 3, quiet machine): tttt **1.32 s / 785
MB** (was 3.65 s / 755 MB; `--build-jobs 1` 1.74 s / 540 MB, 4 jobs 1.19 s / 1.2 GB); tsgo 7.0.2 -b 2.34 s /
577-876 MB, `--singleThreaded` 3.45 s / 584 MB; tsc 5.8 -b 13.6 s / 710-760 MB (both emit on a /tmp copy: a referenced
project can't be noEmit; `bench.sh` now has this section). `--time` summed over the 13 programs at J=1: resolve names
583, typecheck 441, load+parse 316, free 217, libs 74 ms. Callgrind 9f151959 -> end (single-project path): tsc 4.704
-> 4.705, type-fest 3.217 -> 3.216, valibot 5.043 -> 5.042, react 1.631 -> 1.631, nest 1.266 -> 1.266, webpack 3.891
-> 3.892, zod 4.446 -> 4.449, xstate 2.780 -> 2.782, drizzle 13.308 -> 13.311, vue 7.895 -> 7.894 G (flat).

## Plan 41 stage 1: project references (2026-10-04, todo entry 3)
Suite 12376 -> 12377, 0 lost. Corpora unchanged (webpack 6, xstate 12, drizzle 6, zod-root 55, rest 0). New corpus
**tsc-src** (`-b thirdparty/tsc/src`, 13 projects): 12 diagnostics (plan 41's 11 checker bugs + one 4-checker-only),
3.65 s wall, 755 MB peak, threads 1/4 byte-identical; tsc -b on a /tmp copy was 15.6 s (plan 41's measurement, not
rerun). `--time` summed over the programs: resolve names 1.48 s, load+parse 0.78 s, typecheck 0.55 s. `scripts/scenarios.py`
199 pass / 19 fail / 82 skipped. Callgrind, 06d921ad -> end: tsc 4.705 -> 4.704, type-fest 3.223 -> 3.217, valibot
5.054 -> 5.043, react 1.628 -> 1.631, nest 1.263 -> 1.266, webpack 3.889 -> 3.891, zod 4.448 -> 4.446, xstate 2.779
-> 2.780, drizzle 13.307 -> 13.308, vue 7.891 -> 7.895 G (flat; a first version globbed each config twice, webpack
+1.7%: the include walk is 2% of a JS-heavy project).

## Plan 40 stage 2: declaration accessibility diagnostics (2026-10-04, todo entry 2)
Suite 12366 -> 12376, 0 lost. Corpora unchanged (webpack 6, xstate 12, drizzle 6, zod-root 55, rest 0); drizzle's
declaration diagnostics, which the CLI drops because of its other errors, are none either
(`TTTT_DECLARATION_DIAGNOSTICS=1`); with `--declaration` forced on type-fest, react, zod, xstate and vue tsc and tttt
both report none. `threadcheck.sh` identical on tsc/valibot/nest/drizzle. **tttt now does the declaration work tsc
does on nest, drizzle, tsc's compiler and valibot** (declaration diagnostics with noEmit; drizzle only computes them),
and `bench.sh` no longer turns tsc's declarations off for the compiler corpus: like-for-like from here (tsc's wall on
the compiler corpus, 6.65 -> 6.9 s with them, one run). Callgrind, stage 1's end -> end: tsc 4.693 -> 4.705, type-fest
3.214 -> 3.223, valibot 5.041 -> 5.054, react 1.628 -> 1.628, nest 1.256 -> 1.263 (+0.6%), webpack 3.889 -> 3.889,
zod 4.446 -> 4.448, xstate 2.778 -> 2.779, drizzle 13.174 -> 13.307 (+1.0%), vue 7.891 -> 7.891 G. Nearly all of the
walk's cost is printing a few big inferred types (nest: 20 prints, ~5k instructions per character); before the
per-walk accessibility memo and alias index it was +3.5% on nest.

## Plan 40 stage 1: isolatedDeclarations diagnostics (2026-10-04, todo entry 1)
Suite 12354 -> 12366, 0 lost. Corpora unchanged (webpack 6, xstate 12, drizzle 6, zod-root 55, rest 0); tsc's
`src/compiler` and valibot, which enable isolatedDeclarations, now get the declaration walk and stay at 0.
`threadcheck.sh`/`checkerdiff.py` identical. **Instructions are callgrind counts** (perf_event_paranoid was 4, see
`instrcount.sh`; callgrind runs ~3% above perf's instructions:u, don't compare across tools), 1edd7ccc -> end: tsc
4.692 -> 4.693, type-fest 3.215 -> 3.214, valibot 5.034 -> 5.041 (+0.14%: the walk), react 1.629 -> 1.628, nest 1.256
-> 1.256, webpack 3.889 -> 3.889, zod 4.447 -> 4.446, xstate 2.779 -> 2.778, drizzle 13.161 -> 13.174 (+0.1%, the walk
doesn't run there: code layout), vue 7.894 -> 7.891 G. The accessibility half (stage 2) is what nest and drizzle will
pay for; `scripts/bench.sh` against tsc is still not like-for-like on the declaration corpora until then.

## Triage (2026-09-29, todo entry 10)
Suite 12318 -> 12354, 0 lost. Corpora unchanged (webpack 6, xstate 12, drizzle 6, zod-root 55, rest 0). Instructions
(`scripts/instrcount.sh`) 651a3281 -> end: tsc 4.519 -> 4.532, type-fest 3.178 -> 3.183, valibot 4.904 -> 4.911, react 1.527
-> 1.530, nest 1.148 -> 1.150, webpack 3.714 -> 3.727, zod 4.288 -> 4.299, xstate 2.651 -> 2.673 (+0.8%: tuple `this`
members +0.26%, reverse mapped properties keeping their declarations +0.3%), drizzle 12.936 -> 12.930, vue 7.684 ->
7.677 G.

## Plan 38 stage 3: resolve once (2026-09-29, todo entry 8)
Suite 12316 -> 12318, 0 lost. Corpora: xstate 14 -> 12 (emit.test 49/52), others unchanged (webpack 6, drizzle 6,
zod-root byte-identical, rest 0). Instructions (`scripts/instrcount.sh`) 5062bc88 -> end: tsc 4.621 -> 4.520 (-2.2%),
type-fest 3.180 -> 3.176, valibot 5.419 -> 4.911 (-9.4%), react 1.552 -> 1.528, nest 1.150 -> 1.147, webpack 3.722 ->
3.714, zod 4.441 -> 4.292 (-3.4%), xstate 2.799 -> 2.652 (-5.3%), drizzle 13.461 -> 12.929 (-4.0%), vue 8.007 -> 7.689
G (-4.0%).

## Suite checker bugs 7 (2026-09-29, todo entry 7)
Suite 12058 -> 12316 (errors 12750 -> 13078, symbols 13005 -> 13020, types 12775 -> 12819), 0 lost; ~190 fixes by
four Opus sub-agents. `scripts/errtextdiff.py` bucket (right codes at the right positions, other text) 236 -> 45. Corpora:
xstate 18 -> 14, others unchanged (webpack 6, drizzle 6, zod-root 55, rest 0). Instructions (`scripts/instrcount.sh`)
7004dca6 -> end: tsc 4.599 -> 4.620, type-fest 3.170 -> 3.181, valibot 5.398 -> 5.423, react 1.563 -> 1.553, nest
1.148 -> 1.150, webpack 3.725 -> 3.723, zod 4.428 -> 4.440, xstate 2.780 -> 2.799, drizzle 15.696 -> 13.485 (-14%:
relations skip a class's `prototype` as a target property, tsc's propertiesRelatedTo), vue 7.948 -> 8.009 G (+0.8%,
spread thinly over many commits; +0.2% is intersection discriminants explaining arguments, see triage).

## Plan-sized hacks (2026-09-29, todo entry 5)
Instructions (`scripts/instrcount.sh`) 1984c4f4 -> end: tsc 4.633 -> 4.598, type-fest 3.175 -> 3.168, valibot 5.403 -> 5.414,
react 1.563 -> 1.561, nest 1.148 -> 1.144, webpack 3.765 -> 3.720, zod 4.422 -> 4.427, xstate 2.784 -> 2.778, drizzle
15.755 -> 15.717, vue 7.994 -> 7.948 G. Corpora: xstate 19 -> 18, drizzle 8 -> 6 (skipTrue), webpack 6, others 0.

## Plan 39 stages 1-2 (2026-09-29)
Instructions (`scripts/instrcount.sh`) 45c4b056 -> stage 1 (8504fc44) -> stage 2: tsc 4.607 -> 4.612 -> 4.631, type-fest
3.168 -> 3.170 -> 3.171, valibot 5.389 -> 5.394 -> 5.403, react 1.557 -> 1.558 -> 1.561, nest 1.143 -> 1.144 -> 1.145,
webpack 3.741 -> 3.746 -> 3.758, zod 4.401 -> 4.406 -> 4.421, xstate 2.776 -> 2.778 -> 2.783, drizzle 15.709 -> 15.732
-> 15.727, vue 7.964 -> 7.967 -> 7.993 G (stage 2 +0.0..0.4%: annotated bindings and pattern names push on every
read, `ty_binding_type` has no memo; skipping the stack scan when no binding is on it saved nothing).

## Perf session (2026-09-29, todo entry 2, d9bea66d)
Instructions (`scripts/instrcount.sh`: `perf stat -e instructions:u`, `TTTT_NO_REEXEC=1 --checkers 1 --threads 1 -q
--keep-going`), f0dd10eb (the 09-28 perf session) / session start (454a2333) / end:
| corpus | f0dd10eb | start | end |
|---|---|---|---|
| tsc | 4.822 | 5.226 | **4.606** (-11.9%) |
| type-fest | 3.287 | 3.468 | **3.168** (-8.7%) |
| valibot | 5.280 | 5.722 | **5.387** (-5.9%) |
| react | 1.876 | 2.190 | **1.558** (-28.9%) |
| nest | 1.029 | 1.261 | **1.143** (-9.4%) |
| webpack | 3.342 | 4.043 | **3.742** (-7.4%) |
| zod | 4.330 | 4.669 | **4.402** (-5.7%) |
| xstate | | 2.957 | **2.777** (-6.1%) |
| drizzle | | 16.226 | **15.693** (-3.3%) |
| vue | | 9.637 | **7.964** (-17.4%) |
Where the +8% came from (bisected, `perf_leads.md` has the method): the line-start scan per byte (773648eb, +2.4% tsc,
+3% webpack), getNarrowedType via map_type (99ac647b, +1%), isUnknownLikeUnion (c18a5b4d, +2.8%, already gated by
c4b36dfa), then ~0.1-0.5% steps spread over 600 commits (webpack +12% with the JS features). Fixed by missing caches
rather than by reverting: two-member unions, intersections by members, assignment-reduced types (all tsc caches),
flow's atoms, the override check's quadratic name atomizing, inherited members by hash, instantiated props_by_name
from the source's order, template patterns prefiltered by their first/last text.

(Its bench table is replaced by the 2026-10-05 one above.)

## Corpus session (2026-09-29, seventh window's todo entry 1, end of session)
Two corpora pinned, both 0 under tsc 5.8: drizzle (1567 files incl. deps and libs, 577 of its own, 93k lines) and vue
(759 files, 511 of its own, 192k lines). Diagnostics at pinning -> session end: **drizzle 693 -> 8**, **vue 107 -> 0**
(four Opus sub-agents); the others unchanged (tsc, type-fest, valibot, react, nest, zod 0, webpack 6, xstate 19),
output identical at 1 and 4 threads for both. Suite **12040 -> 12047** (0 components lost). Instructions (`perf stat -e
instructions:u`, `TTTT_NO_REEXEC=1 --checkers 1 --threads 1 -q --keep-going`), session start (cb303b2b) -> end: tsc
5.208 -> 5.225 G (+0.3%), type-fest 3.468 -> 3.470, valibot 5.639 -> 5.720 (+1.4%), **react 2.094 -> 2.190 (+4.6%**:
two deep-nesting ports, hacks.md "Relations", already cut down from +29%), nest 1.259 -> 1.261, webpack 4.032 ->
4.044, zod 4.651 -> 4.669, xstate 2.981 -> 2.958 (-0.8%), vue 9.640 -> 9.630, **drizzle 264.8 -> 16.3 G** (TS2589
runaways fixed: object literal instances). Walls under load (agents running, not bench.sh): drizzle tttt 1.4 s / 1.8
GB (at K=4), tsc 5.8 12.7 s (quiet), tsgo 7.2 s / 1.9 GB; vue tttt 0.7 s / 680 MB, tsc 11.6 s, tsgo 5.2 s / 865 MB (tsgo
through bench.sh's tsconfig.tsgo.json, 16 / 8 TS 7 errors of its own). The quiet `scripts/bench.sh` run is owed.

## Triage (2026-09-29, todo entry 10, 82c3cac9)
Suite **12040 / 13540** (errors 12730, symbols 13003, types 12768; 0 lost), JS 898 / 1049, `scripts/failstats.py` errors
component fails in 810 tests, checker log_error output empty. Corpora unchanged (webpack 6, xstate 19, the rest 0).
Instructions (same setup as below) types session end -> triage end: tsc 5.181 -> 5.212 G (+0.6%), type-fest 3.467,
valibot 5.615, react 2.089, nest 1.259 (+1.0%), webpack 4.025 (+1.0%), zod 4.635 (+0.8%), xstate 2.970 (+0.6%), after
gating the escaped-name cooking and the JS TS2565 check (they had cost +1.9% on tsc).

## Suite (2026-10-04, types/symbols fan-out, todo entry 5)
**12712 / 13540** full pass (errors 13143, symbols 13323, types 12980; session start 12377, 0 components lost). Types
component fails in 560 tests (703 at the start), symbols in 217 (506). `scripts/displaydiff.py --fine --first-tsv`, tests
whose errors pass: types 422 (67 JS; 508 at the start), by first differing line: line-missing 56, shape-other 48,
underlines 38, signature-differs 35, tsc-any 32, we-any 26, union-differs 21, typeof-other-name 19; symbols 174 (56 JS;
402), by first line: name 58, line-missing 32, decls-missing 30, decls-differ 18, other 11, decls-extra 11.
`scripts/failstats.py`: errors component fails in 397. `scripts/jsstats.py`: **931** of 1049 JS tests pass. Corpora unchanged
(webpack 6, xstate 12 -> **8** (fan-a: an outer mapper no longer overwrites a generic signature's own type
parameters), drizzle 6, zod-root 55, tsc-src 12, rest 0); threadcheck identical; `scripts/scenarios.py` 199/19/83.
Callgrind 7aca543c -> end: tsc 4.705 -> 4.712, type-fest 3.216 -> 3.222, valibot 5.042 -> 5.051, react 1.631 ->
1.634, nest 1.266 -> 1.265, webpack 3.892 -> 3.913 (+0.5%), zod 4.449 -> 4.466 (+0.4%), xstate 2.782 -> 2.788, drizzle
13.311 -> 13.257, vue 7.895 -> 7.840 G (a bigint commit cost react +0.85% until literal_type() lost its inlined
intern_slot(): bisected and fixed in the session).

## Corpora (2026-09-29, types session, bc2752f2)
Diagnostics (all are 0 under tsc 5.8): tsc `src/compiler` **0**, valibot **0**, react **0**, nest **0**, type-fest
**0**, zod **0**, webpack **6**, **xstate 19** (plan 19 "xstate"; `scripts/threadcheck.sh` counts 23 there, also at the
session start), all unchanged this session. **zod-root** reports 58. Output identical at 1 and 4 threads
(`scripts/threadcheck.sh`). Instructions (`perf stat -e instructions:u`, `TTTT_NO_REEXEC=1 --checkers 1 --threads 1 -q
--keep-going`), parser session end (e8f2e22c) -> types session end: tsc 5.132 -> 5.181 G (+1.0%), type-fest 3.446 ->
3.455, valibot 5.554 -> 5.608 (+1.0%), react 2.074 -> 2.087, nest 1.234 -> 1.247 (+1.1%), webpack 3.962 -> 3.985,
zod 4.544 -> 4.598 (+1.2%), xstate 2.933 -> 2.951. Not profiled: 150 small commits, no single suspect. Still owed to
the next perf session, with the parser session's tokenizer leftovers and the checker bugs sessions' 2-5% (instructions
have crept up ~3-10% per corpus over the last four sessions).
## Memory and variances (2026-09-28, todo entry 9, f0dd10eb + 0c6b4a80)
`TTTT_NO_REEXEC=1 --checkers 1 --threads 1`, `perf stat -e instructions:u`, arena / types from `--time`:
| corpus | before | after |
|---|---|---|
| tsc | 4.822 G, 61 MB, 94k | 4.822 G, 55 MB |
| type-fest | 3.285 G, 415 MB, 578k | 3.287 G, 396 MB |
| valibot | 5.252 G, 304 MB, 384k | 5.280 G, 249 MB |
| react | 1.893 G, 57 MB | 1.876 G, 42 MB |
| nest | 1.029 G, 15 MB | 1.029 G, 13 MB |
| webpack | 3.342 G, 94 MB | 3.342 G, 77 MB |
| zod | 4.328 G, 432 MB, 361k | 4.330 G, 298 MB, 361k |
| zod, `in`/`out` stripped | 7.812 G, 1004 MB, 940k | 4.654 G, 320 MB, 393k |
tsc for comparison (`--extendedDiagnostics`): type-fest 718k types / 915 MB, valibot 332k / 686 MB, zod 589k.
Peak RSS at the defaults (4 checkers, 4 threads), before (Perf table below) -> after: tsc 409 -> 395 MB, type-fest 494 ->
476, valibot 536 -> 489, react 335 -> 302, nest 251 -> 249, webpack 461 -> 434, zod **846 -> 675** (zod-root 1008 ->
812). Walls not re-measured: the machine ran other jobs at load 10 that day (tttt's walls came out ~1.8x the table's).

## Parallel loading (2026-09-28, plan 36 stage 1, todo entry 6)
**Measured on a loaded machine** (load average 8-10 all session from other users' jobs on the 4 hardware threads):
walls are unreliable, `scripts/bench.sh` wasn't run; redo on a quiet machine. Load phase (`--time` libs + files, ms), best of
7, runs interleaved with the previous binary, 4 threads: tsc 189 -> 100, nest 195 -> 125, webpack 243 -> 153, react
134 -> 78, zod 138 -> 86, xstate 106 -> 58, valibot 102 -> 66, type-fest 29 -> 22. Whole-run walls of the same runs
(noise dominated by the check phase): tsc 887 -> 891, nest 330 -> 282, webpack 372 -> 307, react 419 -> 364, zod 817 ->
795, xstate 484 -> 454, valibot 733 -> 769, type-fest 511 -> 493. Instructions at `--checkers 1 --threads 1`
unchanged (tsc 5.052 -> 5.055 G, valibot 5.503 -> 5.499; the thread-local `compiler_options()` costs nothing
measurable). `TTTT_LOAD_POOL_STATS=1`: every corpus 0 files parsed for nothing; the loader parses 55-800 files itself
(nest the most); workers ~90% busy. Corpora unchanged: webpack 9, xstate 19, the rest 0.

## Parallelism prototypes (2026-09-28, plan 36 planning, 0746971; the prototype patch is in git history)
Parallel loading at 4 workers, whole run best of 5: tsc 661 -> 631 ms, webpack 564 -> 527, nest 201 -> 180, react
243 -> 228, valibot 622 -> 599, zod 1315 -> 1260, type-fest 327 -> 321; output byte-identical. K checkers run one
after another, typecheck ms K=1 -> K=4 sum / slowest checker: tsc 464 -> 505 / 243, webpack 337 -> 400 / 107, zod
1147 -> 1266 / 717, valibot 515 -> 586 / 188, react 119 -> 245 / 73. Per-corpus peak RSS K=1 -> 4: tsc 360 -> 404 MB, webpack 382 -> 464, zod 1334 -> 1566, react 235 -> 335.
