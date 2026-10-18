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
8595 / 8595, `$ExpectType` 68639 / 68640, flagflip 18 / 8 / 3, scenarios 210 / 0, source sweep 3559 / 3566, dtsdiff
1193 / 1233, clidiff `--emit` 369 / 396. Memory and wall against bun: plan 67 "Where it stands" (2026-10-18);
instructions: `test/instrcount.txt`.

Rules of the window (vision.md "Memory target"): a memory change costs at most ~3% wall at K1; at the defaults it must
stay faster than bun on every corpus and the sweep's tail. Output byte-identical at every K and budget.

1. Done (2026-10-18): deferred bodies leak nothing at a drop (strings sealed into the arena, the walk's lists the
   AST's; vscode K1 drop leak 12.6 MB -> ~60 KB) and a drop's re-materialization makes the demanded body alone
   (vscode defaults 0.80 -> 0.16 s CPU re-materializing, 6.17 -> 6.10 s, peak 1253 -> 1221 MB). `--checker-memory 0`
   recycles after every file again (the -m 0 oracles weren't recycling small programs): recycle oracle 4 / 3566,
   pre-existing (measurements.md).
2. Done (2026-10-18): plan 82 stage 1. ~140 sites inventoried; tttt already mixes both representations, safe only
   because the adds are idempotent. A one-line flip loses 36 tests and adds 20 corpus diagnostics, all in predicted
   classes. Cost: +0.1% types, effect +4% / ~+1% wall. Stage 2 sized at one session, stage 3 (tuples, parameters) at
   one more.
3. Done (2026-10-18): plan 82 stage 2. Properties store their undefined (missing under exact), every reader add and
   the intersection hacks deleted, `TTTT_CHECK_OPTIONALITY` silent before deletion. Suite 13795 / 13832 (+3 own
   repro tests, +1 tsc test), every oracle at the starting point except the source sweep 3558 / 3566: tiptap/demos'
   predicted TS2590, a pre-existing JSX tag type bug that matched tsc by luck before (bugs/sourcesweep_checker.md).
4. Done (2026-10-18): plan 83. The re-entry came from asking a mapped type's genericity, keys and MappedInfo of its
   expansion (which relates the `as` clause): those now come from the declaration (`ty_generic_mapped()`, an
   instantiation expands from its declaration's info instantiated, not the generic target's expansion). The `as`
   clause kind is tsc's relation alone (shape reading and the `keyof` text test deleted, hacks.md entry gone);
   `never_index_read` narrowed. Suite 13797 / 13834 (+1 tsc test, +2 own), every oracle at the starting point,
   instructions flat (documentation.md "Members asked for while they are resolved").
5. Done (2026-10-18): plan 84. The loader records each file's first package id and what it reached in order;
   `program_collect_files()` (tsgo's collectFiles) picks representatives depth-first, a redirect and what only it
   reaches are excluded (not bound, not reported). HACK `sources_by_package_id` and `program_share_redirect_ref()`
   gone; type references and JS/JSON resolutions give ids too. effect through a symlinked checkout byte-identical
   to tsc (was ~25 diagnostics); base-ui's 10 extra files gone. Suite 13798 / 13835 (+1 own), every oracle at the
   starting point (recycle 3 / 3566, was 4), instructions flat. Worktrees with symlinked corpora now match tsc.
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
2026-11-24); plan 82 stage 3 (tuples and parameters, one session), plus stage 2's remainder if any; plan 76 only if the human reopens it.
