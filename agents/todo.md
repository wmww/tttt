# Todo
Written by the insights of 2026-10-19 (after the twenty-second window); `vision.md` "Course correction" says why.
**Cut short by the human on 2026-10-19 after entry 4: triage, then insights.** Delete entries as they complete
(leave a one-line "done" note with the numbers). New work goes into plans or triage.md, not ahead of the insights
entry. Every entry ends with `scripts/fasttest.sh -s`, `scripts/oracles.py`
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
2. Done: plan 86 stage 4's gate says **go, with two conditions** (plan 86 "Gate result"). Non-intrinsic id ties
   are 0.06% of vscode's; permuting them (`TTTT_ORDER_SEED`) changes no corpus, DT, `$ExpectType` or source sweep
   output and one suite test. The spread-of-a-union class got a structural key (suite +1). The other condition:
   is_deeply_nested_as()'s "newer id" test.
3. Done: plan 86 stage 2's ranking is in the plan ("The ranking"; `scripts/cgrank.py`). Instruction footprint about
   42% of the stall estimate, hash tables 10%/5%, Ty header reads 5%, fresh-memory writes mostly a cachegrind
   artifact. Taken: the SyntaxKind `!==` narrowing memo (tsc -2.3% instructions, -7% D1 misses);
   intern_lookup()/intern_insert() saved only instructions (drizzle -0.5%).
4. Done: plan 87 says **go** (plan 87 "Gate results"). The prototype is in the tree behind `TTTT_BUILD_EMITTED=1`:
   `-b` sweep 406 / 416 (400), scenarios 218 / 0, tsc-src and typescript-eslint clean and faster (wall 0.77x /
   0.87x, CPU and RSS down). Three `-p` outputs-mode bugs fixed on the way (resolution from a reference output's
   source, its specifier naming, a reference's `.json` root not emitted).
   Done anyway (2026-10-20, it had started before the cut reached this tree): the **real-code fan-out** (3 agents, 26
   picks, 0 lost). Source sweep 5225 -> **5234** / 5242 (none lost), `-b` sweep 400 -> 402, dtsdiff 1325 -> 1329,
   clidiff 3823 -> 3824, flagflip spurious 7 -> 3 (flagflip.py runs tttt at `--checkers 1` now, like tsc's
   `--singleThreaded` cache), perfsweep worst types ratio 1.63 -> 1.40, suite +14 (new cases). Cross-file merging by
   file symbol (pixijs, discordx, better-auth: the "architectural" binder verdict took one session). kdiff 2 /
   recycle 4 are all tsgo-shared dependences (tsgo_divergences.md). Left: hono TS2769, discordx http2 TS2344, sunmao
   TS2590, atproto TS2394, lynx-store, es-toolkit, monkeytype (verdicts in bugs/). Valgrind runs of the harness had
   failed on a "possibly lost" since 2026-10-17 (fixed). instrcount within 2% (not re-saved).
5. Done (triage, 2026-10-20): triage.md drained (SIGBUS on truncated sources fixed, pgobuild.sh's corrupt-.gcda
   race closed, the rest dismissed with reasons), measurements.md 2139 -> 408 lines, hacks.md audited. Suite 13944,
   corpora unchanged.
6. **Insights.** report.md: PGO + LTO shipped and what it bought (the human's 12-core rerun if it came), plan 86's
    gate number, plan 87's go, the fan-out's numbers. Whether 7.1.0 stable is out (plan 58). Whether the
    human answered on the many-core checker policy (their 12-core numbers with the CPU column).
    **And bun canary** (oven-sh/bun#45800, measurements.md "bun canary with oven-sh/bun#45800") beats tttt's wall at
    the defaults on vscode, drizzle, excalidraw and nest, which breaks the window's "faster than bun on every corpus"
    rule. Re-weigh plan 86 against it: the PR shipped a heavy-first queue without barriers and shared results (stage 4's
    territory). Its single-threaded speed and its CPU are still well behind tttt's.
    **The human asked (2026-10-19) to reorder for significant perf progress sooner**: the big wins from using many
    cores effectively (or whatever else moves perf by multiples) should land in the next windows, not after the rest
    of plan 86 stage 2. Same day on the 24-thread laptop, vscode at the defaults: tttt 2.50 s wall / 26 s CPU / 1.60
    GB, bun 1.4.3 2.48 / 47 / 2.16, bun canary 1.66 / 34 / 2.32; tttt `--checkers 24` 2.28 / 37 / 1.90. tttt keeps ~10
    cores busy, the canary ~20, and more checkers don't close it: the gap is duplication and scheduling (stage 4, or
    a cheaper first piece such as a heavy-first shared file queue). Stage 2 session 1 gained only ~2% (flat profile),
    so its 1.5-2x target is doubtful. Windows are now 1-5 normal sessions (3 by default), then triage + insights
    (CLAUDE.md).

Dropped from this window when it was cut short (candidates for the next, the insights session reorders them):
**plan 87 stage 1** (its prototype is in the tree behind `TTTT_BUILD_EMITTED=1`: don't leave two `-b` models for
long; the store becomes the only `-b`, the sources model deleted, documentation.md's "Project references"
rewritten); the `-b` sweep's leftovers after plan 87 stage 1 (the real-code fan-out itself ran, above); plan 86 stage 2 sessions 2-3 (instruction
footprint: outline instantiate_inner's and the relation switch's cold arms; or stage 3's loader pre-scan); vue 0 / 2
(the re-entrancy guard question, bugs/sourcesweep_checker.md).

Also for the next window: plan 86 stage 2 continues in perf slots, stage 3 (front end) after
it; plan 86 stage 4's design session if the gate said go; plan 87 stages if go; a fourth refresh chosen by a new rule
(candidates: monorepos with `paths`-heavy configs, Deno/Bun-style `allowImportingTsExtensions` projects, Vue/Svelte
tooling with `.d.ts`-generating steps); the many-core checker-count and budget policy when the human's numbers come;
plan 58 + plan 66 stage 5 when 7.1.0 is out (expected after 2026-11-24); plan 76 only if the human reopens it.
