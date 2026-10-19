# Todo
Written by the insights of 2026-10-19 (after the twenty-second window); `vision.md` "Course correction" says why.
**Ten sessions, then insights.** Delete entries as they complete (leave a one-line "done" note with the numbers). A
plan that overruns its entry takes its extra sessions out of this budget: when 10 sessions have run (count the commits
since this file was written), the next session is insights, whatever is left here. New work goes into plans or
triage.md, not ahead of the insights entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
(every oracle; it exceeds the 10-minute foreground cap: run it in the background with a bounded wait) after checker
changes, `scripts/instrcount.sh`, the memory numbers in measurements.md after memory-relevant changes, an empty `git
worktree list`, /tmp scratch deleted (CLAUDE.md), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry
starts (`npm view typescript dist-tags`: on 2026-10-19 `latest` was 7.0.2 and `next` 7.1.0-dev.20261019.1), do plan
58 (the re-pin) first, then plan 66 stage 5**, and push the rest down. `claude-stop` is gitignored: never commit it.

Starting point (04b3ef35 + this commit): suite 13928 / 13963 (dts 1880 / 1902), corpora 0 / 0 but vue 0 / 2, DT
8595 / 8595, `$ExpectType` 68640 / 68640, flagflip 18 / 7 / 2, scenarios 210 / 0, source sweep 5225 / 5242, `-b`
sweep 400 / 416, clidiff 3823 / 3831, dtsdiff 1325 / 1350, kdiff 2, recycle 4. Perf: the 12-core table in
measurements.md; instructions `test/instrcount.txt`.

Rules of the window (perf-heavy, plan 86): output byte-identical at every K, budget and thread count; K1 changes are
measured by instructions and cachegrind before wall, and cost at most ~3% wall if they're not perf changes; at the
defaults stay faster than bun on every corpus and the sweep's tail; every perf claim at the defaults reports wall,
CPU (user + sys) and peak RSS. Instruction counts change with PGO's inlining: after entry 1, compare against the
re-saved baseline only.

1. Done (c564922b): PGO + LTO ships as `scripts/pgobuild.sh` (`pgo/tttt`). Callgrind is -11.5% to -16.9% on every
   corpus, -15.2% on the untrained vscode. instrcount and bench were re-saved (measurements.md "PGO + LTO shipped").
   The 12-core rerun ran on the human's laptop the same day (measurements.md "The 12-core box with PGO + LTO"):
   tttt has the best wall time on every corpus at K12, K24 is not worth its cost (the cap is confirmed), and bun's
   peak is still lower on 10 / 14.
2. **Plan 86 stage 4's gate** (one session, measurement only): how often `ty_compare_tie()` falls back to type ids
   on every corpus and the sweep's biggest projects. Write the number and the go/stop into plan 86. It decides
   whether a shared concurrent type graph is compatible with byte-identical output, so it comes before stage 2's
   design choices that would differ under one.
3. **Plan 86 stage 2, session 1**: cachegrind (`--cache-sim=yes`) on drizzle and tsc at K1, rank the largest sources
   of LL and I1 misses by function and by data structure; write the ranked lever list into plan 86 stage 2, then
   take the top one.
4. **Plan 87's planning session** (`-b` reads its references' emitted declarations): research tsgo's behaviour,
   prototype on the `-b` sweep, delete-first (the builder window, the reference-source private/internal rules), gate
   as the stub says. Write the real plan or close it.
5. **Real-code fan-out** (3 agents, worktrees, disjoint lists): the source sweep's 17, clidiff's 8, dtsdiff's 25
   (minus tsgo's own split), the `-b` sweep's non-plan-87 leftovers (`-p` bugs first: hono request.ts, ghostfolio,
   atproto expect-type, base-ui, babel TS2590), perfsweep's worst types ratio (lightweight-charts hlc-area, 1.63).
   Skip whatever plan 87 will absorb if entry 4 said go.
6. **Plan 86 stage 2, session 2**: the next lever from entry 3's list.
7. **Plan 87 stage 1** if entry 4 said go (its first stage, as the plan sizes it); otherwise **vue 0 / 2**: tsc's
   in-flight-free member resolution (bugs/sourcesweep_checker.md, the vue bullet), a planning-and-prototype session:
   does dropping tttt's re-entrancy guard for this shape (members_reentered_unguarded()) to let the instantiation
   depth limit decide, like tsc, hold on every oracle?
8. **Plan 86 stage 2, session 3** (or stage 3's first session, the loader pre-scan, if stage 2's list has run dry:
   measure on vscode, webpack and effect with `tttt --time`).
9. **Triage.** Drain triage.md (incl. the SIGBUS item), condense measurements.md (2000+ lines: keep the tables later
   sessions compare against, move superseded sections to git history), audit hacks.md for entries whose fix landed.
10. **Insights.** report.md: PGO + LTO shipped and what it bought (the human's 12-core rerun if it came), plan 86's
    gate number, plan 87's go/no-go, the fan-out's numbers. Whether 7.1.0 stable is out (plan 58). Whether the
    human answered on the many-core checker policy (their 12-core numbers with the CPU column).
    **And bun canary** (oven-sh/bun#45800, measurements.md "bun canary with oven-sh/bun#45800") beats tttt's wall at
    the defaults on vscode, drizzle, excalidraw and nest, which breaks the window's "faster than bun on every corpus"
    rule. Re-weigh plan 86 against it: the PR shipped a heavy-first queue without barriers and shared results (stage 4's
    territory). Its single-threaded speed and its CPU are still well behind tttt's.

After the insights entry (for the next window): plan 86 stage 2 continues in perf slots, stage 3 (front end) after
it; plan 86 stage 4's design session if the gate said go; plan 87 stages if go; a fourth refresh chosen by a new rule
(candidates: monorepos with `paths`-heavy configs, Deno/Bun-style `allowImportingTsExtensions` projects, Vue/Svelte
tooling with `.d.ts`-generating steps); the many-core checker-count and budget policy when the human's numbers come;
plan 58 + plan 66 stage 5 when 7.1.0 is out (expected after 2026-11-24); plan 76 only if the human reopens it.
