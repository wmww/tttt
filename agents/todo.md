# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 08)")
for why.

1. **Execution: plan 19 stage 0** (module augmentation of an aliased export: ~2400 of valibot's 4473), a short
   session; then **plan 14** (syntax gaps), `using` first (~330 tests). 1238 tests have a spurious parse error.
2. **Execution: plan 18** (template literal types, string mappings, mapped `as` clauses), stages 1-2.
3. **Execution: plan 18** stages 3-4.
4. **Execution: plan 20** (conditional instantiation cache: valibot 4.0 s vs tsgo 1.3 s).
5. **Execution: plan 13** (member modifiers), stages 1-3 (access checks, readonly writes).
6. **Planning: plan 16** (error elaboration) from the stub.
7. **Triage.** Plan 12 left a batch of "exposed by excess property checks" items, the end of plan 08 a few display
   and check gaps, plan 17 the TS2578-exposed missing checks (triage.md is ~270 lines: consolidate). Then insights,
   which should decide how much of plan 19's loop (corpora to zero) to interleave with the suite plans.
