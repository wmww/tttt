# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 13)")
for why.

1. **Execution: plan 21, stages 4 and 5** (class expressions and member symbols; `T_1` renaming, 71 tests alone).
   The plan's Progress section says where it stands. Stage 2.1 (function/block scopes for the traversal) is still
   open and can come with stage 4 if the samples ask for it.
2. **Harness + planning: JSX.** Make `baseline_test_suite.c` collect `.tsx` tests (328 files; the pass count will drop,
   save the new baseline and say so in the commit and `measurements.md`), then write plan 22 (JSX parse, `JSX`
   namespace lookup, element/attribute typing, `@jsx` options, `@libFiles` react.d.ts). Measure how much `--syntax`
   and TS2304 it accounts for.
3. **Planning (+ execution if small): alias variances**, triage.md "Perf" first bullet: infer and relate two
   instantiations of one alias by their type arguments with measured variances. It's a correctness rule
   too (tsc does it); valibot no longer loses to tsgo (measurements.md), so perf is no longer the reason. Bench anyway.
4. **Triage.** Drain triage.md, starting with "Missing checks after plan 15"'s stack depth item (a crash in waiting)
   and deciding whether default exports as one merged symbol gets a session. Try removing the `ctx.method` callback hack in `signature_resolve` (hacks.md), now that
   relate.c has tsc's callback rule. Also audit `hacks.md` against the code (several entries are stale after plans 13/16/18:
    "no nominal class rules", "no `as` clauses", the ambient module table note); delete what's fixed. Decide whether
    "binding patterns as types" gets a plan now.
