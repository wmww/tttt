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
- `parse_label` should refuse reserved words (`default:`/`case x:` are fine today only because of parse order).
- `failstats.py --syntax` used to silently reuse the previous dump; it now re-runs the harness unless `--no-run`.
- Remaining syntax gaps after parser round 2 (`./failstats.py --syntax`, 1204 tests, flat tail): object binding
  patterns with computed keys / rest / nested defaults (`const { [k]: v, ...rest } = o`, `function f({ b = '' } = {})`),
  `let x!: T`, `for await (... of ...)`, `export { a as default } from`, `import { default as b }`,
  `import d, * as ns from`, `export import`, `import()`, `new.target`, `o?.()`, regex literal after `=`, `**=`,
  shebang line, labeled `continue`/`break` target validation, `const c;` in ambient files, JSX (whole feature, needs
  its own plan), `accessor` fields, `override`/TS4112+, overloads of `*()`-style nameless members (error recovery).
  Worth one small execution session, not more; many of the rest are error-recovery fidelity.
- Non-strict null widening is missing: `get b() { return null; }` has type `null` (tsc: `any`), so `C.b()` reports
  TS2349. Same for `var x = null`/`undefined` initialisers presumably. Depends on `strictNullChecks` being a real option.
- `[Symbol.iterator]() {}` computed class/interface members go through `InterfaceMember` (`INTERFACE_KEY_EXPR`), so
  their bodies are not traversed for the types/symbols baselines and they print as `() => any`. Wants unique symbols.
- Parameter properties (`constructor(private q: number)`) print their symbol as `q` instead of `A.q` and aren't added
  to the instance type? Verify.
- Function type display underlines (`^ ^^   ^^^^`) mismatch for functions with rest parameters / `any[]` in the types
  baseline (seen in every spread/tagged template probe). Probably one bug in the underline mode of `display_callable`.
- `this` inside methods prints as `C` where tsc prints `this`.
- `propertyAndAccessorWithSameName`: tsc's symbol merging when a property and accessor clash (TS2300) isn't mirrored.
- bench corpus is rebuilt from whatever currently passes, so wall times aren't comparable across sessions. Pin the
  corpus (or report per-line numbers) so perf regressions are visible.

