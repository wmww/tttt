# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-17 (triage session, todo entry 10 of the eighteenth window): the summary/passcount totals
disagreed over three "did not run" tests. Two (checkJsFiles6, jsFileCompilationWithoutJsExtensions: a `.js` root
without allowJs, nothing in the program) have no symbols/types baselines because tsgo writes none for NoContent; the
harness now runs them and renders nothing there. instanceofOperator is UTF-16: the collector now decodes it before
reading its `@target` variations. All three pass; passcount.py counts undefined components as failing and a filtered
run's "total" is the filtered count. The 24-thread machine note was already in measurements.md ("Machine and
caveats", the 12-core sections); this session ran on the 4-vCPU box again (`nproc` 4).
