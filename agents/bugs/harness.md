# Harness, CLI and tools
- `@pretty: true` is done except manyCompilerErrorsInTheTwoFiles (errors).
- tsc's harness adds `error TS-1: Pre-emit (N) and post-emit (M) diagnostic counts do not match!` with the excess
  diagnostics as related info when the emit resolver adds a semantic error (parseErrorIncorrectReturnToken,
  templateInsideCallback, constructorWithIncompleteTypeAnnotation (its TS7017 only post-emit),
  unicodeEscapesInNames02(target=es5) (TS2532 at `new Foo().\u{102A7}`)). tttt's harness has no emit and can't produce it; those tests can only pass their errors if
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
  `paths` only an extended config sets isn't validated; libReplacement (`@typescript/lib-*`:
  libTypeScriptOverrideSimple, libTypeScriptSubfileResolving and their `Config` variants; a test's `/// <reference
  lib>` can't add to the harness's shared lib program either) isn't implemented; APILibCheck wants
  typescript.internal.d.ts and tsserverlibrary, which only a tsc build has.
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
- tsconfig.c reads package.json leniently for `extends`; `json_parse_strict()` matches parseJsonText's syntax errors
  by hand (escapes, odd numbers unchecked). Not investigated.
- The suite's programs share a cached library program (program_copy_globals_from); since 2026-10-07 a Program's
  `order_base` puts the test's files after the libraries in program_order, like tsc's fileIndexMap. Anything else
  that numbers files per program (file_shares, checker lanes) still counts from 0: fine as long as it's only an index.
- tsc's harness always sets noErrorTruncation (harnessIO compileFiles), whatever the test says; the suite sets
  CompilerOptions.no_error_truncation the same way. The CLI default (false) keeps ty_display.c's depth stand-ins for
  the 160-character truncation (HACK: by depth, not length; and no `... N more ...` lists, no TS2719 when both sides
  truncate to the same text: flagflip vscode/eopt's TS2719->TS2322 x3). A real approximateLength model would fix both.
- largeControlFlowGraph (types): tsc's first deep flow query comes from the end of the file (its checker reaches the
  last statement's reference first through some deferred check), so every `data` is errorType there; tttt's from the
  2000th statement. Check order, not a flow bug; one test.
- Moved from hacks.md (hacks audit 2026-10-08), missing program/config diagnostics:
  - `/// <reference lib>` with an unknown name is silently ignored (tsc: TS2726); `@libReplacement: true` is
    unimplemented (libTypeScriptOverrideSimple & co.).
  - Config syntax: not reported are syntax errors in configs it extends (no SourceFile for them), tokens after the root
    value (tsc's TS1012 + TS5092), TS5092 for a non-object root.
  - Project references: `program_error_explaining_file()` gives a root file's TS6059/TS6307 without tsc's "The file is
    in the program because: Matched by include pattern ..." chain (the program doesn't know which spec matched).
    Config diagnostics of references are only for the program's own config's direct references (tsc walks them
    transitively, reported on the parent config).
