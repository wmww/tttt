# Plan 64: `.d.ts` emit (`tsc --emitDeclarationOnly` as a drop-in)

Scoped 2026-10-14 (todo entry 9). **Recommendation: yes, TypeScript sources first, in the next window, five stages of
about one session each (stage 2 may take two); JavaScript declaration emit only if the data after stage 4 asks for
it.**

## Status (2026-10-15, todo entry 7): stages 1-3 done
- **Stage 1 done** (2026-10-14). Suite `dts` component (1408 TS tests have declaration outputs, 344 with JS inputs counted
  apart; `baseline_tests --dts`, `scripts/dtsstats.py` buckets failures), `scripts/dtsdiff.py` (1233 projects, 76294
  files; tsgo runs in a `bwrap` sandbox), `tttt --dts-dump DIR`.
- **Stages 2 and 3 done**: the writer is `src/ty/declaration_emit.c`'s second walk, comments are `src/ty/decl_comments.c`
  (documentation.md "Declaration emit"). **dts 1345 / 1408 (95.5%); dtsdiff 922 / 1233 projects, 74279 / 76294 files
  (97.4%).** Corpora, sweeps and instructions unchanged.
- **Left in the suite (63)**: output paths (`@fullEmitPaths`-like section names, relative reference paths: 15 tests,
  stage 4), declarationMap (8, stage 5), then singles: late bound names through an alias of a property of a variable's
  type (`[Op.or]`), an export= namespace's member naming the unknown symbol, index signatures with several parameters
  (`giant`), error-recovery shapes (`export import "m"`, `export * from X`, `[element: string?]`), a painted
  non-exported binding pattern written whole (`declare const { foo }: ...`, 3 tests), union member comments (TypeNode has no span), a mixin's private class members
  (`declarationEmitLocalClassDeclarationMixin`), `this is X` from a reverse mapped type, and a few checker types.
  `scripts/dtsstats.py` lists them.
- **Left on real code** (dtsdiff buckets): "missing file" 693 (JavaScript sources: stage 6), "type text" ~630 (mostly
  checker types: an IIFE's narrowed literal union printed widened (tttt's narrowed literals are fresh where tsgo's
  return-type reads are regular: triage.md), zod's mapped-type key order, `import()` specifiers naming an internal
  path where tsgo names the package (`import("@apollo/client")`)), "member order" ~170 (tsgo's getNamedMembers sorts by
  compareSymbols: class instance types of mixins and zod-style flattened mapped types list members in another order),
  "import" ~200 (painting gaps: names printed from types that track nothing), "comment" ~180, "other" ~140.

## Why
- The human left `.d.ts` emit to us (vision.md); JS emit stays out of scope (esbuild and co.).
- It's a real workflow, not a niche one. In the source sweep's repos (`thirdparty/sources`), 717 tsconfigs set
  `declaration`/`composite`, 87 set `emitDeclarationOnly` (~40 of ~150 repos: apollo-client, arktype, astro, babel,
  date-fns, hono, kysely, lexical, octokit, openai-node, query, react-router, redux, solid, trpc, vueuse...),
  188 set `declarationMap`, 66 `stripInternal`, 24 `isolatedDeclarations`. "Bundler for JS, `tsc --emitDeclarationOnly`
  for types" is what these libraries do. Today tttt can check those projects but can't stand in for that build step.
- Most of the hard part already exists. `src/ty/declaration_diagnostics.c` (2.6k lines, plan 40) walks exactly what
  tsgo's declaration transformer walks: visible statements, emitted members, late painting (`paint()`: non-exported
  declarations an emitted one names), expandos. It serializes each declaration's type with the pseudochecker's
  equivalence (`ty/pseudo.c`, plan 55: `serialize_declaration()`, `serialize_return()`, `serialize_parameter()`),
  reuses written annotations (`display_annotated()`/`display_node()` print TypeNodes with tsc's printer layout), and
  runs the symbol tracker (`DeclTracker`: accessibility, `import("...")` specifiers via `module_specifiers.c`). It
  prints all of that into scratch strings and throws them away. What's missing is the writing.
- Emit is cheap in tsgo (single-threaded: drizzle check 2.4 s / emit 0.14 s, xstate 0.87 / 0.04, TypeScript6's
  compiler 1.0 / 0.2). tttt should keep that ratio. tttt's checking lead carries over to the whole build.
- What it doesn't buy: `tsc -b` / `tsc` without `emitDeclarationOnly` also emit JS, so those builds stay out of reach.
  (248 `tsc -b`/`--build` uses in the repos' package.json scripts. Most also emit JS.)

## What tsgo has that tttt doesn't (sizes are Go lines, for scale)
- `transformers/declarations/transform.go` (3.0k) + `util.go`, `diagnostics.go` (the latter ported already as
  plan 40), `tracker.go` (ported as `DeclTracker`). The parts not yet in tttt: modifier rewriting (`declare`,
  `export`, stripping `async`/`public`/`override`, `stripExportModifiers`, `ensureModifiers`), bodies and
  initializers dropped (`shouldPrintWithInitializer`: `declare const x = 1`, readonly static literals), class members
  (`private x;` with no type, parameter properties to fields, `#private;` for any ES private member, accessor pairs,
  `omitPrivateMethodType`), overloads hiding the implementation, enums with evaluated member values, namespaces,
  `export default <expr>` -> `declare const _default: T; export default _default;`, `export = `, import elision
  (an import is kept only for the specifiers emitted text uses), `export {};` when a module would otherwise read as a
  script, triple-slash references (kept, plus `/// <reference types=... />` for types the printed text uses:
  `getTypeReferences`/`getLibReferences`), `stripInternal`, `declare global`, augmentations. The CommonJS/expando/
  JSDoc parts (`transformCommonJSExport*`, `transformExpando*`, `transformJSDoc*`, ~900 lines) are JS emit.
- `printer/printer.go` (6.3k, but most of it is JS expressions and statements): tttt needs only declarations, type
  nodes (it has these: `display_node()`), the few expressions a `.d.ts` keeps (literal initializers, computed property
  names, heritage `a.b.C<T>`, `export =` names), and **comments** (leading/trailing/detached, JSDoc kept on members and
  parameters: `commentsFunction(target=es2015).js` shows the shape).
- The node builder in declaration mode (`declarationEmitNodeBuilderFlags`: MultilineObjectLiterals,
  WriteClassExpressionAsTypeLiteral, UseTypeOfFunction, UseStructuralFallback, AllowEmptyTuple,
  GenerateNamesForShadowedTypeParams, NoTruncation; internal AllowUnresolvedNames). tttt's `TyDisplay` prints single
  line (types baseline, messages). It needs a multi-line mode with indentation. A type literal reused from source
  prints multi-line too (the printer has no SingleLine flag on it): `x: {a: number}` comes out as
  `x: {\n    a: number;\n}`.
- `outputpaths/` (240), `compiler/emitter.go` (560): output file names (outDir, declarationDir, rootDir / common
  source directory, `.d.mts`/`.d.cts`, json), noEmitOnError, overwrite errors (TS5055/TS5056), TS6059, writing.
- `sourcemap/generator.go` (390): VLQ mappings for `declarationMap`, `//# sourceMappingURL=` trailer.

## Design decisions (made here; the executor may revisit with evidence)
1. **No synthetic AST, no port of the factory/printer.** tttt is direct-style. The writer prints text straight from
   the source AST and `Ty`s, the way the types baseline already does. tsc's transformer output depends on the source
   node shape plus a handful of emit flags, and the printer's d.ts layout is regular (4-space indent, `;` after
   members, one declaration per line). The suite tells us where that's wrong.
2. **One walk, two sinks** (and inside `check_share()`: plan 67 drops checkers mid-share once their memory passes a
   budget, so the writer, like `declaration_diagnostics_check()`, must run in the checker that checked the file,
   before it is recycled). Extend `declaration_diagnostics.c`'s `Walk` with an optional output writer (null under
   `--noEmit`: zero cost to today's checking), not a second walker. That walk's visibility, late painting and
   serialization decisions *are* emit's, and two copies would drift. Rename the file/module once it emits
   (`declaration_emit.c`), keeping its diagnostics entry points. Late-painted statements are written in tsc's place
   (`transformAndReplaceLatePaintedStatements`: where the statement is, not appended).
3. **Per file, on the checker that checked it**, like the diagnostics now (program.c runs them after checking). Output
   is collected per file and written after checking, in file order. It must be byte-identical at any `--checkers K`
   (tsgo's is: drizzle's 519 `.d.ts` are identical at `--singleThreaded`, 4 and 16 checkers). Extend `scripts/kdiff.py`
   with an emit mode.
4. **When tttt emits**: only when `emitDeclarationOnly` is in effect (tsconfig or command line) and `noEmit` isn't.
   Otherwise tttt stays `tsc --noEmit`, as now (main.c forces `noEmit` unless the command line says otherwise: keep
   that for configs that would emit JS). tsc's exit codes and output listing (`--listEmittedFiles`) apply.
5. **`-b`**: write the declaration outputs of projects whose config is `emitDeclarationOnly`. Projects that would
   emit JS are still only checked. No `.tsbuildinfo` at first: tsgo's format is an internal moving target and tttt
   isn't incremental. Note the consequence (a later `tsc -b` rebuilds everything) in the stage-4 commit, and decide
   then whether a minimal buildinfo is worth it.
6. **JS sources** (`allowJs` + `declaration`: 368 of the suite's 2139 `.d.ts` tests, 66 tsconfigs) are deferred to an
   optional stage 6. Until then a JS file in an emitting program is a clear "not supported" note on stderr (not a
   silent skip), and the suite counts those tests apart.

## Stages

### Stage 1: the oracles (first, half a session)
- **Suite component.** 2139 suite tests carry emitted declarations in their `.js` baseline
  (`thirdparty/typescript/tsc/testdata/baselines/reference/{compiler,conformance}/*.js`: `//// [x.d.ts]` sections
  after the JS outputs; input `.d.ts` files appear as sections before them, so tell them apart by position or by the
  test's input list). 2107 of them pass errors/symbols/types today. Add a fourth component, `dts`, to the harness:
  only for tests with emitted declaration sections and TS inputs. Show it in `fasttest.sh --summary`, save it in
  `test/baseline_results.ts`, don't fold it into the headline pass count until stage 3 (report it as its own
  number: "dts 1234 / 1771"). Also note the 23 tests with a `DtsFileErrors` section (errors of the emitted
  `.d.ts` re-checked) and 13 with `declarationMap`, for later stages.
- **Real-world oracle `scripts/dtsdiff.py`.** tsgo
  `-p P --declaration --emitDeclarationOnly --noEmit false --outDir /tmp/... --declarationDir /tmp/...
  --tsBuildInfoFile /tmp/...` on the corpora (valibot 1105 files, drizzle 519, effect 950, vue 459, xstate 131,
  TypeScript6 78...) and every source sweep project whose config emits declarations. **Never let tsgo write into a
  shared checkout** (memory: tsc-probe-no-emit): override every output directory. Cache a manifest per project
  (relative output path -> sha256, tsgo's exit code and the pin, like the other caches) in `test/dts/`. Regenerate
  tsgo's text into /tmp only when a diff has to be shown (watch the /tmp quota). Report projects / files matching,
  and bucket mismatches by first differing line (comment, modifier, type text, import, missing file...), the way
  `sweep.py` ranks root causes. Add it to `scripts/oracles.py`.

### Stage 2: the writer, TS sources, no comments (one to two sessions)
- Statement and member printing for everything the walk visits (see "What tsgo has" above). Inferred types go through
  a new `TyDisplay` declaration mode (multi-line object literals with the current indent, the flags above). Written
  types go through `display_node()`, printed multi-line where the printer would.
- Import elision, `export {}`, triple-slash references in and out, `_default`, enums' values (`enums.c` evaluates
  them), `declare` placement, `#private`, accessor pairs, overload/implementation, parameter properties, optional
  parameters' `| undefined` (C's constructor above shows it).
- Test with `scripts/filetest.sh`-style probes against tsgo (`thirdparty/typescript/built/tsc a.ts --declaration
  --emitDeclarationOnly --outDir /tmp/x`). Add a `--dts` view to `scripts/test.sh -f NAME` (diff of the d.ts
  component), like `--err`/`--typ`.
- Target: >= 85% of the TS `dts` tests; valibot, xstate and TypeScript6 byte-identical except comments.

### Stage 3: comments, stripInternal, removeComments, the long tail (one session)
- tsc's comment emission for declaration files: leading comments of copied declarations, members and parameters
  (`/** a*/ a: string`), detached comments at file start (`/*! license */`), what's dropped (trailing comments,
  comments inside elided bodies), `removeComments`, `stripInternal` (`@internal` in a leading JSDoc, tsgo's
  `isInternalDeclaration`). `jsdoc_before_ex()` already finds JSDoc from source text: extend that rather than
  making the parser keep comments.
- Target: >= 95% of the TS `dts` tests; the `dts` component joins the headline pass count; every corpus that emits
  declarations byte-identical (or each remaining file written up in a bugs file).

### Stage 4: the CLI and `-b` (one session)
- (From stage 3, 2026-10-15: the `dts` component joins the headline pass count here, once output paths make the 15
  path tests passable: folding it in at stage 3 would have put stage-4/5 features into plan 66's queue.)
- Output paths as tsgo's `outputpaths` computes them (outDir, declarationDir, rootDir or the common source
  directory, `.d.mts`/`.d.cts` from `.mts`/`.cts`, no output for `.d.ts` inputs), noEmitOnError (skip writing, exit
  1), TS5055/TS5056/TS6059 and the other emit-time option diagnostics tsgo reports, `--listEmittedFiles`, exit codes
  (`DiagnosticsPresent_OutputsGenerated`). Writing is parallel (one file per job) after checking.
- `-b` per design decision 5. `scripts/clidiff.py` gains an emit mode: stdout and the written tree against tsgo's.
- `scripts/dtsdiff.py` at full breadth: target >= 90% of source sweep projects byte-identical (every file), the rest
  bucketed in a bugs file. Perf: `bench.sh` with emit on drizzle/effect/vscode-sized inputs: emit adds <= 15% to
  tttt's single-threaded check time, nothing at all under `--noEmit` (instrcount unchanged).

### Stage 5: declarationMap (one session)
- A VLQ source map generator (tsgo's `sourcemap/generator.go`) and the mappings the printer records for declaration
  emit (`emitSourcePos`/`emitTokenWithComment` places: identify which positions tsgo maps for `.d.ts`, likely
  declaration names and node starts). `.d.ts.map` beside the output, `sourceRoot`, `mapRoot`, the trailer comment.
- Oracle: dtsdiff compares the `.d.ts.map` files too (188 configs set declarationMap). Byte equality is the target. If
  it's hopeless on some shape, compare decoded mappings and say so in the script.

### Stage 6 (optional, decide after stage 5): JavaScript sources
- CommonJS exports to `export =`/namespaces, expandos (`declare namespace f { ... }`), JSDoc types to TS types
  (`transformJSDoc*`), `@typedef`/`@callback` to type aliases, classes from constructor functions. 368 suite tests.
  Decide by how many dtsdiff projects need it (66 tsconfigs combine allowJs with declarations: count those that
  actually contain `.js` sources).

## Risks
- **Byte equality amplifies display divergences.** The types component's leftovers (`scripts/displaydiff.py`: union
  order, `typeof` reuse, `T_1` renaming) will show up as `.d.ts` diffs. Union order is the dangerous one: it depends
  on type ids, which depend on what a checker saw first. kdiff guards tttt's K-independence. Matching tsgo is the
  existing types-component work, now with a second, much larger oracle. That's a benefit as much as a cost.
- **Comment fidelity** is fiddly and has no semantic weight. Don't let stage 3 run past a session: write up the long
  tail.
- **Scope creep into JS emit**: don't. The writer must never print a function body or an expression statement.
- **Memory**: per-file output strings are held until written. Write them as each file finishes if vscode-sized
  programs show it in RSS.

## Sizes
About 5-7k lines of C for stages 1-5 (writer ~2.5k on top of the walk, display mode ~0.4k, comments ~0.5k, CLI and
paths ~0.8k, source maps ~0.6k, scripts ~0.6k). Stage 6 adds ~1.5k.

## Validation (the plan is done when)
- `dts` component: >= 95% of the suite's TS `.d.ts` tests pass and count in the headline number; JS ones counted apart.
- `scripts/dtsdiff.py`: every corpus that emits declarations byte-identical (`.d.ts` and `.d.ts.map`), >= 90% of the
  source sweep's declaration-emitting projects byte-identical, the rest bucketed in `agents/bugs/`.
- `scripts/kdiff.py` emit mode clean at K=16. `--noEmit` instruction counts unchanged. Emit <= 15% over check time.
- `tttt -p P` on an `emitDeclarationOnly` project writes what `tsc -p P` writes (clidiff emit mode), and CLAUDE.md /
  documentation.md / vision.md say tttt is a drop-in for `tsc --noEmit` *and* `tsc --emitDeclarationOnly`.
