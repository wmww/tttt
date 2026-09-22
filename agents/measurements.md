# Measurements
Recorded output of `./failstats.py` and `./bench.sh`. Replace (don't append forever) when re-measuring; keep the
date and commit. History is in git.

## 2026-09-21, triage after plan 06
Suite: **1971 full of 13163** (was 1849), errors 6907, symbols 8787, types 3974. What moved: JS gate + fileless
diagnostics + option validation (+54), harness typechecking next to parse errors (+8, -7 honest), property symbols
through unions/apparent types (+54, -6 honest: intersection reduction), string escapes (+54). No perf change.
**`TTTT_IGNORE_UNDERLINES=1`: 3350 full, types 6334**: the `^^^` lines of the types baseline hide 1445 passing tests
(`plans/07_underlines.md`).

Real corpus: **0 parse, 0 name, 1751 type diagnostics** (`typeof o[K]` was the last name error), 0.70 s wall in
`realworld.py` (bench not rerun: nothing perf-relevant changed). By code: TS7006 852, TS2345 317, TS2769 103, TS2322
92, TS2339 79, TS18048 46, TS2495 25.

History summary (full passes / corpus diagnostics): plan 03 (corpus first parses) 1576 / 9699; plan 04 (binder) 1684 /
7754; plan 05 (flow) 1756 / 1750; plan 06 (modules) 1849 / 1752; this triage 1971 / 1751. Per-stage detail is in git
(`git log -- agents/measurements.md`).

## 2026-09-20, plan 06: module resolution, tsconfig
Suite: **1849 full of 13163** (was 1756), errors 6814 (6561), symbols 8514 (8475), types 3897 (3816). ~300 tests gained
a component, 1 lost one honestly (`selfReferentialDefaultNoStackOverflow`, see `triage.md`). Spurious TS2307: 348 tests
-> 44 (what is left: `@libFiles`, outDir input mapping, Windows paths). TS1479/TS1471 (143 + 68 missing) -> 0,
TS2834/TS2835, TS5097, TS6142, TS6053, TS2792 mostly gone; still missing: TS2688 without a file, TS5xxx option checks.

Real corpus: now `tttt -p thirdparty/tsc/src/compiler`, ie the corpus' own tsconfig: 223 files with `@types/node` and
its dependencies found by resolution instead of by hand. **1752 diagnostics** (1750 before), the `source-map` name
error is gone. `./bench.sh`: wall **0.59 s (tsgo 0.72, tsc 4.72)**, load+parse 143 ms (120-144 before: no measurable
cost from resolution, which stats a few hundred paths and reads ~10 package.json files), resolve 77 ms, typecheck
373 ms, RSS 338 MB.

## 2026-09-19, plan 05 stage 4: TS2339 ungated, parameter properties, expression bases, the profile
Suite: **1756 full of 13163** (was 1742), errors 6561 (6536), symbols 8475, types 3816 (3814). 73 tests gained a
component, 48 lost their errors component honestly (spurious TS2339 / TS18046-18048 the gate used to hide, see
`triage.md`). Missing TS2339 937 -> 536, spurious 0 -> 434 in 119 tests (37 tests where it is the only difference).

Real corpus: 1626 -> **1750 diagnostics** (79 TS2339, ~55 "possibly undefined"/`unknown`: real bugs, `triage.md`).

`./bench.sh`: synthetic wall 0.36 s (tsgo 0.48), typecheck 86 ms, RSS 545 MB. Real corpus wall 0.98 -> **0.57 s (tsgo
0.79, tsc 4.71)**, parse 120 ms, resolve 74 ms, **typecheck 778 -> 352 ms**, RSS 317 MB. The profile (14.6 G -> 7.2 G
instructions) had nothing to do with the flow engine itself: N*N unions when widening enum literal unions, no visited
set in `infer_from`, literal pairs through the full relation machinery. What is left on top is in `triage.md`.

