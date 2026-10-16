# Measurements
The latest of each kind of measurement. **Replace a section when re-measuring** (keep its date and commit); don't
append history, git has it (this file before the 2026-09-27 triage holds every entry since plan 05).

## Machine and caveats
AMD EPYC-Turin, 4 vCPUs = 2 cores x 2 SMT threads (KVM), 31 GB; Ubuntu 26.04, GCC 15.2, node 22.22. The oracle (tsc)
is tsgo 7.1.0-dev fed0bf24 (`thirdparty/typescript`, plan 54; every cache since 2026-10-09). `scripts/bench.sh` compares against the pinned tsgo only (since 2026-10-12). Wall times drift by ~15% between days (a shared machine) and anything running alongside skews
them: compare walls only within one session. Callgrind instruction counts (`valgrind --tool=callgrind ./release/tttt
-q --keep-going -p DIR`) are the stable measure across sessions. Neither side is clean on webpack, drizzle, vue, effect and
vscode (exit 2): the same diagnostics on both (realworld.py), so equal work.

## Plan 72, where the serial time is (2026-10-15, todo entry 8)
The phase split per thread count (vscode, webpack, effect), the loader's own non-parse work (vscode ~350 ms at T1,
194k `newfstatat` for 34k paths), declare+link (vscode 150 ms), check work at K = 1 / 4 / 12 (+8-17%) and per-checker
imbalance at K12 (1.3-3.1x the mean): the table is in plans/72_core_scaling.md, which is what a re-measurement replaces.

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
  18 / 6 / 3, source sweep **3352 / 3363** full (sample 817 / 822), dtsdiff 1077 / 1233 projects, 95611 files.
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
"Memory used" (Bun-4 from the other machine, below):

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

## tttt vs Bun 1.4.3's `bun check` vs tsgo 7.1 (2026-10-11, bd67619c + the GCC 16 fix)
**Other machine**: AMD Ryzen AI 9 HX 370, 12 cores / 24 threads, GCC 16.2, load ~2 (an idle desktop). Best of 3 wall
s / peak RSS MB, `/usr/bin/time`. tttt default = 4 checkers / 4 threads (its cap), tttt-12 = `--checkers 12 --threads 12`;
bun default = 24 threads, bun-4 = `--threads=4`; tsgo = the pinned 7.1 at its defaults. tsc-compiler with
`--composite false --incremental false --emitDeclarationOnly false` for all three. Bun 1.4.3 is a port of tsgo 7.0.2
(it claims its full conformance suite), so its diagnostics differ from 7.1's on webpack (591 vs 589), drizzle (18 vs
41), vue (9 vs 14), effect (4 vs 5) and vscode (0 vs 10); tttt matches tsgo 7.1's counts but on drizzle 38 / vue 11 / effect 8.
| corpus | tttt | tttt-12 | bun | bun-4 | tsgo 7.1 |
|---|---|---|---|---|---|
| tsc `src/compiler` | 0.38 / 328 | **0.26** / 449 | 0.36 / 351 | 0.42 / 221 | 0.58 / 497 |
| type-fest | 0.90 / 2393 | **0.48** / 2630 | 1.05 / 835 | 1.70 / 555 | 4.06 / 5436 |
| valibot | 0.24 / 343 | **0.15** / 424 | 0.25 / 276 | 0.43 / 158 | 0.72 / 823 |
| react | 0.11 / 219 | **0.08** / 326 | 0.29 / 377 | 0.30 / 301 | 0.42 / 451 |
| nest | 0.08 / 159 | **0.07** / 194 | 0.10 / 232 | 0.11 / 181 | 0.15 / 258 |
| webpack | 0.53 / 699 | **0.35** / 877 | 0.60 / 521 | 1.10 / 356 | 1.17 / 1172 |
| zod | 0.33 / 583 | **0.25** / 763 | 0.40 / 507 | 0.64 / 325 | 1.05 / 1314 |
| xstate | 0.16 / 313 | **0.13** / 435 | 0.20 / 244 | 0.26 / 173 | 0.42 / 466 |
| drizzle | 0.59 / 918 | **0.37** / 1285 | 0.48 / 597 | 0.91 / 347 | 1.43 / 1631 |
| vue | 0.39 / 438 | **0.21** / 553 | 0.29 / 348 | 0.55 / 224 | 0.88 / 735 |
| effect | 0.74 / 904 | **0.37** / 1059 | 0.59 / 511 | 1.30 / 307 | 1.44 / 1544 |
| vscode | 4.43 / 5952 | **3.08** / 7595 | 3.29 / 2133 | 6.81 / 1557 | 8.21 / 7857 |
| excalidraw | 0.42 / 540 | **0.27** / 753 | 0.41 / 416 | 0.79 / 266 | 1.57 / 1747 |
| tanstack-router | 0.28 / 410 | **0.21** / 525 | 0.51 / 480 | 0.73 / 338 | 1.25 / 987 |
At equal threads (4) tttt is 1.1-2.7x faster than bun but uses more memory on 12 of 14 corpora (up to 4.3x on
type-fest, 3.8x on vscode; less on react and nest). At their defaults bun uses less memory on 10 of 14 (type-fest 2.9x,
vscode 2.8x). Bun's own claim: 3-6.4x faster than tsgo 7.0.2 on a 16-core Mac; here it is 1.4-3.9x
faster than tsgo 7.1 at defaults (2.5x on vscode).

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
