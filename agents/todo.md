# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

Plan 05 (flow graph, narrowing, reachability) is complete and its plan file deleted: `documentation.md`
("Control flow"), `hacks.md` ("Narrowing", "Control flow graph"), `triage.md` ("Plan 05 stage 4") and
`measurements.md` have what it left behind.

Plan 06 (module resolution, tsconfig) is complete and its plan file deleted: `documentation.md` ("Module
resolution"), `hacks.md` ("Module resolution": what it leaves out), `triage.md` ("After plan 06") and
`measurements.md`.

Plan 04 (symbols / new binder) is complete and its plan file deleted: `documentation.md` ("Symbols, tables, scopes",
"Resolve names") describes the result, `measurements.md` the numbers, `hacks.md` ("The binder") the open ends
including the parallelism contract audit.

1. **Triage.** Drain `triage.md` (much of it predates the new binder: delete what plan 04 fixed). Cheap
   JS gate (skip `.js` without `allowJs`, no checker errors without `checkJs`; `check_access()` has a private one
   for TS2339 that should go with it; since plan 06 `.js` files resolve and load with `allowJs`, which makes the
   gate worth more: TS2306 "is not a module" on CommonJS files is 27 tests). Cut `measurements.md` down further.
   Pick from `triage.md` "After plan 06": `@libFiles` in the harness (~40 tests) and diagnostics without a file
   (TS2688 for `types`, TS5xxx option validation, ~60 tests) are the two cheap, category-sized ones.
   Is `engine.c` dead? Catalogue TODO/HACK comments (small-model sub-agent). Refresh readme.md. The "Cannot find
   name" flavours (TS2580-2584) table.

Then: insights. Look at `./realworld.py` next to the pass count.
