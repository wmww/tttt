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
2. **Plan 81 planning**: the checker's density census against tsgo (and bun's totals), with its gate. Rewrites plan 81
   into stages or closes it.
3. **Plan 81 stage 1** (or, if declined, a planning session reopening plan 67's rejected shared frozen declared layer
   with plan 81's duplication numbers).
4. **Plan 81 stage 2** (or that layer's prototype gate).
5. **Debt: deferred bodies' invariants** (hacks.md "Deferred bodies"): the body walk's outputs (`end_flow`, returns,
   yields, `contains_this`, ...) behind accessors so the compiler finds every reader, as the entry's "Correct:" says;
   whatever else in that entry is a session's fix. Delete or shrink hacks.md entries as they go.
6. **Debt: the hacks audit** (plan 71's method): hacks.md grew 573 -> 677 lines over two windows. Pick the entries that
   are guesses on the checker side first (the JS emit guesses are acceptable where byte equality is the oracle), fix
   what is a session, write plans for the rest.
7. Plan 77's fan-out if its rate was 70-90%, else **checker balance** (plan 72 "Lead": a cost known before checking,
   or file-granularity work stealing with K-independent output; measure on the 4-vCPU box with `--checkers 4`).
8. **Perf session**: pays back what entries 3-4 cost; perf_leads.md "Deferred bodies" (re-materialization, ~2% of
   vscode) first. Ends with `scripts/bench.sh --runs 3 --bun` and plan 67's "Where it stands" table refreshed.
9. **Triage.**
10. **Insights.** report.md: plan 77's number, whether the defaults' memory gap to bun moved and why.

After the insights entry (for the next window): plan 58 + plan 66 stage 5 when 7.1.0 is out (expected after
2026-11-24), webpack's serial declare, plan 76 only if the human reopens it.
