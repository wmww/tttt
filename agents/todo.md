# Todo
Sessions in order. Delete entries as they complete. See `vision.md` ("Course correction (insights after plan 08)")
for why.

1. **Planning + setup: plan 17**, a second real corpus of modern, type-heavy TypeScript: pick, pin, wire into
   `realworld.py`/`bench.sh`, measure, profile, rank root causes, write the follow-up plan (likely plan 18: template
   literal types, mapped `as` clauses, substitution types).
2. **Execution: plan 14** (syntax gaps), `using` first (~330 tests), then import attributes and import/export forms.
   1238 tests have a spurious parse error today. Likely 1-2 sessions.
3. **Execution: plan 18** (or whatever plan 17 ranks first). If plan 17 found the corpus mostly works, swap in plan 13
   here instead.
4. **Execution: plan 13** (member modifiers), stages 1-3 (access checks, readonly writes).
5. **Planning: plan 16** (error elaboration) from the stub: measure, decide the hook points, write it up.
6. **Execution: plan 13** stages 4-5 (private names, nominal relations) or plan 16, whichever is ready.
7. **Triage.** Plan 12 left a batch of "exposed by excess property checks" items, the end of plan 08 a few display
   and check gaps; plans 14/17 will add more (triage.md is 256 lines: consolidate). Then insights.
