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

## The prototype (commits after 0ed0dc82, `src/emit_store.{h,c}`, `build()` in main.c)
- `EmitStore`: per build position, the declaration files that program emitted (`store_declarations()`, after
  checking, under the same conditions tsc writes: not with noEmitOnError and diagnostics, not a blocked path),
  whether or not anything is written to disk (`--no-write`, or a config that also emits JavaScript, which tttt never
  writes).
- `EmitView`: a program's `Fs` (wrapped by its `CachedFs`): the inner file system with the files of the visible
  positions on top, their directories existing; relative paths absolutized against the cwd, `..` paths normalized for
  the inner fs (the kernel can't walk through a directory that exists only in memory), lookups through
  `node_modules` links by the lenient realpath of the deepest existing ancestor; `emit_view_real_path()` and
  `emit_view_file_size()` are the program's realpath and size callbacks. Directory listings show only the disk.
- Visibility: the job's references, transitively, plus every job at least the window (`--builders`, default 4) places
  earlier. A job waits until everything it sees is done; build threads take the **first ready job in build order**
  (in-order dispatch blocked lanes: typescript-eslint was +40% wall before that). Output stays printed in build order
  and depends only on the static visibility, not on timing.
- Programs read references like `-p` (`program_set_references(..., outputs = true)`), `project_redirects_new()` gets
  no built list (the store replaces the window's "outputs read as sources").
- `TTTT_BUILD_TIMES=1` prints each job's start and end (the critical path).

## Gate results (2026-10-19, 4-core box under a load average of ~10 from other users)
- `-b` sweep (`scripts/sourcesweep.py --build`): **406 / 416** (400 without the flag), no project lost. Gained:
  better-auth (tsgo's 258 TS7006 etc. now reproduce), lit ×5 (TS2307/TS2882 inside outputs), zigbee2mqtt/test (TS2304,
  TS2503, TS2833 in `dist`), astro's markdown/remark tests (TS2741/TS2322: the body-only import elided). Left: apollo
  cacheOverride (augmentations, not investigated under the new model), astro -TS18048, atproto ×2 (+TS2554, the
  oauth-client TS2307 race), better-auth -TS2345 (the `-p` lazy return context bug), ghostfolio +TS4058, hono ×2,
  sunmao -TS2590: all `-p` bugs or tsgo builder quirks (bugs/sourcesweep_build.md).
- Scenarios: 218 / 0 with and without the flag.
- tsc-src and typescript-eslint `-b`: clean.
- Wall at the defaults, 10 interleaved pairs (median emitted / sources): tsc-src **0.77**, typescript-eslint **0.87**.
  CPU (user + sys, means of 7): tsc-src 4.01 → 2.76 s, typescript-eslint 8.20 → 6.78 s; peak RSS 345 → 234 MB and
  589 → 432 MB. Checking against declarations wins back the ordering constraint and more.
- The suite (13929) is unchanged by the `-p` fixes (it has no `-b`); oracles in the commit message.

## Stage 1 (one session): the only `-b`
1. Drop `TTTT_BUILD_EMITTED`: the store always exists under `-b`.
2. Delete the sources model (plan 47's rule, delete first; each item is dead once `-b` reads outputs, nothing else
   passes `outputs = false`): `SourceFile.from_reference` and what hangs off it (`from_built`, `built_unchecked`,
   `built_plain_js`, `skip_typed_bodies` / `skip_typed_body()` in the parser, `strip_internal` of sources,
   `source_file_reads_as_declaration()`, `reference_const_enums_ambient`, the private-member untyping in
   members.c/expr.c, `source_files_from_reference`, error.c's and declaration_emit.c's checks),
   `program_own_direct_built_files()`, `program_source_of_output()` / `program_redirect_resolved()`'s sources branch,
   the resolver's redirects (`module_resolver_set_redirects()`, `file_exists()` / `directory_exists()`'s redirect
   branches), `project_redirects_built()` and the `built` list of `project_redirects_new()`, `program_set_references()`'s
   `outputs` flag. Keep `reference_preserves_const_enums` (outputs). Grep for what's left; the suite, scenarios and
   both `-b` corpora must hold at every step.
3. Free each position's texts once its last reader is done (a count of readers per position, from the visibility
   table), so the store's peak is the live frontier, not the whole build.
4. Rewrite documentation.md "Project references" (`-b` = tsc -b: build in order, emit in memory, read outputs;
   "Divergences of `-b` by design" shrinks to the window and never-stale outputs), project.h's header comment,
   hacks.md's builder-window entry (the window is now a wait plus visibility), bugs/sourcesweep_build.md (the verdicts
   on emitted declarations are fixed; re-triage what's left under the new model, apollo cacheOverride first).
5. Validate: scenarios 218 / 0, `-b` sweep ≥ 406 / 416 with no project lost against the flag's run, tsc-src and
   typescript-eslint clean, `scripts/oracles.py` (clidiff and dtsdiff don't run `-b` but share the `-p` fixes),
   `scripts/threadcheck.sh` / `checkerdiff.py` on the `-b` corpora (output independent of `--threads` and of
   `--builders` within the default window), the wall / CPU / RSS of both `-b` corpora at the defaults against
   today's (the numbers above), instrcount.

## Stage 2 (perf, after stage 1; one session to measure, maybe one to build): share declaration files across programs
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
