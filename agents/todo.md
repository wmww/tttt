# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 08)")
for why.

(Done since the last insights: plan 16 planned; plan 19 stage 0, valibot 4473 -> 2169; plan 14, suite 4221 -> 4604, spurious syntax
errors 1242 -> 746 tests; plan 18 in one session, suite -> 4690, type-fest 598 -> 424, valibot -> 1835; plan 20,
valibot 4.38 -> 1.19 s and 1834 -> 607 MB, vs tsgo 0.88 s / 834 MB; plan 13 stages 1-3 + typed `super`, suite
4690 -> 4785, valibot -> 1791; plan 16 done in one session, suite 4785 -> ~4930, valibot -> 1883 (false positives split per
property).)

1. **Execution: plan 13** stages 4-5 (private names, nominal privates in relations) and the `super` legality errors
   (triage.md), if one session allows.
2. **Triage.** Plan 12 left a batch of "exposed by excess property checks" items, the end of plan 08 a few display
   and check gaps, plan 17 the TS2578-exposed missing checks (triage.md is ~270 lines: consolidate). Then insights,
   which should decide how much of plan 19's loop (corpora to zero) to interleave with the suite plans, and whether
   alias variances (triage.md, Perf: tsc's same-alias shortcut in inference and relations) get a plan.
