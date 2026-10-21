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

**Superseded on 2026-10-21 evening by `agents/plans/89_next_coordinator.md`**, the next coordinator's whole briefing (the
human's ruling: correctness by construction, not by catching races). Its order:
1. Plan 86 stage 4f design session: the publish-when-clean rule enforced for lazy fields on shared types (overlay vs
   immutable shared types with publication at quiescence), costed on the real code; first task: the variance lock
   removed with first-touch stamps on under the flag (plan 86 4e item 1's late finding), drizzle/effect K12 wall against
   the defaults, the 20-run tables.
2. The build-out by parallel workers; the lock, the redo and the lib warm-up deleted; TSan clean with the flag.
3. Gates as tooling: fingerprint-at-publish oracle, seeded scheduler, TSan on the corpora, the recheck tool.
4. Default-on only after plan 89 §3 item 4's release criterion, including the human's laptop bench with and without
   `TTTT_SHARED_GRAPH=1`.
5. Fillers: plan 58 when 7.1.0 is out (first, ahead of everything), bugs/sourcesweep_checker.md, the front end's
   laptop wall checks.
