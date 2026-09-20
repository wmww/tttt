# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

Plan 04 (symbols / new binder) is complete and its plan file deleted: `documentation.md` ("Symbols, tables, scopes",
"Resolve names") describes the result, `measurements.md` the numbers, `hacks.md` ("The binder") the open ends
including the parallelism contract audit.

1. **Execution x4 - `plans/05-flow-analysis.md`**, one stage per session (1 flow graph in the resolve walk, 2 the
   engine + basic narrowing, 3 predicates/assertions/discriminants/switch/reachability, 4 fallout + measurement).
   Stage 2 starts with the re-entrancy probe (expression memos vs incomplete loop types): it's the one design risk.
   This is what the real corpus is waiting for: ~7000 of its 7754 diagnostics are missing narrowing.
2. **Planning + execution - module resolution -> `plans/06-module-resolution.md`** (can run before or after plan 05,
   independent of it): `typeRoots`/`types`/`/// <reference types>`, automatic `@types` inclusion, `paths`/`baseUrl`,
   package.json `types`/`typings`/`exports`/`typesVersions`, node16/bundler rules and `.mts`/`.cts`, tsconfig in the
   CLI (`realworld.py` passes the corpus' options by hand). See `triage.md`; spurious TS2307 is in ~470 tests and ~50
   tests regressed honestly when the harness got tsc's root file rule.
3. **Triage.** Drain `triage.md` (much of it predates the new binder: delete what plan 04 fixed). Cheap
   JS gate (skip `.js` without `allowJs`, no checker errors without `checkJs`). Cut `measurements.md` down further.
   Is `engine.c` dead? Catalogue TODO/HACK comments (small-model sub-agent). Refresh readme.md. The "Cannot find
   name" flavours (TS2580-2584) table.

Then: insights. Look at `./realworld.py` next to the pass count.
