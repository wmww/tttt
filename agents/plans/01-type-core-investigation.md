# Plan 01 - type core investigation (planning brief)

Read `vision.md` "Architecture assessment" first. Your deliverable is `plans/02-type-core.md` plus todo entries.

## Questions to answer with evidence
1. Confirm or refute the assessment: read `type.h/.c`, `several_types.*`, `generic.c`, `lazy_type.*`,
   `type_pair_cache.h`, `display.c` (hash_key mode), `declaration.*`. How are recursive types, generic
   instantiation, and relation caching handled today, and where do they visibly break? Use `./filetest.sh` on small
   cases (recursive generic interfaces, deeply nested instantiations, large unions, `types` baseline union order).
   Profile (perf/callgrind) the stdlib check from plan 00's bench: what share is clone/drop/display/hash?
2. Design the replacement. Default direction: types are immutable, arena-allocated, referred to by pointer or
   32-bit id; structural interning for literals, unions/intersections (sorted by id, deduped), arrays/tuples,
   instantiations (keyed by target + type-arg ids); object/interface/class types identified by declaration;
   deferred types (conditional, indexed access, mapped, keyof) interned by operand ids; relation caches keyed by
   id pair (+ relation kind); instantiation via a mapper with a cache, not clone-and-substitute. Decide how
   LazyType interacts (likely: LazyType memo holds a type id). Decide what `TYPE_REF`/`parsed`/AST-owned types
   become (likely: AST holds syntax-only type nodes; semantic types are produced from them lazily).
   Look at how tsc and tsgo do it (`thirdparty/tsc`), but design for C and for this codebase.
3. Threading: state how the design allows parallel parse/bind per file and, later, multiple checker instances
   (tsgo's model: N checkers each with own type tables) or a shared concurrent intern table. Don't build it, just
   don't preclude it.
4. Type printing: tsc's union ordering and alias preservation (`aliasSymbol`) depend on ids/creation order.
   Make sure the design can reproduce it.
5. Migration path. Options: (a) staged in-place migration keeping tests green-ish, (b) rewrite type.c and
   friends against the new core, porting behaviour file by file. Recommend one. Stages must be session-sized
   (a few hundred to ~1.5k lines of change), each with a validation criterion (test count not below X, valgrind
   clean on a sample, bench number).

If you conclude the current representation is actually adequate, say so with the evidence, write that into
vision.md, delete this brief and rewrite todo.md accordingly. That's a legitimate outcome, but the bar is high:
you need a credible story for id-free relation caching, instantiation caching and flow-type caching at tsgo speed.
