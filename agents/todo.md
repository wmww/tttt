# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 13)")
for why.

1. **Triage.** Drain triage.md, starting with "Missing checks after plan 15"'s stack depth item (a crash in waiting)
   and deciding whether default exports as one merged symbol gets a session. Try removing the `ctx.method` callback hack in `signature_resolve` (hacks.md), now that
   relate.c has tsc's callback rule. Also audit `hacks.md` against the code (several entries are stale after plans 13/16/18:
    "no nominal class rules", "no `as` clauses", the ambient module table note); delete what's fixed. Decide whether
    "binding patterns as types" gets a plan now.

Plan 22 (JSX) is complete: the next insights session should decide its "After the plan" question (a React corpus) and
then delete the plan file; its leftovers are in triage.md. Plan 21 (display fidelity) is closed (2026-09-26): its
leftovers and the union-origins decision are in triage.md "Display leftovers after plan 21"; `other` (378 TS tests
alone in `./displaydiff.py`) is mostly checker bugs and is a good source of work for the next insights session to
direct.
