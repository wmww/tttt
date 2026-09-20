# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

Plan 05 (flow graph, narrowing, reachability) is complete and its plan file deleted: `documentation.md`
("Control flow"), `hacks.md` ("Narrowing", "Control flow graph"), `triage.md` ("Plan 05 stage 4") and
`measurements.md` have what it left behind.

Plan 04 (symbols / new binder) is complete and its plan file deleted: `documentation.md` ("Symbols, tables, scopes",
"Resolve names") describes the result, `measurements.md` the numbers, `hacks.md` ("The binder") the open ends
including the parallelism contract audit.

1. **Planning + execution - module resolution -> `plans/06-module-resolution.md`** (independent of everything
   else): `typeRoots`/`types`/`/// <reference types>`, automatic `@types` inclusion, `paths`/`baseUrl`,
   package.json `types`/`typings`/`exports`/`typesVersions`, node16/bundler rules and `.mts`/`.cts`, tsconfig in the
   CLI (`realworld.py` passes the corpus' options by hand). See `triage.md`; spurious TS2307 is in ~470 tests and ~50
   tests regressed honestly when the harness got tsc's root file rule.
2. **Triage.** Drain `triage.md` (much of it predates the new binder: delete what plan 04 fixed). Cheap
   JS gate (skip `.js` without `allowJs`, no checker errors without `checkJs`; `check_access()` has a private one
   for TS2339 that should go with it). Cut `measurements.md` down further.
   Is `engine.c` dead? Catalogue TODO/HACK comments (small-model sub-agent). Refresh readme.md. The "Cannot find
   name" flavours (TS2580-2584) table.

Then: insights. Look at `./realworld.py` next to the pass count.
