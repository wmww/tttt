# Harness, CLI and tools
- `@pretty: true` is done except manyCompilerErrorsInTheTwoFiles (errors).
- tsc's harness adds `error TS-1: Pre-emit (N) and post-emit (M) diagnostic counts do not match!` with the excess
  diagnostics as related info when the emit resolver adds a semantic error (parseErrorIncorrectReturnToken,
  templateInsideCallback). tttt's harness has no emit and can't produce it; those tests can only pass their errors if
  the harness learns which diagnostics tsc's emit resolver would add. 5 tests. Mechanism (triage 2026-09-29): the TS
  transformer asks `resolver.getConstantValue()` of every property/element access (const enum inlining), which does
  `checkExpressionCached()` on accesses the checker never visited: the operand of a top-level `return` after TS1108
  (multiLinePropertyAccessAndArrowFunctionIndent1, parserStatementIsNotAMemberVariableDeclaration1: TS2304/TS2503 of
  the operand's names, the arrow parameters' annotations included), a recovered method body
  (parseErrorIncorrectReturnToken `n.toString()`). Emulation: after checking, walk the file for property/element
  accesses without a cached type, check them into a separate ErrorCtx, and print the TS-1 header plus `!!! related
  TSnnnn file:line:col: text` lines. templateInsideCallback (TS7012 of JS `@overload`s) and unicodeEscapesInNames02
  (es5, TS2532 at `new Foo().\u{102A7}`) come from other emit paths, not investigated. Low value (harness-only, the
  CLI never shows these).
- STC's `cregex` silently matches nothing once a `-f` pattern gets long (~100 alternatives); the harness says so when
  a filter selected no tests. Use `-f @names.txt`.
- `filetest.sh`/diffapp with a multi-config test file: `run_baseline_test_case()` gets an empty configuration.
- The harness reruns a test when the first run leaves net allocations (lib cache fill), ~170 times per suite run.
  `failstats.py` counts by test *name*; a few names exist in both `compiler/` and `conformance/`, so its counts can
  differ from the harness by one or two (13540 vs 13542).
- The CLI (`program.c`) loads the default lib even when a file says `/// <reference no-default-lib="true"/>`; only the
  harness honours it.
- Option diagnostics go on the tsconfig (program_set_config_file(), option_sites()) since suite checker bugs 6. Left:
  `paths` only an extended config sets isn't validated; libReplacement (`@typescript/lib-*`, 4 tests) isn't
  implemented; APILibCheck wants typescript.internal.d.ts and tsserverlibrary, which only a tsc build has.
- Module resolution leftovers: resolution-mode attributes (~20 tests), `tryLoadInputFileForPath` (outDir mapping back
  to the input `.ts`, 8), Windows rooted paths, tsconfig `references`/`watchOptions`.
- The stdlib checks clean: a cheap regression check for lib-related changes (`./bench.sh`'s `lib_closure` lists the
  84 files to pass as roots; `--no-lib esnext.full.d.ts` alone ignores `/// <reference lib>`).
- Dead-ish code: `display.c`'s syntax printer is only needed for `--print-ast` and written type names
  (`brief_d()` has no callers but is a print-debugging helper: keep). The old type world's double pointers are in
  hacks.md "Old type world leftovers".
- The git stash list holds a foreign entry ("WIP on (no branch): 2d0b590", relate.c, still there 2026-10-05);
  worktree agents share the stash, don't use it.
- `baseline_test_traversal.c` `port_item_add_alias`: the `import type`/`export type` declared-type rule checks the
  resolved symbol for class/interface/enum/alias only; tsc's getDeclaredTypeOfSymbol also takes type parameters and
  enum members. Unverified whether any test reaches that.
- `./tscbaseline.sh` writes no `.symbols` for a file with an interface member named `[-1]` (tsc's symbol writer
  throws), so `./filetest.sh` reports "did not run" without saying why.
- A corpus's output can depend on the checker count (4 by default): a file can look fine alone and fail in the corpus.
  Reproduce with `--checkers 1` and the suspect file first in `files`; `./checkerdiff.py` flags it.
