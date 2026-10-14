# Plan 66: 100% of the suite (the human's request)

Written 2026-10-14 (triage, todo entry 10 of the fifteenth window). The human asked (triage.md): "make the final
tests passing if not done already, so we're 100% green instead of 99.X%". At 17967e35 the saved results are
**13516 / 13630** (114 failing configurations, 113 test files: `build/failstats/tests.tsv` after
`scripts/failstats.py`). This plan is the route to zero. The no-special-casing rule still holds: a test passes
because tttt does what tsgo does, never because it recognizes the test.

## The rule for "impossible"
Nothing in the list is known to be impossible. A test may only be declared out of reach with evidence that tsgo's own
output depends on something tttt shouldn't reproduce (goroutine scheduling, absolute paths of the build machine), written
into `bugs/tsgo_divergences.md` and reported to the human in report.md. "It depends on tsgo's check order" is **not**
such evidence: several tests below depend on tsgo's harness emitting before it collects diagnostics, and a declaration
emitter (plan 64) is how tttt gets the same order honestly.

## The 113, by what they need (bucket = the agents file that already describes them)
Component letters are the *failing* components (E errors, S symbols, T types).

1. **Content mappers (10, plan 56)**: every `contentMapper*` test. Plan 56 said "don't implement now" because nothing
   real uses the feature; the human's request overrides that. tsgo's harness runs Go test mappers in process
   (`internal/testutil/contentmappertest`, ~1.6k lines: `compiler-test-mapper`, `verbatim-mapper`, failing,
   supplemental...; `spawner.go` serves them over a `net.Pipe` with the real `internal/ipc` JSON-RPC). Don't port
   them to C: build a tiny Go `main` in the `test/oracle` overlay that serves `contentmappertest`'s handler for
   `argv[1]` on stdin/stdout, and have tttt's suite harness map a mapper's `exec[0]` to that binary. Then tttt
   implements the real host (spawn, JSON-RPC, span maps, supplemental files, diagnostics mapped back) and the suite
   tests it end to end. Two sessions; scenarios.py's skipped content-mapper scenario comes with it.
2. **Emit order and declaration emit (~12)**: tests whose expected output comes from tsgo's harness emitting first
   (harness.md "Emit before diagnostics": circularBaseTypes, incorrectRecursiveMappedTypeConstraint,
   typeParameterWithInvalidConstraintType, recursiveMappedTypes, manyCompilerErrorsInTheTwoFiles?), and the
   declaration-emit diagnostics or `.d.ts`-printed types (TS4023: cjsObjectDefinePropertyPrivateModuleDeclarationEmit,
   jsDeclarationsTypeReassignmentFromDeclaration2; TS9013 isolatedDeclarationsJsThisPropertyAssignmentInference;
   TS5088/TS7056 reverseMappedDeclarationSerialization, commonJSImportNestedClassTypeReference;
   hugeDeclarationOutputGetsTruncatedWithError, declarationEmitOptionalParameterUndefined, declarationEmit*). These ride
   on **plan 64**: once the writer exists, the harness emits declarations before diagnostics when the test emits, like
   tsgo's `harnessutil.CompileFiles`. Re-bucket after plan 64 stage 2; don't chase them before.
3. **Type display, types/symbols only (~32, bugs/display.md)**: the largest bucket and the classic fan-out material:
   `scripts/displaydiff.py --fine` ranks them. Includes the `*NoCrash`/recursive-conditional printing depth tests.
4. **Missing checks and circularity (17, bugs/missing_checks.md)** and **type core (9, bugs/type_core.md)**: circular
   constraints and TS2589 depth limits (circularConstraintYieldsAppropriateError, recursiveIndexedAccessSimplification,
   circularInlineMappedGenericTupleTypeNoCrash), TS5114/TS5115 (mutuallyRecursiveInference,
   keyofGenericExtendingClassDoubleLayer: plan 65 is the same mechanism), inference leftovers
   (inferFromGenericFunctionReturnTypes3, contextualTypingGenericFunction2, genericCallWithGenericSignatureArguments3).
5. **JS (15, bugs/js.md)** and **binder/modules (13, bugs/binder_modules.md)**: expandos (TS7022), CommonJS aliases,
   augmentations, late-bound assignment declarations, merged declarations.
6. **Parser/scanner (~5)**: jsxInvalidEsprimaTestSuite (recovery), scannerUnexpectedNullCharacter1 (a NUL byte ends the
   file in tttt: hacks.md), bigintPropertyName (TS1539 bigint keys), varianceAnnotationsWithCircularlyReferencesError,
   arbitraryModuleNamespaceIdentifiers_syntax, jsxParsingErrorImmediateSpreadInAttributeValue.
7. **Harness quirks (3)**: augmentExportEquals2 (tsgo's harness writes the only root first, so a repeated
   `@filename` keeps the *first* copy: harness.md has the mechanism, mirror it), largeControlFlowGraph (check order).
8. Unfiled when this was written: moduleExportsAliasLoop2, circularTypeofWithVarOrFunc, fixSignatureCaching,
   jsxNamespacePrefixInNameReact, objectLiteralShorthandPropertiesErrorFromNotUsingIdentifier: since filed (display.md, js.md, first differing
   line only).

## Stages
1. **Fan-out (one session)**: four Opus agents in worktrees, buckets 3, 4, 5+8, 6+7 (disjoint lists, 0 lost at merge,
   the usual oracles at every merge: vision.md "Fan-out sessions"). Each agent ends by writing, per test it didn't
   fix, one line in its bugs file: the mechanism and the estimated size. Expected: ~40-50 of the ~85 non-feature tests.
2. **Plan 56 (content mappers, through the Go test mappers as above)**: 10 tests, two sessions. Update plan 56 first.
3. **Plan 64** as scheduled; after its stage 2, re-bucket bucket 2 and add the harness's emit-before-diagnostics.
4. **Second fan-out** on what's left; every remaining test gets either a fix, a plan, or a `tsgo_divergences.md`
   argument (see the rule above).
5. After plan 58 (re-pin to 7.1.0 stable, ~2% of the suite moves) the target moves: one fan-out for the new failures.

## Validation
`python3 scripts/passcount.py` prints `full N / N`; `scripts/oracles.py` unchanged or better at every stage (a test
fixed by a rule that loses a sweep project isn't fixed). report.md says when 100% is reached, and what it cost.
