# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

1. **Planning - symbols / new binder -> `plans/04-symbols.md`.** First reproduce the real corpus' biggest bug, it is
   a binder bug and the plan must make it impossible: ~4000 diagnostics are "`Identifier` is not assignable to `Node`
   ... missing baseURI, childNodes", ie a `Node`/`Symbol`/`Type` imported through the `_namespaces/ts.ts` barrel
   (`export *` of 60 files that import the barrel back) resolves to the global lib type in some files (`triage.md`,
   "Real corpus, first look"; a 3-file cycle doesn't reproduce it). Import types (`parse_import_type`, `any` today)
   need module symbols and file loading from non-toplevel positions. Then: replace, don't patch, the ~1.5k-line binder
   (`scope.c`, `declaration.c`, `port_*.c`, the bind-time `TypeNode` links): persistent symbols with alias links
   instead of declaration copies, tsc's merging rules (namespaces, `Intl`, fundules, interface+class), persistent
   scopes/locals, per-file bind that doesn't mutate shared state (lib scope shared read-only across programs, parallel
   bind later), symbol accessibility for printing (`qualified_name`, symbols baseline names). Inputs: `hacks.md` "The
   AST-side walk and what's left of the old type world", `canonical_declaration`, `port_item_resolve_alias`; the triage
   items tagged plan 04; a callgrind profile of name resolution on the real corpus (it costs as much as parsing;
   `hmap_arc_declaration_bucket_lookup_` was 16% self time). Say what flow analysis (plan 05) will need from it: a
   stable symbol per reference, and where flow nodes would hang. Stage it so the suite runs throughout, as plan 02 did.
2. **Execution - plan 04**, as many sessions as the plan stages (expect 2-4; add entries here).
3. **Planning - flow graph + narrowing -> `plans/05-flow-analysis.md`.** Bind-time flow graph, per reference per flow
   node caching, narrowing by typeof/instanceof/truthiness/equality/discriminants/`in`/assignment/type predicates
   (`x is T` isn't in the AST yet), reachability (`never` returns, TS7027), definite assignment (TS2454).
   `ty_identifier_reference_type()` is the hook. Expect the fallout pattern of the type core migration: correct
   narrowing will stop forgiving other approximations (fresh/regular literals, declared vs narrowed `typeof x`).
4. **Triage.** Drain `triage.md` (much of it is covered by plan 04: delete those). Cheap
   JS gate (skip `.js` without `allowJs`, no checker errors without `checkJs`). Cut `measurements.md` down further
   (plan 03 removed type core stages 1-4b2; history is in git). Is
   `engine.c` dead? Catalogue TODO/HACK comments (small-model sub-agent). Refresh readme.md.

Then: insights. Look at `./realworld.py` next to the pass count.
