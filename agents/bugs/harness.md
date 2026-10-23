# Harness, CLI and tools
(pruned 2026-10-09 against the tsgo 7.1 suite: tsgo's harness has no TS-1 pre/post-emit count line.)
- `@pretty: true` is done.
- Emit before diagnostics: tsgo's harnessutil.CompileFiles emits (postProgram.Emit) before collecting the semantic
  diagnostics it baselines, and the emit resolver checks expressions (a property access's constant value, `x.foo`)
  first. What's resolved first then shows: TS2313's "Circularity originates in type at this location" (TS2751, tsgo's
  getResolvedBaseConstraint relates it to currentNode when that's elsewhere) points at the first property access
  through the type parameter in typeParameterWithInvalidConstraintType,
  incorrectRecursiveMappedTypeConstraint and recursiveMappedTypes, and a `new x()` with `T extends T` says `{}` there
  but `unknown` in the CLI. tttt (noEmit) checks in the CLI's order; the mechanism (record the current node when
  the constraint walk finds the cycle, relate it in generic_typecheck()) was tried 2026-10-12 and dropped: it can't
  match without emulating the emit resolver's queries.
- Shared lib programs (baseline_test_case.c's `my libs`, bound once with their `declare global` augmentations
  already in the globals) put a lib augmentation's declarations before a test file's script declarations of the same
  global; tsc (and the CLI) merges script locals first, augmentations after. Seen: es2026.json's `declare global {
  interface RawJSON extends RawJSONInstance {} }` with a test's own `interface RawJSON { readonly rawJSON: number }`
  (mutant jsonRawJSON2 ann-string-number): the CLI reports TS2430 at the test's interface, the harness picks the lib's
  as the first declaration and reports nothing (interface_heritage_typecheck()). Any first-declaration choice can differ
  so (valueDeclaration, TS2717's first type). Fix: merge the test's globals in front of the lib's augmentation
  declarations, or keep those apart until the test program merges them.
- `FAST=1 scripts/test.sh -f '.*import[A-Z].*'` (some test in that set) ends with valgrind's "131 bytes in 3 blocks
  definitely lost" from symbol_text() in namespace_decl_compile_test_info (baseline_test_traversal.c), at the
  base commit too: a leaked cstr in the symbols walk.
- STC's `cregex` silently matches nothing once a `-f` pattern gets long (~100 alternatives); the harness says so when
  a filter selected no tests. Use `-f @names.txt`.
- `filetest.sh`/diffapp with a multi-config test file: `run_baseline_test_case()` gets an empty configuration.
- The harness reruns a test when the first run leaves net allocations (lib cache fill), ~170 times per suite run.
  `failstats.py` counts by test *name*; a few names exist in both `compiler/` and `conformance/`, so its counts can
  differ from the harness by one or two (13540 vs 13542).
- The harness decides a test's libs before loading it (the libs are a shared cached program): every `/// <reference
  lib>` in the test's text adds one (baseline_test_case.c), whether or not its file ends up in the program. Since the
  7.1.0-beta's defaultTypesExclude*LibraryReferences tests, one in a `node_modules/@types/X/` unit doesn't unless
  `@types` names X or `*` (tsgo's default `types` is empty); an @types package an import loads would be missed
  (no test has one). Correct: libs a test's program can add to.
- The CLI (`program.c`) loads the default lib even when a file says `/// <reference no-default-lib="true"/>`; only the
  harness honours it.
- Option diagnostics go on the tsconfig (program_set_config_file(), option_sites()) since suite checker bugs 6. Left:
  `paths` only an extended config sets isn't validated; APILibCheck wants
  typescript.internal.d.ts and tsserverlibrary, which only a tsc build has.
- Module resolution leftovers: resolution-mode attributes (~20 tests), `tryLoadInputFileForPath` (outDir mapping back
  to the input `.ts`, 8), Windows rooted paths, tsconfig `references`/`watchOptions`.
- The stdlib checks clean: a cheap regression check for lib-related changes (`scripts/bench.sh`'s `lib_closure` lists the
  84 files to pass as roots; `--no-lib esnext.full.d.ts` alone ignores `/// <reference lib>`).
- Dead-ish code: `display.c`'s syntax printer is only needed for `--print-ast` and written type names
  (`brief_d()` has no callers but is a print-debugging helper: keep).
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
  CompilerOptions.no_error_truncation the same way. The CLI default (false) does
  tsgo's 160-character truncation (approximateLength ported, `display_over()`). Whether TS2719 now fires where both
  sides truncate to the same text (flagflip vscode/eopt's TS2719->TS2322 x3) is unchecked.
- tsgo's flowAnalysisDisabled (set at flow depth 2000, restored only by a function or module body's checkBlock)
  persists in its checker after a file whose top level overflowed: later files of that checker get no flow
  analysis either. tttt resets it per file (checker-count independence); its types walk emulates the persisting flag
  for the file itself (largeControlFlowGraph). Not observed across files anywhere.
- Moved from hacks.md (hacks audit 2026-10-08), missing program/config diagnostics:
  - `/// <reference lib>` with an unknown name is silently ignored (tsc: TS2726).
  - Config syntax: not reported are syntax errors in configs it extends (no SourceFile for them), tokens after the root
    value (tsc's TS1012 + TS5092), TS5092 for a non-object root.
  - Project references: `program_error_explaining_file()` gives a root file's TS6059/TS6307 without tsc's "The file is
    in the program because: Matched by include pattern ..." chain (the program doesn't know which spec matched).
    Config diagnostics of references are only for the program's own config's direct references (tsc walks them
    transitively, reported on the parent config).
- Widening the DefinitelyTyped oracles (from the DT breadth sweep, not done): install more real npm dependencies for the
  packages tsc rejects with TS2307/TS2688 (webpack 5, postcss, moment...: regenerate
  `corpora-locks/DefinitelyTyped.package.json` and rerun `scripts/sweep.py --tsc -f` for the affected packages; tens
  of packages each); beyond DT, the shipped `.d.ts` of the top-N npm packages with a generated test importing each.
  `scripts/expecttypes.py` skips the ~600 assertions on declarations (`DECLARATION_KINDS`: type aliases, parameters,
  binding elements...): comparing them needs getTypeAtLocation of a declaration on tttt's side (`src/expect_types.c`).
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

- dtsdiff/clidiff `--emit` sandbox flake (2026-10-19, once): bwrap "Can't bind mount /oldroot/... remount
  /newroot/run/credentials/serial-getty@ttyS1.service: Invalid argument" (a host mount bwrap can't remount
  read-only) made a whole corpus "no output". Not seen again. If it recurs: bind only the dirs the probe needs
  instead of `/`, or retry the bwrap call once on that message.
## TS 7.1 retarget leftovers (2026-10-09)
- `--showConfig`, `--explainFiles`, `--watch`, `--init` are refused (exit 1): the CLI modes (parked: todo.md) sizes them. `--listFiles` and
  `--listFilesOnly` match tsgo on probes (2026-10-16) but no oracle compares them.
- `tsgo_skips_configuration()` still reads module/moduleResolution as written to skip tsgo's removed configurations;
  `@module: none` no longer parses (TS6046), so such configurations run with the default module unless they have no
  baselines (all 7 `@module: none` tests have none).
- If a TS2589 runaway needs tsc's per-node instantiation budget again (the deleted tsc 6 `scripts/tscinstcount.js`),
  port it as a `test/oracle` overlay: a patched copy of `internal/checker/checker.go` logging `c.instantiationCount`
  where it's reset (scratch branch, never committed; test/oracle/README.md's "instrument the oracle").
- scenarios: 210 passed, 0 failed, 14 skipped (2026-10-14). The skips are harness artifacts (tsc's fake libs, a fake
  content-mapper executable, Windows drive-letter paths) or unsupported build modes (`--clean`, `--dry`: both need build
  info tttt doesn't write; TS6377). `--dry` would print "A non-dry build would build project ..." per project.
- clidiff `--sample 4` text mismatches left (2026-10-14; nodemailer, nest, vitest/browser-preview fixed):
  - rxjs/packages/rxjs: fixed at K1 2026-10-18 (b3c2fbfa); its K dependence is tsgo's own (tsgo_divergences.md).
  - es-toolkit isEmpty.spec.ts:56: `{ constructor: { (): void; prototype: ...; }; }` (tsc) against `prototype: any`.
    `function Foo() {}; Foo.prototype = { constructor: Foo }`: tsc types `{ constructor: Foo }` while Foo's type is
    in flight (the expando `prototype` reads itself: any, printed `...` at depth); tttt's types baseline shows it
    resolving Foo fully (`{ constructor: typeof Foo }`). A probe of those lines alone prints `any` in both.
  - gemini-cli eval-coverage/eval-inventory.test.ts: `location: { line: number; column: number; }` (tsc) against
    `location: EvalSourceLocation`: the merged optional spread property is the subtype-reduced union of the literal's
    `{ line: number; column: number }` and the interface; which one survives (tsc's removeSubtypes with the strict
    subtype relation, identical structures) differs.
  - vitest/test/coverage-test, query-codemods (older entries, not rechecked).
  - apollo-client/integration-tests/vite: truncation (`DeepPartial<...>` in tsc, printed in full in tttt) and two
    missing `'undefined & TData' is not assignable to type 'RegExp & ...'` lines in a deep TS2322 chain (also at the
    an earlier binary, deterministic).
- File include explanations (program_error_explaining_file()): type reference directives, automatic type directives
  and lib files aren't recorded as reasons.
- source sweep `+TS6059` (react-router/packages/create-react-router, 11 projects in 2 repos): a file outside rootDir
  brought in by an import (`tsdown.config.ts` imports `../../build.utils.ts`) is TS6059 at the import in tttt, nothing
  in tsgo; a root file outside rootDir is TS6059 in both. tsgo adds it as a processing diagnostic from
  CommonSourceDirectory(), maybe only for files known at that point.
- failstats.py's totals still disagree with the harness: `pass: all 13595 (of 13846 ran)` against the harness's 13316 /
  13571 (2026-10-11). The errors-only tests without baselines and the skipped configurations are counted differently.
  Make failstats read the harness's saved results for its totals.
- TS2880 (`assert` import attributes) is a parse error in tsgo (parser.go: import declarations, import types,
  `export ... from`); tttt reports it from the checker (`port_statement.c`, `several_types.c`). The suite matches now,
  but on the CLI a syntactic diagnostic gates semantic checking, so a project with `assert` gets other diagnostics in
  tttt than in tsgo. Move it to the parser (the dynamic `import(m, { assert })` one stays the checker's in tsgo too).
- TS7016 at a package's import of its own name (`@vitest/browser/internal/vendor-types` from inside
  vitest/packages/browser, resolved through its own exports) lacks tsc's TS7058 chain line ("If the '@vitest/browser'
  package actually exposes this module, try adding..."): the self-name resolution leaves `ModuleRef.package_name`
  empty, so `program.c`'s package hint pass skips it (vitest/test/e2e/dts/browser-playwright).
- `-b` with noEmitOnError in an upstream project with errors: tsgo reports TS6305 'Output file ... has not been built
  from source file' at the downstream import (its .d.ts wasn't written); tttt reads referenced projects from their
  sources and doesn't. scenarios.py leftovers (each also a diagnostics mismatch): dependencyUpdate/...absolute-non-
  root-dependency, inferredTypeFromMonorepoReference, projectReferenceRedirect/...multiple-sub-projects.
- npm 12 defaults to `allow-remote=none`: a corpus lock entry resolved outside registry.npmjs.org breaks
  `thirdparty/ensure-corpora.sh`. bulletproof-react's (inherited from upstream's yarn.lock, npmmirror) was rewritten
  2026-10-11; `--relock` may bring it back (rewrite resolved URLs to registry.npmjs.org after relocking).
- defaultRootDirInheritedInclude (new in the beta, `@currentDirectory` with the config in a subdirectory extending
  one above): tsgo's harness prints the include reason's config absolute (`'/project/example/tsconfig.json'`), tttt
  relative (`'example/tsconfig.json'`), and the baseline's file order puts the root before the extended config.
  Harness path display only.
- skippedCircularMatcherBases (new in the beta, a circular interface base through `ReturnType<typeof
  expect.containing>` in a skipLibCheck'd .d.ts): errors pass and the CLI agrees with tsgo, but the types baseline
  prints `expect.containing : error` where tsgo has the method type: the harness's types walk meets the circular
  resolution in a different order.
