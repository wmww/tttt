# Todo
Written by the insights of 2026-10-18 (after the twentieth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle; it exceeds the 10-minute foreground cap: run it in the background with a bounded wait) after checker
changes, `scripts/instrcount.sh`, the memory numbers in measurements.md after memory-relevant changes (vscode,
effect, webpack, drizzle, type-fest: RSS after parse / after names / peak at K1 and at the defaults), an empty `git
worktree list`, /tmp scratch deleted (CLAUDE.md), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry
starts (`npm view typescript dist-tags`: on 2026-10-18 `latest` was 7.0.2 and `next` 7.1.0-dev.20261017.1), do plan
58 (the re-pin) first, then plan 66 stage 5**, and push the rest down. `claude-stop` is gitignored: never commit it.

Starting point (40afe12a + this commit): suite 13792 / 13829 (dts 1837 / 1860), corpora 0 / 0 but vue 0 / 2, DT
8595 / 8595, `$ExpectType` 68639 / 68640, flagflip 18 / 6 / 3, scenarios 210 / 0, source sweep 3559 / 3566, dtsdiff
1193 / 1233, clidiff `--emit` 369 / 396. Memory and wall against bun: plan 67 "Where it stands" (2026-10-18);
instructions: `test/instrcount.txt`.

Rules of the window (vision.md "Memory target"): a memory change costs at most ~3% wall at K1; at the defaults it must
stay faster than bun on every corpus and the sweep's tail. Output byte-identical at every K and budget.

1. **Deferred bodies: the drop leak, then per-body re-materialization** (hacks.md "Deferred bodies", perf_leads.md
   "Deferred bodies"). A dropped materialization leaks what its parse and walk malloc'd (~8 MB per round of vscode's
   bodies): move it into the body's arena or free it at the drop, valgrind and `TTTT_AST_POISON` clean. Then a
   re-materialization parses and walks only the demanded body when its first materialization's cross-body writes
   survive the drop (check `deferred_bodies_undo()`'s log first; stop and write it up if they don't). Gate: vscode
   defaults' wall and peak, every oracle unchanged.
2. **Plan 82 stage 1** (optional types carry their undefined): the investigation; it ends with stage 2 sized in the
   plan.
3. **Plan 82 stage 2**: the migration, if stage 1 sized it at one session. If not, do as much as fits with the
   cross-check mode keeping the output unchanged, and put the remainder after the insights entry.
4. **Plan 83** (re-entrant member resolution): fix, or a sized plan.
5. **Plan 84** (tsgo's two-phase load): the package-id HACK deleted.
6. **Declaration emit tail by fan-out** (bugs/dts_emit.md): up to four Opus sub-agents in worktrees, one dtsdiff /
   clidiff `--emit` / dtsstats bucket each, disjoint, 0 lost at every merge (vision.md "Fan-out merge discipline").
   Goal: dtsdiff >= 1210 / 1233; the uninvestigated buckets written up either way.
7. **Second fresh-repository refresh** (plan 77's recipe, vision.md "Refresh the breadth oracles"): ~25 repositories
   chosen by a new rule: libraries that emit declarations (`declaration`/`emitDeclarationOnly` configs, feeding
   dtsdiff) and JavaScript projects with `checkJs`. Report the day-one match rate on both the source sweep and dtsdiff
   before fixing anything, then fix what fits.
8. **Perf session**: webpack's serial declare (plan 72 "Declare + link"), the checker count for a large shared warm
   state (perf_leads.md: dokploy, inbox-zero; must not lose a corpus against bun), instructions; `scripts/bench.sh
   --runs 3 --bun` into plan 67 "Where it stands".
9. **Triage.**
10. **Insights.** report.md: did the debt plans delete their hacks, the refresh's two day-one numbers, whether the
    human's 12-core numbers arrived (vision.md item 22 waits on them).

After the insights entry (for the next window): plan 58 + plan 66 stage 5 when 7.1.0 is out (expected after
2026-11-24); plan 82's remainder if any; plan 76 only if the human reopens it.
