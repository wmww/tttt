# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 08)")
for why.

(Done since the last insights: plan 19 stage 0, valibot 4473 -> 2169; plan 14, suite 4221 -> 4604, spurious syntax
errors 1242 -> 746 tests; plan 18 in one session, suite -> 4689, type-fest 598 -> 424, valibot -> 1965.)

1. **Execution: plan 20** (conditional instantiation cache: valibot 4.4 s vs tsgo 0.9 s; type-fest's 0.68 s is the
   same profile since plan 18).
2. **Execution: plan 13** (member modifiers), stages 1-3 (access checks, readonly writes).
3. **Planning: plan 16** (error elaboration) from the stub.
4. **Triage.** Plan 12 left a batch of "exposed by excess property checks" items, the end of plan 08 a few display
   and check gaps, plan 17 the TS2578-exposed missing checks (triage.md is ~270 lines: consolidate). Then insights,
   which should decide how much of plan 19's loop (corpora to zero) to interleave with the suite plans.
