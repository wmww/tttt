# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 13)")
for why.

1. **Execution: plan 15, session 1.** Re-derive the table. Start with unused locals/parameters (TS6133 & co., 120
   tests miss only that): design the "referenced" bit + report pass once, as the plan says. Then declaration checks.
2. **Planning: plan 21, display fidelity.** Rank the mechanisms behind the ~2600 tests that pass errors but fail
   types/symbols (vision.md has first numbers; `./displaydiff.py` buckets the types diffs by mechanism, extend it to
   symbols). Start with the 274 tests whose lines all match (only underlines or line order differ). Decide how to do tsc's annotation reuse (`typeof a`, `Array<X>` as written) for real rather
   than extending the fitted rules in `ty_display.c` (hacks.md "Types baseline underlines"). Fold in triage.md's
   "Display fidelity" and "Class expressions and member symbols".
3. **Execution: plan 21, first stages.**
4. **Execution: plan 15, session 2.**
5. **Harness + planning: JSX.** Make `baseline_test_suite.c` collect `.tsx` tests (328 files; the pass count will drop,
   save the new baseline and say so in the commit and `measurements.md`), then write plan 22 (JSX parse, `JSX`
   namespace lookup, element/attribute typing, `@jsx` options, `@libFiles` react.d.ts). Measure how much `--syntax`
   and TS2304 it accounts for.
6. **Planning (+ execution if small): alias variances**, triage.md "Perf" first bullet: infer and relate two
   instantiations of one alias by their type arguments with measured variances. It's a correctness rule
   too (tsc does it); valibot no longer loses to tsgo (measurements.md), so perf is no longer the reason. Bench anyway.
7. **Triage.** Drain triage.md. Try removing the `ctx.method` callback hack in `signature_resolve` (hacks.md), now that
   relate.c has tsc's callback rule. Also audit `hacks.md` against the code (several entries are stale after plans 13/16/18:
    "no nominal class rules", "no `as` clauses", the ambient module table note); delete what's fixed. Decide whether
    "binding patterns as types" gets a plan now.
