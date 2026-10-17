# Plan 79: a copying checker recycle (the per-checker memory lever)
Brief by the insights of 2026-10-16 (after the eighteenth window; vision.md "Memory target"). **Entry 6 of the
window plans and prototypes it; entries 7-8 execute if the gate passes.**

## The problem
Checker recycling (plan 67 stage 1) drops the whole checker at a file boundary once it passes its budget and starts
fresh; the fresh checker re-derives the warm state (the libraries' and imported modules' types: drizzle +23% at
K1 B=128, dokploy's ~150 MB of trpc/drizzle state per recycle until `check_share()`'s warm-up guard). So budgets
stay at 128 MB per checker (64 at K24 under the 1.5 GB cap, plan 75), and at 24 checkers the checkers are ~1.5-2 GB
of vscode's ~4.7 GB. bun's per-thread state is ~30 MB (vscode 2.1 GB at 24 threads with a ~1.4 GB program).

## The idea
Recycle by copying, not dropping: trace from declaration-level roots (declared types of symbols, members of named
types, global/lib types, module exports, alias targets, variances) and copy the reachable types, signatures, members
and intern entries into the fresh checker's arena with a forwarding table; discard everything expression-level
(per-node `links` of checked files, flow types, call resolutions, fresh literal types, relation caches, LazyMemo
pages). Type ids are per checker already, so survivors get new ids and the id-keyed caches start empty. With the warm
state surviving, a recycle costs a copy instead of a re-derivation, and budgets can be a few tens of MB.

## What the planning session must answer (the gate)
1. **How much survives**: at the moment a recycle fires on vscode, dokploy/apps/dokploy, drizzle, type-fest, mark
   from the roots and report reachable bytes against the arena and the malloc side (`checker_memory()`): per type
   kind, per memo kind. **Gate: survivors < ~25% of the arena** on vscode and dokploy (else copying is most of a
   drop's cost and the lever is elsewhere).
2. **Roots and invariants**: which `KEY_*` memo kinds are declaration-level (survive) vs expression-level (dropped);
   what a dropped memo can be recomputed from; lazy types in flight at a file boundary (none, by `check_share()`'s
   segment boundaries); `Ty` kinds whose identity matters across the copy (interned by content: re-intern on copy;
   deferred references, tuple targets' shared `tuple_params`, `this` types, substitution types); the sweep's
   history dependences (undefinedProperties order) must not get worse (`scripts/kdiff.py -k 1 -m 0`).
3. **Cost**: a prototype copy on vscode at B=64 and B=32: wall at K1 and T4 against today's drop-and-rederive, and
   the budget policy that follows (per checker floor, the CLI's cap). **Gate: < 3% wall at the defaults at a budget
   that halves the checkers' share on vscode K24** (the 12-core table's K24 B64 row is the reference).
4. The alternative if copying fails the gate: a generational arena (declaration-level allocations in a long-lived
   arena, expression-level in a per-segment one, reset per segment) needs the same root classification and no copy;
   say which allocations can be classified at allocation time.

## Validation
Byte-identical output at every budget (`scripts/checkerdiff.py -k 1 -m 0`, kdiff, the suite, the corpora),
measurements.md "Plan 79" with the K1/T4 wall and peak table of plan 67 stage 1 redone, the 12-core run asked of the
human (report.md).
