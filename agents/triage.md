# Triage
Uninvestigated items. Triage sessions drain this.

- readme.md says development is halted and describes the project as "Tess"'s; refresh it.
- Catalogue the ~60 PERF/HACK/TODO/FIXME comments in `src/` into hacks.md (smaller-model sub-agent job).
- Parser: no tokenizer + backtracking with fail cache. Fine for speed in principle, but check parse-error fidelity
  against tsc (the errors baseline includes syntax errors) and worst-case behaviour once plan 00's bench exists.
- `engine.c` (JS interpreter, 634 lines): is it used by the checker (const enum / literal evaluation?) or dead
  weight? Remove if unused.
- Does `./test.sh` (valgrind) currently run clean on a sample? Unknown after the cleat port.
- Perf (from `./bench.sh`, see `measurements.md`): ~25% of tttt's wall time is outside the four timed phases,
  presumably `program_destroy` freeing node by node. A real CLI should just exit; long term, arenas.
- Perf: peak RSS is ~120 bytes per source byte (500 MB for a 4 MB corpus), worse than tsgo. AST node size / per-node
  allocations / `DEBUG_ALLOCATOR` (always defined in `prelude.h`, even in release; adds a header word per allocation
  and a counter) are the suspects. Measure with it off.
- Perf: `resolve names` costs as much as parsing (92 ms vs 88 ms on the bench corpus). Surprising for a phase that
  should be a hash lookup per identifier; look for per-scope allocation or linear scans.
- The bench corpus is weak (easy passing tests x200 copies in namespaces). Replace with a real project as soon as
  tttt can get through one without parse errors.
- Harness runs a test twice when the first run leaves net allocations (lib cache fill), 173 times per suite run. Fine,
  but it means "### TEST" markers / log output can repeat; `failstats.py` dedupes.
- `failstats.py` counts by test *name*; a handful of names exist in both `compiler/` and `conformance/`, so its pass
  counts can differ from the harness by one or two. The harness has the same collision in `baseline_results.ts`.
- Checking the stdlib itself (`tttt --no-lib --keep-going thirdparty/lib/esnext.full.d.ts`) gives 32 name errors and
  143 type errors; tsc gives none. Cheap regression target for lib-related bugs (Intl namespace merging,
  `globalThis.`).
- tttt's `main.c` only loads 4 lib files and ignores tsconfig; `error.c` "Error added with invalid span" fires ~1.4k
  times per suite run, mostly duplicate-identifier / redeclare errors whose other declaration is in a lib.
- Namespaces with the same name don't merge (`module A {} module A {}`, also namespace + enum/function): `declaration.c`
  logs "Tried to add multiple namespaces to declaration" in 167 tests now that `module` parses. Probably belongs in
  the symbols plan (03) rather than a patch on `Scope`.
- Non-exported namespace members appear to be visible from outside (`scope_evaluate_value_interface` puts every
  declaration with a value into the namespace's value type, and namespace scopes have no `export_scope`). Verify,
  write up as a bug.
- `for (var a ...) {}` then using `a` after the loop gives TS2304: `var` isn't hoisted out of for-loop scopes to the
  function/file scope. Verify for blocks in general.
- Type display uses the declared name / full namespace path (`typeof A.B`, `C`) where tsc prints the name as
  accessible from the use site, preferring aliases (`typeof AB`, `AB.C`). Symbols baseline likewise wants
  qualified names (`Symbol(A.x, ...)`) for namespace members. Both are for the symbols/printing design.
- Error recovery: with `await` unsupported, `a = (await x) ? y : z` now mis-recovers into a labeled statement. Goes
  away with `await`, but label parsing (`parse_label`) should probably refuse reserved words.
- `failstats.py --syntax` used to silently reuse the previous dump; it now re-runs the harness unless `--no-run`.
