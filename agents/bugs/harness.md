# Harness, CLI and tools
(pruned 2026-10-09 against the tsgo 7.1 suite: tsgo's harness has no TS-1 pre/post-emit count line.)
- `@pretty: true` is done except manyCompilerErrorsInTheTwoFiles (errors).
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
- The stdlib checks clean: a cheap regression check for lib-related changes (`scripts/bench.sh`'s `lib_closure` lists the
  84 files to pass as roots; `--no-lib esnext.full.d.ts` alone ignores `/// <reference lib>`).
- Dead-ish code: `display.c`'s syntax printer is only needed for `--print-ast` and written type names
  (`brief_d()` has no callers but is a print-debugging helper: keep). The old type world's double pointers are in
  hacks.md "Old type world leftovers".
- The git stash list holds a foreign entry ("WIP on (no branch): 2d0b590", relate.c, still there 2026-10-05);
  worktree agents share the stash, don't use it.
- `baseline_test_traversal.c` `port_item_add_alias`: the `import type`/`export type` declared-type rule checks the
  resolved symbol for class/interface/enum/alias only; tsc's getDeclaredTypeOfSymbol also takes type parameters and
  enum members. Unverified whether any test reaches that.
- `scripts/tscbaseline.sh` writes no `.symbols` for a file with an interface member named `[-1]` (tsc's symbol writer
  throws), so `scripts/filetest.sh` reports "did not run" without saying why.
- A corpus's output can depend on the checker count (4 by default): a file can look fine alone and fail in the corpus.
  Reproduce with `--checkers 1` and the suspect file first in `files`; `scripts/checkerdiff.py` flags it.
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
- Widening the DefinitelyTyped oracles (ex plan 44 stage 3, not done): install more real npm dependencies for the
  packages tsc rejects with TS2307/TS2688 (webpack 5, postcss, moment...: regenerate
  `corpora-locks/DefinitelyTyped.package.json` and rerun `scripts/sweep.py --tsc -f` for the affected packages; tens
  of packages each); beyond DT, the shipped `.d.ts` of the top-N npm packages with a generated test importing each.
  `scripts/expecttypes.py` skips the ~600 assertions on declarations (`DECLARATION_KINDS`: type aliases, parameters,
  binding elements...): comparing them needs getTypeAtLocation of a declaration on tttt's side (`src/expect_types.c`).
- instanceofOperator (UTF-16 LE with a BOM) "did not run": the harness doesn't read the `// @target: es5, es2015`
  directive out of a UTF-16 file, so it looks for an unsplit `instanceofOperator.symbols` while tsgo's baseline is
  `instanceofOperator(target=es2015)` (es5 is a skipped configuration). The bom-utf16 tests pass (no options).
- Errors baseline of a file with both `\n` and lone `\r` breaks: tsc's iterateErrorBaseline splits its lines on `\n`
  only (a lone `\r` stays inside a printed line) while its squiggles index computeLineStarts (every break), so lines
  and squiggles drift apart; tttt places squiggles by every break. No suite test has such a file (a file with only
  lone `\r` breaks is split on `\r` like tsc's, render_lines_woven_with_file).
- Config diagnostics (2026-10-08 triage, after TS5023/TS5025 and the root-file chains): a diagnostic in a config that
  is *extended* has no location (tsc reports it in that file: `base.json(1,24): error TS5025`); `-p /abs/dir` names
  the config relative to the working directory in "Matched by include pattern '...' in '...'" where tsc keeps the
  path as given (absolute). The CLI's unknown option exits before anything else (tsc too) but doesn't know tsc's
  short names (`-p`, `-b`, `-w`... are tttt's own handling). tsc's TS5024 (an option given a value of the wrong type)
  and watchOptions/typeAcquisition keys aren't checked.

## TS 7.1 window leftovers (triage 2026-10-09)
- `--showConfig`, `--explainFiles`, `--watch`, `--init` are refused (exit 1). `--listFiles` prints tttt's own lib paths
  (thirdparty/lib/X.d.ts, not lib.X.d.ts) in tttt's file order: not compared against tsc.
- `tsgo_skips_configuration()` still reads module/moduleResolution as written to skip tsgo's removed configurations;
  `@module: none` no longer parses (TS6046), so such configurations run with the default module unless they have no
  baselines (all 7 `@module: none` tests have none).
- If a TS2589 runaway needs tsc's per-node instantiation budget again (the deleted tsc 6 `scripts/tscinstcount.js`),
  port it as a `test/oracle` overlay: a patched copy of `internal/checker/checker.go` logging `c.instantiationCount`
  where it's reset (scratch branch, never committed; test/oracle/README.md's "instrument the oracle").
- scenarios 209 / 213 left: tsbuild/dependencyUpdate/rebuilds-when-absolute-non-root-dependency-is-updated (spurious
  TS2307 for an absolute import of a non-root dependency under -b), tsbuild/inferredTypeFromMonorepoReference (spurious
  TS2883, declaration emit across references), tsbuild/projectReferenceRedirect/uses-correct-project-reference-
  redirect-when-file-belongs-to-multiple-sub-projects (spurious TS2322: a file in two sub-projects should take the
  redirect of the project that imports it), tsbuild/contentMapperOptionDiagnostics (plan 56).
- clidiff `--sample 4` text mismatches (type printing in messages): nodemailer (`typeof nmfetch` where tsc prints the
  function's structural type `{ (url...): ...; DEFAULT_TIMEOUT }` for a function with expando properties),
  query-codemods (`import('jscodeshift')` quoting/union order in a long TS2345), vitest/browser-preview (TS2339 type
  text), vitest/test/coverage-test (an extra "is missing the following properties" line under a TS2345).
- File include explanations (program_error_explaining_file()): type reference directives, automatic type directives
  and lib files aren't recorded as reasons.
- source sweep `+TS6059` (react-router/packages/create-react-router, 11 projects in 2 repos): a file outside rootDir
  brought in by an import (`tsdown.config.ts` imports `../../build.utils.ts`) is TS6059 at the import in tttt, nothing
  in tsgo; a root file outside rootDir is TS6059 in both. tsgo adds it as a processing diagnostic from
  CommonSourceDirectory(), maybe only for files known at that point.
