# Plan 87: `-b` reads its references' emitted declarations

Planning session 2026-10-19 (todo entry 4): **go**. The prototype is in the tree behind `TTTT_BUILD_EMITTED=1` and
passes every point of the gate; stage 1 makes it the only `-b` and deletes the sources model.

## Why
tttt's `-b` typed a referenced project by reading its *sources* under the reader's options; tsc -b reads the
reference's emitted `.d.ts` at its output path. The twenty-second window approximated the `.d.ts` view three times on
top of sources (the builder window, a reference source's private members untyped, `stripInternal` type literal
members), and what was still missing can't be approximated from sources at all: errors inside emitted declarations
(lit, lit-element, zigbee2mqtt/test), imports only a body uses (astro), tsgo's emit bugs leaking downstream
(better-auth's 258 TS7006). Every new divergence of this family was another approximation.

## What tsgo does (researched 2026-10-19)
- `tsc -b` (`execute/build/buildtask.go`): each build task waits on its upstream tasks (`waitOnUpstream`), builds
  its program with `compiler.NewProgram` *without* `UseSourceOfProjectReference` (only the LSP, `project/project.go`,
  sets it), emits, writes to disk (`writeFile`) and unblocks downstream. Downstream programs read the written `.d.ts`
  like `tsc -p` does. The build host caches parsed `.d.ts` and `.json` files across the build's programs
  (`host.GetSourceFile`, a `parseCache` keyed by parse options): lib files and every reference output are parsed
  once per build, not once per program.
- A reference output's module names resolve from its **source** file, with its project's options
  (`projectReferenceFileMapper.getRedirectForResolution`: output dts → `output.Source`; a reference's own `.d.ts`
  input too; `realpathDtsToSource` through a link). tttt's `-p` resolved from the output's location: a `-p` bug the
  prototype fixed (`program_resolution_containing()`).
- Module specifiers name a reference output by its source, the output listed first
  (`GetSourceOfProjectReferenceIfOutputIncluded`, then GetEachFileNameOfModule's `referenceRedirect`). Fixed in the
  prototype (`program_module_specifier_ending()`).
- Any root file of a referenced project is the reference's, not the program's to emit
  (`sourceFileMayBeEmitted`'s `GetProjectReferenceFromSource`): a `.json` one too (zigbee2mqtt/test's TS6307). Fixed
  (`emit_source_may_be_emitted()`).
- Builders: 4 by default, taking tasks in build order and waiting only on references, so an *unreferenced* project's
  outputs exist or not by timing. tttt keeps its deterministic window (a project at least `--builders` places earlier
  counts as built) and now *waits* for it.

## Stage 1: done 2026-10-20
`-b` reads emitted declarations only (`EmitStore`/`EmitView`, documentation.md "Project references"); the sources
model is deleted (~350 lines net: `from_reference` and everything hanging off it, the resolver's redirects, the
built list, `program_set_references()`'s flag). Each position's texts are freed once its last reader is done
(`build_job_done()`). The `-b` sweep is byte-identical to the flag's run (409 / 416), scenarios 221 / 0, tsc-src and
typescript-eslint clean, every oracle unchanged. Re-triaging the leftovers found apollo's cacheOverride to be a `-p`
bug (an alias re-entered below a variance measurement), fixed; the rest are in bugs/sourcesweep_build.md.

## Stage 2 (perf; one session to measure, maybe one to build): share declaration files across programs
Every `-b` program parses and binds the default lib and every reference output itself: typescript-eslint's 56
programs each take at least 80-130 ms (`TTTT_BUILD_TIMES`), most of it that floor. tsgo parses each `.d.ts`/`.json`
once per build (`parseCache`). Measure first: the floor's split (lib parse, bind, checker setup) with `tttt --time` on a
trivial program; then decide whether ASTs (and their binder scopes) can be shared read-only between programs: what
in a `SourceFile` is program-specific (options-dependent parse: `impliedNodeFormat`, the language version; the
file's arena ownership; `program_materialize_file()`'s deferred bodies, which declaration files don't have; checker
links keyed by node). If sharing is out of reach, at least the lib files: they're the same in every program with the
same `lib`/`target`. Gate: typescript-eslint and the sweep's biggest solutions (atproto, astro, jest) at the defaults.

## Not in scope
- The window's race in tsgo (atproto oauth-client's TS2307): deterministic by design.
- Writing JavaScript (tttt never does): a config that emits JS still gets its declarations into the store only.
- tsbuildinfo / up-to-date checks: tttt always builds everything (never-stale outputs).
