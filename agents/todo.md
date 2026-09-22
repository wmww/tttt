# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

The triage after plan 06 is done (commit log: JS gate, fileless diagnostics, option validation, string escapes,
property symbols through unions, `typeof` entity names; `triage.md` rewritten; `measurements.md` cut to 43 lines;
`readme.md` refreshed). Its one big finding: `plans/07_underlines.md`.

1. **Insights.** Look at `./realworld.py` next to the pass count (`measurements.md` has both). Decide whether plan 07
   (types-baseline underlines, 1445 tests, harness fidelity rather than typechecking) goes first or after a checker
   plan; the case for first is that the types component can't measure type-string bugs in half the suite until it's
   done. The candidates for the next checker plan, by corpus weight (`triage.md` "Category-sized"): contextual
   typing of object-literal methods against overloaded/optional interface members (852 TS7006), `this` types,
   the iteration protocol + well-known symbols, JS/JSDoc (900 tests, now failing quietly), parser recovery
   fidelity (the harness typechecks next to parse errors now). Write `todo.md` for the next 2-10 sessions, ending
   with a triage session.
