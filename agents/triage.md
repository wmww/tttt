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
- (effect corpus, 2026-10-05) 7 of effect's 63 diagnostics differ between `--checkers 1` and 4 (`./checkerdiff.py
  effect`: fiberRuntime.ts 3612 TS2769, Sink racing/zipping 57/73, vitest internal.ts 175 only at K=4; three only at
  K=1). K=4 twice is identical, so it's check order, not a race: some memo depends on what was checked before it
  (generic rest arguments keep their type from the first candidate that typed them, `infer_rest_variadic` skips
  arguments with a type; a suspect). Find the first order-dependent one with `--dump` diffs on a probe.
- (effect corpus) `infer_rest_variadic`/`check_rest_arg` keep an argument's type from whichever candidate typed it
  first (`if (arg->type) continue`), though its contextual type is now the candidate's rest element: with overloads
  whose rest types differ, a later candidate sees types checked against an earlier one. tsc re-checks per candidate
  (checkExpressionWithContextualType caches nothing across candidates for non-sensitive arguments).

- `has_inferable_index()` (relate.c) grants every anonymous object type an implicit index signature; tsc only types with a symbol of the right kind (isObjectTypeWithInferableIndex). Found by the sweep (plan 44, rrdtool: D/literalKeyof.ts); check how much wider tttt's rule is and whether it hides errors elsewhere.

- **A project reached through a symlink is checked differently and ~45x slower.** `tttt -p thirdparty/corpora/webpack` run from a worktree (where `thirdparty/corpora` is a symlink to the main checkout) takes 45 s instead of 1 s, makes 525k type mappers (93% repeats) and reports different diagnostics (TS2859 "Excessive complexity" in webpack's examples) than the same project by its real path. Probably files reached by both the symlinked and the real path are loaded twice (tsc resolves symlinks by default, preserveSymlinks false), so every type exists twice and nothing is identical. Repro: `cd /tmp/<worktree> && ./release/tttt -q --keep-going -p thirdparty/corpora/webpack | md5sum` vs the same with `$(realpath thirdparty/corpora/webpack)`. Measurement scripts that pass relative paths from a worktree (arenasites.sh copies, `--time` probes) measure this pathology instead of the real project; instrcount.sh and threadcheck.sh use realpath.
