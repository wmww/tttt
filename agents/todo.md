# Todo
Written 2026-10-21 after the first coordinator day (vision.md "Course correction (2026-10-21)" explains why). Work runs
as a coordinator with parallel workers in worktrees (CLAUDE.md "Workflow"); entries are sized in sessions of one
worker. Delete entries as they complete, leaving a one-line "done" note with the numbers. New work goes into plans or
triage.md. Every entry ends with `scripts/fasttest.sh -s`, the oracles its change can move (`scripts/oracles.py`
after checker changes, in the background with a bounded wait), `scripts/instrcount.sh`, an empty `git worktree list`
and deleted scratch (CLAUDE.md), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry starts (`npm view
typescript dist-tags`; on 2026-10-20 `latest` was 7.0.2 and `beta` 7.1.0-beta), do plan 58 (the re-pin) first, then
plan 66 stage 5**, and push the rest down. The beta is not a re-pin target. `claude-stop` is gitignored: never commit it.

Starting point (b3a67c3e5): suite 13958 / 13992 (dts 1887 / 1909), corpora 0 / 0 but vue 0 / 2, DT 8595 / 8595,
`$ExpectType` 68640 / 68640, flagflip 18 / 3 / 2, source sweep 5235 / 5242 (2026-10-21 sampled a quarter: 1272 /
1274), `-b` sweep 411 / 416, clidiff 3824 / 3831, dtsdiff 1327 / 1350 (1330 on 2026-10-20, not looked at), kdiff 4,
recycle 4, perfsweep worst types ratio 1.40 (measurements.md "Oracle totals"). Instructions `test/instrcount.txt`.
`--shared-graph`: measurements.md "Plan 86 gate G2".

Rules of the window (plan 86 stage 4e): the default path stays byte-identical and instruction-flat until the flag
flips; with the flag, K1 is byte-identical and flat, K4/K12 give K1's output on every `-p` corpus, and the 20-run
tables (drizzle, zod, effect, excalidraw, nest at K12 T12; drizzle 300 loaded runs for anything touching variances or
waiting) hold. A change that makes a flake rarer without a mechanism is not a fix: write the mechanism down or keep
looking.

1. **4e items 1-2: narrow the variance lock, then the shared relation cache** (plan 86 "Stage 4e"; a worker is on
   these now). The lock first needs the mechanism: what one measurement reads from another in flight. Gate: drizzle's
   K12 wall at or below the defaults' with 300 / 300 loaded runs K1's output; excalidraw's K12 relation pairs within
   5% of K1's.
2. **The oracle set with `TTTT_SHARED_GRAPH=1`** (the env alias exists, b3a67c3e5): sweep, source sweep, `-b` sweep,
   `$ExpectType`, flagflip, clidiff, dtsdiff, scenarios, kdiff -k 1 vs 16 with the flag. Only the suite and the
   corpora have run with it. Each mismatch is a sharing bug or a history dependence: root-cause before item 5.
3. **4e item 3: memory** (3-4 sessions): each checker's arena split into a published and a private part, the private
   one recycled at today's budget points, the graph kept. Target vscode K12 peak within ~1.3x of today's defaults
   (1.4 GB), drizzle/effect within 1.2x. Item 4 (the redo's gaps: deferred-body materialization, leaked temporaries,
   retired checkers' memory) folds into it.
4. **The bench on the human's 12-core laptop**: `scripts/bench.sh --runs 3 --bun PATH --tscrs PATH` with and without
   `--shared-graph`, the CPU and RSS columns, and per-file sums at K1 / K12 on vscode and drizzle (a quiet box: this
   one's walls are load-bound). With it, stage 3's wall checks: `TTTT_LOAD_POOL_INLINE=1` on vscode at 12 threads
   (how many inline parses are of files queued < 1 ms before: decides the import pre-scan) and `tttt --time`'s
   front-end phases against measurements.md "The 12-core box at efb91998". The commands go in a brief for the
   human; the numbers into measurements.md.
5. **4e item 5: default on** (2-3 sessions) where the flag wins (threads > 1, programs big enough: the table from
   item 4), after items 2-3 are green; recycling's oracle (kdiff -m 0) replaced by item 3's; type-fest and effect
   (where the flag costs CPU) looked at; vision.md's "Perf and memory" bullet rewritten.
6. **Plan 58** when 7.1.0 is on npm (expected after 2026-11-24), then plan 66 stage 5's fan-out: ahead of everything.

Not in this window: stage 4f (determinism at breadth: the sweeps repeated with the flag, flakes root-caused), plan 87
stage 2 (share declaration files across `-b` programs), plan 88 stages C-E (C after B; D only with a
contextual-typing rework; E no), stage 2's leftovers in perf slots, a fourth refresh by a new rule (monorepos with
`paths`-heavy configs, `allowImportingTsExtensions` projects, Vue/Svelte tooling), bugs/sourcesweep_checker.md's
cheapest item, plan 76 only if the human reopens it.
