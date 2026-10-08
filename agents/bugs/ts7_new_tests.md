# Crashes and runaways in tsgo's new suite tests
Found by plan 54's prototype (2026-10-08): the native suite at `thirdparty/typescript` (fed0bf24) through tttt
0cb63c56. These are tests tsgo added after 4d4f005c, regressions for shapes tsgo itself once crashed on, so they're
real tttt bugs, not oracle noise. Plan 54 stage 1 has to fix them: the suite can't run with a segfault in it.

- **Segfault** (release build, `scripts/findcrash.py` on the native tree): `forOfSelfReferentialDottedName`
  (`for (const a of a) { a(); a; }`), `templateLiteralTypeExcessiveLength`, `unreachableFlowAfterThrowingForHead1`,
  `unreachableFlowAfterThrowingForHead2`, `unreachableFlowAfterThrowingForInOfHead1`,
  `unreachableFlowAfterThrowingForInOfHead2`, `variadicTupleMismatch`. All in `tsc/testdata/tests/cases/compiler`.
  The four `unreachableFlowAfterThrowing*` are probably one control-flow bug (a `throw` in a `for` head).
- **63 s**: `intersectionConstructorReductionCrash` (passes, but is 85% of the suite's wall time).
  `declarationEmitOversizedCachedType` 4.5 s. Check them with `TTTT_FILE_TIMES=1` / callgrind: a runaway the suite
  survives can be a corpus hang.
