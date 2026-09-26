# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 13)")
for why.

1. **Execution: plan 22 stage 3, rest** (children elaboration TS2745-7, TS2558 on tag type arguments, TS2783, types and
   symbols of components: see the plan's Progress). Stage 4 is the session after; if stage 3 finishes early, start it.
2. **Execution: plan 21 stage 6** (symbol lines and declarations; sample `our-typeof` first, see the plan's Progress).
3. **Triage.** Drain triage.md, starting with "Missing checks after plan 15"'s stack depth item (a crash in waiting)
   and deciding whether default exports as one merged symbol gets a session. Try removing the `ctx.method` callback hack in `signature_resolve` (hacks.md), now that
   relate.c has tsc's callback rule. Also audit `hacks.md` against the code (several entries are stale after plans 13/16/18:
    "no nominal class rules", "no `as` clauses", the ambient module table note); delete what's fixed. Decide whether
    "binding patterns as types" gets a plan now.
