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

## Suite (2026-09-27, triage after the second window, at the session's end)
**9409 / 13540** full pass (errors 11168, symbols 12349, types 10743). `./failstats.py`: errors component fails in
2372 tests: tsc reports errors and tttt nothing 656, both missing and spurious 584, only missing 389, **same
codes+positions, other text 311**, tttt errors where tsc has none 257, only spurious 145, column differs 30. 323 tests
have a spurious TS1xxx (42 of them expect no errors). Checker log_error output: 27 lines, all in one test (invalid
spans of the silent call resolutions).
- `./displaydiff.py`, tests fixed by one kind (TS, JS): other 394 + 35, we-any 127 + 39, line-extra 118 + 6,
  underlines/order only 98 + 21, tsc-any 101 + 14, union-order 47 + 3, our-typeof 34 + 2, line-missing 27 + 4.
- `./errtextdiff.py --coarse` (311 tests): fixed by one part: chain 95, header 78, underline 60, related 46, outside
  diagnostics 15.
- `./jsstats.py`: 1049 JS tests, **395 pass**. Failing by category (only / involved): other 86 / 86, CommonJS 58 /
  157, expandos 55 / 198, advanced JSDoc 13 / 100, JSDoc 73 / 280, leniency 2 / 18, syntax 33 / 137, emit 16 / 39,
  `.ts`-only 17 / 26.
- `./syntaxstats.py`: **402** tests whose parser diagnostics differ (44 JS). TS tests: A 83, B 25, C 40, D 53, E 79,
  M 78. Clusters: other:M 70, lexical:literals 60, other:A 42, other:E 34, lexical:identifiers 32, other:D 30,
  recovery:list-context 23, permissive:modifiers 21, other:B 9, permissive:decorators 9, other:C 8, yield-await 7.

## Corpora (2026-09-27, same commit)
Diagnostics (all are 0 under tsc 5.8): tsc `src/compiler` **0**, valibot **0**, react **0**, nest **0**, type-fest
**4** (mixin classes, plan 32 stage C), webpack **135** (JSDoc, plan 26), zod **15** (plan 19).

## Perf (2026-09-27, `./bench.sh --runs 2`, quiet machine)
Best wall / peak RSS:

| corpus | tttt | tsc 5.8 | tsgo | tsgo 1 thread |
|---|---|---|---|---|
| synthetic (976 tests x 200, 2.66 M lines) | 5.93 s / 5088 MB | 42.8 s / 4292 MB | 9.71 s / 5456 MB | 12.74 s / 3708 MB |
| tsc `src/compiler` (193k lines) | **0.80 s** / 432 MB | 7.46 s | 1.24 s / 430 MB | 1.72 s |
| type-fest | 0.41 s / 476 MB | 5.34 s | 1.94 s / 800 MB | 2.31 s |
| valibot | 0.74 s / 484 MB | 8.94 s | 1.61 s / 835 MB | 2.42 s |
| react (1511 files) | 0.29 s / 265 MB | 3.74 s | 1.04 s / 538 MB | 0.93 s / 317 MB |
| nest (2394 files) | 0.26 s / 274 MB | 2.57 s | 0.40 s / 249 MB | 0.51 s / 213 MB |
| webpack (1188 files, checkJs) | 0.69 s / 455 MB | 8.75 s | 0.61 s / 496 MB | **0.60 s** / 307 MB |
| zod (621 files) | 1.43 s / 923 MB | 10.87 s | 2.95 s / 1362 MB | 3.64 s / 1017 MB |

tttt phases (parse files / resolve / typecheck, ms): synthetic 1315 / 880 / 3533; tsc corpus 148 / 119 / 523;
type-fest 20 / 13 / 371; valibot 66 / 48 / 598; react 90 / 56 / 130; nest 130 / 69 / 34; webpack 180 / 103 / 383;
zod 106 / 79 / 1209; the stdlib alone (esnext.full, 42k lines) 20 / 10 / 37. Libs load+parse ~17 ms.
- **webpack is the one corpus where tsgo, even single-threaded, beats tttt** (0.60 vs 0.69 s: JSDoc checking and
  RSS). nest is the closest TS corpus (0.26 vs 0.40): parsing is half of tttt's time there (skipLibCheck).
- zod is the heaviest (typecheck 1.2 s; profile in `bugs/perf_leads.md`; DeepPartial's TS2589 may be part of it).

## Instructions (2026-09-27, from the entries of that day)
`perf stat` (user+kernel, higher than callgrind's count) at the zod pinning session: tsc corpus 5.43 G, valibot 5.74 G,
type-fest 3.87 G, react 2.25 G, nest 1.46 G, webpack 3.87 G, zod 11.74 G (callgrind 10.85 G). Callgrind history: tsc corpus 6.92 G at the 09-27 perf
check-up start -> 5.91 G (that session) -> 5.79 G (plan 28, calls resolve once) -> 5.05 G (parser perf: first-char
dispatch, keyword probes). Re-measure all with callgrind at the next perf session and replace this section.
