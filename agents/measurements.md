# Measurements
The latest of each kind of measurement. **Replace a section when re-measuring** (keep its date and commit); don't
append history, git has it (this file before the 2026-09-27 triage holds every entry since plan 05).

## Machine and caveats
AMD EPYC-Turin, 4 cores, 31 GB; Ubuntu 26.04, GCC 15.2, node 22.22, tsc 5.8.2 (`thirdparty/tsc`), tsgo =
`typescript@7.0.2`. Wall times drift by ~15% between days (a shared machine) and anything running alongside skews
them: compare walls only within one session. Callgrind instruction counts (`valgrind --tool=callgrind ./release/tttt
-q --keep-going -p DIR`) are the stable measure across sessions. tsgo exits 1 on valibot (TS5108,
`moduleResolution=node10` removed) and on webpack (3 TS1003 on JSDoc it parses differently), so it may do less work
there than a clean run.

## Suite (2026-09-27, plan 35 stage 1, bef38cb)
**9666 / 13540** full pass (errors 11180, symbols 12356, types 11049); 9667 after the 09-28 perf session, **9729**
after the 09-28 plan 26 session (errors 11242, symbols 12407, types 11090; errors component: 2298 failing, tsc
reports errors and tttt nothing 578), whose other numbers below weren't re-measured. A suite run takes ~23 s
(was 50: relationComplexityError alone took 27). `./failstats.py`: errors component fails in
2360 tests: tsc reports errors and tttt nothing 654, both missing and spurious 585, only missing 389, **same
codes+positions, other text 311**, tttt errors where tsc has none 254, only spurious 137, column differs 30. 323 tests
have a spurious TS1xxx (42 of them expect no errors). Checker log_error output: 27 lines, all in one test (invalid
spans of the silent call resolutions). The lines below the displaydiff ones are from the triage before (9409).
- `./displaydiff.py`, tests fixed by one kind (TS, JS): other 393 + 39, we-any 93 + 41, underlines/order only
  105 + 21, union-order 47 + 3, tsc-any 32 + 2, line-missing 28 + 4, line-extra 20 + 0.
- `./displaydiff.py --fine`, TS tests by the kind of their first differing line: underlines/order 105,
  we-any/declaration 95, line-missing 73, shape-other/declaration 58, union-order 57, signature-differs/declaration
  48, object-differs/declaration 46, union-differs/declaration 44, our-typeof 42 (plan 35's queue).
- `./errtextdiff.py --coarse` (311 tests): fixed by one part: chain 95, header 78, underline 60, related 46, outside
  diagnostics 15.
- `./jsstats.py` (2026-09-28, plan 26 session, 7236285): 1049 JS tests, **470 pass**. Failing by category (only /
  involved): other 78 / 78, CommonJS 67 / 151, expandos 41 / 160, advanced JSDoc 8 / 86, JSDoc 73 / 262, leniency
  2 / 17, syntax 20 / 88, emit 34 / 39, `.ts`-only 14 / 23.
- `./syntaxstats.py`: **402** tests whose parser diagnostics differ (44 JS). TS tests: A 83, B 25, C 40, D 53, E 79,
  M 78. Clusters: other:M 70, lexical:literals 60, other:A 42, other:E 34, lexical:identifiers 32, other:D 30,
  recovery:list-context 23, permissive:modifiers 21, other:B 9, permissive:decorators 9, other:C 8, yield-await 7.

## Corpora (2026-09-28, perf session, todo entry 3)
Diagnostics (all are 0 under tsc 5.8): tsc `src/compiler` **0**, valibot **0**, react **0**, nest **0**, type-fest
**4** (mixin classes, plan 32 stage C), webpack **116** (JSDoc, plan 26; 130 -> 118 at the perf session: union
inference with circularity; 118 -> 116 at the plan 26 session, which also turned on TS2339 in JS: 3 of those left),
zod **13** (plan 19; its 2 TS2589s went with the inference depth fix). Re-checked 2026-09-28 (7236285).

## Perf (2026-09-28, perf session, `./bench.sh --runs 2`)
Best wall / peak RSS. tttt now re-executes itself with glibc's malloc on transparent huge pages and madvises its
arena (`TTTT_NO_REEXEC=1` to measure without: 5-20% slower, same RSS):

| corpus | tttt | tsc 5.8 | tsgo | tsgo 1 thread |
|---|---|---|---|---|
| synthetic (987 tests x 200, 2.70 M lines) | 4.61 s / 3984 MB | 51.2 s / 4309 MB | 10.30 s / 5719 MB | 12.40 s / 3782 MB |
| tsc `src/compiler` (193k lines) | **0.65 s** / 351 MB | 7.36 s | 1.27 s / 406 MB | 1.72 s |
| type-fest | 0.32 s / 476 MB | 5.33 s | 1.90 s / 800 MB | 2.34 s |
| valibot | 0.62 s / 453 MB | 8.97 s | 1.60 s / 832 MB | 2.40 s |
| react (1511 files) | 0.24 s / 228 MB | 3.74 s | 1.04 s / 539 MB | 0.94 s / 295 MB |
| nest (2394 files) | 0.19 s / 225 MB | 2.57 s | 0.43 s / 240 MB | 0.51 s / 212 MB |
| webpack (1188 files, checkJs) | **0.56 s** / 375 MB | 8.75 s | 0.67 s / 450 MB | 0.60 s / 307 MB |
| zod (621 files) | 1.31 s / 1302 MB | 10.75 s | 2.89 s / 1367 MB | 3.71 s / 971 MB |

tttt phases (parse files / resolve / typecheck, ms; checker arena, types): synthetic 760 / 709 / 3173 (970 MB, 2.1 M);
tsc corpus 104 / 89 / 455 (60 MB, 94k); type-fest 15 / 7 / 293 (413 MB, 575k); valibot 48 / 37 / 518 (291 MB, 374k);
react 65 / 42 / 118; nest 101 / 54 / 30; webpack 131 / 78 / 335 (93 MB); zod 72 / 62 / 1159 (1026 MB, 943k); the
stdlib alone (esnext.full, 42k lines) 13 / 5 / 33. `--time` prints the resident set per phase and the arena/types.
- **tttt beats single-threaded tsgo on every corpus now**, webpack included (0.56 vs 0.60; was 0.69).
- Memory is the weak side: zod 1.3 GB (deep-partial.test.ts alone is 745 MB, 587k types where tsc makes 168k),
  type-fest 476 MB for 575k types. `perf_leads.md`.

## Instructions (2026-09-28, perf session, callgrind `-q --keep-going -p DIR`)
At the session's start (028246a) -> end: tsc corpus 5.100 -> 4.661 G, type-fest 3.348 -> 3.276, valibot 5.262 -> 5.012,
react 1.929 -> 1.934, nest 1.008 -> 1.022 (the parser's shrink_to_fit reallocs), webpack 3.383 -> 3.282,
zod 10.889 -> 9.057. (valgrind runs don't re-exec, so huge pages aren't in these.)

## Parallelism prototypes (2026-09-28, plan 36 planning, 0746971 + `plans/36_prototype.patch`)
Parallel loading at 4 workers, whole run best of 5: tsc 661 -> 631 ms, webpack 564 -> 527, nest 201 -> 180, react
243 -> 228, valibot 622 -> 599, zod 1315 -> 1260, type-fest 327 -> 321; output byte-identical. K checkers run one
after another, typecheck ms K=1 -> K=4 sum / slowest checker: tsc 464 -> 505 / 243, webpack 337 -> 400 / 107, zod
1147 -> 1266 / 717, valibot 515 -> 586 / 188, react 119 -> 245 / 73. Details and the load-phase split in plan 36.
