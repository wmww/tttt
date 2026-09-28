# Harness, CLI and tools
- **`@pretty: true`** (14 tests: prettyFileWithErrorsAndTabs, duplicateIdentifierRelatedSpans1-7,
  manyCompilerErrorsInTheTwoFiles, multiLineContextDiagnosticWithPretty...): the errors baseline starts with tsc's
  formatDiagnosticsWithColorAndContext output (ANSI colours, source line with gutter, `~` underline, related info,
  "Found N errors") before the usual section. The CLI could use the same formatter.
- `error.c` "Error added with invalid span": 29 lines per suite run (2026-09-28), in two tests:
  modularizeLibrary_ErrorFromUsingES6ArrayWithOnlyES6ArrayLib (`@lib: es2015.core` alone: 27 diagnostics inside lib
  files, `Cannot find name '_'`, which error_ctx_error_s() drops; harmless, but find what in the lib refers to `_`)
  and reservedWords3 (plan 27 leftovers). `grep "invalid span" build/failstats/stderr.txt`.
- `@baselineFile`, `@fullEmitPaths` are ignored; bad option values are ignored (tsc's harness throws).
- `filetest.sh`/diffapp with a multi-config test file: `run_baseline_test_case()` gets an empty configuration.
- The harness reruns a test when the first run leaves net allocations (lib cache fill), ~170 times per suite run.
  `failstats.py` counts by test *name*; a few names exist in both `compiler/` and `conformance/`, so its counts can
  differ from the harness by one or two (13540 vs 13542).
- The CLI (`program.c`) loads the default lib even when a file says `/// <reference no-default-lib="true"/>`; only the
  harness honours it.
- Diagnostics without a file that depend on emit paths: TS5055 (38 baselines, "would overwrite input file"), TS5056,
  TS6054, TS6082/TS5053/TS5069 (outFile/sourceMap/declaration combinations), TS5101/TS5107 under
  `ignoreDeprecations`. With a tsconfig tsc puts option diagnostics on the option's json span; tttt skips them.
- Module resolution leftovers: resolution-mode attributes (~20 tests), `tryLoadInputFileForPath` (outDir mapping back
  to the input `.ts`, 8), Windows rooted paths (3), tsconfig `references`/`watchOptions`.
- The stdlib checks clean: a cheap regression check for lib-related changes (`./bench.sh`'s `lib_closure` lists the
  84 files to pass as roots; `--no-lib esnext.full.d.ts` alone ignores `/// <reference lib>`).
- Dead-ish code: `display.c`'s syntax printer is only needed for `--print-ast` and written type names
  (`DISPLAY_TYPE_MAX_DEPTH`, `BriefType` may be dead).
  Nodes aren't cloned any more: collapse `owned_symbol`/`symbol_ref` (`GenericItem`, `InferredType`, mapped keys) and
  `key.expr.owned`/`ref` into one pointer each; drop `LAZY_TYPE_FUNCTION`.
- The git stash list holds a foreign entry ("WIP on (no branch): 2d0b590", relate.c); worktree agents share the
  stash, don't use it.
- `baseline_test_traversal.c` `port_item_add_alias`: the `import type`/`export type` declared-type rule checks the
  resolved symbol for class/interface/enum/alias only; tsc's getDeclaredTypeOfSymbol also takes type parameters and
  enum members. Unverified whether any test reaches that.
