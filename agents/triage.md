# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-28 (triage session, todo entry 10 of the third window). Fixed: the zod root-config segfault
(deferred reference args in reverse mapped inference), object rest with computed keys, `export default;`, the
webpack-in-worktree numbers (a symlinked corpus path loads webpack twice; realworld.py uses real paths now), and
the override and variance annotation checks (a sub-agent; leftovers in `bugs/missing_checks.md`). Routed:
plan 27 leftovers (rest element grammar, `interface void`, tagged template parsing, TransportStream, reservedWords3),
plan 26 leftovers (JS `0other`), `bugs/type_core.md` (const type parameters with several candidates, plan 32's
anyBaseTypeIndexInfo), `bugs/display.md` (`shows_alias`, `undefined[]`), `bugs/missing_checks.md` (TS2507/TS2735),
`bugs/harness.md` (`port_item_add_alias`, lib-file log_errors), `bugs/perf_leads.md` (valibot +2.5%, the 89 s
duplicated webpack), hacks.md (`string_to_double`).
