# Track S: `--shared-graph` at one thread (plan 86 stage 4, ahead of 4d)

The question: with K checkers run one after another (`--threads 1 --checker-memory off`), does a graph they all
share, holding only what was computed *clean*, bring K12's check work back to K1's with the output unchanged?

## What is shared (one `TyGraph` per program, `src/ty/graph.c`)
- **Atoms, type ids, the id -> Ty table**: one atom table, one id counter, one set of id-table chunks. Sequentially
  these are *handed over* in `checker_make_current()` (`graph_enter()` / `graph_leave()` copy them into the current
  checker and back): zero cost on the hot paths. In 4d they become a global atom table (4c), an atomic id counter
  handing out blocks, and a CAS-allocated chunk directory.
- **The lib singletons**: intrinsics and `typeof_type` are made once by the first checker and copied into the others
  at `checker_new()`; the lazily made ones (`array_target`, `auto_array`, `readonly_array_target`, `tuple_params`,
  `unknown_signature`, `global_types`, `iteration`, `disposable_targets`) are handed over like the ids. In 4d: made
  before the workers start.
- **Intern tables** (`types`, `links`, `intersection_props`) **and `node_links`**: each checker keeps its private
  tables as today; the graph has a shared table of each. Lookup: private first, then (on a private miss, kind
  shareable) shared. The two never hold different values for one key: a checker creates a private entry only when
  the shared table has none. Shared blocks are the publishing checker's arena blocks (no copy). Classification by
  kind (`graph_kind_class()`, from survivors.c's kind_fate()): *identity* kinds (constructors and per-symbol/node
  roots: the value is the key's identity) always publish; *memo* kinds (a computed value: simplified, base
  constraint, conditional instances, variances...) publish only when clean; *scratch* kinds (expression, flow,
  inference, report-once, relation numbering) never.
- **Declaration LazyType memos** (class fields and accessors, initialized bindings): a shared memo store keyed by
  slot; `lazy_type_evaluate()` (the slow path) takes a done shared memo before evaluating.
- **Ty-resident lazy fields** (members, prop types, signatures, constraints, SymbolLinks, FunctionReturn): shared by
  construction once the Ty is, written by whichever checker computes them first. This is the part 4a moves into
  overlays; here it is measured (tainted Ty-resident results on another checker's types, counted), not overlaid.
- One arena: each checker allocates from its own as today, never recycled (the flag forces `--checker-memory off`);
  every checker lives to the end of the program, so nothing a shared Ty points to goes away.

## Publication
Sequentially, publishing a clean result when its computation ends and publishing every clean result when its
checker's share ends (`graph_publish()`, end of `check_share()`) give the next checker exactly the same graph, so
the measurement batches it: walk the private tables, publish the entries of identity kinds, and of memo kinds when
clean and done (non-null value, not an in-flight placeholder). In 4d `graph_publish_entry()` is called at each
computation's end with a CAS instead. Keys are disjoint, so there is never a conflict sequentially.

## Taint (cheap)
A thread-local counter every taint event bumps (the G0 sites in taint.h, live in this mode instead of only under
`-DTTTT_TAINT`; `--lenient`'s rule). Per entry: an intern miss records the counter (a tiny ring by hash, like
taint.c's), the put compares; a key put before its value (a placeholder) stays on a pending list, and an event
taints every pending entry still without its value. A tainted block gets a mark (a bit in a side set), and reading a
tainted entry is itself an event. Declaration memos: the existing TAINT_BEGIN/END frames around their evaluation.
Order: build sharing first *without* taint (an upper bound on the share, and the output tells whether taint is even
needed on these corpora), then taint, then measure both.

## Order of work
1. TyGraph, hand-over of atoms/ids/id table/singletons, `--shared-graph` flag (forces checker-memory off). K1 with
   the flag: byte-identical (nothing to share with).
2. Shared intern tables + node_links + publication (no taint). K4/K12 outputs and work.
3. Declaration memos.
4. Cheap taint; publish memo kinds only when clean. Share and taint stats per kind (`TTTT_GRAPH_STATS=1`).
5. Measurements (work at K1/4/12, callgrind, RSS, outputs), the default path checked flat (instrcount vs 4eadfcc74).

## What I expect to break
- Code that writes into an existing slot (sentinel then value, "first answer stays" updates) now writes into a
  shared block: KEY_SIMPLIFIED, KEY_BASE_CONSTRAINT, KEY_TYPE_VARIABLE_REDUCTION, conditional instance entries.
- Ty-resident fields left IN_FLIGHT or tainted by one checker, read by the next as done/cycles.
- Peeks at memo done-ness (`lazy_type_state_peek()`) see a private "not started" where the shared memo is done.
- Id order: a checker's new types are newer than everything shared (is_deeply_nested_as(), union ties): the gate's
  "another history" assumption gets its first real test.
- Mixed derivations (census B §4): a shared declaration type next to the checker's own derivation of its
  initializer's expressions.
