# Plan 56: content mappers (TS 7.1 `contentMappers`)

Rewritten 2026-10-14 to plan 66's design (todo entry 2). 15 `contentMapper*` suite tests (10 failing a component at
the rewrite) and scenarios.py's skipped content-mapper scenario. Nothing in the corpora or sweeps uses the feature.

## What it is
A tsconfig option letting an external program turn files TypeScript can't read (`.vue`, `.box`) into virtual
TypeScript at program construction, gated on the command-line-only `--runExternalCode` (tsconfig.c's
check_content_mappers() already reports TS5024/TS18065-18068):
```json
"contentMappers": [{ "package": "mapper", "extensions": [".box"], "options": {...} }]
```
`package` resolves from the config's directory (tsoptions/contentmappers.go: bundler resolution's
ResolvePackageDirectory, then its package.json): `"typescript": { "contentMapper": { "exec": [argv...],
"compilerOptions": ["target", "jsx"], "dynamicConfig": bool } }`, plus `name` and `version` (identity `name@version`).

## Protocol (internal/contentmapper/hostimpl.go, internal/ipc, internal/jsonrpc)
JSON-RPC 2.0 over the child's stdin/stdout with LSP `Content-Length:` framing; the child runs `exec` in the package
directory, one process per mapper identity. Requests, in order:
- `initialize {positionEncodings: ["utf-8","utf-16"], locale?}` -> `{positionEncoding, diagnosticSource}` (5 s
  timeout; failure kinds in host.go InitializeError).
- `openProject {configFileName, projectHandle, options?, compilerOptions}` -> `{configIdentity, watchedFiles?,
  optionDiagnostics?}`; `compilerOptions` holds only the manifest's declared options that are set, as tsgo's Go JSON
  encoding (`target` es2020 is `7`: the ScriptTarget enum value), in declared order. projectHandle `name@version:N`.
- `transform {fileName (absolute), content, projectHandle}` -> `{text, extension, mappings, diagnosticDirectives?,
  diagnostics?, supplemental?}`. `mappings` is the span map: tuples `[virtualStart, virtualLength, originalStart,
  originalLength, kind(0 verbatim,1 atom,2 alias), features?]` (spanmap.go Unmarshal/Validate/VirtualToOriginalSpan).
- `closeProject {projectHandle}`.

## Results (transform.go ParseResult)
The text parses as `<file><extension>` (script kind from it; `.mts/.cts/.mjs/.cjs` force a module) under the original
name; the mapper's diagnostics (original coordinates, code shown as `<diagnosticSource><code>`, e.g. `box1000`) attach
to the file; supplemental outputs parse as `<file>.<i><ext>` (a real file of that name: SupplementalFileCollision).
Failures (no/invalid mappings, unsupported extension, failing process) become TS18069 "The content mapper 'X' failed
to transform this file." + a chained reason, and an empty placeholder file. Diagnostics in mapped files are mapped
back through the span map (directives may suppress/expect them) and printed against the original text.

## The suite
tsgo's harness (harnessutil.CompileFiles) serves ~18 Go test mappers in process (contentmappertest's registry, by
`exec[0]`: `compiler-test-mapper`, `verbatim-mapper`, `failing-mapper`, `supplemental-*`...). We don't port them:
`test/oracle/tttt-test-mapper` (overlay: `cmd/tttt-test-mapper/main.go` + `internal/testutil/contentmappertest/
tttt_stdio.go`) is a binary serving the mapper named by its argv[1] on stdin/stdout, built by
`thirdparty/ensure-typescript.sh` into `typescript/built/tttt-test-mapper`. The harness (`baseline_tests`) runs a
manifest's exec as `[that binary, exec...]` (`TTTT_CONTENT_MAPPER_LAUNCHER`, see content_mapper.h); the CLI runs exec as
written. Baselines (compiler_runner.go): the `.errors.txt` leaves out mapped files and their diagnostics; a
`.contentmapper` baseline (tsbaseline/contentmapper_baseline.go: original, transformed, the mapped diagnostics in
pretty format against the original text) holds them; `.types`/`.symbols` show the transformed text. tttt compares
the `.contentmapper` file as part of the errors component.

## Stages
1. **Done 2026-10-14** (8 of 15 pass; documentation.md "Content mappers"). (Part 1) Go test mapper binary; `src/content_mapper.{h,c}`: manifest resolution, the host (posix_spawn, framing,
   JSON writer/reader on json.c, initialize/openProject/transform/closeProject, one process per identity behind a
   lock: the load pool calls it from workers), span map parse/validate/map; tsconfig's mappers onto CompilerOptions;
   program_make_source() transforms a mapped file and parses the result; root discovery and module resolution see
   the extensions; the harness's `.contentmapper` comparison. Target: contentMapperTransform and the plain
   diagnostics tests.
2. (Part 2) Left: diagnosticDirectives (contentMapperDiagnosticDirectives, ...InvalidDiagnosticDirectives),
   supplemental outputs (...SupplementalDiagnostics/FileCollision/Globals/Module), contentMapperDeclarationEmit,
   initialization failures as tsgo's one global TS18099 with the chained reason (a probe with a nonexistent exec:
   tttt says TS18069 per file), TS18075-18079 for a package that doesn't resolve (resolve_content_mapper()), the
   failure budget (maxContentMapperFailures 5, TS18070), option diagnostics from openProject, the 5 s initialize
   timeout, utf-16 positions, span map features (hacks.md "Content mappers"). Also failure diagnostics (TS18069 family, initialize errors, option diagnostics), diagnostic directives,
   supplemental files and collisions, declaration emit names, symlinks, the scenario; plan deleted when done.

## Validation
The 15 `contentMapper*` tests pass; `tttt -p` on a probe project with a real mapper script (any language speaking
the protocol) matches `thirdparty/typescript/built/tsc -p --noEmit --runExternalCode`; nothing changes without
`contentMappers` (the oracles unchanged, no instructions spent: everything is gated on the option).
