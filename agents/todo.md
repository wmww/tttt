# Todo
Rewritten 2026-10-22 after the third coordinator session (vision.md "Course correction (2026-10-22)"). Work runs
as a coordinator with parallel workers in worktrees (CLAUDE.md "Workflow"; `agents/coordinator.md`); entries are sized
in sessions of one worker. Delete entries as they complete, leaving a one-line "done" note with the numbers. New work
goes into plans or triage.md. Every entry ends with `scripts/fasttest.sh -s`, the oracles its change can move
(`scripts/oracles.py` after checker changes, in the background with a bounded wait), `scripts/instrcount.sh`, an empty
`git worktree list` and deleted scratch (CLAUDE.md), and a commit. **If TypeScript 7.1.0 stable is on npm when an entry
starts (`npm view typescript dist-tags`; on 2026-10-22 `latest` was 7.0.2, `next` 7.1.0-dev, `beta` 7.1.0-beta), do plan
58 (the re-pin) first, then plan 66 stage 5**, and push the rest down. `claude-stop` is gitignored: never commit it.

Starting point: the end-state totals in measurements.md "Oracle totals (2026-10-22 ...)" (suite 13972 / 14006). Instructions: `test/instrcount.txt` is at a recycle-free K1 now (the memory default), so a jump against it
needs attribution first.

**The next coordinator's whole briefing is `agents/plans/91_next_coordinator.md`.** Order:
1. **Plan 91 section 4**: the checks and the merger (the laptop bench of the new defaults is done: measurements.md "at f2ad73c9",
   vscode 1.93 s, nothing lost, the defaults stay).
2. **Plan 91 section 2**: the next 1.5x on vscode. The parser rewrite as a spike first (token stream, node arenas; plan
   86 stage 3, perf_leads.md "Time"); one checker-speed worker from k1's leftovers (LiteralBuilder's first realloc,
   object literal item atoms on the AST, LazyMemo pages; measurements.md "Stage 2 session 2"); policy on the laptop.
3. **Plan 91 section 3, plan-sized correctness**: loop flow analysis errors (five sightings), instantiation identity
   (hono, xstate), the satisfies elaboration, the error pass's first callback check, the zod getter, binding elements
   (bugs/missing_checks.md); parser-recovery mutants belong to plan 27.
4. Fillers: plan 58 when 7.1.0 is stable; bugs/sourcesweep_checker.md. The shared graph is parked: no work on it
   without hardware counters (plan 86 "Stage 4f: the perf accounting").

Rules of the window: byte-identical output across checker and thread counts (`scripts/checkerdiff.py`, `scripts/threadcheck.sh`), instruction-flat or better per commit, `$ExpectType` in every check batch. A change that makes a flake rarer without a mechanism
is not a fix: write the mechanism down or keep looking.
