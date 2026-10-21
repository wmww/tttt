# Todo
Rewritten 2026-10-23 after the second coordinator session (vision.md "Course correction (2026-10-23)"). Work runs
as a coordinator with parallel workers in worktrees (CLAUDE.md "Workflow"); entries are sized in sessions of one
worker. Delete entries as they complete, leaving a one-line "done" note with the numbers. New work goes into plans or
triage.md. Every entry ends with `scripts/fasttest.sh -s`, the oracles its change can move (`scripts/oracles.py`
after checker changes, in the background with a bounded wait), `scripts/instrcount.sh`, an empty `git worktree list`
and deleted scratch (CLAUDE.md), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry starts (`npm view
typescript dist-tags`; on 2026-10-20 `latest` was 7.0.2 and `beta` 7.1.0-beta), do plan 58 (the re-pin) first, then
plan 66 stage 5**, and push the rest down. The beta is not a re-pin target. `claude-stop` is gitignored: never commit it.

Starting point (ai-main at 8d693aeb5 / 126fd38fe, merger's runs of 2026-10-21/22; measurements.md "Oracle totals
(2026-10-23)"): suite 13962 / 13996, corpora 0 / 0 but vue 0 / 2, DT 8595 / 8595, `$ExpectType` 68640 / 68640, flagflip
18 / 3 / 2, source sweep sample 1272 / 1274, `-b` sweep 411 / 416, clidiff 3824 / 3831, dtsdiff 1327 / 1350, kdiff 3
(rxjs, sequelize x2; with the flag 8), recycle 3, perfsweep worst types ratio 1.40. Instructions `test/instrcount.txt`
(PGO: nest +6.8% since the last `--save`, a layout swing to recheck). `--shared-graph`: measurements.md "Plan 86 stage
4f: the end state".

**The next coordinator's whole briefing is `agents/plans/90_next_coordinator.md`** (it supersedes plan 89 except its
section 1, the workflow, and section 4, the lessons). Mandate (the human, 2026-10-22): the shared graph on by default,
economically. Its order:
1. **Residues** (plan 86 "Stage 4f: done"; one Opus worker each, disjoint files): C conditional/alias instance identity
   vs resolution (mikro-orm x3, drizzle seeds' query.ts(139,3) TS2322, effect's Schema.tst.ts(2936,39) cascade); V
   variance groups' side state (no project shows it now; rule recorded); W trpc/examples/nuxt, folo's renderer,
   react-navigation (object-literal identity keyed syntactically), the recheck's 3 drizzle files.
2. **Release criterion** (plan 89 section 3 item 4, restated in plan 90 section 3 item 2): full `scripts/oracles.py`
   plain and with the flag identical, seeded runs (`scripts/sharedgraph/seeds.sh`), 300 loaded drizzle + 100 effect
   runs, TSan zero groups at T4/T12, then **the human's laptop bench** (`scripts/bench.sh --runs 3 --bun PATH --tscrs
   PATH`, with and without `TTTT_SHARED_GRAPH=1`) decides the flip and the defaults' policy (checkers per core,
   `TTTT_GRAPH_BUDGET_MB`, memory up to ~3x bun for a clear win).
3. **After the flip**: delete the plain path (its state bytes, <0.1%), re-measure `test/instrcount.txt`, rewrite
   vision.md's perf bullet.
4. **Follow-ups named in plan 86, only after 1-3 and only with a measured payoff**: a history-independent order for
   deep-nesting cuts (K12 work gate 1.25x, relaxed from 1.15x: the cuts are history by tsc's own semantics), the
   narrower outer-inference event, the state bytes after the flip, union-order keys (KEY_UNION_PAIR tie-break by
   per-worker ids), the flag's remaining 2% K1 cost (one look-and-claim call), object literals' and JS function
   expressions' identity keyed syntactically, effect's per-instruction critical path (Schema.ts 2x slower with the flag).
5. Fillers: **plan 58 first if `npm view typescript dist-tags` shows 7.1.0 stable** (then plan 66 stage 5);
   bugs/sourcesweep_checker.md; the front end's laptop wall checks. Plan 58 still waits (on 2026-10-20 `latest` was
   7.0.2).

Rules of the window: the default path stays byte-identical and instruction-flat (~0.3%) until the flag flips; with the
flag, K1 byte-identical, K4/K12 give K1's output on every `-p` corpus. The gates are tooling now (scripts/sharedgraph/
README.md: `tsan.sh`, the recheck, `fingerprint.sh`, `seeds.sh`). A change that makes a flake rarer without a mechanism
is not a fix: write the mechanism down or keep looking.
