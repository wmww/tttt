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

`./bench.sh --runs 3` on a quiet machine (load < 1), best wall / peak RSS, tttt at its defaults (4 checkers, 4
threads). The synthetic corpus grew (1065 tests x 200, 3.08 M lines; was 987 / 2.70 M): one file of it (t256, a
512-way return union of arrays: O(n^2) subtype reduction, 12 ms per copy where tsgo takes 22, tsc 30) is a quarter of
the work, so one checker takes 3.2 s and the others 0.85. tsc 5.8 aborted (exit 134) on it.
| corpus | tttt | tsc 5.8 | tsgo | tsgo 1 thread |
|---|---|---|---|---|
| synthetic | **4.77 s** / 5014 MB | 47.9 s (aborted) / 4286 MB | 11.32 s / 6716 MB | 14.09 s / 4207 MB |
| tsc `src/compiler` | **0.46 s** / 421 MB | 7.45 s / 590 MB | 1.25 s / 430 MB | 1.71 s / 355 MB |
| type-fest | **0.28 s** / 529 MB | 5.37 s / 1022 MB | 1.96 s / 771 MB | 2.33 s / 773 MB |
| valibot | **0.38 s** / 518 MB | 8.97 s / 884 MB | 1.61 s / 832 MB | 2.41 s / 594 MB |
| react | **0.18 s** / 323 MB | 3.73 s / 502 MB | 1.05 s / 536 MB | 0.93 s / 318 MB |
| nest | **0.16 s** / 266 MB | 2.59 s / 401 MB | 0.44 s / 250 MB | 0.51 s / 208 MB |
| webpack | **0.38 s** / 457 MB | 8.73 s / 725 MB | 0.69 s / 475 MB | 0.60 s / 307 MB |
| zod | **0.41 s** / 701 MB | 10.76 s / 1304 MB | 2.91 s / 1363 MB | 3.68 s / 956 MB |
| xstate | **0.28 s** / 389 MB | 5.88 s / 601 MB | 0.99 s / 474 MB | 1.28 s / 352 MB |
| drizzle | **1.33 s** / 1817 MB | 12.63 s / 1448 MB | 3.72 s / 1869 MB | 3.96 s / 1054 MB |
| vue | **0.61 s** / 681 MB | 11.28 s / 953 MB | 1.90 s / 857 MB | 2.87 s / 685 MB |
tttt phases (load files / resolve / typecheck ms): tsc 68 / 100 / 296, type-fest 10 / 10 / 254, valibot 16 / 42 /
297, react 13 / 48 / 90, nest 43 / 63 / 24, webpack 61 / 89 / 197, zod 25 / 69 / 292, xstate 11 / 40 / 192, drizzle
57 / 74 / 1201, vue 48 / 89 / 447. tttt is faster than multi-threaded tsgo everywhere, 2.8-11x; memory is above
single-threaded tsgo's on most (drizzle 1.8 GB vs 1.05).

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

## Suite (2026-09-29, types session, todo entry 9, bc2752f2)
**11962 / 13540** full pass (errors 12681, symbols 12952, types 12733; session start 11472, 0 components lost). The
types component fails in 807 tests (1368 at the start). `./displaydiff.py`, tests (errors pass, types fail) 467 (73
JS; 968 at the start); fixed by one kind (TS, JS): other 153 + 17, underlines/order only 47 + 5, line-missing 25 + 1,
union-order 18, tsc-any 11, we-any 10 + 8, tsc-import() 9 + 2. `--fine`, TS tests by first differing line:
line-missing 50, underlines/order 47, shape-other/declaration 34, union-order 24, signature-differs/declaration 22,
typeof-other-name/declaration 19, object-differs/declaration 13, tsc-import() 12. `./failstats.py`: errors component
fails in 859 tests (247 other text, 166 only missing, 161 missing+spurious, 153 tsc errors/tttt none, 65 only
spurious, 55 tttt errors/tsc none); 47 tests have a spurious TS1xxx; log_error only in the two `_` tests (triage).
`./jsstats.py`: **892** of 1049 JS tests pass. `./syntaxstats.py`: 34 parser-divergent tests (unchanged). The
errtextdiff and older per-code lines of the previous entry weren't rerun.

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
