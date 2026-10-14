# Plan 56: content mappers (TS 7.1 `contentMappers`)

Status: not started. Written 2026-10-09 by the 7.1 fan-out (agent B) after reading tsgo; its decision "don't implement
now" is **reversed** (2026-10-14): the human wants the suite at 100%, plan 66 schedules it and says how the suite
should run tsgo's Go test mappers. 10 suite tests (`contentMapper*`, 15 in tsgo's tree) and nothing in the corpora or sweeps uses the feature yet.

## What it is
A tsconfig option that lets an external program turn files TypeScript can't read (`.vue`, `.box`...) into virtual
TypeScript at program construction:

```json
"contentMappers": [{ "package": "mapper", "extensions": [".box"], "options": {...} }]
```
`package` is resolved like a module (node resolution from the config's directory); its package.json's
`"typescript": { "contentMapper": { "exec": [argv...], "compilerOptions": ["target", "jsx"], "dynamicConfig": ... } }`
is the manifest (`internal/contentmapper/contentmapper.go`: Definition, Manifest, Mapper; resolution in
`internal/tsoptions`). Files with a registered extension become root/importable files; each is sent to the mapper and
the reply parsed in its place.

## What tsgo does
- `internal/contentmapper/hostimpl.go` (1.4k lines): spawns `exec` in the package directory (one process per mapper
  identity `name@version`, shared by projects), talks JSON-RPC over stdin/stdout (`internal/ipc`): `initialize`
  (position encoding, diagnostic source name), `openProject` (the declared compiler options' values, the mapper's
  `options`; may return option diagnostics), `transform` (file name + content -> text, virtual extension, span map,
  diagnostics, diagnostic directives, supplemental files), `closeProject`.
- `transform.go` `ParseResult`: validates the span map (`internal/spanmap`: segments verbatim/synthesized with
  feature flags), parses the text as `<file><virtual ext>` (forced module for .mts/.cts...), attaches the mapper's
  diagnostics, and parses each supplemental output as `<file>.<i><ext>` (collision with a real file is an error).
- Diagnostics in the mapped file are mapped back to the original text through the span map, filtered by the
  segments' features and the directives (suppress/expect-unused); failures (`failing-mapper`, bad mappings, unknown
  extension) become dedicated diagnostics and a placeholder file. Declaration emit and `.d.ts` naming follow the
  virtual name. The LS side (go-to-def, completions through span maps) is out of scope for tttt.
- Tests: `// @runExternalCode: true`; the harness doesn't exec anything, `internal/testutil/contentmappertest`
  registers ~18 in-process Go mappers by argv[0] (`compiler-test-mapper` = `transforming.go`: prepends
  `const __VERSION = "1.0.0";`, substitutes `#{option}` by the option's value, TS1000-style diagnostic for an
  unclosed `#{`; `verbatim-mapper`, `supplemental-*-mapper`, `failing-mapper`, `lisp-mapper`...). Baselines: an extra
  `.contentmapper` file per test (original, transformed, mapped diagnostics); the `.types`/`.symbols` show the
  *transformed* text under the original file name.

## How tttt would do it
1. tsconfig: parse `contentMappers` (tsconfig.c), resolve the package and read the manifest (module_resolution.c has
   node resolution); extensions join the supported ones for root file discovery and import resolution.
2. A mapper host: posix_spawn + pipes, a minimal JSON-RPC client (json.c exists), synchronous per file is enough for
   the CLI (tsgo batches/parallelizes; tttt's load pool could call it from its threads with one connection per
   mapper behind a lock).
3. SourceFile: keep the original text and a span map next to the transformed text the parser sees; map diagnostic
   spans back when reporting (error.c's position printing), apply directives.
4. Test harness: the suite can't run tsgo's Go mappers. Either reimplement the test mappers in C inside
   `baseline_tests` (a table keyed by argv[0], like tsgo's spawner; transforming/verbatim/supplemental are small, the
   whole set ~1.5k lines of Go) or build a tiny external `tttt-test-mapper` executable speaking the protocol. The
   `.contentmapper` baseline component would need a fourth comparison in the harness (or be ignored, comparing only
   errors/types/symbols, which already show the transformed text).

Effort: 2-3 sessions (host + protocol + span maps ~1 session, harness mappers ~1, diagnostics mapping/directives and
the long tail of failure diagnostics ~1). Value today: 10-15 suite tests; real projects (Vue/Svelte tooling) would
adopt it only once mapper packages ship manifests.

## Validation
The 15 `contentMapper*` suite tests pass (errors/types/symbols), `tttt -p` on a probe project with a real mapper
script (e.g. a 30-line Node or Python JSON-RPC mapper) matches `thirdparty/typescript/built/tsc -p --noEmit`, and no
cost when `contentMappers` is absent (gate on the option).
