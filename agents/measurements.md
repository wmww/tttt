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

## Suite (2026-09-29, parser session, todo entry 8, 82f0b81a)
**11472 / 13540** full pass (errors 12661, symbols 12865, types 12172; session start 11274, 0 components lost).
`./failstats.py`: errors component fails in 879 tests (1074 at the start): same codes+positions, other text 257, only
missing 165, both missing and spurious 165, tsc reports errors and tttt nothing 155, only spurious 66, tttt errors
where tsc has none 59. **47 tests have a spurious TS1xxx** (196). Top missing codes: TS2339 62 in 12 tests, TS1335
51/1, TS2322 47/28, TS2304 44/25, TS9007 36/6; top spurious: TS2304 137 in 39, TS2339 97/31, TS2345 69/22, TS2322
59/30. `./unimplemented_codes.sh`: TS1335, TS9007 (isolatedDeclarations: TS9007/9010/9038/9012/9011), TS18057,
TS2700. `./jsstats.py`: **888** of 1049 JS tests pass. `./syntaxstats.py`: **34** tests whose parser diagnostics
differ (197 at the start; 3 JS): A 8, B 4, C 0, D 2, E 16, M 1; clusters other:E 11, other:B 4, recovery:list-context
4, other:A 3, then 1-2 each (plan 27 "Leftovers"). `./displaydiff.py`, tests fixed by one kind (TS, JS): other 307 +
22, underlines/order only 74 + 6, we-any 59 + 8, union-order 45, tsc-any 41, line-missing 31 + 1, tsc-typeof 26 + 4,
our-typeof 25 + 1. The `--fine`, errtextdiff and older jsstats category lines of the previous entry weren't rerun.

## Corpora (2026-09-29, parser session, 82f0b81a)
Diagnostics (all are 0 under tsc 5.8): tsc `src/compiler` **0**, valibot **0**, react **0**, nest **0**, type-fest
**0**, zod **0**, webpack **6**, **xstate 19** (plan 19 "xstate"), all unchanged this session. **zod-root** reports 55
like tsc. Output identical at 1 and 4 threads (`./threadcheck.sh`). Instructions (`perf stat -e instructions:u`,
`TTTT_NO_REEXEC=1 --checkers 1 --threads 1 -q --keep-going`), JS session end (9b547473) -> parser session end: tsc
5.080 -> 5.132 G (+1.0%), type-fest 3.439 -> 3.447, valibot 5.530 -> 5.554, react 2.047 -> 2.072 (+1.2%), **nest 1.201
-> 1.231 (+2.5%)**, webpack 3.901 -> 3.956 (+1.4%), zod 4.507 -> 4.542, xstate 2.907 -> 2.929. The parser's recovery
work ran the tokenizer (`tok_peek`/`tok_scan_end`) on valid code in several places; 82f0b81a removed the hottest
(nest 1.239 -> 1.231). What's left is spread thin: `parse_type` became `parse_type_inner` + an out-of-line
`parse_identifier_type` (nest: 3.85% -> 4.2% of samples), `scope_init_declaration` +0.7%, `parse_class_item_inner`,
`parse_identifier`, `parse_modifier_list` +0.3-0.6% each. Owed to the next perf session together with the checker
bugs sessions' 2-5% (instructions have crept up ~2-8% per corpus over the last three sessions).
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

## Perf (2026-09-28, plan 36 stage 3, `./bench.sh`, tttt at its new defaults: 4 checkers on 4 threads)
Best wall / peak RSS. **The machine is 2 physical cores x 2 SMT threads** (KVM, `lscpu`): 4 busy threads run ~55%
slower each (4 independent single-threaded processes do too), so 4 threads buy ~2.4x on the check phase, not ~3.7x.
tttt re-executes itself with glibc's malloc on transparent huge pages (`TTTT_NO_REEXEC=1` to measure without).
tttt 1 thread is `--checkers 1` (the previous numbers, re-measured for tsc and synthetic this session).

| corpus | tttt | tttt 1 thread | tsc 5.8 | tsgo | tsgo 1 thread |
|---|---|---|---|---|---|
| synthetic (987 tests x 200, 2.70 M lines) | **3.70 s** / 4356 MB | 4.86 s / 4252 MB | 51.2 s | 10.30 s / 5719 MB | 12.40 s / 3782 MB |
| tsc `src/compiler` (193k lines) | **0.48 s** / 409 MB | 0.66 s / 365 MB | 7.36 s | 1.26 s / 444 MB | 1.73 s / 348 MB |
| type-fest | 0.28 s / 494 MB | 0.32 s / 476 MB | 5.35 s | 1.93 s / 805 MB | 2.32 s |
| valibot | **0.38 s** / 536 MB | 0.62 s / 453 MB | 8.95 s | 1.65 s / 825 MB | 2.40 s |
| react (1511 files) | 0.23 s / 335 MB | 0.24 s / 228 MB | 3.77 s | 1.03 s / 543 MB | 0.92 s / 315 MB |
| nest (2394 files) | 0.20 s / 251 MB | 0.19 s / 225 MB | 2.58 s | 0.40 s / 250 MB | 0.51 s / 213 MB |
| webpack (1188 files, checkJs) | **0.40 s** / 461 MB | 0.56 s / 375 MB | 8.75 s | 0.67 s / 463 MB | 0.60 s / 308 MB |
| zod (621 files) | **0.45 s** / 846 MB | 0.73 s / 665 MB | 10.74 s | 2.92 s / 1406 MB | 3.70 s / 1029 MB |
| xstate (651 files; 2026-09-28 corpus session, not bench.sh) | **0.27 s** / 370 MB | | 6.0 s | 1.00 s / 483 MB | |

tttt phases at K=4 (parse files / resolve / typecheck, ms): synthetic 836 / 728 / 2116; tsc corpus 110 / 90 / 281
(slowest checker 281 ms: the one with checker.ts); type-fest 16 / 8 / 249 (one heavy test file); valibot 51 / 37 /
275; react 70 / 42 / 105; nest 108 / 55 / 23; webpack 137 / 78 / 162; zod 77 / 62 / 293. **Load + resolve is now
40-90% of every run but type-fest's** (plan 36 stage 1, then parallel name resolution).
- Peak RSS at K=4 vs 1: synthetic +2%, type-fest +4%, tsc +12%, nest +12%, valibot +18%, webpack +23%, zod +27%,
  react +47% (every checker instantiates the React typings). Still at or under multi-threaded tsgo's everywhere.
- Memory is still the weak side: zod 846 MB (deep-partial.test.ts alone is 745 MB at K=1, 587k types where tsc makes
  168k), type-fest ~480 MB for 575k types. `perf_leads.md`.
- Parsing the tsc corpus after plan 27 stage 3: 104 -> 109 ms, callgrind in `parse_file` 728 -> 750 M (unicode-aware
  identifier scanning, `text_decode`, the number scanner; `hop_whitespace_slow`'s switch is sensitive to extra cases).

## Parallelism prototypes (2026-09-28, plan 36 planning, 0746971 + `plans/36_prototype.patch`)
Parallel loading at 4 workers, whole run best of 5: tsc 661 -> 631 ms, webpack 564 -> 527, nest 201 -> 180, react
243 -> 228, valibot 622 -> 599, zod 1315 -> 1260, type-fest 327 -> 321; output byte-identical. K checkers run one
after another, typecheck ms K=1 -> K=4 sum / slowest checker: tsc 464 -> 505 / 243, webpack 337 -> 400 / 107, zod
1147 -> 1266 / 717, valibot 515 -> 586 / 188, react 119 -> 245 / 73. Details and the load-phase split in plan 36.
