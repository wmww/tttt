# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

Plan 04 (symbols / new binder) is complete and its plan file deleted: `documentation.md` ("Symbols, tables, scopes",
"Resolve names") describes the result, `measurements.md` the numbers, `hacks.md` ("The binder") the open ends
including the parallelism contract audit.

1. **Planning - flow graph + narrowing -> `plans/05-flow-analysis.md`.** Bind-time flow graph, per reference per flow
   node caching, narrowing by typeof/instanceof/truthiness/equality/discriminants/`in`/assignment/type predicates
   (`x is T` isn't in the AST yet), reachability (`never` returns, TS7027), definite assignment (TS2454). Flow nodes
   hang on the resolve walk (`*_lookup_names()`: it visits statements and expressions in source order with the scope
   at hand; `documentation.md`, "Resolve names") and become arena objects of the file, pointed to by identifier/
   `this`/access expressions and by statements that need reachability. What a reference gives you: one stable
   `Symbol*` (`target`, aliases resolved), flags to tell const/let/var/parameter apart, the declarations' nodes;
   function scopes are not kept on their AST nodes yet ("captured and assigned in a closure" will want that).
   `ty_identifier_reference_type()` is the hook; a binding's declared type is `ty_binding_type()` (elements of a
   destructuring pattern know their parent pattern, which narrowing on destructured references will want). Expect
   the fallout pattern of the type core migration: correct narrowing will stop forgiving other approximations (fresh/regular literals, declared vs narrowed `typeof x`).
   This is what the real corpus is waiting for: ~7000 of its 7754 diagnostics are `X | undefined` arguments.
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
