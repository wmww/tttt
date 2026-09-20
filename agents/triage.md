# Triage
Uninvestigated items. Triage sessions drain this.

- **JS files are parsed as TypeScript.** Since the harness honours multi-config and `@noTypesAndSymbols` tests, ~900
  `@allowJs`/`@checkJs` tests run; tttt loads `.js`/`.jsx` files like `.ts` (CommonJS `module.exports`, JSDoc types,
  no type annotations allowed...) and without `allowJs` tsc doesn't load them at all (TS6504). At minimum: don't
  report checker errors in JS files unless `checkJs`, and skip them without `allowJs`. `compiler_options()->allow_js`
  / `check_js` exist. Needs a plan of its own (JSDoc is big).
- Spurious TS2304 for `import`, `export`, `using`, `const`, `module`, `exports`, `with`, `of` (4k diagnostics): parser
  recovery turns keywords it can't place into identifier expressions. Partly the JS item above, partly `using`
  declarations and import/export forms that don't parse. `./failstats.py --syntax` has the shapes.
- Target-gated diagnostics that can now be implemented because `compiler_options()->target`/`module` exist: TS18028
  (private identifiers < ES2015), TS2705/TS2468 (async needs `Promise`), TS1375/TS1378 (top-level await), TS2802
  (iteration needs `downlevelIteration` or ES2015), TS1056 (accessors, ES3). Small each; check counts with
  `./failstats.py --code`.
- `for (x of iterator)`: TS2495 "is not an array type or a string type" for anything that isn't an array, now visible
  because ES2015 libs load (`for-of13`). for..of needs the iteration protocol (`[Symbol.iterator]()`), which needs
  well-known symbol property names.
- Harness: `@libFiles`, `@noImplicitReferences`, `tsconfig.json` in a test, `@currentDirectory`, `@link` are ignored
  (see `hacks.md`). `@noImplicitReferences` (155 tests) decides which files are roots, probably the most valuable.
- `filetest.sh`/diffapp with a multi-config test file: `run_baseline_test_case()` gets an empty configuration there
  (the last value of "es5,es2015" is a bad value and ignored -> defaults). Untested.
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
- The bench corpus is weak (easy passing tests x200 copies in namespaces). Replace with a real project as soon as
  tttt can get through one without parse errors.
- Harness runs a test twice when the first run leaves net allocations (lib cache fill), 173 times per suite run. Fine,
  but it means "### TEST" markers / log output can repeat; `failstats.py` dedupes.
- `failstats.py` counts by test *name*; a handful of names exist in both `compiler/` and `conformance/`, so its pass
  counts can differ from the harness by one or two. The harness has the same collision in `baseline_results.ts`.
- Checking the stdlib itself (`tttt --no-lib --keep-going thirdparty/lib/esnext.full.d.ts`) gives 32 name errors and
  143 type errors; tsc gives none. Cheap regression target for lib-related bugs (Intl namespace merging,
  `globalThis.`).
- tttt's `main.c` only loads 4 lib files and ignores tsconfig; `error.c` "Error added with invalid span" is down to ~70 lines per
  suite run since plan 04 (TS2511 on a lib class, names found in a `_` of some lib).
- Type display uses the declared name / full namespace path (`typeof A.B`, `C`) where tsc prints the name as
  accessible from the use site, preferring aliases (`typeof AB`, `AB.C`). Symbols baseline likewise wants
  qualified names (`Symbol(A.x, ...)`) for namespace members. Both are for the symbols/printing design.
- `parse_label` should refuse reserved words (`default:`/`case x:` are fine today only because of parse order).
- `failstats.py --syntax` used to silently reuse the previous dump; it now re-runs the harness unless `--no-run`.
- Remaining syntax gaps (`./failstats.py --syntax`; the real corpus has none left, so these are test-suite only):
  `for await (... of ...)`, `export { a as default } from`, `import { default as b }`, `import d, * as ns from`,
  `export import`, `import()` expressions, `new.target`, `**=`, shebang line, labeled `continue`/`break` target
  validation, `const c;` in ambient files, JSX (whole feature, needs its own plan), `accessor` fields,
  `override`/TS4112+, overloads of `*()`-style nameless members (error recovery), TS1161 for an unterminated regex
  (`parse_regex` just fails), TS6188/6189 misplaced numeric separators, TS1263/1264 misplaced `let x!`.
- **Real corpus, first look at the 9.8k checker diagnostics** (`./realworld.py`, plan 03). Not investigated beyond this:
  - ~4000 of the 6840 TS2345 ("'Identifier' is not assignable to 'Node' ... missing baseURI"): **root cause found**
    (plan 04 planning): `utilities.ts` imports `Node`/`Symbol`/`Type`... *and* declares `function Node(...)` etc; the
    copy-based binder reports a false TS2440 and drops the import, so the type `Node` falls through to the DOM's.
    Not a cycle. Repro `test/probes/plan04/aliasMergesWithLocalValue.ts`, fixed by plan 04 stage 1. Recount all the
    rows below after it lands.
  - Corpus name errors that aren't the binder's: a mapped type's `as` clause can't see its key parameter
    (`[K in T as K["kind"]]`, TS2304 x5 in parser.ts/utilities.ts/visitorPublic.ts); `typeof X[K]` in a type argument
    parses `X[K]` as a value access (scanner.ts:299, TS2693).
  - 896 TS7006 (parameter implicitly any): contextual typing of callbacks not reaching (probably the same `Node`
    mixup killing overload resolution first, recount after).
  - 565 TS2769 / 179 TS2349 / 38 TS2558: overload resolution and calls on unions.
  - 567 TS2322 + 173 TS2367 + the `X | undefined` share of TS2345: no narrowing (plan 05).
  - 147 TS2495 / 12 TS2461 / 10 TS2493: iterating `Map`/`Set`/iterables in for-of and array destructuring (lib es2020
    iterables aren't understood; tsconfig `lib`/`target` aren't read either, tttt runs with its default libs).
  - 104 TS2304: `require`, `process`, `Buffer`...: `types: ["node"]` / `node_modules/@types` aren't resolved, and
    `import("fs").Stats`-style import types are parsed but are `any` (`parse_import_type`).
  - 68 TS7022, 56+20+19 TS2362/3/4 (arithmetic on enums/any), 25 TS2538.
- Real corpus perf: see `measurements.md` for the profile notes (after the two fixes typecheck is ~600 ms of 0.9 s).
  `find_prop`'s intersection memo caches whatever depth it was computed at (`depth > 8` gives "not found"), harmless
  so far. `ty_intersection` still distributes over non-primitive unions with no size limit (tsc: TS2590 at 100k).
- `check_with()` (ty/expr.c) computes an expression with a contextual type *without* going through the LazyType, so
  it has no cycle detection: `let [x = x] = []` recursed forever when a binding default was checked contextually
  (`lower_binding_access` now checks defaults uncontextually to stay on the detecting path). Any other contextual
  self-reference can still overflow the stack.
- Optional chaining has no semantics beyond stripping null/undefined from the receiver (`a?.b`, `a?.[i]`, `f?.()`):
  the result doesn't get `| undefined`, and the rest of the chain isn't short-circuited.
- An array literal contextually typed by an array binding pattern should be a tuple (`const [a = 1, b] = [undefined, "s"]`
  gives `(string | number | undefined)[]`), object rest (`{ a, ...rest }`) gives the whole object instead of tsc's
  `getRestType()`, and the symbols baseline wants property symbols for binding pattern keys (`{ q: { r } }`).
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
- Parser: optional/rest *named* tuple elements (`[a: string, b?: number,
  ...rest: boolean[]]`) are parse errors. A leading `&`/`|` in a type becomes an `any`/`never` operand
  (`parse_type`), which every consumer has to know to skip.
- `Declaration` pointer stability: references hold `decl_ref` from lookup time, while lib declarations are
  copy-on-write (`shared_between_scopes`) and import aliases are copies (`port_item_resolve_alias`). The new core keys
  declared types by `Declaration*`, so a copied declaration is a *different type* with the same name. Check whether
  that happens for types in practice (import of an interface, then compare with the original); plan 04 (symbols) should make
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
- (stage 4b1) Type predicates (`x is T`, `asserts x`) aren't in the AST: `parse_type_predicate` reads them as `boolean`/`void`. Needed by
  inference (`arr.find(isNumber)`, `arrayFind`, `typeGuardFunctionGenerics`) before narrowing ever uses them. Belongs in
  the scope of plan 04 (symbols) or a parser session before it.
- (stage 4b1) A function merged with a namespace (`function F() {}` + `namespace F {}`) is a TS2451 at the top level, and
  inside a namespace across two blocks the call finds no signatures (`genericFunduleInModule2`). Legacy binder, plan 04 (symbols).
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
  declaration's type, tsc the alias's own. Wants per-declaration symbols (plan 04, symbols).
- Baseline saving: 4b1 *and* 4b3 forgot `./fasttest.sh -s`. CLAUDE.md says so now; consider making `--summary` say
  when the results file is older than the last commit.
- From type core stage 4b3 (see `hacks.md` "Expressions on the new core" for the full list): `this` types are `any`
  (`m.set("a", 1)` on a `Map` returns `any` instead of the map: needs the `this` type parameter); the symbols
  traversal finds no declaration for a property accessed on a primitive (`x.toFixed`, `s.length`: the lookup in
  `expr_compile_test_info_inner` only looks at object types, it should go through `ty_apparent_type`); a failed type-argument-arity call returns
  `any` where tsc still resolves a signature; reverse mapped type inference is now the loudest inference gap.
- From type core stage 5:
  - Union member order depends on when literal types are created: tsc makes them when the type node is first resolved,
    we lower annotations eagerly in a different order (`stringLiteralsWithEqualityChecks03`: `Refrigerator | "foo"`).
    Worth a failstats pass over types/errors diffs that differ only by union order.
  - `aliasUsageIn*` (4 tests): `var m: typeof moduleA = i` where `i` has a `typeof Backbone.Model` property reports a
    missing property now that `typeof` is real. Look at how the static side of a class reached through
    `import x = require()` relates to the same class reached through another import (declaration copies, see
    `canonical_declaration` and `port_item_resolve_alias` in `hacks.md`).
  - `CallableType` could shrink to `{ FunctionImpl* }` (see `hacks.md`), which would delete `CallableArg`, one of the two
    signature resolution paths in `signature_resolve`, and `callable_type_resolve_names`' copying.
  - `display.c`'s syntax printer is only needed for debug output and for written type *names* in the `.types` traversal
    now. `DISPLAY_TYPE_MAX_DEPTH`, `BriefType` and the underline mode (`DisplayUnderlineTarget`) may be dead.
  - `engine.c` (the JS interpreter) still builds; is anything using it?
  - Parse (175 ms) and name resolution (166 ms) are each twice the typecheck phase (87 ms) on the bench. Name
    resolution copying declarations per import (`declaration_clone_in_place`) and the lib scope per program is the
    first suspect; nobody has profiled either phase yet.
- Plan 04 stage 1 leftovers (see `hacks.md`, "The binder"): one test logs "Declaration B acquired multiple times"
  (grep `build/failstats/stderr.txt`); the ~35 tests that lost their symbols component (`python3` diff of
  `test/baseline_results.ts` against the commit before) are mostly `declare global` in odd places, `export { x }`
  next to an exported `x`, and moduleAssignmentCompat1-4 (`Symbol(A)` without a Decl for a non-instantiated namespace
  used as a value).
- The realworld corpus' remaining name errors are node globals (`process`, `require`, `Buffer`: `types: ["node"]`),
  `arguments` and the `debugger` statement (parsed as an identifier).
