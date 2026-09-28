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
which reports 55): tttt 83, of which 28 spurious and 0 missing (plan 19; 82 -> 83 is plan 37 E's TS2412).
Instructions (`perf stat -e instructions:u`, stable like callgrind, much faster) at the end of the triage session:
tsc 4.67 G, type-fest 3.30, valibot 5.06, react 1.91, nest 1.02, webpack 3.25, zod **4.26** (8.54 at the corpus
session; bisected to 62d8c89, variance annotations taken as given: zod's `out` types were measured unreliable before,
forcing structural comparisons; 932k -> 352k types, arena 975 -> 412 MB). Current walls: zod 0.73 s / 665 MB.
After plan 37 B (union origins, `TTTT_NO_REEXEC=1 --checkers 1 --threads 1`): valibot 5.06 -> 5.09 G (+0.7%), zod
4.28 -> 4.29 (+0.2%); valibot makes 0.6% more types (unions with an origin are distinct from their plain twins).

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
