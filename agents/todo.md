# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 13)")
for why.

1. **Execution: plan 22 stage 4** (react-jsx/react-jsxdev runtime import: TS2875/TS2792 are the top single-code
   failures of the JSX subset; jsxImportSource, jsxFactory/jsxFragmentFactory and their pragmas; fragments resolved like
   calls of the fragment factory). Then rank what's left with `./failstats.py --jsx` and finish or triage.
2. **Execution: plan 21 stage 6** (symbol lines and declarations; sample `our-typeof` first, see the plan's Progress).
3. **Triage.** Drain triage.md, starting with "Missing checks after plan 15"'s stack depth item (a crash in waiting)
   and deciding whether default exports as one merged symbol gets a session. Try removing the `ctx.method` callback hack in `signature_resolve` (hacks.md), now that
   relate.c has tsc's callback rule. Also audit `hacks.md` against the code (several entries are stale after plans 13/16/18:
    "no nominal class rules", "no `as` clauses", the ambient module table note); delete what's fixed. Decide whether
    "binding patterns as types" gets a plan now.
