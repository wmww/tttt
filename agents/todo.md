# Todo
Written by the insights of 2026-10-15 (after the sixteenth window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle) after checker changes, `scripts/instrcount.sh`, an empty `git worktree list` and /tmp scratch deleted
(CLAUDE.md, the disk filled once), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry starts
(`npm view typescript dist-tags`: on 2026-10-15 `next` was `7.1.0-dev.20261015.1`), do plan 58 (the re-pin) first,
then plan 66 stage 5 (a fan-out on the new failures)**, and push the rest down (they take two entries' places).

Starting point (44fedbe0 + this commit): suite **13660 / 13672** (tsgo's 13556 / 13568 + 104 own tests), corpora
0 / 0 everywhere but vue 0 / 2, DT sweep 8594 / 8595, `$ExpectType` 68639 / 68640, flagflip 18 / 6 / 3, scenarios
210 / 0 failed, source sweep 3344 / 3363 (sample 814 / 822), kdiff 1 / 3363, perfsweep median ~0.2x, worst types
ratio 1.62, dts 1345 / 1408, dtsdiff 940 / 1233 projects (74466 / 76294 files). Instructions: measurements.md
"Triage (2026-10-15, todo entry 10)".

**This window: ship the two half-built things.** Memory (plan 67) and `.d.ts` emit (plan 64) to user-facing
completion, then pay down what the suite push left (plan 71), plan core scaling (plan 72). **The suite's last 12 are
off the table** (vision.md): no session works on them unless real code shows the same mechanism.

1. Done (plan 67 stage 1): `--checker-memory` (default 256 MB, at least 128 per checker). vscode K1 4190 -> 2575 MB,
   T4 4786 -> 2858 at +2-4% wall; type-fest K1 +16% wall and +21% instructions; oracle `recycle` 4 / 3363.
2. Done (plan 64 stage 4): the CLI writes `.d.ts` where the options only emit declarations (`-b` too), dts counts in
   the headline (suite 13611 / 13677, dts 1358 / 1409), `clidiff.py --emit` 164 + 145 (maps only) / 396, dtsdiff
   through the CLI 942 / 1233. Every oracle script passes `--noEmit` / `--no-write` now (CLAUDE.md).
3. Done (plan 67 stage 2 item 1): scopes in their own arena, more walk-only scopes released: vscode after names
   2281 -> 2255 MB (the ~-100 MB estimate was arenasites counting popped scopes, fixed). The binder is < 25% of the
   program and every binder diet together < 60 MB: the AST (1.74 GB) is the lever (plan 67 stage 2).
4. Done (plan 67 stage 2 items 2-3): PortItem, PortStatement, Statement, ObjectExprItem diets; vscode after names
   2255 -> 2191 MB, K1 peak 2610 -> 2549. Struct work is exhausted (< 120 MB left); measurements.md has vscode against
   bun-1 (program 2191 vs ~1300 MB, check ~360 vs ~80) and plan 67 stage 2 the compact AST recommendation.
5. Done (plan 64 stage 5): declarationMap. dtsdiff maps 19884 / 20435 byte-identical (385 / 509 projects), dtsdiff
   932 / 1233 projects with maps compared (942 without), clidiff `--emit` 309 / 396 (164); suite 13625 / 13675 (the
   dts component compares `.d.ts.map` sections: +13). Stage 6 (JS sources, 82 projects / 27 blocked by it alone):
   yes, after entry 6's type text.
6. Done (fan-out, 4 agents, ~60 picks): dtsdiff 932 -> **1077 / 1233** projects (type text 197 -> ~45 projects;
   JS sources are 77 of the 156 left), source sweep 3344 -> **3352 / 3363** (0 lost), DT **8595 / 8595**, suite
   13625 / 13675 -> 13698 / 13735 (dts 1440 / 1465, +60 own tests), vue 0 / 2 argued plan-sized (bugs/sourcesweep_checker.md). Leftovers in
   bugs/dts_emit.md and bugs/sourcesweep_checker.md.
7. Done (plan 71): hacks.md 963 -> 573 lines (notes to documentation.md, stale entries gone, the rest debt with its
   correct rule and oracle). Four artifact hacks removed (OBJECT_WIDENED_COPY, merge_detached, the literal fresh twin,
   infer_from_objects' shortcut -> tsc's rule), 2 tests argued in tsgo_divergences.md; suite 13697 / 13739. Member
   symbols: the symbols baseline's copy gone, the rest is plan 73. Two-pass relations: ~0.5% of relation work, tsc
   does it too in most callers: plan 74 (half a session, deletes failed_log). intersection_source's tsc rule found
   but +8% instructions (hacks.md).
8. Done (plan 72, planning): parallel declare/link is a "not now" (vscode 150 ms, ~8% at 12 cores; webpack 48).
   The biggest serial part is the loader thread's non-parse work, ~350 ms on vscode (194k stats for 34k paths, no
   exists cache; imports re-resolved): plan 72 stage 1. Checker imbalance is a lead (1.3-3x the mean at K12).
   `TTTT_RESOLVE_TIMES=1` times resolve's steps.
9. Done (perf session): scaleprobe `--emit` found six quadratic emit paths (fixed, e.g. 57 -> 0.09 s) and two in
   merged overloads; the gated declaration walk stops at a sure error; recycling's warm-up is redone shared work (a
   tail rule tried, dropped). Instructions flat or down (tsc -1.7%, effect -2.0%, drizzle -1.2%); bench.sh `--bun`:
   tttt 1.3-3x faster than bun, 1.6-2.3x its memory at the defaults (plan 67 "Where it stands").
10. **Triage.** Then create `claude-stop` in the repo root: the human asked (triage.md, 2026-10-15) to stop the
    agent loop after the next triage session. Leave the insights entry below for whoever restarts the loop.
11. **Insights.**

After the insights entry (for the next window): plan 72 stage 1 (the loader's serial work, one session), plans 73 and 74 (small cleanups), plan 64 stage 6 (chosen: JS sources), plan 58 +
plan 66 stage 5 if not done, the compact AST question (plan 67 "Not doing").
