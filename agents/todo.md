# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction") for why.

1. **Execution - `plans/03-real-world-corpus.md`.** tttt can't parse real code: tsc's own `src/compiler` gives 1119
   syntax errors (regex literals, `?.()`, braceless `if/else`, comma operator...) and then heap corruption.
   `./realworld.py` is the tool. Get it to parse and run end to end, record real-corpus numbers, pin it as a benchmark.
2. **Execution - 03 continued**, only if session 1 left syntax errors or a crash behind. Otherwise skip.
3. **Planning - symbols / new binder -> `plans/04-symbols.md`.** Replace, don't patch, the ~1.5k-line binder
   (`scope.c`, `declaration.c`, `port_*.c`, the bind-time `TypeNode` links): persistent symbols with alias links
   instead of declaration copies, tsc's merging rules (namespaces, `Intl`, fundules, interface+class), persistent
   scopes/locals, per-file bind that doesn't mutate shared state (lib scope shared read-only across programs, parallel
   bind later), symbol accessibility for printing (`qualified_name`, symbols baseline names). Inputs: `hacks.md` "The
   AST-side walk and what's left of the old type world", `canonical_declaration`, `port_item_resolve_alias`; the triage
   items tagged plan 04; a callgrind profile of name resolution on the real corpus (it costs as much as parsing;
   `hmap_arc_declaration_bucket_lookup_` was 16% self time). Say what flow analysis (plan 05) will need from it: a
   stable symbol per reference, and where flow nodes would hang. Stage it so the suite runs throughout, as plan 02 did.
4. **Execution - plan 04**, as many sessions as the plan stages (expect 2-4; add entries here).
5. **Planning - flow graph + narrowing -> `plans/05-flow-analysis.md`.** Bind-time flow graph, per reference per flow
   node caching, narrowing by typeof/instanceof/truthiness/equality/discriminants/`in`/assignment/type predicates
   (`x is T` isn't in the AST yet), reachability (`never` returns, TS7027), definite assignment (TS2454).
   `ty_identifier_reference_type()` is the hook. Expect the fallout pattern of the type core migration: correct
   narrowing will stop forgiving other approximations (fresh/regular literals, declared vs narrowed `typeof x`).
6. **Triage.** Drain `triage.md` (it's at 180 lines; much of it is now covered by plans 03/04: delete those). Cheap
   JS gate (skip `.js` without `allowJs`, no checker errors without `checkJs`). Cut `measurements.md` down to the
   current numbers + the real-corpus table (its header says replace, sessions appended: history is in git). Is
   `engine.c` dead? Catalogue TODO/HACK comments (small-model sub-agent). Refresh readme.md.

Then: insights. Look at `./realworld.py` next to the pass count.
