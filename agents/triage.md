# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-27 (triage session, todo entry 10). Everything that was here went to:
- `bugs/missing_checks.md`: diagnostics tttt lacks or reports under another code (the queue for "suite checker bugs"
  sessions), including JSX checks and the single-code buckets.
- `bugs/type_core.md`: inference, relations/reduction, circularity, classes, narrowing and flow.
- `bugs/display.md`: alias display rules, structure, specifiers, underlines, the symbols baseline.
- `bugs/harness.md`: harness, CLI, tools, dead code. `bugs/perf_leads.md`: what to profile/try next.
- Plan 27 "Leftovers collected by triage" (parser), plan 26 "Leftovers collected by triage" (JS).
Items whose tests pass now were dropped (requiresScopeChange, computed-name index signatures, invalidNewTarget,
interfaceThatIndirectlyInheritsFromItself, reverseMappedUnionInference, the `W["_i"] | undefined` display...).

