# Todo
Written by the insights of 2026-10-08 (after the twelfth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle) after checker changes, `scripts/instrcount.sh` and a commit.

Starting point: suite **14835 / 14969**, every corpus 0 but effect 3 (zod-root = tsc's 53), sweep 8594 / 8595,
`$ExpectType` 68631 / 68640, flagflip 17 / 11 / 4, source sweep 3003 / 3359 (71 / 174 repos).

**This window is plan 53**: TypeScript 7 as the target, tttt as a drop-in `tsc` (the human's request). Read plan 53
and 53_findings.md before any entry. Entry 2 is a prototype that may rewrite entries 3-8 (within this budget): it
knows more than this file does. From entry 3 on, the oracle is the pinned 7.1 nightly; 4d4f005c stays fetched only
until nothing reads it.

1. ~~Plan 53 stage 1: CLI parity~~ **done**: tsc's formats (plain when piped, pretty on a terminal), sorted like
   tsc, tsc 7's exit codes, no chatter, tsgo's flags, implied `--noEmit`; `scripts/clidiff.py` 243 / 261 byte-identical
   against tsc 6, 551 / 827 against tsgo 7.0.2 (0 exit code or order mismatches); every oracle unchanged.
2. **Plan 53 stage 3 prototype** (planning). Pin a `main` commit + its `typescript@7.1.0-dev` nightly; sparse fetch
   (`tsc/testdata`, bundled libs, CHANGES.md); install Go and build tsgo's test runner at that commit (or decide
   against it with numbers); run the native suite through the harness in a worktree and size the drop by mechanism
   (failstats on the new tree); decide how each cache is regenerated (CLI vs `unstable/sync`). Write the switch as
   **plan 54** (plan-48 shape: stages, session count, validation) and rewrite entries 3-8 below to match it.
3. **The switch** (plan 54 stage 1): ensure-tsc.sh, harness on the native tree, 7.x libs, 7.0's option errors
   (plan 53 stage 2), `filetest.sh`/`tscbaseline.sh` on the new oracle. Saved results regenerated: expect ~90%.
4. **Delete what 7 deleted**: es5/es3, amd/umd/system/none, node10/classic, baseUrl, outFile, downlevelIteration,
   the false-only interop options, and the JS/JSDoc/CommonJS features CHANGES.md removes. A net deletion; suite 0
   lost against entry 3's saved results except tests that only passed through the deleted paths.
5. **Oracle caches on tsgo**: corpora (re-pin the ones 7 disagrees on: webpack's JS), flagflip, scenarios, source
   sweep through the CLI with `--noEmit --tsBuildInfoFile /tmp/...` (fixes the 26% fidelity gap), DefinitelyTyped
   sweep and `$ExpectType` (CLI per package / `unstable/sync`), mutate.py. `scripts/oracles.py` runs on the new
   caches; measurements.md gets the new baseline numbers.
6. **Fan-out on the 7.x checker deltas** (4 sub-agents, by mechanism): no suggestion limit, code-point template
   inference, the `strict: false` parameter rule, elaboration codes (TS2345 -> TS2741/2740), package dedup
   (`deduplicatePackages`), TS6305 under references, the 7.1 ordering fixes in `ty/order.c`.
7. **Fan-out on the JS rework** (4 sub-agents): CHANGES.md's changed JS/JSDoc/CommonJS rules (exports in branches
   union, `@param` scope, hoisted `@typedef`, `@type` blocking narrowing, `arguments`), webpack back to 0.
8. **Plan 53 stage 4: 7.1 language**: source phase imports, import attributes on ambient modules (TS1550-1558),
   target-gated regex syntax, the new diagnostics, es2026 lib. Probe file per feature against the nightly.
9. **Perf session against tsgo 7.1**: `scripts/bench.sh` re-baselined on the new binary (single- and
   multi-threaded), instrcount after the deletions; measurements.md's perf section replaced. vscode's margin first.
10. **Triage.** Drain triage.md, bookkeeping for the switch (documentation.md "baseline_tests", CLAUDE.md's oracle
    paragraph and script descriptions, hacks.md entries that referred to deleted options), decide whether anything
    still reads 4d4f005c. Write the 7.1.0 stable re-pin (after 2026-11-24) into a plan if it hasn't happened.
11. **Insights.**
