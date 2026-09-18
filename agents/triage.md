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

- Parser bug: a statement following a block statement *on the same line* gives TS1005 "';' expected":
  `function f(x: number) { if (x === 1) { return 1; } return 2; }` (newline after the inner `}` makes it parse).
  Probably ASI logic treating `}` like an expression end. Likely affects many minified-looking tests; cheap fix.
- Perf: `hmap_arc_declaration_bucket_lookup_` is 16% of all instructions (self time!) on the bench corpus, plus 3.4%
  for insert, nearly all under `scope_init_declaration` / `namespace_decl_add_names`. A hash lookup shouldn't cost
  that: suspect a poor hash or huge probe chains for `Span`/`cstr` keys, or the global scope (thousands of DOM names)
  being copied/rehashed. This is the "resolve names costs as much as parsing" item above; callgrind it.
- `tttt` finds `thirdparty/lib/` relative to the cwd ("File 'thirdparty/lib/default.d.ts' not found" from any other
  directory). Should be relative to the executable / an install prefix.
- No `perf` on the dev box; callgrind works (`valgrind --tool=callgrind ./release/tttt -q files...`, then
  `callgrind_annotate [--inclusive=yes]`). Inclusive numbers for recursive functions are inflated. Worth a
  `./profile.sh` wrapper if more sessions need it.
- Parser: construct signatures in type literals/interfaces (`{ new (x: string): C }`, `{ new(): T }`) parse as a method
  named `new` (seen via `--shadow-types`). Optional/rest *named* tuple elements (`[a: string, b?: number,
  ...rest: boolean[]]`) are parse errors. A leading `&`/`|` in a type becomes an `any`/`never` operand
  (`parse_type`), which every consumer has to know to skip.
- `Declaration` pointer stability: references hold `decl_ref` from lookup time, while lib declarations are
  copy-on-write (`shared_between_scopes`) and import aliases are copies (`port_item_resolve_alias`). The new core keys
  declared types by `Declaration*`, so a copied declaration is a *different type* with the same name. Check whether
  that happens for types in practice (import of an interface, then compare with the original); plan 03 should make
  it impossible.
- `type Phantom<T> = {}`: `Phantom<string>` and `Phantom<number>` are one `Ty` printing as `Phantom<T>`, because
  instantiating a node that mentions no type parameter returns the node's type, alias arguments and all. tsc gives
  each its own alias arguments. Cosmetic until something prints it.

- (stage 3) `./release/tttt file.ts` without `-q` prints the parsed AST before typechecking; `display_expr` evaluates
  expression types for that, and with no current checker `lazy_type_evaluate` aborts (seen on `inferTypes1.ts`).
  Either make the debug display not evaluate, or `program_make_current()` before printing.
- (stage 3) Union member order: relations now lower types in check order, which moved ~250 shadow-mode renderings
  like `(Derived1 | Derived2)[]` to `(Derived2 | Derived1)[]`. Not visible in baselines yet (the traversal still
  prints legacy types) but will be in stage 4. tsc's order is type creation order; find out which operation of ours
  creates `Derived2` first (probably the structural check of an array literal's best common type).
- (stage 3) `unionRelationshipCheckPasses`: `{ foo?: number | undefined }` -> `{ foo?: undefined } | { foo: number }`
  needs tsc's discriminated-union decomposition of the source (`typeRelatedToDiscriminatedType`).
- (stage 3) Object literal elaboration: tsc reports a mismatching property of an object *literal* at the property
  (`t1.ts(2,14): Type 'string' is not assignable to type 'number'` + related TS6500), we report the whole
  assignment with a chain. Needs expression-level elaboration (`elaborateError`), after 4b.
- (stage 3) TS18050 ("The value 'null' cannot be used here", 680 missing in 38 tests) is what tsc says for null/
  undefined operands; `expr.c` operator checks use `type_extends_strict_nulls` + TS2365 instead.
- Parameter properties (`constructor(public x: number)`): the parser drops the modifier, so neither the legacy checker
  nor `builder_add_class_items` gives the class an `x`. Common in real code and in the class tests.
- `hmap_arc_declaration_bucket_lookup_` is ~15% of the bench run (self time). `checker_atom` had the same profile until
  its FNV hash got a finalizer (stage 4a: similar names cluster in the low bits under linear probing); check whether
  the STC map's hash for `SvStr` keys can get the same treatment (a custom `i_hash`), it could be a cheap 10%.
- The debug AST dump (`tttt` without `-q`) evaluated expression types before there was a checker and panicked on any
  file with a call in a class body; it prints `?` now (`display_expr`). Is that dump worth keeping at all?
- Members of ambient non-const enums without initializer should be computed (no value), `ty_resolve_enum` auto-increments.
- tsc prints string-named enum members as `(typeof E)["b c"]`.
- `filetest.sh` takes `FAST=1` now (release build, no valgrind): seconds instead of a minute per probe.
- (stage 4b1) Type predicates (`x is T`, `asserts x`) aren't in the AST: the parser reads them as `boolean`. Needed by
  inference (`arr.find(isNumber)`, `arrayFind`, `typeGuardFunctionGenerics`) before narrowing ever uses them. Belongs in
  plan 03's scope or a parser session before it.
- (stage 4b1) A function merged with a namespace (`function F() {}` + `namespace F {}`) is a TS2451 at the top level, and
  inside a namespace across two blocks the call finds no signatures (`genericFunduleInModule2`). Legacy binder, plan 03.
- (stage 4b1/4b2) An array literal is never a tuple without contextual typing (`correctOrderOfPromiseMethod`; the
  mapped-type-over-tuple half is done, `instantiate_mapped_over_list`). 4b3.
- (stage 4b1) `error_clone()` was shallow (shared `sub_errors`, double free once anything cloned an error with
  children); it's deep now. Nothing else was cloning errors with children, but worth knowing.
- (stage 4b1) `lower_identifier_internal` no longer treats a bare `C` inside generic class `C<T>` as `C<T>` (only `this`
  is): with a generic `LowerCtx` that turned `x: CustomEvent` into the uninstantiated declared type instead of
  `CustomEvent<any>`. tsc reports TS2314 for the bare name; nothing here does yet.
- (stage 4b1) Ideas not done: cache call resolutions per `CallExpr` in the checker (each call resolves twice today);
  variances for type aliases (inference and relations both want them).
- (stage 4b2) Reverse mapped type inference (`{ [K in keyof T]: Box<T[K]> }` as a parameter type) doesn't exist:
  `infer_from` only sees through the homomorphic `T[K]` template. `reverseMappedTupleContext`,
  `homomorphicMappedTypeWithNonHomomorphicInstantiationSpreadable1` lost their errors component to it in 4b2.
- (stage 4b2) Permissive instantiation for conditional types is still missing (`ty_conditional_resolve` defers where tsc
  picks the false branch). A flat `TyMapper` can't say "every type parameter -> any"; wants a mapper kind.
- (stage 4b2) Circular aliases: `type N<T, K extends string> = T | { [P in K]: N<T, K> }[K]; type M = N<number, "M">` is
  `any` + TS2589-ish in tsc (`recursivelyExpandingUnionNoStackoverflow`), here it prints `N<number, "M">`. TS2456 is
  still only a TODO in `ty_declared_type_of`.
- (stage 4b2) `type A = {}` clashing with `interface A` (`noSymbolForMergeCrash`): the traversal prints the merged
  declaration's type, tsc the alias's own. Wants per-declaration symbols (plan 03).
- (stage 4b2) Annotations print through the legacy display of the written node (`ex : Exclude<"a" | "b", "a">` where tsc
  prints the resolved `"b"`); goes away when the traversal prints `Ty` (4b3). Worth a failstats check afterwards for
  union member *order* (`"c" | "b"` in tsc for `Exclude<"a" | "b" | "c", "a">`: literal ids follow first creation).
- (stage 4b2) `test/baseline_results.ts` wasn't saved by the 4b1 session (3 tests showed as "newly passing" at the start
  of 4b2). Sessions should end with `./fasttest.sh -s`; consider making `--summary` say when the file is stale.
