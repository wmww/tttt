# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 13)")
for why.

1. **Execution: plan 24** (`plans/24_binding_pattern_types.md`): the type implied by a destructuring pattern. First
   because unannotated destructured parameters with defaults (`({ q = 1 }) => q`) get a spurious TS7031 under
   `strict`, which real code hits constantly.
2. **Execution: plan 23** (`plans/23_symbol_merging.md`): default exports as a merged symbol, TS2300/TS2813/TS2814
   and the other merge checks (~70 tests).
3. **Insights.** Decide plan 22's "After the plan" question (a React corpus as the next oracle) and delete the plan
   file; decide when JS/JSDoc starts; direct `./displaydiff.py`'s `other` bucket (mostly checker bugs, see triage.md
   "Display leftovers after plan 21"). Plan 19 (corpora to zero) is still open: type-fest 168, valibot 164.
