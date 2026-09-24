# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 08)")
for why.

(Done since the last insights: plan 16 planned; plan 19 stage 0, valibot 4473 -> 2169; plan 14, suite 4221 -> 4604, spurious syntax
errors 1242 -> 746 tests; plan 18 in one session, suite -> 4690, type-fest 598 -> 424, valibot -> 1835; plan 20,
valibot 4.38 -> 1.19 s and 1834 -> 607 MB, vs tsgo 0.88 s / 834 MB; plan 13 stages 1-3 + typed `super`, suite
4690 -> 4785, valibot -> 1791; plan 16 done in one session, suite 4785 -> ~4930, valibot -> 1883 (false positives split per
property); plan 13 stages 4-5 + `super(...)` resolution, `super` legality, this-before-super, `in` checks, duplicate
package redirects, suite 4931 -> 5116, valibot -> 1871. Plan 13 has only small leftovers now.)

1. **Insights.** Triage after plan 13 is done (2026-09-23): triage.md is regrouped by theme, the old checker items
   re-probed. Decisions waiting there: which of the new category-sized items get plans (binding patterns as types;
   display fidelity: annotation reuse and union origins; class expressions + member symbols as a single session;
   substitution types), how much of plan 19's loop (corpora to zero) to interleave with the suite plans, whether alias
   variances (triage.md "Perf") get a plan, and when JS/JSDoc starts (it's also most of the spurious TS2304, the
   biggest spurious code). Plan 15 (missing checks) is ready to run as is.
