# Measurements
The latest of each kind of measurement. **Replace a section when re-measuring** (keep its date and commit); don't
append history, git has it (this file before the 2026-09-27 triage holds every entry since plan 05).

## Machine and caveats
AMD EPYC-Turin, 4 vCPUs = 2 cores x 2 SMT threads (KVM), 31 GB; Ubuntu 26.04, GCC 15.2, node 22.22, tsc = TypeScript 4d4f005c (6.0.3 + stable ordering, `thirdparty/tsc`, since 2026-10-07; 5.8.2 before), tsgo =
`typescript@7.0.2`. Wall times drift by ~15% between days (a shared machine) and anything running alongside skews
them: compare walls only within one session. Callgrind instruction counts (`valgrind --tool=callgrind ./release/tttt
-q --keep-going -p DIR`) are the stable measure across sessions. tsgo 7.0.2 isn't clean on webpack, drizzle, vue and
effect (TS 7 differences, plan 48 stage 5), so it may do less work there than a clean run.

## Plan 50 (2026-10-08, twelfth window's todo entry 3)
CommonJS export= merged in place at link time: suite 14829 -> 14832 / 14969 (0 lost), corpora unchanged (0 but
effect 3, zod-root 53), sweep 8594 / 8595, `$ExpectType` 68628 / 68640, flagflip 18 / 10 / 4. Instructions
(callgrind): tsc 4.774, type-fest 19.759, valibot 4.237, react 1.543, nest 1.150, webpack 10.615, zod 5.090, xstate
3.184, drizzle 8.055, vue 7.039, effect 10.041, vscode 92.469, excalidraw 7.968, tanstack-router 5.877 G: flat.

## Plan 51 stage 1 (2026-10-08, twelfth window's todo entry 1)
Every oracle identical to 55458150 (`./oracles.py`): suite 14829 / 14969, corpora 0 but effect 3 and zod-root 53,
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
excalidraw 7.956, tanstack-router 5.878 G. `./bench.sh --runs 3` (quiet machine but for one idle agent session), best
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
`./realworld.py` (4 checkers): every corpus **0** except effect 3 (known TS2578, plan 19), vscode **1** (8 -> 1),
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
lines). `./realworld.py` (4 checkers): tsc, tsc-src, react, nest, zod, xstate, drizzle, vue, tanstack-router,
typescript-eslint 0; excalidraw 1; effect 3; valibot 4; type-fest 6 (3 with `--checkers 1`); vscode 8; webpack 64;
zod-root 53 (= tsc). Flagflip regenerated: 182 cells, tsc 116954 diagnostics, tttt missing **260**, spurious **90**,
other code 10. Sweep 8594 / 8595, `$ExpectType` 68620 / 68640, suite 14804 / 14969: unchanged. Instructions
(callgrind, c30d6bd6; tsc, react, nest identical to the session start's 36cc5a5e): tsc 4.815, type-fest 19.455,
valibot 4.176, react 1.542, nest 1.150, webpack 10.708, zod 5.040, xstate 3.177, drizzle 8.019, vue 7.053, effect
9.984, vscode 92.239, excalidraw 7.981, tanstack-router 5.866 G (instrcount.sh gives valgrind a 64 MB main stack now:
type-fest's IntRange needs ~16 MB).

## Oracles on tsc 4d4f005c (2026-10-07, eleventh window's todo entry 6, plan 48 stage 4)
New caches: `./sweep.py` (DefinitelyTyped 2026-10-02 pin) 8585 on the fresh oracle -> **8594 / 8595** match, 0 crash
(5.8's last: 8589); `./expecttypes.py` 68574 -> **68620 / 68640** (5.8's last: 68619); `./scenarios.py` 200 / 16 / 90
skipped -> **240 / 0 / 66**; `./mutate.py` seed 1 (2000) **4** of 1030 that change tsc's codes, seed 2 (8000) **34** of
4051 (5.8: drained to 6). Flagflip not regenerated (waits for the re-pin, plan 48 stage 5). Suite **14804 / 14969**
unchanged, corpora unchanged (`./realworld.py`: tsc, tsc-src, zod 0; type-fest, react, drizzle, vue, excalidraw,
typescript-eslint 1; nest 4; effect 5; tanstack-router 28; xstate 30; valibot 31; vscode 543; webpack 5297; vscode and
typescript-eslint identical to the session start's build). Instructions (callgrind, fa904e94 -> end) flat: tsc 4.816
-> 4.815, type-fest 3.139, valibot 4.522 -> 4.523, react 1.546 -> 1.545, nest 1.157, webpack 5.136 -> 5.135, zod
5.035, xstate 3.033 -> 3.035, drizzle 8.350 -> 8.353, vue 6.794 -> 6.796, effect 9.629 -> 9.626, vscode 33.168 ->
33.171, excalidraw 5.249 -> 5.248, tanstack-router 4.467 -> 4.468.

## Plan 48 stage 2 session B (2026-10-07, eleventh window's todo entry 5)
Creation-order deletion, then a 4-agent fan-out: suite 14712 -> **14804 / 14969** (errors 14890, symbols 14917,
types 14855; lost only `stableTypeOrdering(stabletypeordering=false)`, accepted). Corpora unchanged (`./realworld.py`:
tsc, zod 0; type-fest, react, drizzle, vue, excalidraw 1; nest 4; effect 5; tanstack-router 28; xstate 30; valibot
31; webpack 5297). checkerdiff as before (vue's `--checkers 1` TS2345, webpack 4 inside quotes). Instructions
(callgrind, e1ff62a5 -> after the deletion -> end): tsc 4.804 -> 4.796 -> 4.816, type-fest 3.143 -> 3.132 -> 3.139,
valibot 4.500 -> 4.496 -> 4.522, react 1.544 -> 1.546, nest 1.155 -> 1.157, webpack 5.140 -> 5.105 -> 5.136, zod
5.004 -> 5.035, xstate 3.021 -> 3.033, drizzle 8.312 -> 8.304 -> 8.350, vue 6.755 -> 6.794, effect 9.624 -> 9.570 ->
9.629, vscode 33.117 -> 33.056 -> 33.168, excalidraw 5.233 -> 5.249, tanstack-router 4.447 -> 4.468 (fan-out creep
+0.2-0.6%, spread over ~75 picks; a +1.6% drizzle from one pick was fixed by its author).

## Plan 48 stage 3 fan-out (2026-10-07, eleventh window's todo entry 4)
Suite 14269 -> **14712 / 14969** (errors 14846, symbols 14905, types 14782; 0 lost). Corpora (`./realworld.py`, 4
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
14858, types 13883). Corpora with their current (5.x-era) pins, tttt only (tsc's numbers under 6.0.3 are in plan 48):
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
Callgrind (`./instrcount.sh`), dd69f2c7 -> end: tsc 4.625 -> 4.619, type-fest 3.142 -> 3.097 (-1.4%), valibot 4.273
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
`./sweep.py --flags nuia` 8585 -> **8588** / 8595, `--flags eopt` **8588** (both equal to the default sweep's 8588 /
6 / 1 but react-relay under nuia). `./mutate.py --tests multi,js`: seed 1 (3000) 92 -> **12**, seed 2 (5000) 177 ->
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
`./expecttypes.py`: 68640 compared assertions in 2703 DefinitelyTyped packages, tttt ~27 s at -j4. The oracle now
checks every non-lib file before reading types (agent A; ~90 assertions had differed only by tsc's lazy creation
order). 68416 -> **68610 match**, 2637 -> 2683 packages all match. Five sub-agents, 0 tests lost: suite 13104 ->
**13227**, corpora unchanged (effect 7, the rest 0, vscode/typescript-eslint/tsc-src 0), sweep 8580 / 14 / 1 -> **8584
/ 10 / 1**, flagflip 81 / 63 / 12 -> **79 / 63 / 12**. Instructions (callgrind, `./instrcount.sh`), 30f379db -> end:
tsc 4.638 -> 4.614, type-fest 3.114 -> 3.135 (+0.7%), valibot 4.120 -> 4.270 (+3.6%: +2.7% is 1e72625d, tsc's
forConstraint permissive check in distributive constraints, accepted as a cost tsc pays), react 1.596 -> 1.543,
nest 1.254 -> 1.258, webpack 3.842 -> 3.836, zod 4.533 -> 4.572 (+0.9%), xstate 2.897 -> 2.932 (+1.2%), drizzle
13.286 -> 13.062, vue 7.799 -> 7.890 (+1.2%), effect 9.758 -> 9.785, vscode 32.121 -> 32.348 (+0.7%). Two
regressions caught mid-session and fixed by their authors: +30% drizzle (ab972a46), +1.1% effect (a9ca5fdf). The
xstate/vue +1.2% are many small commits in the first half (not bisected; todo entry 8).

## Mutation drain (2026-10-06, tenth window's todo entry 2, plan 46 stage 2, five sub-agents)
`./mutate.py`: seed 2 (8000 mutants) 94 -> 6 mismatches, seeds 3/4/5 67 -> 29, 50 -> 31, 43 -> 40. `./flagflip.py`
103 / 68 / 17 -> **81 / 63 / 12**. Suite 13099 -> **13103** (0 lost), corpora unchanged (effect 7, the rest 0), sweep
8580 / 8595 flat. Instructions (callgrind, `./instrcount.sh`), b05e2950 -> end: tsc 4.621 -> 4.639 (+0.4%), type-fest
3.110 -> 3.115, valibot 3.905 -> **4.097 (+4.9%**: template literal placeholders now checked; nearly all of it is one
`${IssueDotPath<TSchema>}` relation that tsc pays proportionally too, accepted), react 1.620 -> 1.597 (-1.4%, the
inferred true branch cached), nest 1.255 flat, webpack 3.845 -> 3.842, zod 4.489 -> 4.523 (+0.75%), xstate 2.893 ->
2.897, drizzle 13.278 -> 13.353 (+0.6%), vue 7.713 -> 7.797 (+1.1%), effect 9.750 -> 9.753, vscode 32.081 -> 32.119.
vue bisected: +0.55% at c762615d (relation error heads name a non-augmenting class as written: two locals and a
lookup in is_related_to, now gated on `report`, which only won 0.1%: probably GCC inlining/layout of is_related_to);
the other +0.5% vue and zod's +0.75% not bisected: todo entry 8.

## Flag flips (2026-10-06, tenth window's todo entry 1, plan 46 stage 1, four sub-agents)
`./flagflip.py` (12 corpora x 13 flag sets, tsc 5.8 cached): 47455 tsc diagnostics; tttt missing / spurious / other
code 603 / 1460 / 1029 -> **103 / 68 / 17** (per flag: plan 46). Suite 13099 -> 13099 (0 lost), corpora at default
flags unchanged (effect 7, zod-root the same 55, the rest 0), sweep 8579 -> **8580** / 8595, vscode with
noUncheckedIndexedAccess / exactOptionalPropertyTypes / non-strict identical at `--checkers 1` and 4. Instructions
(callgrind, `./instrcount.sh`), b66bb745 -> end: tsc 4.607 -> 4.621, type-fest 3.110 flat, valibot 3.869 -> 3.905
(+0.9%), react 1.618 -> 1.620, nest 1.254 -> 1.255, webpack 3.834 -> 3.845, zod 4.475 -> 4.489, xstate 2.880 ->
2.893, drizzle 13.280 -> 13.278, vue 7.692 -> 7.713, effect 9.719 -> 9.749, vscode 31.926 -> 32.081 (+0.5%): ~+0.3%
for ~25 checks tsc does too, not bisected per fix.

## Triage (2026-10-05, ninth window's todo entry 10, four sub-agents)
Suite 13097 -> **13099** (0 lost). Corpora unchanged (effect 7, the rest 0), scenarios 214 / 0 failing. Instructions
(callgrind, `./instrcount.sh`), 6f877de4 -> end: tsc 4.606 -> 4.607, type-fest 3.106 -> 3.110, valibot 3.861 -> 3.868,
react 1.699 -> **1.618 (-4.8%**: deferred JSX checks change variance measurement order, perf_leads.md; -0.24% from
guarding ty_string_index_only_key), nest 1.254 flat, webpack 3.829 -> 3.834, zod 4.467 -> 4.475, xstate 2.873 ->
2.880, drizzle 13.291 -> 13.280, vue 7.674 -> 7.692 (+0.23%, lazy contextual name literals' second lookup), effect
9.709 -> 9.719, vscode 31.964 -> 31.926. The deep-nesting mapped-type unwrap (520ddc17) is most of the +0.1-0.2% on
valibot/zod/xstate (a correctness fix; tsc does the same work). webpack through a symlinked path (two copies of
types.d.ts): ~60 s -> 1.5 s.

## Types/symbols tail fan-out (2026-10-05, todo entry 9, four sub-agents)
Suite 12933 -> **13097** (0 lost); types component failing 488 -> 345, symbols 178 -> 131, errors 150 -> 147. Corpora
unchanged (effect 7, the rest 0). Sweep 8575 -> 8579 / 8595 match (ember v3 x4, immediate, macrotask fixed; 0 crash,
0 timeout). Instructions (callgrind, `./instrcount.sh`), d33bf103 -> end: tsc 4.615 -> 4.606, type-fest 3.109 ->
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
3, 4, 7 (`./checkerdiff.py`). Instructions (callgrind, `./instrcount.sh`), 223f3d18 -> f2870a70: tsc 4.642 -> 4.616,
type-fest 3.239 -> 3.109, valibot 5.072 -> **3.866 (-23.8%)**, react 1.667 -> 1.685 (+1.1%: de52c3b4 +0.65%, the
intersection relation skipping the source's own property; 2b57049a +0.36%, a conditional's object-vs-primitives
shortcut; both tsc behaviour), nest 1.249 -> 1.254, webpack 3.872 -> 3.834, zod 4.488 -> 4.475, xstate 2.900 ->
2.869, drizzle 13.441 -> 13.256, vue 8.206 -> 7.671, effect 11.056 -> **9.740 (-11.9%)**, vscode 33.305 -> 32.042.

## Inference invokeOnce for conditional targets (2026-10-05, todo entry 8, effect agent)
Callgrind K1 (`./instrcount.sh`), 0979dc97 -> the fix: effect 11.520 -> 9.728 G (-15.6%), type-fest 3.232 -> 3.111
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
already ran on --build-jobs threads). Callgrind K1 (`./instrcount.sh`) 1507bfb8 -> end: tsc 4.632 -> 4.642 G
(+0.22%), type-fest 3.236 -> 3.239 (+0.09%), valibot 5.067 -> 5.072 (+0.10%), react 1.664 -> 1.667 (+0.18%), nest
1.241 -> 1.249 (+0.64%), webpack 3.865 -> 3.872 (+0.18%), zod 4.479 -> 4.488 (+0.20%), xstate 2.894 -> 2.900
(+0.21%), drizzle 13.368 -> 13.441 (+0.55%), vue 8.200 -> 8.206 (+0.07%), effect 11.038 -> 11.056 (+0.16%), vscode
33.241 -> 33.305 G (+0.19%): the shard bookkeeping (a shard per file, a TLS check per binder allocation), spread thin.

## Errors-tail fan-out (2026-10-05, tenth window's todo entry 6, f93bac3f)
Callgrind K1 (`./instrcount.sh`), session start dfde5fbc -> end: tsc 4.602 -> 4.632 G (+0.65%), type-fest 3.226 ->
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
checkerdiff as before: effect's 7 known), `./scenarios.py` 215/3 as before, the sweep unchanged (7969 match, 1 crash)
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
Callgrind (`./instrcount.sh`) c6572d83 -> end: tsc 4.736 -> 4.695, type-fest 3.227 -> 3.219, valibot 5.002 -> 4.983,
react 1.685 -> 1.639, nest 1.274 -> 1.231, webpack 3.899 -> 3.846, zod 4.478 -> 4.421, xstate 2.879 -> 2.851, drizzle
13.330 -> 13.288, vue 8.120 -> 8.058, effect 11.064 -> 10.988 G. The exact-size lists alone cost +0.7% on nest (+0.2%
webpack); the smaller nodes paid it back (fewer bytes moved). nest's live malloc heap after name resolution 172 ->
88 MB, all-zero words 66% -> 45% (`./memtrack.sh`). Biggest single wins: `InterfaceMember` 584 -> 176 bytes,
`FunctionImpl` 744 -> 296, `Expr` 144 -> 88, `Scope` 296 -> 136, `PropertyExpr` 400 -> 216, intern entries 24 -> 16
(drizzle -30 MB), a `Ty` allocated by its kind's variant (type-fest -39 MB).

## Breadth sweep (2026-10-05, plan 44 stage 1; `./sweep.py`)
DefinitelyTyped ac3977d8, 8208 packages (8665 minus the generated gapi.client.*), 6968 clean under tsc 5.8. tttt
matches tsc's (file, line, code) set exactly on **7969**; spurious only 123, missing only 105, both 10, **crash 1**
(react-relay, SIGSEGV), timeout 0. 114 s wall at -j4; tttt 456 s summed single-threaded vs tsc 5.8 5314 s (one warm
process, libs parsed once). tttt's median package 41 ms (startup ~6 ms: libs aren't the cost). **Slower than tsc**:
carbon-components-react 25.4 s vs 5.9 s, d3-geo 1.85 s vs 0.62 s (both fixed the same day: 2.1 s and 0.07 s). Before the session's symlink fix ~30% of the sample had thousands of
duplicate-declaration errors (every file of a linked @types package loaded twice). Top causes in plan 44.

## Effect corpus pinned (2026-10-05, ninth window's todo entry 2)
Effect 6300 -> 63 (plan 19 "effect"); suite 12758 -> 12759 (+1 component more), 0 lost; the other corpora unchanged
(webpack 6, xstate 8, drizzle 6, rest 0). Callgrind (`./instrcount.sh`, now with effect) c21bcd49 -> end: tsc 4.736 ->
4.736, type-fest 3.226 -> 3.227, valibot 5.003 -> 5.002, react 1.685 -> 1.685, nest 1.273 -> 1.274, webpack 3.898 ->
3.898, zod 4.477 -> 4.478, **xstate 2.772 -> 2.879 (+3.9%)**, drizzle 13.332 -> 13.328, vue 8.120 -> 8.120, effect
11.405 -> 11.064 G. xstate's cost is 7ea58a30 (a literal with a generic function in it is skipped in a candidate's
first check, so the candidate is inferred a second time with everything, as tsc does); bisected, the other commits
are within 0.1%. Effect: tsc 5.8.3 16.8 s / 1.8 GB; tttt ~1 s wall / 1.17 GB at K=4 (63 false errors, so not yet a
like-for-like number).

## Plan 42 (2026-10-05): stages 1-2 (todo entry 9), stages 3-4 (next window's entry 1)
Suite 12728 -> 12731 (stages 1-2) -> 12758 (3-4), 0 lost. Every corpus byte-identical throughout (webpack 6, drizzle
6, xstate 8, zod-root 58 lines, rest 0; tsc-src/typescript-eslint 0). Callgrind (`./instrcount.sh`) f0fba718 -> stage
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
55, rest 0. `./scenarios.py` 199 -> **208** pass / 10 fail / 83 skipped. typescript-eslint: tttt -b 3.03 s / 650-705
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
`./scenarios.py` 199 / 19 / 83 skipped. **`-b thirdparty/tsc/src`** (best of 3, quiet machine): tttt **1.32 s / 785
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
rerun). `--time` summed over the programs: resolve names 1.48 s, load+parse 0.78 s, typecheck 0.55 s. `./scenarios.py`
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
pay for; `./bench.sh` against tsc is still not like-for-like on the declaration corpora until then.

## Triage (2026-09-29, todo entry 10)
Suite 12318 -> 12354, 0 lost. Corpora unchanged (webpack 6, xstate 12, drizzle 6, zod-root 55, rest 0). Instructions
(`./instrcount.sh`) 651a3281 -> end: tsc 4.519 -> 4.532, type-fest 3.178 -> 3.183, valibot 4.904 -> 4.911, react 1.527
-> 1.530, nest 1.148 -> 1.150, webpack 3.714 -> 3.727, zod 4.288 -> 4.299, xstate 2.651 -> 2.673 (+0.8%: tuple `this`
members +0.26%, reverse mapped properties keeping their declarations +0.3%), drizzle 12.936 -> 12.930, vue 7.684 ->
7.677 G.

## Plan 38 stage 3: resolve once (2026-09-29, todo entry 8)
Suite 12316 -> 12318, 0 lost. Corpora: xstate 14 -> 12 (emit.test 49/52), others unchanged (webpack 6, drizzle 6,
zod-root byte-identical, rest 0). Instructions (`./instrcount.sh`) 5062bc88 -> end: tsc 4.621 -> 4.520 (-2.2%),
type-fest 3.180 -> 3.176, valibot 5.419 -> 4.911 (-9.4%), react 1.552 -> 1.528, nest 1.150 -> 1.147, webpack 3.722 ->
3.714, zod 4.441 -> 4.292 (-3.4%), xstate 2.799 -> 2.652 (-5.3%), drizzle 13.461 -> 12.929 (-4.0%), vue 8.007 -> 7.689
G (-4.0%).

## Suite checker bugs 7 (2026-09-29, todo entry 7)
Suite 12058 -> 12316 (errors 12750 -> 13078, symbols 13005 -> 13020, types 12775 -> 12819), 0 lost; ~190 fixes by
four Opus sub-agents. `./errtextdiff.py` bucket (right codes at the right positions, other text) 236 -> 45. Corpora:
xstate 18 -> 14, others unchanged (webpack 6, drizzle 6, zod-root 55, rest 0). Instructions (`./instrcount.sh`)
7004dca6 -> end: tsc 4.599 -> 4.620, type-fest 3.170 -> 3.181, valibot 5.398 -> 5.423, react 1.563 -> 1.553, nest
1.148 -> 1.150, webpack 3.725 -> 3.723, zod 4.428 -> 4.440, xstate 2.780 -> 2.799, drizzle 15.696 -> 13.485 (-14%:
relations skip a class's `prototype` as a target property, tsc's propertiesRelatedTo), vue 7.948 -> 8.009 G (+0.8%,
spread thinly over many commits; +0.2% is intersection discriminants explaining arguments, see triage).

## Plan-sized hacks (2026-09-29, todo entry 5)
Instructions (`./instrcount.sh`) 1984c4f4 -> end: tsc 4.633 -> 4.598, type-fest 3.175 -> 3.168, valibot 5.403 -> 5.414,
react 1.563 -> 1.561, nest 1.148 -> 1.144, webpack 3.765 -> 3.720, zod 4.422 -> 4.427, xstate 2.784 -> 2.778, drizzle
15.755 -> 15.717, vue 7.994 -> 7.948 G. Corpora: xstate 19 -> 18, drizzle 8 -> 6 (skipTrue), webpack 6, others 0.

## Plan 39 stages 1-2 (2026-09-29)
Instructions (`./instrcount.sh`) 45c4b056 -> stage 1 (8504fc44) -> stage 2: tsc 4.607 -> 4.612 -> 4.631, type-fest
3.168 -> 3.170 -> 3.171, valibot 5.389 -> 5.394 -> 5.403, react 1.557 -> 1.558 -> 1.561, nest 1.143 -> 1.144 -> 1.145,
webpack 3.741 -> 3.746 -> 3.758, zod 4.401 -> 4.406 -> 4.421, xstate 2.776 -> 2.778 -> 2.783, drizzle 15.709 -> 15.732
-> 15.727, vue 7.964 -> 7.967 -> 7.993 G (stage 2 +0.0..0.4%: annotated bindings and pattern names push on every
read, `ty_binding_type` has no memo; skipping the stack scan when no binding is on it saved nothing).

## Perf session (2026-09-29, todo entry 2, d9bea66d)
Instructions (`./instrcount.sh`: `perf stat -e instructions:u`, `TTTT_NO_REEXEC=1 --checkers 1 --threads 1 -q
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
through bench.sh's tsconfig.tsgo.json, 16 / 8 TS 7 errors of its own). The quiet `./bench.sh` run is owed.

## Triage (2026-09-29, todo entry 10, 82c3cac9)
Suite **12040 / 13540** (errors 12730, symbols 13003, types 12768; 0 lost), JS 898 / 1049, `./failstats.py` errors
component fails in 810 tests, checker log_error output empty. Corpora unchanged (webpack 6, xstate 19, the rest 0).
Instructions (same setup as below) types session end -> triage end: tsc 5.181 -> 5.212 G (+0.6%), type-fest 3.467,
valibot 5.615, react 2.089, nest 1.259 (+1.0%), webpack 4.025 (+1.0%), zod 4.635 (+0.8%), xstate 2.970 (+0.6%), after
gating the escaped-name cooking and the JS TS2565 check (they had cost +1.9% on tsc).

## Suite (2026-10-04, types/symbols fan-out, todo entry 5)
**12712 / 13540** full pass (errors 13143, symbols 13323, types 12980; session start 12377, 0 components lost). Types
component fails in 560 tests (703 at the start), symbols in 217 (506). `./displaydiff.py --fine --first-tsv`, tests
whose errors pass: types 422 (67 JS; 508 at the start), by first differing line: line-missing 56, shape-other 48,
underlines 38, signature-differs 35, tsc-any 32, we-any 26, union-differs 21, typeof-other-name 19; symbols 174 (56 JS;
402), by first line: name 58, line-missing 32, decls-missing 30, decls-differ 18, other 11, decls-extra 11.
`./failstats.py`: errors component fails in 397. `./jsstats.py`: **931** of 1049 JS tests pass. Corpora unchanged
(webpack 6, xstate 12 -> **8** (fan-a: an outer mapper no longer overwrites a generic signature's own type
parameters), drizzle 6, zod-root 55, tsc-src 12, rest 0); threadcheck identical; `./scenarios.py` 199/19/83.
Callgrind 7aca543c -> end: tsc 4.705 -> 4.712, type-fest 3.216 -> 3.222, valibot 5.042 -> 5.051, react 1.631 ->
1.634, nest 1.266 -> 1.265, webpack 3.892 -> 3.913 (+0.5%), zod 4.449 -> 4.466 (+0.4%), xstate 2.782 -> 2.788, drizzle
13.311 -> 13.257, vue 7.895 -> 7.840 G (a bigint commit cost react +0.85% until literal_type() lost its inlined
intern_slot(): bisected and fixed in the session).

## Corpora (2026-09-29, types session, bc2752f2)
Diagnostics (all are 0 under tsc 5.8): tsc `src/compiler` **0**, valibot **0**, react **0**, nest **0**, type-fest
**0**, zod **0**, webpack **6**, **xstate 19** (plan 19 "xstate"; `./threadcheck.sh` counts 23 there, also at the
session start), all unchanged this session. **zod-root** reports 58. Output identical at 1 and 4 threads
(`./threadcheck.sh`). Instructions (`perf stat -e instructions:u`, `TTTT_NO_REEXEC=1 --checkers 1 --threads 1 -q
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
walls are unreliable, `./bench.sh` wasn't run; redo on a quiet machine. Load phase (`--time` libs + files, ms), best of
7, runs interleaved with the previous binary, 4 threads: tsc 189 -> 100, nest 195 -> 125, webpack 243 -> 153, react
134 -> 78, zod 138 -> 86, xstate 106 -> 58, valibot 102 -> 66, type-fest 29 -> 22. Whole-run walls of the same runs
(noise dominated by the check phase): tsc 887 -> 891, nest 330 -> 282, webpack 372 -> 307, react 419 -> 364, zod 817 ->
795, xstate 484 -> 454, valibot 733 -> 769, type-fest 511 -> 493. Instructions at `--checkers 1 --threads 1`
unchanged (tsc 5.052 -> 5.055 G, valibot 5.503 -> 5.499; the thread-local `compiler_options()` costs nothing
measurable). `TTTT_LOAD_POOL_STATS=1`: every corpus 0 files parsed for nothing; the loader parses 55-800 files itself
(nest the most); workers ~90% busy. Corpora unchanged: webpack 9, xstate 19, the rest 0.

## Parallelism prototypes (2026-09-28, plan 36 planning, 0746971 + `plans/36_prototype.patch`)
Parallel loading at 4 workers, whole run best of 5: tsc 661 -> 631 ms, webpack 564 -> 527, nest 201 -> 180, react
243 -> 228, valibot 622 -> 599, zod 1315 -> 1260, type-fest 327 -> 321; output byte-identical. K checkers run one
after another, typecheck ms K=1 -> K=4 sum / slowest checker: tsc 464 -> 505 / 243, webpack 337 -> 400 / 107, zod
1147 -> 1266 / 717, valibot 515 -> 586 / 188, react 119 -> 245 / 73. Details and the load-phase split in plan 36.
