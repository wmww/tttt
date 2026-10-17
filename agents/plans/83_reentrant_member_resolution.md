# Plan 83: member resolution re-entered like tsc's (mapped `as` clauses by relation)
Written by the hacks audit of 2026-10-17 (todo entry 6). Short: an investigation that ends in a fix or a sized plan.

## The problem
tsc's getMappedTypeNameTypeKind relates a mapped type's `as` clause to its key (Filtering when assignable, else
Remapping). tttt reads the clause's shape first (`name_type_filters()`, `peek_cond_branch()`), and relates
(`mapped_name_type_filters()`, members.c, KEY_MAPPED_FILTERING) only when the clause's source text has no `keyof`
token: relating `${K}.${keyof Self<T[K]>}` from inside member resolution resolves members of mapped types already in
flight, and tttt then *keeps* the incomplete answer (mappedTypeAsClauseRecursiveNoCrash1 loses keys). tsc's
resolveStructuredTypeMembers installs empty members up front (setStructuredTypeMembers with emptySymbols before
resolveMappedTypeMembers fills them), so a re-entrant read sees nothing *for that read only* and the outer resolution
still completes and is what gets cached. hacks.md: the bold "A mapped type's `as` clause kind" entry (Type core).

Other places in-flight members are worked around the same way and may share the fix (check each): `ty_mapped_template()`
recomputes when re-entered, the eager-base model's "members in flight" probes (`probe_eager_bases()`, a deliberate
tsgo-artifact model: don't break it), `never_index_read` (keyless mapped types' members tipping TS5115 in xstate),
`ty_has_unresolved_base()`.

## What to do
1. Find what tttt does when a type's members are requested while they're being resolved (members.c: the in-flight
   state of `Members`, what a re-entrant reader gets and whether the outer resolution's result or the inner partial
   one ends up cached). Write it in documentation.md if it isn't.
2. Make it tsc's: the re-entrant reader sees empty members (not cached for that reader's dependents if tsc doesn't),
   the outer completes and is cached. Then drop the `keyof` text test in `mapped_name_type_filters()` and, if
   possible, `name_type_filters()`'s shape reading (relation for every clause, cached per declared mapped type).
3. If (2) changes output beyond the targeted tests (tsgo has its own artifacts from reading empty members:
   circularity diagnostics, `{}`-typed reads), compare against tsgo on each and keep what matches; if the change is a
   multi-session refactor, write the sized plan here instead.

## Validation
mappedTypeAsClauseRecursiveNoCrash1 keeps its keys, suite 0 lost, every oracle unchanged (`scripts/oracles.py`;
arktype and type-fest in the source sweep/DT are the `as`-clause-heavy code), perfsweep types ratio flat (relating
every clause costs relations: measure), the hacks.md entry deleted or narrowed.
