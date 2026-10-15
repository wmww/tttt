# Leftovers of the tsc 6 era in the corpora
tsgo 7.1 is the oracle since plan 54 (the corpora compare against its own cached diagnostics, `test/corpora.tsc.jsonl`).
What the old tsc 6 oracle forced on the corpora and could now be undone:

- **An Effect 4 corpus** (effect `main`): tsgo-clean where tsc 6 wasn't, heavy (tsgo 8.6 s on its src): a perf corpus
  candidate next to vscode.
- **zod-root**: tsgo 7.0.2 ran away on it (18 GB); the 7.1 cache exists, measurements.md has tttt's count. Which
  package blew 7.0.2 up was never bisected: a perf hazard worth a look if tttt slows down there.

# Suite tests whose expected output depends on tsgo's JS emit (plan 66 bucket 2, 2026-10-15)
tsgo's harness (`harnessutil.CompileFiles`) emits before it collects the diagnostics it baselines, and its emit is the
JS emitter's and the declaration transformer's, file by file. Their resolver queries (a property access's constant
value, alias references for import elision, the types the declaration transformer prints) resolve types before the
checker walks the files, which moves where a circularity is first found. Tried 2026-10-15 (todo entry 8): running
tttt's declaration walk on every file before checking any (`program_set_emit_first()`, a harness-only switch, not
kept) fixed none of these and broke mappedTypeWithAsClauseAndLateBoundProperty2. Three of them have no declaration emit
at all, so only the JS emitter's queries can explain their order, and JS emit is out of scope (the human,
vision.md). Argued out of reach unless JS emit ever exists:
- incorrectRecursiveMappedTypeConstraint, typeParameterWithInvalidConstraintType, manyCompilerErrorsInTheTwoFiles (no
  `@declaration`): TS2313's related TS2751 location, extra TS2313s.
- circularBaseTypes, recursiveMappedTypes (`@declaration`, but the JS emitter runs first per file): the extra TS2313 for
  a mapped type's key over the interface being resolved.
`scripts/tscbaseline.sh` (tsgo's CLI order: check, then emit) reproduces tttt's output for them, which is the
evidence that only the emit-first order differs.
