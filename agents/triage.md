# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-05 (triage session, todo entry 10 of the eighth window; suite 12731 -> 12753, 0 lost, every
corpus unchanged bar one union order inside webpack's existing cleverMerge.js false positive; scenarios 208 -> 215,
only out-of-scope TS6377 left). Five Opus sub-agents: (A) the 1-vs-4-checker nondeterminism was import attribute
values skipping name resolution (all shared expression cache slot 0; slot 0 now refused) plus a stack over-read in
quoted member names: suite dumps are byte-identical at 1 and 4 checkers; Pick/Partial excess properties; bare-return
return types. (B) CLI gating as tsc's emitFilesAndReportErrors, absolute module paths in CLI messages, tsconfig syntax
errors and missing `extends`. (C) getTargetofModuleDefault/TS2613, TS1479 for the JSX runtime import, the
isolatedModules/verbatimModuleSyntax alias checks (+13). (D) the checkReturnStatement port landed after fixing its
two order bugs: flow typed assigned values without their contextual type, and function expression bodies are now
deferred to the end of the file like tsc's checkNodeDeferred. (E) plan 43 (AST memory). Leftovers are in hacks.md,
missing_checks.md "Module and import checks", type_core.md "Circularity" (getter_cycle.ts).

- (triage 2026-10-05, D) tttt now defers function expression bodies to the end of the file (`Checker.defer_functions`,
  tsc's checkNodeDeferred), but tsc also defers class expressions, JSX elements and type assertion checks; those
  still run in place. Order-dependent answers there would look like the zod TS2344s did: check before porting more.
