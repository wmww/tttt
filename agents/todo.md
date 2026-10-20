# Todo
Written by the insights of 2026-10-20 (after the twenty-fourth window). `vision.md`'s "Course correction" explains
why. Delete entries as they complete, leaving a one-line "done" note with the numbers. New work goes into plans or
triage.md, never ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
after checker changes (it exceeds the 10-minute foreground cap: run it in the background with a bounded wait),
`scripts/instrcount.sh`, the memory numbers in measurements.md after memory-relevant changes, an empty `git worktree
list`, /tmp scratch deleted (CLAUDE.md), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry starts
(`npm view typescript dist-tags`: on 2026-10-20 `latest` was 7.0.2, `beta` 7.1.0-beta published that day, `next`
7.1.0-dev.20261020.3), do plan 58 (the re-pin) first, then plan 66 stage 5**, and push the rest down. The beta is
not a re-pin target (one re-pin, to stable). `claude-stop` is gitignored: never commit it.

Starting point (b7ad0eac): suite 13957 / 13991 (dts 1887 / 1909), corpora 0 / 0 but vue 0 / 2, DT 8595 / 8595,
`$ExpectType` 68640 / 68640, flagflip 18 / 3 / 2, source sweep 5235 / 5242, `-b` sweep 411 / 416, clidiff 3824 /
3831, dtsdiff 1330 / 1350, kdiff 4, recycle 4 (measurements.md "Oracle totals"). Instructions `test/instrcount.txt`.

Rules of the window (plan 86 stage 4a): **nothing is shared yet**. Every stage is a K1 refactor: the output stays
byte-identical at every K, budget and thread count; K1 instructions within 1% per stage and 3% over 4a-4c
(`scripts/instrcount.sh`, cachegrind before wall); `scripts/taintstats.sh --lenient --cleandiff` on drizzle,
excalidraw, zod, effect and vscode after each session, and its mismatch counts must not grow (G0 condition 1). If a
4a session finds the overlay split can't be made without semantic changes the oracles reject, stop and write up why
(plan 86's fallback is stage 3 plus the snapshot recycle): a no-go is a valid outcome.

1. Done 2026-10-20 (session 1): plan 88 stage A (`Checker.barriers`, ty_resolving_reenters()) and variance claims
   (`VarianceClaim`, the links hold only finished `Variances`); the overlay's record shape and API are in plan 86
   ("4a's overlay"). Suite 13957, corpora, DT, `$ExpectType`, flagflip, source sweep, `-b`, perfsweep, dtsdiff and
   clidiff unchanged; K1 instructions +0.04-0.12% (not saved); taint cleandiff mismatches identical to b7ad0eac's on
   all five corpora. **kdiff and recycle were not run** (stopped: the machine was being moved): run
   `scripts/oracles.py --only kdiff,recycle` first in session 2.
2. **Stage 4a, session 2: the rest of the in-flight state.** The IN_FLIGHT LazyStates, LazyMemo flags,
   KEY_SIMPLIFIED's placeholder, KEY_BASE_CONSTRAINT's sentinels, early and partial members (census A/B lists).
   Decide plan 88 stage B here (barrier termination instead of `reevaluating` / `prop_reentry_depth`): do it only if it
   makes the overlay simpler and the suite and the source sweep hold; otherwise the flags move into the overlay as
   they are and B stays in plan 88.
3. **Stage 4a, session 3: finish 4a, G0 condition 3.** Whatever sessions 1-2 left; root-cause the 46 lenient-only
   drizzle prop types (does flow reach a type? if yes, the G0 rule gets an end-condition event there and the clean
   shares are remeasured). Tally in plan 86: instructions against the session-start counts, the census sites left,
   sessions left to G1. If 4a is done with time to spare, start 4b with M1 (the distributed twin).
4. **Real code: the IntersectionState audit** (hacks.md "IntersectionState is sticky", one session): clear both
   states on entering structured comparison and pass them on only at tsc's sites (relater.go's isRelatedTo vs
   recursiveTypeRelatedTo calls); mikro-orm's IndexFilterQuery stays fixed, perfsweep's types ratios don't rise. With
   time left, bugs/sourcesweep_checker.md's cheapest item.
5. **Triage.**
6. **Insights.** report.md: 4a's state and cost, whether G1 is in reach next window. Check plan 58 (7.1.0 stable).

Not in this window (candidates for the next, which the insights session reorders): plan 86 stages 4b-4c up to gate G1
(M1, M2, diagnostics as data, global atoms and ids, first-touch stamps), then 4d behind `--shared-graph`; plan 88
stages C-E (C after B; D only with a contextual-typing rework; E no); stage 3's front end (the import pre-scan, then
parallel declare + link) as the fallback or as perf filler; plan 87 stage 2 (share declaration files across `-b`
programs); stage 2's leftovers in perf slots; a fourth refresh by a new rule (monorepos with `paths`-heavy configs,
`allowImportingTsExtensions` projects, Vue/Svelte tooling) after G1; plan 58 and plan 66 stage 5 when 7.1.0 is out
(expected after 2026-11-24); plan 76 only if the human reopens it.
