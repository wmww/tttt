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
4. **Plan 67 stage 2**, second session: plan 63 stage 1b items by MB per effort; write vscode's breakdown next to
   bun's so the next insights can decide on the compact AST.
5. **Plan 64 stage 5**: declarationMap; end by deciding stage 6 (JS sources) with the count the plan asks for.
6. **Real code**: the full source sweep's 19 projects, dtsdiff's checker-type buckets ("type text": fresh vs regular
   narrowed literals, mapped-type key order, `import()` specifiers), vue 0 / 2, DT's last one. Fan out if the buckets
   are disjoint (vision.md "Fan-out merge discipline").
7. **Plan 71**: the hacks.md audit (908 lines -> ~600; the member-symbol copies; one reporting relation run?).
8. **Plan 72**: planning core scaling (serial remainder after recycling, parallel declare/link: plan or a "no").
9. **Perf session**: instructions after this window's features (recycling's warm-ups, emit's cost under `--noEmit`
   must be zero), perfsweep, scaleprobe on emit, bench.sh against tsgo and bun-1.4.3 (plan 67's table refreshed).
10. **Triage.**
11. **Insights.**

After the insights entry (for the next window): what plan 72 decides, plan 64 stage 6 if chosen, plan 58 + plan 66
stage 5 if not done, the compact AST question (plan 67 "Not doing").
