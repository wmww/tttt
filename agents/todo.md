# Todo
Written by the insights of 2026-10-08 (after the twelfth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle) after checker changes, `scripts/instrcount.sh` and a commit.

Starting point: suite **14835 / 14969**, every corpus 0 but effect 3 (zod-root = tsc's 53), sweep 8594 / 8595,
`$ExpectType` 68631 / 68640, flagflip 17 / 11 / 4, source sweep 3003 / 3359 (71 / 174 repos).

**This window is plan 53**: TypeScript 7 as the target, tttt as a drop-in `tsc` (the human's request). Read plan 53,
53_findings.md and **plan 54** (the switch, written by entry 2 with the prototype's numbers) before any entry. From
entry 3 on, the oracle is the pinned 7.1 nightly; 4d4f005c stays fetched only until nothing reads it (entry 5).

1. ~~Plan 53 stage 1: CLI parity~~ **done**: tsc's formats (plain when piped, pretty on a terminal), sorted like
   tsc, tsc 7's exit codes, no chatter, tsgo's flags, implied `--noEmit`; `scripts/clidiff.py` 243 / 261 byte-identical
   against tsc 6, 551 / 827 against tsgo 7.0.2 (0 exit code or order mismatches); every oracle unchanged.
2. ~~Plan 53 stage 3 prototype~~ **done**: pinned `main` fed0bf24 = `typescript@7.1.0-dev.20261008.1`
   (`thirdparty/ensure-typescript.sh`: checkout, Go 1.27.2, tsgo's CLI built from it); tools decided (Go + `go build
   -overlay`: tsgo's own test runner gives any file's baselines in 45 ms, an overlay main can call the checker);
   native suite through tttt's harness: **11842 / 13572 (87.3%)**, failures bucketed. **Plan 54** has it all.
3. ~~Plan 54 stage 1: the switch~~ **done**: the native suite is the suite (**11860 / 13571**, 10 s; 1721 skipped
   configurations), 7.1 libs + generated lib tables + es2026, the `^^^` machinery deleted, the 7 crashes and 2 slow
   tests fixed, suite scripts on `scripts/suite.py`. Corpora, `$ExpectType`, flagflip unchanged; DT sweep 8586 (8 new
   spurious: 7.1 `lib.dom` vs 6.0's caches, stage 3). filetest.sh/mutate.py still use 6.0's baselines (underlines
   stripped) until entry 4.
4. **Plan 54 stage 2: oracle tools on tsgo.** `test/oracle/` Go overlay: the probe (`tscbaseline.sh`,
   `filetest.sh`, diffapp, `mutate.py`) and `tttt-oracle` (per-project diagnostics with CLI semantics,
   `$ExpectType`), replacing `test/sweep_tsc.js`; `--as-emitting` goes.
5. **Plan 54 stage 3: regenerate every cache** (DT sweep + flag variants, `$ExpectType`, source sweep, flagflip,
   mutate, corpora expectations, scenarios if cheap), port the scripts that read tsc's JS sources, TypeScript 6's
   `src` becomes a corpus, **`thirdparty/tsc` deleted**; documentation.md, CLAUDE.md, measurements.md rewritten.
6. **Plan 54 stage 4: delete what 7 deleted** (plan 53 stage 2's option errors, then the es5/amd/umd/system/
   node10/classic/baseUrl/outFile/downlevelIteration code paths). A net deletion.
7. **Fan-out on the 7.x TS deltas** (4 sub-agents by mechanism, plan 54 "Stage 5+"): elaboration codes, TS6196,
   TS2300, the suggestion limit, TS2303, source quote style (109 tests), alias naming, lib declaration lists,
   `ty/order.c`'s 7.1 fixes.
8. **Fan-out on the JS rework** (4 sub-agents by CHANGES.md table; 452 tests): deletions first; webpack's corpus
   expectations re-checked.
9. **7.1 language and the new tests** (281 tests; fan-out if they split cleanly): source phase imports,
   ambient-module import attributes, target-gated regex, the new diagnostics. A probe per feature against
   `thirdparty/typescript/built/tsc`.
10. **Triage.** Drain triage.md, bookkeeping for the switch (CLAUDE.md's script descriptions, hacks.md entries that
    referred to deleted options), check nothing reads 4d4f005c. The perf session against tsgo 7.1 (`bench.sh`
    re-baselined, measurements.md's perf section) didn't fit this window: schedule it for the next. Write the
    7.1.0 stable re-pin (after 2026-11-24, plan 54 "The pin") into todo if it hasn't happened.
11. **Insights.**
