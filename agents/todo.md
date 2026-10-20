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

1. ~~Plan 86 stage 4: design session~~ done 2026-10-20: **go, staged, ~25 sessions** (plan 86 "The design"; census
   in `86_stage4_census.md`). Duplication at K12 is 1.40-1.68x K1's check work serialized, a frozen seed can't remove
   it (only concurrent sharing can), and the id test in is_deeply_nested_as() is observable (random ids change zod,
   drizzle, effect), so it gets per-worker first-touch stamps if global ids prove observable.
2. ~~Plan 86 stage 4-0: the taint prototype, gate G0~~ done 2026-10-20: **go, with three conditions** (plan 86
   "Gate G0 result"; `scripts/taintstats.sh`). Under the G0 rule the four hot kinds are 98.9-100% clean in entries and
   90.2-100% in time (strict flow tainting would be a no-go: 39-76% of time). Cleandiff leaves two named mechanisms
   for 4b (the distributed twin made on demand, an instance's alias missing from its key), a residue of <= 0.03% per
   kind, and 46 drizzle prop types that only the lenient flow rule admits (root-cause before 4d).
3. ~~Plan 87 stage 1~~ done 2026-10-20: `-b` reads emitted declarations only, the sources model deleted; `-b`
   sweep 409 / 416 byte-identical to the flag's run, scenarios 221 / 0, oracles unchanged. Re-triage: apollo
   cacheOverride was a `-p` alias-re-entry bug (fixed), the rest in bugs/sourcesweep_build.md.
4. ~~Perf: costliest file first, then the front end measured~~ done 2026-10-20: each checker starts with its
   share's biggest file (TypeScript6 -6% wall at K4, type-fest -2%, the rest flat on 4 cores, output byte-identical;
   type-fest's costly files are tiny, so no syntactic cost model helps; kdiff 2 -> 4, sequelize's tsgo-shared TS7022). Front end: the loader's serial 279 ms on
   vscode, then declare + link's 138 ms, so stage 3 starts with the import pre-scan (measurements.md "Costliest file
   first and the front end's split"). The 12-core command is in report.md.
5. ~~Real code~~ done 2026-10-20 (three agents): apollo signatures/modern (never-reduction's HasNeverType), atproto
   lex-schema -p and -b (apparently-any intersections in mapped types, NoSupertypeReduction by type), ghostfolio
   TS4058 (combined import symbols), react-three-fiber's nested mapped rest, two mutation seeds' fixes (accessor-joined
   property symbols, comparable heads, union signatures' last overload). Source sweep 5235 / 5242, `-b` 411 / 416,
   dtsdiff 1330 / 1350, suite 13955 / 13989, the rest unchanged. **Plan 88** (re-entrancy below resolution barriers,
   `scripts/reentrytrace.py`): no general mechanism this session, staged A-E; vue 0 / 2 is stage E (won't fix unless
   members resolve in tsc's order), better-auth stages B-D. A mapped-target cut cost mikro-orm 3.7x types mid-session
   (narrowed back, triage has the relation bug behind it).
6. **Triage.** Drain triage.md and condense whatever the window grew (measurements.md, plan 86).
7. **Insights.** report.md: the stage 4 gate's verdict and what it means for the next windows, `-b`'s single model,
   costliest-first's numbers. Check whether 7.1.0 stable is out (plan 58) and whether the human ran the 12-core
   command.

Not in this window (candidates for the next, which the insights session reorders): plan 88 stages A-B (one barrier
stack, barrier termination instead of "once" flags; weigh against plan 86 stage 4's in-flight markers); plan 86 stages 4a-4c up to gate
G1 (if G0 says go); stage 3's chosen piece; plan 87 stage 2 (share declaration files across `-b` programs); stage 2's leftovers in
perf slots (outlining instantiate_inner's and the relation switch's cold arms, the big-union walks, the intern table
footprint); the snapshot recycle (perf_leads.md "Shared warm state") if stage 4 is no-go; a fourth refresh chosen by
a new rule (monorepos with `paths`-heavy configs, `allowImportingTsExtensions` projects, Vue/Svelte tooling); plan 58
and plan 66 stage 5 when 7.1.0 is out (expected after 2026-11-24); plan 76 only if the human reopens it.
