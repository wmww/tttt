# Measurements
The latest of each kind of measurement. **Replace a section when re-measuring** (keep its date and commit); don't
append history, git has it (this file before the 2026-09-27 triage holds every entry since plan 05).

## Machine and caveats
AMD EPYC-Turin, 4 vCPUs = 2 cores x 2 SMT threads (KVM), 31 GB; Ubuntu 26.04, GCC 15.2, node 22.22, tsc 5.8.2 (`thirdparty/tsc`), tsgo =
`typescript@7.0.2`. Wall times drift by ~15% between days (a shared machine) and anything running alongside skews
them: compare walls only within one session. Callgrind instruction counts (`valgrind --tool=callgrind ./release/tttt
-q --keep-going -p DIR`) are the stable measure across sessions. tsgo exits 1 on valibot (TS5108,
`moduleResolution=node10` removed) and on webpack (3 TS1003 on JSDoc it parses differently), so it may do less work
there than a clean run.

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
(serial: a quarter of the wall, plan 45's lever), typecheck 1410 ms (four checkers at 1.29-1.41 s, arenas 171-215 MB).
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
carbon-components-react 25.4 s vs 5.9 s (all in `intersection` under `ty_keyof` from JSX props instantiation: bugs/
sweep_perf.md), d3-geo 1.85 s vs 0.62 s. Before the session's symlink fix ~30% of the sample had thousands of
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

`./bench.sh --runs 3` at session start (02ca5d29 + the base_constraint memo, round robin; quiet machine), best wall /
peak RSS, tttt at its defaults:
| corpus | tttt | tsc 5.8 | tsgo | tsgo 1 thread |
|---|---|---|---|---|
| synthetic (1077 x 200, 3.11 M lines) | **4.45 s** / 5297 MB | 43.9 s (aborted) / 4294 | 11.14 s / 6690 | 13.91 s / 4286 |
| tsc `src/compiler` | **0.43 s** / 435 MB | 6.73 s / 595 | 1.33 s / 610 | 1.86 s / 537 |
| type-fest | **0.24 s** / 539 MB | 4.72 s / 1022 | 1.76 s / 793 | 2.14 s / 789 |
| valibot | **0.33 s** / 513 MB | 7.93 s / 806 | 1.48 s / 806 | 2.15 s / 595 |
| react | **0.17 s** / 320 MB | 3.30 s / 498 | 0.91 s / 543 | 0.81 s / 311 |
| nest | **0.15 s** / 273 MB | 2.25 s / 400 | 0.38 s / 246 | 0.45 s / 212 |
| webpack | **0.35 s** / 470 MB | 7.64 s / 733 | 0.58 s / 492 | 0.53 s / 303 |
| zod | **0.38 s** / 701 MB | 9.61 s / 1302 | 2.44 s / 1426 | 3.34 s / 1004 |
| xstate | **0.23 s** / 394 MB | 5.25 s / 602 | 0.88 s / 472 | 1.15 s / 361 |
| drizzle | **1.09 s** / 1686 MB | 11.25 s / 1456 | 3.26 s / 1881 | 3.56 s / 1073 |
| vue | **0.56 s** / 680 MB | 10.11 s / 918 | 1.77 s / 866 | 2.58 s / 670 |
| `-b` tsc `src` (14 configs) | **1.33 s** / 822 MB | 13.55 s / 744 | 2.32 s / 842 | 3.44 s / 578 |
Like-for-like now: tsc does declaration diagnostics on nest, drizzle, valibot and the compiler corpus, and so does
tttt (plan 40). tttt is 1.5x (webpack) to 7.3x (type-fest) faster than multi-threaded tsgo, 2.6-10x than its
single thread (`-b` 1.7x / 2.6x). tsgo exits 1 on valibot, webpack, xstate, drizzle, vue and `-b` (TS 7 differences).

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
