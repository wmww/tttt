# Todo
Written by the insights of 2026-10-18 (after the twenty-first window); `vision.md` "Course correction" says why. **Ten
sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A plan
that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle; it exceeds the 10-minute foreground cap: run it in the background with a bounded wait) after checker
changes, `scripts/instrcount.sh`, the memory numbers in measurements.md after memory-relevant changes (vscode,
effect, webpack, drizzle, type-fest: RSS after parse / after names / peak at K1 and at the defaults), an empty `git
worktree list`, /tmp scratch deleted (CLAUDE.md), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry
starts (`npm view typescript dist-tags`: on 2026-10-18 `latest` was 7.0.2 and `next` 7.1.0-dev.20261018.1), do plan
58 (the re-pin) first, then plan 66 stage 5**, and push the rest down. `claude-stop` is gitignored: never commit it.

Starting point (a7d8232b + this commit): suite 13833 / 13869 (dts 1864 / 1886), corpora 0 / 0 but vue 0 / 2, DT
8595 / 8595, `$ExpectType` 68639 / 68640, flagflip 18 / 8 / 3, scenarios 210 / 0, source sweep 3626 / 3634, clidiff
3612 / 3675 (54 text, 9 diagnostics), dtsdiff 1244 / 1274 (+-2 flicker), kdiff 2, recycle 5 / 3675. Perf: the
12-core table in measurements.md; the 4-vCPU bench in plan 67 "Where it stands"; instructions `test/instrcount.txt`.

Rules of the window: output byte-identical at every K, budget and thread count; K1 changes cost at most ~3% wall; at
the defaults stay faster than bun on every corpus and the sweep's tail, and now **report CPU (user + sys) next to wall
and RSS** for every perf claim at the defaults.

1. Done (2026-10-18): `scripts/bench.sh` and `perfsweep.py --defaults` report CPU; oversubscribed K tables in
   measurements.md "Checkers per physical core" (K24 vs K12: corpora +5-34% CPU, dokploy +71%, monkeytype +45%, peak
   +1-75%); default checkers now capped at the physical cores but every thread up to 4 (no change on 4 vCPUs, K12 on
   the 12-core box, unconfirmed: the human's command is in that section); dtsdiff deterministic at K1 (the flicker
   wasn't tttt's timing), K>1 emit is tsgo's own split, one ui K1 history case in bugs/dts_emit.md.
2. Done (2026-10-18): plan 85 **no-go, closed** (measurements.md "Plan 85's census"): declaration files'
   declarations are 2-6% of the re-derived CPU (vscode 15%); what multiplies is instantiations of the project's own
   code. On 4 vCPUs the defaults' extra CPU is mostly SMT inflation, then recycling. A snapshot-recycle lead for the
   thrashers in perf_leads.md.
3. Done (2026-10-18): plan 82 stage 3, the plan is complete and deleted (documentation.md "The missing type").
   Optional parameters and tuple elements carry their undefined, `ty_sig_param_type()` and `KEY_OPTIONAL_PARAM` are
   gone; every oracle unchanged (suite 13833, dtsdiff 1244, clidiff 3612, source sweep 3650 / 3658).
4. Done (2026-10-18): clidiff fan-out (3 agents, 37 picks, 0 conflicts kept, 0 lost): clidiff 3612 -> **3665** /
   3675 (text 54 -> 3: apollo vite, babel-traverse, tiptap/demos truncation; diagnostics 9 -> 7, written up in
   bugs/sourcesweep_checker.md), webpack's text matches; suite 13833 -> 13865, every other oracle unchanged,
   instructions flat (type-fest -0.9%).
5. Done (2026-10-18): perf session. Per-body re-materialization had already landed (ccb6129a). Types tail:
   date-fns/pkgs/docs 1.62 -> 0.64 (genericity of mapped types read from the declaration, tsc's
   isGenericMappedType; 681 more projects -56k types), worst now 1.54 on 1-3k-type projects; the K1 check-ratio tail
   was load noise (perf_leads.md "The types-ratio tail"); `variancetrace.py` prints types per measurement. Every
   oracle unchanged.
6. Done (2026-10-19): real-code tail fan-out (3 agents, ~37 picks, 0 conflicts, 0 duplicates). dtsdiff 1244 ->
   **1250** / 1274 (refresh 2's checkJs projects and vest nearly all match; what's left is tsgo's checker split or
   tsgo's own misnaming), source sweep 3650 -> **3654** / 3658 (arktype, knip, redux-toolkit, better-auth's TS2339
   fixed; left: pixijs binder merge unit, better-auth's recursive resolveCall, es-toolkit, monkeytype's 5M count, all
   with verdicts), clidiff 3665 -> **3669** (text 1, diagnostics 5), `$ExpectType` 68640 / 68640, flagflip 18 / 7 / 2,
   expando instantiation fixed (bug file deleted), recycle 5 -> 4; suite 13865 -> 13893, instructions flat. vue 0 / 2
   is plan-sized (tsc's in-flight-free member resolution, sourcesweep_checker.md).
7. Done (2026-10-19): third refresh, 23 repos (`tsc -b` solutions, decorators): day one `-p` 153 / 156 (98.1%),
   clidiff 153 / 156, dtsdiff 74 / 76; a new **`-b` oracle** (`sourcesweep.py --build`, in oracles.py): day one 382 /
   416 solutions, 394 after three config fixes; the rest in bugs/sourcesweep_build.md (the `-p` sweep never checked
   configs not named `tsconfig.json`). Source sweep 3807 / 3814.
8. **Refresh fixes + perf session**: the refresh's day-one mismatches by root cause (bugs/sourcesweep_build.md's
   22 solutions first: a fan-out fits, each first checks `-p` on the config), then `scripts/instrcount.sh`
   against the window's start and any flagged rise.
9. **Triage.**
10. **Insights.** report.md: plan 85's no-go (the census) and the defaults' CPU column (12-core: ask the human to rerun with
    entry 1's command), clidiff's number, the third refresh's day one. Whether the human answered on plan 76.

After the insights entry (for the next window): **plan 86 (multiples on many-core cold runs; the human asked for it 2026-10-19): stage 1 (PGO + LTO, one session, -8-9% measured) and the stage 4 gate session fit the next window, stage 2's sessions go in its perf slots**; plan 58 + plan 66 stage 5 when 7.1.0 is out (expected after
2026-11-24); the many-core checker-count and budget policy from the human's 12-core numbers (tsgo keeps 4 checkers;
at K12 duplication is as big as recycling); plan 76 only if the human reopens it.
