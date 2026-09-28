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

## Suite (2026-09-28, insights, 9352ec1)
**10215 / 13540** full pass (errors 11709, symbols 12530, types 11353). A suite run takes ~23 s. `./failstats.py`:
errors component fails in 1831 tests: both missing and spurious 510, tsc reports errors and tttt nothing 400, **same
codes+positions, other text 297**, only missing 252, tttt errors where tsc has none 219, only spurious 132, column
differs 21. 253 tests have a spurious TS1xxx (29 of them expect no errors). Checker log_error output: 29 lines in 2
tests (invalid spans, `bugs/harness.md`). Top missing codes: TS1005 157 in 84 tests, TS2322 99/53, TS2339 98/28,
TS2304 90/56; top spurious: TS2304 484 in 164 tests, TS2339 347/99, TS1005 260/88, TS2322 246/107. Single-code
tests: spurious TS2339 40, spurious TS2322 39, spurious TS2304 35. The tool lines below are from 7aef64e (10189).
- `./displaydiff.py`, tests fixed by one kind (TS, JS): other 390 + 53, underlines/order only 110 + 22, we-any 65 +
  37, union-order 48 + 3, tsc-any 34 + 5, names-only 28 + 5, tsc-typeof 24 + 9, line-missing 23 + 4.
- `./displaydiff.py --fine`, TS tests by the kind of their first differing line: underlines/order 110, line-missing
  73, shape-other/declaration 62, union-order 57, we-any/declaration 50, signature-differs/declaration 50,
  union-differs/declaration 43, object-differs/declaration 40, names-only 39, our-typeof 32 (plan 35's queue).
- `./errtextdiff.py --coarse` (298 tests): fixed by one part: chain 93, header 75, underline 61, related 46.
- `./jsstats.py`: 1049 JS tests, **519 pass**. Failing by category (only / involved): other 64 / 64, CommonJS 67 /
  150, expandos 39 / 157, advanced JSDoc 8 / 82, JSDoc 71 / 253, leniency 2 / 17, syntax 18 / 83, emit 10 / 11,
  `.ts`-only 17 / 26.
- `./syntaxstats.py`: **262** tests whose parser diagnostics differ (42 JS). TS tests: A 61, B 17, C 17, D 32, E 62,
  M 31. Clusters: other:A 40, other:E 34, other:M 30, other:D 28, permissive:modifiers 22, recovery:list-context 14,
  lexical:identifiers 9, permissive:decorators 9, other:C 8, other:B 8, recovery:object-literal 6.

## Corpora (2026-09-28, triage session)
Diagnostics (all are 0 under tsc 5.8): tsc `src/compiler` **0**, valibot **0**, react **0**, nest **0**, type-fest
**0**, zod **0**, webpack **107** (JSDoc, plan 26). **zod-root** (new: zod's root tsconfig, not clean under tsc,
which reports 55): tttt 82, of which 28 spurious and 1 missing (plan 19).
Instructions (`perf stat -e instructions:u`, stable like callgrind, much faster) at the end of the triage session:
tsc 4.67 G, type-fest 3.30, valibot 5.06, react 1.91, nest 1.02, webpack 3.25, zod **4.26** (8.54 at the corpus
session: the drop came with suite checker bugs 4, probably the TS2322 sub-agent's relation work; not investigated).

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
Parsing the tsc corpus after plan 27 stage 3 (2026-09-28): 104 -> 109 ms, callgrind in `parse_file` 728 -> 750 M
(unicode-aware identifier scanning, `text_decode`, the number scanner; it was 116 ms / 825 M before a cleanup:
`hop_whitespace_slow`'s switch is sensitive to extra cases, and the keyword probe/identifier loops to any extra work).
- **tttt beats single-threaded tsgo on every corpus now**, webpack included (0.56 vs 0.60; was 0.69).
- Memory is the weak side: zod 1.3 GB (deep-partial.test.ts alone is 745 MB, 587k types where tsc makes 168k),
  type-fest 476 MB for 575k types. `perf_leads.md`.

## Parallelism prototypes (2026-09-28, plan 36 planning, 0746971 + `plans/36_prototype.patch`)
Parallel loading at 4 workers, whole run best of 5: tsc 661 -> 631 ms, webpack 564 -> 527, nest 201 -> 180, react
243 -> 228, valibot 622 -> 599, zod 1315 -> 1260, type-fest 327 -> 321; output byte-identical. K checkers run one
after another, typecheck ms K=1 -> K=4 sum / slowest checker: tsc 464 -> 505 / 243, webpack 337 -> 400 / 107, zod
1147 -> 1266 / 717, valibot 515 -> 586 / 188, react 119 -> 245 / 73. Details and the load-phase split in plan 36.
