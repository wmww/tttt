# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

1. ~~Plan 04 stage 1~~ done (symbols, persistent scopes, tsc's merge rules, aliases, export tables, lib overlay;
   `measurements.md` has the numbers, the plan's "Stage 1 as built" what differs from the plan).
2. **Execution - plan 04 stage 2**: the type side (`Symbol.decl`/`Declaration`, `DeclHandle`, `MergedType`,
   `TYPE_LAZY`/`TYPE_REF` links go; `decl_ref` + `symbol` on identifiers become one `Symbol const*`; the lib
   `frozen -> overlay` redirect replaces `clone_parent_symbol`'s payload clone). Start from `hacks.md`, "The binder".
2b. **Execution - plan 04 stages 3 + 4**: module symbols from non-toplevel positions (import types), ambient modules,
   `export =`, accessibility-based name printing; then arenas, profile, parallelism contract audit, docs. Split in two
   sessions if stage 3 alone fills one.
3. **Planning - flow graph + narrowing -> `plans/05-flow-analysis.md`.** Bind-time flow graph, per reference per flow
   node caching, narrowing by typeof/instanceof/truthiness/equality/discriminants/`in`/assignment/type predicates
   (`x is T` isn't in the AST yet), reachability (`never` returns, TS7027), definite assignment (TS2454). Plan 04
   ("Phases") says where flow nodes hang (its resolve walk) and what a reference gives you (a stable `Symbol*`).
   `ty_identifier_reference_type()` is the hook. Expect the fallout pattern of the type core migration: correct
   narrowing will stop forgiving other approximations (fresh/regular literals, declared vs narrowed `typeof x`).
4. **Triage.** Drain `triage.md` (much of it is covered by plan 04: delete those). Cheap
   JS gate (skip `.js` without `allowJs`, no checker errors without `checkJs`). Cut `measurements.md` down further
   (plan 03 removed type core stages 1-4b2; history is in git). Is
   `engine.c` dead? Catalogue TODO/HACK comments (small-model sub-agent). Refresh readme.md.

Then: insights. Look at `./realworld.py` next to the pass count.
