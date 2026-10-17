# Todo
Written by the insights of 2026-10-17 (after the nineteenth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle; it exceeds the 10-minute foreground cap: run it in the background with a bounded wait) after checker
changes, `scripts/instrcount.sh`, the memory numbers in measurements.md after memory-relevant changes (vscode,
effect, webpack, drizzle, type-fest: RSS after parse / after names / peak at K1 and at the defaults), an empty `git
worktree list`, /tmp scratch deleted (CLAUDE.md), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry
starts (`npm view typescript dist-tags`: on 2026-10-17 `latest` was 7.0.2 and `next` 7.1.0-dev.20261017.1), do plan
58 (the re-pin) first, then plan 66 stage 5**, and push the rest down. `claude-stop` is gitignored: never commit it.

Starting point (fa5f3fca + this commit): suite 13773 / 13812 (dts 1834 / 1859), corpora 0 / 0 but vue 0 / 2, DT
8595 / 8595, `$ExpectType` 68639 / 68640, flagflip 18 / 6 / 3, scenarios 210 / 0, source sweep 3356 / 3363, kdiff
1 / 3363, recycle 4 / 3363, dtsdiff 1193 / 1233, clidiff `--emit` 369 / 396. Memory: plan 67 "Where it stands" (the
bench against bun and tsgo); instructions: measurements.md "Perf session paying for the window".

The memory rule of the last window stands (vision.md "Memory target"): a memory change costs at most ~3% wall at K1
and the defaults, output byte-identical at every K and budget.

1. Done (ab6a8674 + the commit after): plan 77, 28 fresh repos, day one 195 / 203 projects (96.1%), after 203 / 203;
   whole sweep 3559 / 3566; every oracle unchanged. Entry 7 is therefore checker balance.
2. Done (this commit): plan 81 planning. Density came in under its gate (eight items, 18-24% of the K1 arena), and
   per-type cost at one thread is already bun's. bun's defaults re-derive types rather than keep them (its peak falls
   from 1 to 2 threads, its CPU rises 15-110%). At 4 x 32 MB tttt is at or below bun on 5 of 8 corpora, still faster
   than bun. Plan 81 is rewritten around the budget policy; measurements.md "Plan 81 planning" has the numbers.
3. Done (this commit): plan 81 stage 1. Several checkers start at 16 MB, doubling after segments under 16 files; gate
   missed on both sides by a little (corpora 9 / 14 at or below bun's peak, was 4, +12% wall; tail +14% wall, 96%
   faster than bun, -22.5% peak). measurements.md "Plan 81 stage 1"; the insights decides.
4. Done (this commit): plan 81 stage 2. Namespace imports share the module's members, mapper lists a cache: effect's
   K1 arena -22%, the tail -0.4% wall. A recycle's fixed cost is ~1 ms; the tail pays for re-derivation, which only
   the policy moves (one more frontier point, measurements.md "Plan 81 stage 2"). Stage 3 dropped as a tail lever.
5. Done (this commit): deferred bodies' invariants. The body walk's outputs (`end_flow_`, returns, yields,
   `contains_this_`, `return_flow_`, `first_super_call_`, `ts_assigned_`, ...) behind materializing accessors; two raw
   readers found (`declaring_constructor`'s `return_flow`, safe only because JS is eager). Gated runs parse the bodies
   for syntax errors instead of restarting: gated sweep projects -25% wall. Every oracle unchanged.
6. Done (this commit): hacks audit. Eleven checker guesses replaced by tsc's rule (suite 13789 / 13826, 0 lost, every
   oracle unchanged, instructions flat or better); two entries were wrong about their cause (Awaited, intersection
   caching). Plans 82 (optional types carry undefined) and 83 (re-entrant member resolution) written; hacks.md 679 ->
   644 lines. Triage: the harness's leak list (157 tests) is deferred-body noise.
7. Plan 77's fan-out if its rate was 70-90%, else **checker balance** (plan 72 "Lead": a cost known before checking,
   or file-granularity work stealing with K-independent output; measure on the 4-vCPU box with `--checkers 4`).
8. **Perf session**: pays back what entries 3-4 cost; perf_leads.md "Deferred bodies" (re-materialization, ~2% of
   vscode) first. Ends with `scripts/bench.sh --runs 3 --bun` and plan 67's "Where it stands" table refreshed.
9. **Triage.**
10. **Insights.** report.md: plan 77's number, whether the defaults' memory gap to bun moved and why.

After the insights entry (for the next window): plan 58 + plan 66 stage 5 when 7.1.0 is out (expected after
2026-11-24), webpack's serial declare, plan 76 only if the human reopens it.
