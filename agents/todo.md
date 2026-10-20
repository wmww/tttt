# Todo
Written by the insights of 2026-10-20 (after the twenty-third window). `vision.md`'s "Course correction" explains
why. Delete entries as they complete, leaving a one-line "done" note with the numbers. New work goes into plans or
triage.md, never ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
after checker changes (it exceeds the 10-minute foreground cap: run it in the background with a bounded wait),
`scripts/instrcount.sh`, the memory numbers in measurements.md after memory-relevant changes, an empty `git worktree
list`, /tmp scratch deleted (CLAUDE.md), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry starts
(`npm view typescript dist-tags`: on 2026-10-20 `latest` was 7.0.2 and `next` 7.1.0-dev.20261019.1), do plan 58 (the
re-pin) first, then plan 66 stage 5**, and push the rest down. `claude-stop` is gitignored: never commit it.

Starting point (9f9cb96b + this commit): suite 13944 / 13978 (dts 1885 / 1907), corpora 0 / 0 but vue 0 / 2, DT
8595 / 8595, `$ExpectType` 68640 / 68640, flagflip 18 / 3 / 2, source sweep 5234 / 5242, `-b` sweep 402 / 416,
clidiff 3824 / 3831, dtsdiff 1329 / 1350, kdiff 2, recycle 4. Perf: the 12-core tables in measurements.md ("The
12-core box with PGO + LTO", "bun canary"); instructions `test/instrcount.txt`.

Rules of the window: the output stays byte-identical at every K, budget and thread count. K1 changes are measured
by instructions and cachegrind before wall, and may cost at most ~3% wall unless they are perf changes. Every perf
claim at the defaults reports wall, CPU (user + sys) and peak RSS. **This box has 4 vCPUs**: many-core claims are
priced by per-file time sums (`TTTT_FILE_TIMES=1`) under real parallel runs at K4, plus a command the human can run
on the 12-core box (put it in report.md, as 2026-10-19's ask did).

1. **Plan 86 stage 4: design session** (planning; plan 86 "Order" and stage 4). The shared concurrent type graph:
   the lazily written state census (`Ty` fields, `Checker` tables, LazySlots) split into publish-once and scratch,
   which re-estimates the build-out; what bun #45800 shares between workers and how; the table design (lock-free open
   addressing, resize); the replacement for is_deeply_nested_as()'s "newer id" test (gate condition 2); the memory
   story (no recycling, or shared graph plus scratch recycling); how diagnostics stay K-independent. It writes the
   prototype's scope and its go/no-go into the plan. It may say no: then the plan says why, and entry 2 becomes the
   fallback (plan 86 "Order").
2. **Plan 86 stage 4: prototype + gate** (one session, a second only if the first shows the gate is in reach). The
   prototype is drizzle at K4 here, measured against today's duplication by per-file sums, with kdiff and
   checkerdiff byte-identical. It can be behind a flag and incomplete, but not wrong. The result goes into the plan
   with numbers. If go, the next insights schedules the build-out window. If no-go, the prototype is deleted.
3. **Plan 87 stage 1**: `-b` reads emitted declarations only. Drop `TTTT_BUILD_EMITTED`, delete the sources model
   (the plan's list), rewrite documentation.md's "Project references", and hold scenarios, both `-b` corpora and the
   `-b` sweep (>= 406 / 416) at every step. Two `-b` models must not live on into another window.
4. **Perf: costliest file first, then the front end measured** (plan 86 "Order", stage 3). Start each checker's
   share with its costliest file (tsc's checker.ts and type-fest's biggest file are the critical path; bun canary's
   heavy-first queue is the same idea). Size is the first cost model to try; the output must not change. Then
   `tttt --time` and `TTTT_LOAD_POOL_STATS=1` on vscode, webpack and effect at `--threads 4` and 1, to say which
   stage 3 piece (loader pre-scan, parallel declare/link, parser) comes first. Write the 12-core command for the
   human.
5. **Real code**: vue 0 / 2 (the re-entrancy guard question, bugs/sourcesweep_checker.md) and whatever the source
   sweep, `-b` sweep and dtsdiff leftovers rank highest (the last fan-out's verdicts in bugs/). Prefer a mechanism
   over a project.
6. **Triage.** Drain triage.md and condense whatever the window grew (measurements.md, plan 86).
7. **Insights.** report.md: the stage 4 gate's verdict and what it means for the next windows, `-b`'s single model,
   costliest-first's numbers. Check whether 7.1.0 stable is out (plan 58) and whether the human ran the 12-core
   command.

Not in this window (candidates for the next, which the insights session reorders): plan 86 stage 4's build-out (if
go); stage 3's chosen piece; plan 87 stage 2 (share declaration files across `-b` programs); stage 2's leftovers in
perf slots (outlining instantiate_inner's and the relation switch's cold arms, the big-union walks, the intern table
footprint); the snapshot recycle (perf_leads.md "Shared warm state") if stage 4 is no-go; a fourth refresh chosen by
a new rule (monorepos with `paths`-heavy configs, `allowImportingTsExtensions` projects, Vue/Svelte tooling); plan 58
and plan 66 stage 5 when 7.1.0 is out (expected after 2026-11-24); plan 76 only if the human reopens it.
