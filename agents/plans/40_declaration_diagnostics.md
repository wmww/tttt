# Plan 40: declaration-emit diagnostics without emit (planning stub)
Brief by the insights of 2026-09-29. **A stub for a planning session**: investigate, decide, replace this file with
the real plan (or delete it and route the items to `bugs/` if it isn't worth it).

## Why
Declaration emit (writing `.d.ts`) stays out of scope, but the *diagnostics* tsc's declaration emitter reports are
checking, and real users see them: every library built with `declaration: true` gets TS2742 ("The inferred type of X
cannot be named without a reference to ..."), TS4xxx (private name used in an exported declaration) and, with
`isolatedDeclarations`, TS9xxx. tttt reports none of them. In the suite (failstats of 2026-09-29) **45 failing tests
involve these codes, 41 of them fail only on them**: TS2742 10, TS9007 6, TS9010/9038/9011/9013 4 each, TS4023 3,
TS9023 3, TS4094/4025/4109/2527 2...

## What tsc does (to confirm)
The declaration transformer walks every exported declaration and, where a type is inferred rather than written,
serializes it through the checker's typeToTypeNode (the same machinery tttt's type printer and `symbol_chain.c`
already approximate for the types baseline), asking isSymbolAccessible / isEntityNameVisible for each symbol it
names; a failure is the TS4xxx diagnostic of the declaration kind, TS2742 when the symbol is reachable only through a
module that isn't a direct dependency. isolatedDeclarations (TS9xxx) reports where an inference would be needed at
all, mostly syntactic.

## Questions for the planning session
- Can a diagnostics-only walk (no text output) reuse `ty_display.c`/`symbol_chain.c`'s accessibility logic, or does
  tsc's accessibility (isSymbolAccessible with its alias/export chains) need its own port? Size it.
- isolatedDeclarations first? It is mostly syntactic and is 20+ of the 45 tests.
- Does any pinned corpus build with `declaration: true` (zod, valibot, type-fest are libraries)? If so, it is also a
  real-world oracle for TS2742 false positives: they must stay at 0.
- Perf: the walk only runs with `declaration`/`isolatedDeclarations`; measure its cost on a corpus that enables it.
- Validation: the tests above via `./failstats.py --code TS2742` etc., 0 lost, corpora unchanged.
