# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 13)")
for why.

1. **Planning (+ execution if small): alias variances**, triage.md "Perf" first bullet: infer and relate two
   instantiations of one alias by their type arguments with measured variances. It's a correctness rule
   too (tsc does it); valibot no longer loses to tsgo (measurements.md), so perf is no longer the reason. Bench anyway.
2. **Execution: plan 21 stages 2.1 + 2** (symbol accessibility from real scopes; the symbols `name` bucket, 231 tests
   alone). The plan's Progress section says where it stands.
3. **Execution: plan 22 (JSX) stages 0 + 1** (harness `/.lib` + `@libFiles`, then the parser). `./failstats.py --jsx`
   before and after.
4. **Execution: plan 22 stage 2** (JSX namespace, intrinsic elements, baseline traversal). Stage 3 (components through
   `ty/call.c`) is the session after; if stage 2 finishes early, start it.
5. **Triage.** Drain triage.md, starting with "Missing checks after plan 15"'s stack depth item (a crash in waiting)
   and deciding whether default exports as one merged symbol gets a session. Try removing the `ctx.method` callback hack in `signature_resolve` (hacks.md), now that
   relate.c has tsc's callback rule. Also audit `hacks.md` against the code (several entries are stale after plans 13/16/18:
    "no nominal class rules", "no `as` clauses", the ambient module table note); delete what's fixed. Decide whether
    "binding patterns as types" gets a plan now.
