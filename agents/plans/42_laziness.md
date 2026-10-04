# Plan 42: where tttt is eager and tsc is lazy
Stub by the insights of 2026-10-04. **A planning session turns this into a real plan** (or concludes that the sites
are independent and routes them one by one).

## Why
The same root cause now ends most of the hard leftovers, from different plans and sessions:
- plan 38 "not done" 1 (resolvingSignature: -10 +1 because tttt re-enters calls tsc doesn't: eager return types,
  relation-time member resolution);
- plan 39 "Stage 3" 1-2 (nested call resolution; constraint lowering under the push breaks because `homomorphic_variable`
  -> `ty_members` and `ty_keyof` resolve members while lowering);
- hacks.md: `has_conflicting_discriminants` (intersections reduced when made), eager reverse mapped types
  (`reverse_mapped_depth`, depth 4 cut), `builder_add_interface_member` (index signatures lowered eagerly), tttt-only
  TS2589 from eager instantiation poisoning later users;
- bugs/type_core.md: contextual types instantiated eagerly, "laziness artifacts" of inference, reverse mapped types
  refusing generic intersections;
- bugs/perf_leads.md: props instantiated all at once instead of per name, index infos instantiated eagerly.

Each session that met one of these patched around it locally. That is the pattern the insights sessions exist to stop:
the next fixes in this area should be the port, not more workarounds.

## The planning session's job
1. Inventory every eager site (start from the list above; `grep -n HACK src` and hacks.md "Relations"/"Types"),
   with what tsc does instead (deferred resolution: `resolveStructuredTypeMembers` on demand, instantiated symbols
   resolving their type per property via `getTypeOfInstantiatedSymbol`, `getReducedType` at use, reverse mapped
   types' per-property `getTypeOfReverseMappedSymbol`).
2. For each, prototype or estimate: tests it would fix (the lists above name them), corpus effect, and instructions /
   memory (`./instrcount.sh`, `--time` arena size; lazy per-name props are likely a perf *and* memory win on drizzle,
   whose 1.8 GB is above single-threaded tsgo's 1.05).
3. Find the shared groundwork, if any (likely: an instantiated object type whose members are resolved per name on
   demand, with the circularity stack of plan 39 as the guard), and order the stages so each is 0 lost.
4. Write the plan with stages, validation and a go/no-go. It may say "don't": e.g. if laziness only buys the ~15
   circularity tests and costs instructions, route the sites to bugs/ and close it.

Validation of the final plan's stages: `./fasttest.sh --summary` 0 lost, every corpus unchanged or better (zod-root
diffed against tsc), `./threadcheck.sh` / `./checkerdiff.py` identical, `./instrcount.sh` not worse overall.
