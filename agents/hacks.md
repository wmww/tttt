# Hacks
Suboptimal-but-working code. Prefer function names over line numbers.

- `current_checker` / `current_lazy_slots` (thread locals, `checker_make_current`): how the AST-side walk
  (`*_typecheck`, `*_evaluate_type`, `ty_of_node`, the `Ty const*` displayable) finds its checker. Stays set after
  `program_typecheck` because the test traversal and debug display evaluate types afterwards. The type core itself
  takes a `Checker*`; the walk should get one too (a check context with checker + errs + options).
- `lazy_slot_new`: LazyType slot numbers come from one process-wide atomic counter and are never reused, and
  everything that might need a memo takes one eagerly. A checker's slot table is sparse so this only costs page fragmentation (bench RSS +6%), and it
  panics after 1G slots. Per-file numbering (plan 02 §2) didn't fit because Declarations, Scopes and check-time
  identifier types belonged to no file; since plan 04 stage 2 every LazyType is in the AST of a file, so it would. Slots hold a `Ty*` now and check-time clones are gone, so this can be
  revisited; a long-running process (language server) needs reuse.
- `lazy_type_drop`: a slotted LazyType dropped while a checker is current resets its slot (it must be a check-time
  temporary); the AST's own slots are dropped by `checker_drop`, which `program_drop` runs before dropping the AST.
  Dropping an AST while *another* program's checker is current would reset nothing (different slots), so it's safe,
  but the rule is implicit.
- `CompilerOptions` (`compiler_options.c`): every tsc boolean/enum option parses and resolves its defaults, but the
  checker only *reads* `strict_null_checks`, `strict_function_types`, `no_implicit_any` and
  `no_unchecked_indexed_access`. Everything else (target-gated diagnostics, `allowJs`/`checkJs`, `jsx`, module kind,
  unused locals, `exactOptionalPropertyTypes`...) is carried but inert. `compiler_options()` is a global like the
  `ErrorConfig` it replaced, set around load/resolve/check by `program.c`; becomes a `Checker`/`Program` field read
  through a context when signatures get one.
- `baseline_test_case.c: apply_setting`: harness-only directives that are still ignored: `@libFiles` (99 of its 100
  uses are `react.d.ts`: nothing until JSX), `@baselineFile`, `@fullEmitPaths`. Bad option values are ignored too
  (tsc's harness throws). A `tsconfig.json` unit is honoured for options and roots; option diagnostics
  (`program_validate_options`) are skipped when there is one, since tsc then puts them on the option's json span.
- `error_ctx_global_error_s`: diagnostics without a file are `Error`s with a null span, sorted first (by code, then
  message, like tsc), shown by the harness before the first file (`formatted_global_errors`). The CLI's
  `display_error` prints them as `[unkown file]:2`. `error_ctx_error_s` still drops errors with an invalid span.
- `program_apply_comment_directives` (`@ts-expect-error`/`@ts-ignore`): suppression goes by tttt's error *phase*,
  not tsc's diagnostic kind. Only RESOLVE_NAMES/TYPECHECK errors are suppressed, so a tsc checker grammar error
  (`checkGrammar*`, TS1xxx) that tttt reports while parsing is not, and a tsc parse error that tttt reports while
  typechecking wrongly is. In-file program diagnostics (tsc's `getProgramDiagnostics`, eg a bad `/// <reference>`)
  aren't suppressed at all. Line breaks are `\n` only (tsc: also lone `\r`, U+2028/9) and `\s` is ASCII only.
  Directives are recorded by `hop_comment` (deduplicated in `source_file_add_comment_directive`), so this relies on
  every comment going through it and nothing that isn't a comment (JSX text) going through it.
- `SourceFile.unchecked` + `error_ctx_remove_unchecked`: the JS gate is a *filter* run after name resolution and
  after typechecking (semantic errors in an unchecked file are removed from the ErrorCtx), not a skip: names are
  still resolved (imports from an unchecked file need its exports) and `block_typecheck` is skipped only for the
  whole-file case. `check_access()` keeps its own "JS file: no TS2339" gate on top (`is_js`), because expando
  members don't exist. `program_load` refuses a `.js` root without `allowJs` (TS6504) but a `.js` file reached
  through an import is refused by `program_load_module_ref` with `MODULE_REF_IS_JS` (TS7016-style), two paths.
- `TTTT_IGNORE_UNDERLINES=1` (`compare_outputs`): the types component compared without its `^^^` lines, a
  measurement of what the underlines still cost (73 tests on 2026-09-22), not a mode to save results in.
- Types baseline underlines (`ty_display.c` node reuse, see `documentation.md`), approximations of tsc's node builder:
  - A reused annotation is *marked* reused but printed from the `Ty`. tsc re-prints the annotation itself, so where
    the two differ the text is wrong (`w: Array<string>` prints `string[]`, `r: N.Q` written inside N prints `Q`,
    binding pattern parameters print with their initializers `{ a, b = 1 }`).
  - `annotation_accessible()` checks only the first name of each type reference against `enclosing`, treats every
    type parameter as accessible and never looks into `typeof x`. tsc's `visitExistingNodeTreeSymbols` does much more.
  - Instantiated signatures: "reused if the mapper left the type alone and it doesn't mention the signature's type
    parameters" is fitted to probes, not ported; `mentions()` is conservative (any non-reference object type counts).
    `f<number>` (instantiation expressions) reuse nothing in tsc, and we don't print that line at all yet.
  - `single_return_assertion()`: only a body that is exactly `return x as T`; tsc allows other statements before
    it, and runs its whole `typeFromExpression` (literals, object literals, arrows: `() => (X) => {}` prints `X`
    synthetic there) which is not ported.
  - Index signatures, tuple element names, mapped types, type parameter defaults: always synthetic (matches the
    probes, not checked beyond).
  - The `any` underline rule reads whether *tsc's* `.errors.txt` exists (`baseline_test_result_run`), so the types
    component doesn't inherit failures of the errors component. `BaselineTestCase.any_keeps_underline` is a one-shot
    flag set by the traversal right before the add; a site that sets it and then adds no types line leaks it to the
    next line.
- `baseline_test_result_run`: with `@noTypesAndSymbols` the symbols and types components count as *passed*, so the
  per-component totals include ~900 free passes. A test without types/symbols baselines is run before we know
  whether it should have had any.
- `baseline_test_cache_get_program_with_libs`: one cached lib `Program` per distinct lib list (dozens per suite run,
  each with the DOM in it), never evicted. An unknown lib name is logged at info level and skipped, no TS6046.
- `display_type` depth guard (`DISPLAY_TYPE_MAX_DEPTH`): the syntax display has no cycle detection. Nodes can't be
  cyclic any more (the structural namespace types that were are gone), so this is probably dead; check and delete.
- `main.c: default_libs`: without `--lib`/`--target` the CLI loads ES5 + DOM + three ES2015 libs (what the bench
  corpus needs) rather than tsc's default for the target.
- ~60 `PERF`/`HACK`/`TODO`/`FIXME` comments in `src/` not yet catalogued here (triage).
- `failstats.py: line_ratio`: "line match" is multiset overlap of lines, not a diff (difflib was far too slow), so
  it overstates similarity when lines are reordered. Good enough for ranking nearly-passing tests.
- `failstats.py: syntax_gaps`: locates the offending source line by re-parsing tttt's rendered errors output;
  approximate for multi-file tests.
- `bench/make_corpus.py`: corpus is passing tests wrapped in namespaces and replicated. Flatters tttt.
- `main.c: --keep-going`: typechecks after name errors purely so the bench can time the stdlib; not validated.
- `parse_import_equals`: `import x = require("m")` is treated exactly like `import * as x from "m"`. Wrong for
  `export =` modules (not supported at all yet).
- `namespace_decl_add_names`: ambient modules (`declare module "m" {}`) get a scope for their contents but are not
  registered anywhere, so `import ... from "m"` still reports TS2307. Needs a program-level ambient module table
  (stale? plan 04 added `Binder.ambient_modules`; verify and delete). Their `"m"` name line is also missing from the symbols/types baselines.
- `parse_jump_target` / `Statement.jump_label`: labels are parsed and shown in the types baseline, but never checked
  (TS1116 unknown label, TS1107 crossing function boundary, TS7028 unused label, duplicate labels). `inside_label`
  in `BlockTypecheckCtx` just permits any labeled `break` inside any labeled statement.
- `run_baseline_test_case: path_is_script`: virtual files that aren't TS/JS by extension are left out of the program
  entirely. Right for package.json/tsconfig.json, wrong for `.json` modules under `resolveJsonModule` (5 tests).
- `EnumDecl.is_const`: only decides the symbol flag (a const enum doesn't merge with a namespace); no TS2475 etc.
- `parse_import_type`: import attributes (`{ with: { "resolution-mode": ... } }`) are parsed and dropped, on import
  declarations too: `ModuleRef.syntax` only knows import / require / dynamic, so an override never reaches
  `program_load_module_ref()` (resolutionMode* tests, TS1454-1456, TS2836/2841...).
- `parse_type_predicate`: `x is T` / `asserts x is T` become `boolean` / `void`, the predicate is dropped (plan 05).
  `parse_generic_decl_item` likewise parses and drops `in`/`out`/`const` type parameter modifiers, and
  `parse_variable_statement` drops the `!` of `let x!: T`.
- Destructuring (`binding.c`, `lower_binding_access`): pattern elements are still `TYPE_INDEXED` link nodes with
  `rest` / `default_value` bolted on (the default is a non-owning `Expr*` into the `Binding`). Object rest returns the
  whole object type, defaults are checked without a contextual type (see triage: `check_with` has no cycle detection).
- `parse_if_else` looks ahead past a `;` for `else`, because statement terminators belong to
  `parse_append_statement_sequence` rather than to the statement; `ended_on_brace` there is the same kind of guess.
- `EXPR_REGEX` carries nothing but its span: no flag/pattern validation (tsc reports TS1499+ for bad flags and groups).
- `parse_object_item`: `{ a = 1 }` (shorthand with default, only legal in destructuring assignment) parses and throws
  the default away; `?`/`!` after a member name is swallowed without TS1162. `ObjectExprItem` methods are plain
  `EXPR_FUNCTION` values.
- Class accessors (`ClassItem.method.accessor`, `class_item_accessor_evaluate_type`): pairing is by name+staticness in
  `class_decl_lookup_names` (raw pointers between items of the same vec). No TS2300 for property-vs-accessor clashes,
  no check that getter and setter types agree, readonly only means "no setter". Interface/type-literal accessors are
  converted to properties in `parse_interface`.
- Private names (`#x`): the `#` is just part of the member name string. No TS18028 target check, no "not accessible
  outside class" (TS18013), no `#x in obj` (plan 13).
- `p!: T` / `let x!: T`: the `!` is hopped and forgotten (class fields only; `let resp!: T` still doesn't parse).
- Generators (`infer_generator_type`, `check_yield`): no next type is inferred from what the `yield`s are used as
  (tsc's `nextTypes` from the yield's contextual type): it's the contextual/declared TNext or `unknown`. No TS7055/TS7057
  (implicit any generators), no check that the annotation can be a generator's return type (TS2322 "A generator
  cannot have a 'void' type annotation"), returns aren't checked against the annotation's TReturn.
- `infer_generator_type`: a `yield*` of something that isn't iterable prints its type, because tsc reports TS2488 from
  inside the return type inference and the message can find the return type in progress (makes `yield* foo` inside
  `foo` any, YieldExpression6_es6). Faithful to what tsc observably does, but it is a side effect of printing.
- Async functions (`FunctionImpl.is_async`, `promise_decl`): inferred return is `Promise<T>` built from the global
  `Promise` looked up by name during name resolution (`any` if the lib has none, eg target ES5 without `@lib`); returning a promise nests instead of flattening; returns aren't checked against the declared type;
  no TS1064/TS2705. `type_awaited` only unwraps types literally named `Promise`/`PromiseLike`.
- `await_is_operator`: decides keyword-vs-identifier from `ParserShared.in_async`/`function_depth` plus a peek at the
  next character. Top-level await never reports TS1375/TS1378 (needs module detection + `@module`/`@target`).
  Class field initialisers and parameter defaults count as "top level"/inherit the enclosing function's context.
- Decorators (`ClassDecl.decorators`, `ClassItem.decorators`, `FunctionArg.decorators`): parsed, name-resolved,
  typechecked and traversed as ordinary expressions. Nothing checks that they're valid decorators for their target
  (TS1238/TS1240/TS1241...), and `@experimentalDecorators` ordering rules (`export @dec class`, TS1206) aren't applied.
- Computed member names (`interface_member_typecheck`, `class_computed_key_typecheck`): TS1166/1168/1169 exempt entity
  names like tsc, but tsc then makes a *late bound index signature* of an entity-name key whose type is string/number/
  symbol (`[k]: T` with `k: string` is `[x: string]: T`, getIndexInfosOfIndexSymbol); here the member is dropped.
- `ClassItem.method/field.name_string` of a computed key (`[Symbol.iterator]() {}`) is the key's source text: that's
  what accessor pairs and overloads are matched by (tsc matches the late bound symbol, `["a"]` and `a` pair there).
- Unique symbols: the escaped property name of one (`__@sym@12`, `Ty.literal.string`) is mapped back to its type by a
  checker-wide table (`ty_unique_symbol_of_name`), so a *string* property named `__@x@3` would be taken for a symbol.
  `out_unique_symbol_name`: a member of an interface is reached through the global variable named like the interface
  without `Constructor` (`SymbolConstructor.iterator` -> `Symbol.iterator`); tsc finds any variable of that type.
  No TS1330-1335 (misplaced `unique symbol`: it's plain `symbol` there, silently).
- `variable_statement_typecheck` TS1254: any property access is accepted as a "literal enum reference".
- `EnumCase.name`/`full_name`: string-literal member names take their name from the source text between the quotes
  (escapes not processed); enum member types still print as `E.b c` rather than `(typeof E)["b c"]`.
- `engine.c` ignores every construct added in parser rounds 1 and 2 (returns undefined). See triage: is it used?


## The binder (`scope.c`, plan 04 done)
- Identifiers keep two symbol pointers, `symbol` (what the name found, maybe an alias) and `target` (the one with the
  meaning the name is used with, aliases resolved). The plan wanted one pointer plus an accessor; `target` is that
  accessor's result cached at resolve time, so that checking never walks alias chains.
- `binder_redirect` / `Binder.overlays`: a program that adds to a symbol of the shared lib program gets a clone in its
  overlay table (`clone_parent_symbol`, nothing is written to the frozen original), and the checker maps symbols
  through the redirect at its entry points (`canonical()` in `ty/lower.c`: `ty_declared_type_of`,
  `ty_static_type_of`, `ty_type_of_symbol`, `lower_identifier`). Anything new that keys checker state by `Symbol*`
  must do the same or lib-internal references get a second type. `Ty.alias`, `literal.enum_decl` etc. hold canonical
  symbols because they come out of those entry points. Known fallout: `console.log = (...args) => {}` with an
  augmented `Console` now sees both `log` signatures and wants tsc's `getIntersectedSignatures` for the contextual
  signature (signatureCombiningRestParameters2 lost its errors component).
- `make_gather_target`: `import * as ns` / `import x = require()` / `export as namespace N` is an alias whose target is a
  namespace symbol of its own (`Symbol.gather_module` = the module, `symbol_members()` looks through it) instead of the
  module symbol itself, so that its type prints as `typeof ns`. With `out_symbol_name()` in place the module symbol
  could be the target and the display could find the alias in scope; not done, because the symbols baseline also goes
  by the gather symbol's declaration.
- `declare global {}` merges into the globals wherever it is written, also inside a namespace where tsc rejects it
  (moduleAugmentationGlobal8).
- Module augmentations (`merge_augmentations` in `scope.c`, tsc's mergeModuleAugmentation): each `declare module "m"
  {}` block of a module declares into a symbol of its own, `binder_link` merges it into "m" (into an alias' target,
  what comes through `export *`, what `export =` assigns) and retargets the declarations' AST nodes
  (`retarget_decl`). Not merged: into a symbol of the shared lib program, an alias declared inside an augmentation that
  meets an existing name. A name an augmentation *adds* through an `export *` module is added to that module's own
  resolved table, not to the tables of modules that `export *` from it and were resolved earlier. TS2671 (augmenting
  a non-namespace `export =`) is not reported. TS2664 (augmenting a module nobody declares), TS2649, TS1259/
  TS2497 (default/namespace import of an `export =` module without esModuleInterop) and TS2300 for a second
  `export =` are not reported. Named imports from an `export =` module only see the *namespace* side of what is
  assigned, not the properties of its type; `export = a.b` (not a plain identifier) is a variable, so it loses the type
  meaning. `import("./a")` of an `export =` module has no synthetic `{ default }`. An ambient module of the shared lib
  program would need an overlay like the globals (no lib declares one). `binder_module_of_ref()` memoises in the AST's
  `ModuleRef`: all of them are resolved in the link phase, so nothing writes later.
- `parse_import_type`/`add_module_ref`: a `ModuleRef` made during a parse attempt that is later abandoned stays on the
  file's list: harmless (one more file looked for, errors come from the users of a ref, not from the list), but not
  free.
- `globalThis` is a namespace symbol over `Binder.globals`: its type has *every* global as a property, `let`/`const`
  and types' values included (tsc: only `var`s and functions), and `ty_namespace_type` builds all ~2k props the first
  time a program touches it.
- `identifier_expr_lookup_names`: `arguments` has no symbol (typed `IArguments` by name in
  `ty_identifier_reference_type`), and is accepted inside arrow functions at top level too (TS2304/TS2496 missing:
  emitArrowFunctionWhenUsingArguments02_ES6).
- `scope_export_default`: a second default export that means something else (interface + value) is dropped instead of
  merged. `resolve_alias_target`: TS1192 is only suppressed for declaration files under
  `allowSyntheticDefaultImports`; TS2303 (circular alias) and TS2308 (ambiguous `export *`) are not reported.
- **Parallelism contract audit** (the contract is in `documentation.md`, "Resolve names"), what still writes to shared
  state:
  declare phase: (1) scripts and `declare global` declare straight into `Binder.globals`, ambient modules into
  `Binder.ambient_modules` (both want per-file contribution tables merged in the link phase, like module augmentations
  already are; cross-file conflicts are told from same-file ones by comparing the
  source of the first declaration in `report_conflicts`); (2) every symbol, table and scope comes out of the one
  `Binder.arena`, and `binder_new_symbol` appends to `Binder.symbols` (the list `binder_link` resolves aliases from):
  per-file arenas + per-file alias lists; (3) one `ErrorCtx`.
  resolve phase: read-only on symbol tables as far as I can tell (aliases, `export *` tables and `ModuleRef`s are all
  resolved in link), but function/block scopes and their symbols are *made* during this walk, from the same shared
  arena and symbol list; same fix as (2). Flow nodes (`flow.c`) come out of `Binder.arena` too. The walk also writes
  `is_assigned`/`is_assigned_in_closure`/`last_assignment_end` on symbols, but only on `is_local` ones (declared in
  the locals of a scope of the same file), and `Scope.flow` of a file's own persistent scopes (file, namespace, enum)
  for the duration of the walk.
  check phase: the thread-local `current_checker`, see below.
- Scripts declare straight into `Binder.globals` while they're walked, in file order (`SCOPE_SCRIPT`,
  `SCOPE_GLOBAL_AUGMENTATION`). The plan's contract wants a per-file "global contributions" table merged in the link
  phase; until then declare is not parallelisable and cross-file conflicts are told from same-file ones by comparing
  the source of the first declaration (`report_conflicts`).
- Exported and local declarations of one name live in two tables with no link between them (tsc has a local
  `exportSymbol` stub). TS2440 against an exported local is special-cased in `binder_link`; `export { b }` of a local
  that is also declared exported doesn't show the merged declarations (mergedDeclarationExports), TS2395 is missing.
- `block_is_instantiated`: syntactic; an `export { x }` always instantiates a namespace, a const enum does too
  (for the symbol; the TS7027 rules ask without const enums unless `preserveConstEnums`).
- `let`/`const` against a hoisted `var` of an inner block (TS2451 by tsc's checker), TS2403 (subsequent `var` of a
  different type: the first declaration gives the type), function declarations in blocks under sloppy mode: not done.
- Class and interface *members* are not symbols (`Prop` has the identity the checker needs); duplicate member
  diagnostics (TS2300 in ~80 tests) still come from nowhere.
- `symbol_table_collect` allocates; `ty_namespace_type` members and `extend_export_stars` go through it.
- One test logs "Declaration B acquired multiple times" (a node whose names get added twice; not found yet).
- `check_access` reports TS2339 on modules too now (`import * as ns; ns.missing`): where module resolution picks the
  wrong file (nodeModulesTypesVersionPackageExports) that shows as spurious TS2339 next to the TS2307s (plan 06).

## Control flow graph (`flow.c`, the builder parts of `expr.c`/`statement.c`/`function_impl.c`; plan 05)
- The checker half of TS7027 (tsc's `checkSourceElement`: statements whose flow node turns out unreachable) doesn't
  exist and `Statement` has no flow pointer: see the plan's stage 1 notes.
- No `FLOW_ARRAY_MUTATION` nodes (`x.push()`, `x[i] = v`): only evolving array types need them (out of scope).
- Class *expression* methods are FUNCTION_IS_DECLARATION (tsc: narrowing continues outside like for object literal
  methods); `export { x }` doesn't mark the local as assigned anywhere (tsc's `markNodeAssignments` does).
- `expr_is_narrowable_reference()`: `a = b` counts whatever the left side is (tsc wants a left-hand side expression).
  `expr_is_optional_chain()` re-walks the chain from the top for every link (quadratic in chain length, chains are
  short).
- A destructuring assignment nested in another pattern (`[[a] = [1]] = e`) gets assignment nodes from the outer
  pattern only; tsc makes them twice.
- `statement_is_bare_return()`: the parser represents `return;` as an `undefined` literal whose span starts with
  `return`. A `has_value` bit on the statement would be honest.
- An `else if` that starts unreachable code can't be reported (TS7027): `IfStatement` is a flattened chain and the
  nested if has no `Statement`. Same for its span in `last_assignment_end`.
- `function_impl_check_end()`: an anonymous `function` expression is reported at its parameter list (tsc: the
  `function` keyword); async functions and generators with a return annotation are skipped (needs the awaited /
  returned part of the annotation), as is TS7030 on bare `return;` statements (tsc's `checkReturnStatement`).
- Body-less functions (overloads, signatures, function *types*: the libs) get a `FlowBuilder` on the stack but no
  start node, so parameter default expressions in them have no flow nodes.
- `Statement.span` (16 bytes per statement) exists only for TS7027 ranges and `last_assignment_end`.

## Narrowing (`ty/flow.c`, the hooks in `ty/expr.c`; plan 05 stage 2)
- Aliased conditions (`aliased_condition`, plan 08) only narrow identifier references (tsc also readonly properties).
- Not there (each has a TODO in `flow.c`): destructured discriminants (`const { kind } = x`), `x.constructor === C`, `[Symbol.hasInstance]` predicates,
  `"a" in x` intersecting with `Record<"a", unknown>` when no member is known to have the property, `unknown`
  narrowed by the cases of a `switch`,
  the key property map tsc uses for big unions (`getKeyPropertyName()`: an optimisation, but it also changes which
  members `getNarrowedType()` starts from), erasure of generic construct signatures in `constructor_instance_type()`.
- `narrowed_by_candidate_member()`: tsc tries the strict subtype relation, then the (non-strict) subtype relation.
  There is only `TY_RELATION_STRICT_SUBTYPE` here.
- `prop_has_explicit_type()`: a property of a mapped type (`Readonly<Services>`) counts as explicitly typed; tsc asks
  the property it was mapped from. Object literal members never count (right), but neither does anything tsc finds
  through a JSDoc type.
- No TS2775 / TS2776 (an assertion whose callee has a name without an explicit type is silently not an assertion),
  no TS1228 for predicates in the wrong place: `new (x) => x is T` and accessors parse as predicates and are treated
  as such, `(x: this is T)` parameter types don't parse at all (a parse error hides every semantic error of the
  file: `assertionTypePredicates1`).
- `Statement.flow` costs 8 bytes per statement for the sake of TS7027 after `never` calls, which is an error only
  under `allowUnreachableCode: false`. Labeled statements have none (tsc gives both the label and its body one).
- `chain_object_type()` (`ty/expr.c`): tsc marks the `undefined` of an optional chain that ended with a marker type
  and strips only that when the chain continues. Here every `undefined` of an object that continues a chain is taken
  for the marker, so `a?.b.c` with an optional `b` isn't an error. Wants an `optional` intrinsic that prints as
  `undefined`.
- `remove_nullable_by_intersection()`: tsc intersects with `{} | null` / `{} | undefined` when the other nullable is
  still possible; here always with `{}`.
- No `getNarrowableTypeForReference()` (substituting constraints for generic references in narrowable positions), no
  `x!` rule of `getFlowTypeOfReference()` (declared type when the narrowed type has nothing non-nullish left), no
  evolving arrays (`let x = []` and `x = []` on an auto-typed `let x;` are `any[]`), no TS2563 at flow depth 2000 (the query answers the error type silently).
- `pattern_path_type()`: the assigned type of a target in a destructuring *assignment* is looked up with `ty_access()`
  by index / name; rest elements, computed keys and iterables fall back to the declared type (no narrowing).
- Auto types (`let x;` under noImplicitAny, `TY_AUTO`): `IdentifierExpr.outer_reference` is decided by the resolve
  walk (a hoisted `var` declared after the closure isn't outer), and tsc's isNeverInitialized (outer, but never
  definitely assigned) is approximated by `!symbol->is_assigned`.
- The "outer variable is assumed initialized" rule is decided when the walk reaches a start node
  (`Symbol.flow_start` vs the node, `FlowQuery.left_container`), not up front like tsc: same answers, but an inlined
  IIFE's locals belong to the flow around it, which is what tsc says too.
- Optional parameters read as `T | undefined` inside the function (`value_entry_type()`), but signatures still show
  and relate `x?: T` without it (tsc prints `x?: T | undefined` under strictNullChecks).
- `ty_flow_expr_type()` results computed while a loop is in progress are cached per back edge walk under a
  generation number in `Checker.links`: the entries of finished generations are garbage until the checker dies.
- `LazyMemo.hits`: expression memos allow 8 re-entrant evaluations before they answer the error type. It's a runaway
  guard, not a design: tsc has no expression-level cycle detection at all.
- Narrowing by destructured discriminants
  (`dependentDestructuredVariables`) and `in`
  adding `Record<"a", unknown>` to `object`/`unknown` (`inKeywordTypeguard`) don't exist. They show as spurious
  TS18048 / TS2339 now that property access is checked.

## The AST-side walk and what's left of the old type world
- `TYPE_LAZY`, `TYPE_REF`, the binding `TYPE_INDEXED`, `CallableArg`, `type_clone` and its whole family are gone:
  `TypeNode` is syntax only. Leftovers of that world: `owned_symbol` + `symbol_ref` on `GenericItem`/`InferredType`/
  mapped keys and `key.expr.owned` + `ref` on interface members only existed for clones, one pointer each would do;
  `LAZY_TYPE_FUNCTION` memoises `ty_function_type()`, which is interned already (the LazyType could go, its users
  are `value_entry_type`, `class_item_get_type` and the test traversal).
- `signature_resolve`: a method signature *written as a type* (`FunctionImpl.is_type`, `ctx.method`) lowers its
  parameter types with `ctx.method` still set, so a callback parameter of an interface method is itself a "method"
  signature (bivariant parameters) wherever that type ends up. This was an accident of the old `CallableType` path
  and stands in for tsc's rule (callbacks of a bivariantly compared signature are compared bivariantly, `Callback`
  check mode in compareSignaturesRelated). Without it `Array<X>` -> `ReadonlyArray<Y>` fails in
  `nonInferrableTypePropagation2`. Class methods (not `is_type`) don't get it. The honest fix is in `relate.c`.
- `ty_binding_assigned_type`: not memoised per binding (the root's LazyType is, the accesses down a nested pattern
  are redone per query); arrays only, no iterables; an object rest is the whole object.
- `FunctionImpl.constructed_class`: a class constructor's return type is `ty_class_instance_type()` of its class,
  set in `class_decl_lookup_names` (which also reports TS1093 and drops a written return type).
- `ty/check.c: check_access` (TS2339 and friends) is honest about unions, intersections, `null`/`undefined` and
  `unknown` objects since plan 05 stage 4, but lets through what it knows the type core can't answer yet: **JS files**
  (members declared by assignment: `this.x = 1`, `module.exports`, prototypes), **callables without construct
  signatures** (expando functions, `function f() {} f.x = 1`, also in TS), **generic mapped types** (`Pick<T, K>`,
  `Readonly<T>`: members only through the constraint) and **classes with a base `class_base()` can't fully see**
  (`ty_has_unresolved_base()`: mixins `class extends B` with `B: T`, unresolvable `extends` expressions, the static
  side of any expression base). Bracket access never reports a missing property. TS2550 ("do you need to change
  your target library") and TS2551 ("did you mean") are reported as TS2339 / not at all; TS18050 never.
- `expr_typecheck()` `EXPR_PROPERTY`: everything after the `?.` of a chain is checked against the non-nullable type
  of its object, because the `undefined` of the short circuit isn't told apart from one the property has itself
  (tsc: `optionalType` marker): `a?.b.c` with `b?: X` misses its TS18048.
- `class_base()` (`ty/members.c`): an `extends` that isn't a class name is the return type of the *first* construct
  signature of the expression's type; no written type arguments, no static side, no intersection with the base
  constraint for mixins (hence the gate above).
  A source that is `any` is let through to anything, even `never` (`check_assignable`).
- `TY_RELATION_ASSIGNABLE_STRICT_NULLS`: what the operator checks in `expr.c`/`ty/expr.c` mean by "extends number"
  (null/undefined don't, whatever strictNullChecks says). tsc checks operands differently (TS18050, never emitted here).
- `function_impl_typecheck_signature` TS2370: skipped for `...args: infer A` by looking at the written node, and for
  a *reference* to an unconstrained `infer` type parameter (`(...a: A1) => R1` in the true branch): the implied
  `unknown[]` constraint only exists once the signature that declares `A1` was resolved (`imply_rest_constraint`),
  which depends on evaluation order. tsc derives it from the position of the `infer` (getInferTypeConstraint).
- `generic_params_expr_typecheck` (`f<A>` outside a call): constraints are checked against the first signature only.
- `for_each_loop_statement_evaluate_var_type`: `for..in` is `string` (no `Extract<keyof T, string>`); `for await`
  doesn't parse.
- Iteration (`ty/iterate.c`): no TS2763-2765 (the next type of an iterator vs what for-of/spread/destructuring send),
  no TS2489/TS2490 (an iterator without `next()`/`value`: it just isn't iterable), errors are only reported at the top
  (tsc adds related info). Array destructuring *assignments* (`[a, b] = it`) check against the iterated type but the
  array literal target still types as an array, not a tuple (tsc's inDestructuringPattern), and a binding pattern
  isn't the contextual type of its initializer (`var [a, [b]] = [1, [2]]` gives `(number | number[])[]`).
- `ty_function_return_type`: `void` when there's no `return` (tsc: `never` if the end isn't reachable), async wraps in
  `Promise<T>` without awaiting `T`. A return type that is asked for while it's being inferred makes it `any` (tsc's
  failed popTypeResolution), without TS7023.
- `lower` TYPE_TYPEOF: `typeof x` is the type of the expression `x`, narrowed like any reference (tsc narrows type
  queries too, but only identifiers and property paths exist there).
- The "written type shows as written" hack is gone (plan 04 stage 3: `out_symbol_name()`, names by accessibility from
  `TyDisplay.enclosing`); removing it uncovered what it hid in 10 tests: unresolved qualified names and `this` types
  lowering to `any`, alias names lost (`Pair<FooBar>` printing structurally). The enclosing scope is tracked per file
  and namespace only: function/class/block scopes aren't kept on their AST nodes, so a local declaration shadowing an
  outer one prints unqualified where tsc would qualify.
- `ty_merged_value_type`: the merged type copies the first named entry's `declared` struct (a class static side or an
  enum object) and gives it all entries' members; it's a second `Ty` next to the unmerged one (`ty_static_type_of`,
  which is what `this` in a static member and a derived class's base static side see).


## Calls, inference and property access (`ty/call.c`, `ty/infer.c`, `ty/access.c`): `ty/call.c`, `ty/infer.c`, `ty/access.c`
The algorithms are meant to stay; what's listed is where they stop short of tsc.
- `infer_from` (infer.c): no reverse mapped types (only the homomorphic `T[K]` template is seen through), alias type arguments are only used for deferred types (there are no variances for aliases), no
  `InferencePriority` beyond naked-type-variable and return-type, type predicates don't exist (parser drops `x is T`).
- `ty_union_signatures` (relate.c, tsc's getUnionSignatures): tsc's identity relation is approximated as "the same
  `Ty`, or a strict subtype both ways"; union signatures aren't cached per union type (built on every call); `this`
  parameters aren't intersected (there are none). `resolve_call` still falls back to member-by-member resolution when
  some member has no signatures of the kind (tsc: TS2349 "Not all constituents of type ... are callable").
- `report_arity`: no "An argument for 'x' was not provided" related info; the span for too few arguments is the
  callee's `inner_span`, not tsc's name-of-the-property-access rule.
- Heritage checks (`ty_check_heritage`, `class_heritage_typecheck`): no TS2417 (static side), no TS2320 (inherited
  properties of several bases that aren't identical), no tsc-specific elaborations ("The types returned by 'toSeq()'
  are incompatible between these types", "'X' is assignable to the constraint of type 'this', but..."); interface
  heritage (TS2430) is off (triage.md).
- `skipLibCheck` makes declaration files `unchecked` (their semantic diagnostics are dropped, like tsc's
  skipTypeChecking); `skipDefaultLibCheck` isn't looked at.
- Contextual `this` (`ty_contextual_this_type`, plan 10 stage 2): the object literal's contextual type is remembered
  when it is checked (`KEY_LITERAL_CONTEXT`, the last one wins) and so is its enclosing literal (`KEY_LITERAL_PARENT`),
  since there are no parent pointers; a `this` asked for before the literal was checked falls back to the literal's
  own type. `ThisType<T>` is recognised by name (a global `ThisType`), and its argument isn't instantiated with the
  inference in progress (tsc: getMapperFromContext(getInferenceContext(literal))). No TS7041 (`this` captured by an
  arrow function at the top level), no TS7017 on `typeof globalThis`.
- `clone_type_params` (members.c, plan 11): an instantiated generic signature gets fresh type parameters (tsc's
  cloneTypeParameter) only for those with a constraint or default (`ty_param_is_bounded`); unbounded ones stay shared
  with the target (indistinguishable, and keeps `Box<A>.map` vs `Box<B>.map` a plain comparison instead of an
  inference). Something that relies on two instantiations *not* sharing an unbounded type parameter would notice.
- Higher-order inference (plan 11, `call.c` pass 2, `infer.c: inference_propagate_generic`): tsc's two rounds per
  candidate are tttt's two passes; generic function arguments (`Arg.generic_function`) and calls of generic functions
  returning functions (`Arg.generic_call`, decided once per call in `collect_args` from the callee's apparent type) go
  to pass 2 in argument order, and a round that skips context-sensitive arguments skips them too
  (`waits_for_sensitive`). A failed call's return type is re-inferred without them (tsc's
  inferSignatureInstantiationForOverloadFailure), after which the arguments are checked against the failed candidate
  once more only to put their memos back (the types baseline prints the last check).
- Function expressions take the contextual signature's type parameters (`signature_resolve`, via `KEY_CONTEXT_DONE`)
  only if the signature is resolved after the context was assigned; tsc does it in assignContextualParameterTypes.
- `ty_awaited`: `Promise<T>`/`PromiseLike<T>` by name.
- `ty_access`: `null`/`undefined`
  members of a union object are skipped silently (TS18047/18048 aren't reported), optional chaining isn't known here.

## Expressions (`ty/expr.c`), contextual typing, the call passes
`ty_check_expr(expr, contextual)` is meant to stay; what's listed is where it stops short of tsc.
- **Assignment targets** (`assignment_target_typecheck`, expr.c): a property target is checked against its *read*
  type; tsc uses the write type (`getWriteTypeOfSymbol`), so divergent accessors (`get x(): number; set x(v: number |
  string)`) give a false TS2322 (divergentAccessors*, getterSetterSubtypeAssignment). Needs a write type on `Prop`.
  Array pattern rest elements, object rest and non-array iterables aren't checked (null source), and `{ a = 1 } = o`
  isn't checked at all because the parser drops shorthand defaults (`ObjectExprItem.shorthand_default`). The array
  pattern's own type isn't the tuple tsc gives it. Expando assignments to functions (`foo.constructor = 1`, nullPropertyName)
  are checked against the existing member.
- **Array literal types aren't marked** (tsc's ObjectFlags.ArrayLiteral clones the array reference): inference
  approximates tsc's unionObjectAndArrayLiteralCandidates with `Inference.array_literals`, the types of array literal
  arguments (not array literals nested in other arguments). Object literal types are marked (`anonymous.object_literal`).
- **No evolving arrays** (`let x = []` then `x.push(1)`, tsc's autoArrayType): `initialized_binding_evaluate_assigned_type`
  gives an unannotated variable initialized with `[]` the type `any[]` under noImplicitAny. Everywhere else `[]` is
  `never[]` under strictNullChecks like in tsc.
- Fresh literals (plan 12): the `.types` side prints regular expression types, but annotation reuse is what makes
  tsc print `(p: E.x)` / `(a: Yes)` for a parameter annotated with a single-member enum member or an alias of one;
  tttt prints `E` / `Choice.Yes`. `check_with` doesn't strip freshness from a literal of the contextual type (tsc's
  checkExpressionWithContextualType): harmless while relations normalise. Literal ids follow tsc only where tttt
  makes literals in the same order (`1 | 0` vs `0 | 1` in `literalTypes2/3`: tttt makes literals outside checking).
- Excess property checks (plan 12, `has_excess_properties` in relate.c): no elaboration (tsc's elaborateError drills
  into the literal and reports at the nested property with a related TS6500; tttt reports the outer relation, located
  at the nested property name). No TS2561 (spelling suggestion). The keys of a generic mapped type count as known
  (`members->mapped`), the intersection-target re-check of nested properties (tsc's structuredTypeRelatedTo, "Nested
  excess property") isn't there. The discriminant reduction of a union target is tsc's discriminateTypeByDiscriminableItems
  without the key-property map. Contextual types of union members are reduced (`string | "a"` is `string`, tsc keeps
  both with noReductions), so a discriminant can widen and pick the wrong member (`contextualTypeShouldBeLiteral`).
  The for-of destructuring assignment check (statement.c) relates the regular element type (HACK: tsc checks a
  pattern property by property). Binding patterns don't make an implied type (tsc's getTypeFromBindingPattern), so
  `function f({ x, y = 0 } = { x: 0 })` has `{ x: number }` and `f({ x: 1, y: 1 })` is a spurious TS2353
  (`destructuringWithLiteralInitializers`, also the missing optional `c1?:` in `literalTypes2`).
- `widen_nullable` (null/undefined -> any without strictNullChecks) is shallow: top level and array elements only.
- Expression memo (`check_with`, `LazyMemo.context`, `lazy_type_set`): the memo of an `Expr` is whatever the last check
  with a contextual type produced (overload candidates and inference passes overwrite each other; the chosen candidate
  is checked last). `kind_uses_context` decides which expressions get rechecked for a new contextual type. Correctness
  rests on the rule "whoever knows the contextual type checks first": `expr_typecheck` evaluates the expression's own
  type before walking into it, `initialized_binding_typecheck`/return statements/calls check with their context first.
  An expression first reached some other way (the `.types` traversal of a program that wasn't typechecked) is typed
  without context. Flow analysis is the main "other way": it asks for assigned types bottom-up, before the walk
  reaches them (return type inference of a function referenced early). `initial_or_assigned_type()` re-derives the
  context for context-sensitive values of `x = e` and annotated declarations; other bottom-up entry points (compound
  and logical assignments, destructuring defaults) still type function expressions without it.
- `Checker.return_context`: the contextual type of `return` expressions is checker state saved/restored around
  function bodies (`function_impl_enter_return_context`), because `statement_collect_return_types` and
  `statement_typecheck` have no parameter for it.
- `assign_contextual_signature`: assigned once per function expression (tsc's ContextChecked), from the first contextual
  type that has a usable signature. Generic contextual signatures aren't instantiated (their type parameters leak into
  the parameter types), no contextual `this`. `discriminate_by_object_members` skips the optional properties a literal
  leaves out and tsc's getMatchingUnionConstituentForObjectLiteral shortcut.
- `ty_infer_type_predicate` (flow.c): builds its synthetic condition flow nodes and reference `Expr` in the arena on
  every call of `signature_predicate` for an unannotated function (once per signature, memoized there).
- `narrowable_type` (expr.c, tsc's getNarrowableTypeForReference): only the constraint-position trigger, marked on
  identifiers by the resolve walk (`IdentifierExpr.constraint_position`); property access chains (`a.b.c` with a
  generic `a.b`) and the "contextual type without generic types" trigger are missing.
- `infer_type_arguments` second pass: a type parameter is fixed before a context-sensitive argument if the contextual
  signature's parameter types mention it, or those of a function its return type returns, for a function argument
  (`fixed_by_argument`: probed by instantiating with `unknown`; anything that isn't a single-signature object type
  fixes everything it mentions). tsc fixes lazily, when a contextual parameter type is actually asked for. Inference from the contextual return type only
  happens when that type has no type variables (no outer inference context, no return mapper). Still no reverse mapped
  types: `contravariantOnlyInferenceFromAnnotatedFunction`, `reverseMappedTypeInferenceSameSource1` and friends used to
  pass because object literal arguments only had to be *comparable*; that mitigation is gone.
- `resolve_call` still runs twice per call (type, then errors) and nothing is cached per `CallExpr`; argument memos make
  the second run cheap but not free.
- `ty_non_nullable` of a type parameter or deferred type whose constraint allows null/undefined returns the stripped
  *constraint* (tsc: `T & {}`), which loses `T`.
- `ty_unresolved`: an unresolved type name lowers to an error-`any` that prints as the written name (tsc does the same);
  it's a `TY_INTRINSIC` with `TYF_ANY | TYF_ERROR`, interned by name in the literal table.
- `check_object`: computed keys that aren't literals, unique symbols or plain string/number/symbol/any are **left out
  of the type** (unions of literals). A setter-only property takes its parameter type, no `this` typing for
  methods, duplicate names: last one wins silently. Spread copies the apparent type's properties (no optionality
  handling, no generic spread).
- `check_array`: an empty array literal is `any[]` (tsc: `never[]` under strictNullChecks, evolving `any[]` for an
  unannotated variable). Spread elements of non-arrays are `any`. Tuples only from a tuple-like contextual type or
  `as const`.
- `compute`: `typeof x` is `string` (tsc: the union of typeof results), tagged templates take the return type of a lone non-generic signature (no call resolution), `yield` is `any`,
  binary arithmetic is always `number` (no bigint; unary `-`/`~`/`++`/`--` follow tsc's getUnaryResultType), `+` goes
  by `TY_RELATION_ASSIGNABLE_STRICT_NULLS`. Bigint values are doubles (`Value.bigint`), so `2n ** 64n` literals lose
  precision.
  `satisfies` contextually types its operand but the result isn't narrowed by it.
- `ty_apply_type_arguments` (instantiation expressions): maps the type parameters of every signature that takes at
  least that many type arguments; signatures that don't fit aren't dropped, and nothing reports TS2635.
- `baseline_test_traversal.c`: the `extends Base` line shows the return type of the base's first construct signature.
  Known display gaps: `this: void` parameters don't print (`contextualTyping24`), qualified names in the type of an
  assignment expression (`recursiveGenericUnionType1`).
- `is_construct_signature_start` (parser): in a type, `new` followed by `(` or `<` is a construct signature. A method
  that is really called `new` with a type parameter list (`new<T>(): void` meaning a method) is misparsed now; tsc
  treats that as a construct signature too.

## Type core (`src/ty/`): known gaps
- Template literal types and string mappings (`template.c`, plan 18): `apply_mapping` (Uppercase & co.) maps ASCII,
  Latin-1, basic Greek and Cyrillic only (JS's toUpperCase covers all of Unicode, some characters to several); a
  one-character placeholder match steps one UTF-8 code point (JS: one UTF-16 unit). The cross-product cap (100k,
  `ty_template_literal`) and the tuple cap (10k, `normalized_tuple`) return the error type without reporting
  TS2590/TS2799/TS2800. `checkTemplateLiteralType` (TS2322 for a placeholder type that isn't string | number | bigint
  | boolean | null | undefined, e.g. `${symbol}`) doesn't exist: such a template is silently `string`. `NoInfer<T>` is
  plain `T` (tsc: a substitution type that inference skips), made in `intrinsic_alias_instance` (lower.c).
- Alias renaming of a generic alias instance (`renames` in `lower`, TYPE_IDENTIFIER): tsc's rule depends on how the
  alias body instantiates, approximated syntactically: never for `keyof` and intrinsic bodies, a conditional body only
  if the result still carries the alias (deferred, or distributed over a union), a homomorphic mapped body only if the
  result is a union. Unions don't have tsc's `origin` (how a union was written: `P & ("a" | "b")` prints distributed).
- `keyof` of a generic mapped type without `as` stays a deferred `keyof` (tsc's getIndexTypeForMappedType gives the
  constraint; doing that broke inference in localTypeParameterInferencePriority), and with `as` it isn't the `as`
  type over the constraint either.
- Relations to a generic mapped type (`generic_mapped_target_related`, relate.c) follow tsc's isGenericMappedType
  rule, but only a source without type variables gets its answer: a generic one that fails it falls through to the
  member-by-member comparison, which a mapped type without members passes. That permissive fallback stands in for
  mappedTypeRelatedTo (generic mapped to generic mapped), homomorphic `Readonly<T>` to `T`, and the like. The mapped
  type parameter P gets no constraint when lowered, so `S[P]` is compared as `S[Q]` (with an `as` clause a stand-in
  P' constrained to Q is used, `mapped_key_param_mapper`, cached per MappedInfo). Target `T[K]` uses the write type
  (intersection over a union constraint of T, `write_constraint_of_indexed`).
- `class_base`: the base class is found syntactically (`extends Name`, `extends Name<Args>`, `extends ns.Name`); tsc
  evaluates the expression and takes the return type of its construct signatures (mixins, `extends f()`). The static
  side inherits properties from the base's static side and, without a constructor of its own, its construct signatures.
- `builder_add_class_items`: no accessibility (private/protected are ordinary members; plan 13).
- `this` types (plan 10 stage 1): `Prop.thisless` follows tsc's syntactic isThisless* rules, so every class member
  without annotations is instantiated once per class (and per derived class: inherited members are the base's with
  this-argument = the derived `this`). No `this is T` as a *property* type (`isFSO: this is FileSystemObject`
  doesn't parse, typeGuardOfFormThisMember), no TS2526 for `this` outside a class/interface, `this` in a static member
  is the static side (tsc: error), relations compare a reference with a this-argument structurally with the plain one
  (tsc: the this-argument is one more covariant type argument).
- `ty_resolve_enum` / `evaluate` (enums.c, also the constant folding of template expressions): no property accesses of
  `const` objects, `const` variables in another file count as constant without tsc's resolvedOtherFiles flag, members
  of ambient enums without initializer auto-increment (tsc: computed). Declaration-merged enums don't get this far
  (`declaration_init` refuses a second namespace). String-named members print as `E.b c`, not `(typeof E)["b c"]`.
- `relate.c` gaps: no weak type check, no private/protected/nominal class
  rules, no optionality of parameters (`undefined` isn't added
  under strictNullChecks), signatures/index signatures of a source intersection aren't combined (properties are).
  Array<S> -> ReadonlyArray<T> is a shortcut on the element types (`structured_related`).
- `has_conflicting_discriminants` (checker.c): tsc reduces an intersection with disjoint discriminants to never lazily
  (getReducedType), tttt when the intersection is made. That can be in the middle of lowering one of the properties
  or an alias its type names (`type B = X & { e: A }; type A = I | B`, the corpus' BindableStatic*Expression), so a
  property that is in flight or whose node reaches an alias in flight (through unresolved aliases,
  `ty_node_reaches_alias_in_flight`) isn't judged, and the answer isn't remembered. The intersection itself is
  interned with that answer. The real fix is a lazy reduced view of intersections.
- `get_variances`: arrays, tuples and synthetic targets are hardcoded covariant (tsc does the same for arrays and
  tuples). `VARIANCE_UNRELIABLE` is set from a checker-wide flag whenever a deferred type or mapped type is compared
  while *any* variance is being measured; cached sub-results don't set it. No alias variances: instantiated
  aliases of object types compare structurally (the maybe-cache keeps that finite).
- Relation cache keys are plain (source id, target id); tsc's key postfix for in-scope type parameters and
  intersection state isn't there. `RelationState.overflow` (100 deep) reports through `instantiation_limit_hit`,
  which only `check_assignable_or_error_internal` turns into TS2589.
- `ty_conditional_resolve`: without a permissive instantiation, a failed check with type variables nested in either
  side stays deferred (tsc would pick the false branch if it fails even with them replaced by `any`). `infer X extends
  C` constraints aren't checked. `infer_from`: no priorities, no contravariant candidates (union of everything),
  union targets infer to every member. An `infer` in rest position gets its implied `unknown[]` constraint by
  mutating the type parameter's state (`imply_rest_constraint`, and up front in `lower` for TYPE_TERNARY).
- `TyCondBranch` / `ty_deferred_reference`: conditional branches and array/tuple nodes are only lazy when the node
  syntactically reaches the alias being resolved (`node_reaches_alias_in_flight*`); tsc defers by position. A named
  generic reference to the alias in flight outside of those (`type T = Foo<T>` is an error in tsc, but `type T =
  { a: T } | Foo<T>`-style through other aliases isn't always) still becomes the error type.
- `expand_mapped`: mapped types expand to properties/index signatures once the key type is a union of literals and
  primitives. No `as` clauses, `-?` doesn't strip `undefined`, modifiers come from `MappedInfo.modifiers_type` only when
  the key type was written as `keyof T`.
- `instantiate_mapped_over_list`: a homomorphic mapped type instantiated with an array or tuple becomes an array or
  tuple. Recognised syntactically (one mapped member over `keyof`) so instantiation doesn't force members; bails out
  (object with mapped array methods, as before) for tuples with rest/variadic elements, unions of lists, `any`, and a
  type parameter constrained to a list.
- `ty_realias` / alias names: `type Bar = Omit<Foo, "c">`, and arrays, tuples, `T[K]` and conditional types written as
  an alias body, go by the alias name like in tsc. A renamed object type is a second `Ty` with the same target +
  mapper (members resolved twice); renamed deferred types are copies interned by (type, alias, args), related to their
  unnamed twin structurally. tsc's "local alias" exception isn't applied, the union a distributive conditional
  resolves to gets the alias only when it comes out of `ty_instantiate`, and `keyof` never has one (same as tsc).
- `Ty.conditional.root` + `.mapper`: instantiation restarts from the conditional type as written (what makes
  `Exclude<keyof T, K>` distribute inside `Omit`), with the mapper cut down to the root's outer type parameters and
  cached by what they map to (`instantiate_conditional`, tsc's getConditionalTypeInstantiation). The outer
  parameters come from `collect_free_params` on the ternary node (`conditional_set_outer`), so they share its limits
  (below); when it gives up (`TyConditionalOuter.unknown`: `this`, `typeof`, more than 16 parameters) instantiation
  falls back to the old uncached path with the full composed mapper. Results cut short by the instantiation limits
  aren't cached; anything else that depends on state outside the key (a branch in flight reads as the error type)
  is, as in tsc.
- `lazy_type_get((LazyType*)...)` const casts (`ty/expr.c`, `ty/lower.c`, `ty/members.c`): harmless, the memo is in the
  checker; they go when `lazy_type_get` takes a const pointer.
- `ty_keyof` of primitives and `ty_indexed` on anything but plain props / index signatures / tuple and array
  elements stay deferred.
- `registered_type_param`: `infer X` and mapped-type keys are `DECLARATION_TYPE` declarations, indistinguishable
  from aliases unless the conditional/mapped type that introduces them was lowered first (true whenever lowering
  starts from the enclosing declaration or annotation, which is the only way they're in scope).
- `lower_identifier`: the `object` keyword is recognised by being a builtin declaration named "object".
- `lower` (composites): skips the `any` placeholder `parse_type` makes for a leading `&`.
- `lower_tuple`: rest elements keep the written type (`...T[]` holds `T[]`), tsc holds the element type. Tuple
  targets have no members yet (`length`, numeric props, array methods).
- `collect_free_params` / `Ty.anonymous.free_params`: which outer type parameters an anonymous type's node mentions,
  found by walking the node when the type is created. Parameters declared inside the node (signatures, mapped keys,
  `infer`) are taken out at the end rather than tracked by scope, and it gives up (`free_params_unknown`, = assume all) on `typeof`, `this`, computed keys,
  parameters with initializers, and for every function and merged value type (a body can mention any type parameter in scope). `TyMapper` is always flat (no composed/merged forms yet,
  inference will want them). `LowerCtx.generic` is only a "don't bother walking" hint.
- `signature_resolve` (instantiated): keeps the target's type parameters instead of making fresh ones. Return types
  are lazier than the rest (`signature_return_type`), because they're often inferred from a body that is being checked.
- `builder_add_interface`: index signature and mapped type operand types are lowered eagerly (property and signature
  types are lazy); computed keys that aren't string/number literals are dropped (unique symbols).
- `ty_union`: no subtype reduction, doesn't drop `null`/`undefined` without strictNullChecks. TS2456 is only a TODO.

## Module resolution (`src/module_resolution.c`, `src/tsconfig.c`, plan 06)
- A resolved `.json` module is `any` (`MODULE_REF_IS_JSON`): the file isn't parsed into a type, `path_is_script` keeps
  it out of the harness' program too. `requireOfJson*`, `jsonFileImport*` (~35 tests).
- A `.js` file that resolves with `allowJs` is loaded and parsed **as TypeScript**; without `checkJs` its semantic
  diagnostics are dropped (`SourceFile.unchecked`), but its *shape* is still wrong: CommonJS `exports.x =` files
  are not modules (TS2306 in the importer, 27 tests), JSDoc types are comments. See `triage.md`.
- JavaScript from a package is never part of the program (`maxNodeModuleJsDepth` is taken as 0, the option isn't read).
- `apply_realpath` only runs for node_modules results, like tsc, but tsc also keeps the *original* path
  (`originalPath`) for declaration emit and for "is this the same package" checks (TS2300-style duplicate package
  detection, `duplicatePackage*` tests): we have no package ids at all (`name@version` from package.json).
- `tryLoadInputFileForPath` (mapping an `exports`/`imports` target in `outDir`/`declarationDir` back to the input
  `.ts`) is not implemented: `nodeNextPackageSelfNameWithOutDir*`, `nodeNextPackageImportMapRootDir`, 8 tests.
- TS7016 has no elaboration ("Try `npm i --save-dev @types/x`..." needs package ids) and no "alternate result"
  chain (TS6278: types exist but `exports` hides them; tsc re-resolves without exports to find out).
- Automatic `@types` packages that don't resolve are silent (tsc: silent too); `types` entries get TS2688
  (`program_load_automatic_types`). TS5055/TS5056/TS6054 and the outFile/sourceMap family need emit paths: not done.
- Windows-style rooted names (`c:/root/...`, `A:/foo`) are not paths to us (3 tests).
- `/// <reference lib="..." />` only works between lib files (same directory); in a program file it is ignored,
  because the harness keeps libs in a shared lib program a test can't add to.
- `/// <reference path>` tries "", .ts, .tsx, .d.ts by hand in `program_load_reference_path()`; tsc also honours
  `allowJs` extensions and reports TS6231/TS1006-style problems. `no_resolve` skips it entirely (tsc: TS6053-free).
- The resolver's caches are per `ModuleResolver` = per `Program` (package.json by directory) and unsynchronised:
  parallel loading needs a lock or per-thread resolvers. There is no cache of resolutions themselves (tsc caches per
  directory + name + mode); on the 223-file corpus resolution doesn't show in the profile, so none was built.
- `tsconfig.c`: no `references`, no `watchOptions`, config errors are one `failable` message (no TS5xxx), `include`
  globs skip dot-directories and the three package directories always (tsc: only under `**`), the JS gate for
  `allowJs` in `include` depends on the option having been read before the files are collected (it has).
- `main.c`: strict by default *only without* a tsconfig; command line options override the config's. `--lib`/`--target`
  default (`default_libs`) only when neither the config nor the command line decides.

## Comment catalogue (2026-09-21, `grep -rnE "\b(PERF|HACK|TODO|FIXME)\b" src`, 155 comments; the ones not above)
Checker gaps marked in the code, by function:
- `class_decl_typecheck`: `implements Foo` is lowered as a type but never checked (TS2420).
  `interface_decl_typecheck`: an `extends` target isn't checked for TS2312/TS2430.
- `check_access`: indexing with a type parameter / deferred index type returns silently instead of TS2536.
- `lower_type_args`: a missing required type argument without default becomes any/error, no TS2314.
  `lower_identifier`: `import("./a").B.C` with a missing intermediate member gives `any`, no TS2339.
  `binding_rest_elements`: a rest binding from a readonly array/tuple loses `readonly`.
  `append_qualified_name`: always fully qualifies (the traversal has the accessibility walk, error messages don't).
- `scope_add_port`: `export as namespace N` never reports TS2686 on use; `export` inside a namespace body is dropped
  silently instead of TS1194. `scope_default_export_table`: default export outside a module isn't diagnosed (TS1319).
- `ty_identifier_reference_type`: a bare `this` where nothing declares one is `undefined` (tsc: `typeof globalThis`
  or implicit any). `binary_result`: no TS2695 for the unused left side of a comma expression.
- `binary_op_typecheck` (expr.c): `<`/`>`/`<=`/`>=`/`in`/`instanceof` have no checks; assignment to a property
  expression isn't checked against the property's type (identifier targets are); `+` redoes the relation checks
  evaluation already did. `expr_typecheck`: `delete`/`await`/`yield` unchecked (no TS1064); `as`/`satisfies` never
  report TS2352 for non-overlapping types.
- `prop_type` (ty/members.c): an in-flight (cyclic) property type is the error type silently, no TS2502.
- `for_in_accepts` (statement.c): a type parameter should be judged by its constraint (spurious TS2407 possible).
- `binding_add_names`: literal types on an exported binding aren't widened the way tsc does for
  `defaultNamedExportWithType1`.
- `instantiate_anonymous` (ty/instantiate.c): instantiation expressions are never instantiated a second time.
- `ty_union_subtype_reduced`: subtype reduction bails above 64 members (quadratic check).
Parser: `is_identifier_char` is ASCII-only (no Unicode ID_Start/ID_Continue); `parse_identifier` excludes keywords
by a chain of `span_equals`; `parse_binary_op` is an if/else chain; `parse_class_decl` gives a generic trailing-comma
error for `extends B, C`; `parse_regex` fails instead of TS1161; numeric separators (`string_helpers.c`) never
report TS6188/6189; `parse_string` had dropped every escape but `\n`/`\r` until this catalogue found it
(`parse_string_escape` now, `display_string_literal` in `ty_display.c` mirrors tsc's `escapeString`).
Perf notes in the code: `CallExpr` argument types recomputed per use; `Value.object` refcount churn; `IdentifierType`
could be split into a small common case; `CallableType`/`FunctionImpl` two allocations; `DEBUG_ALLOCATOR` always
on; `error_cmp` orders by span only (a TODO says "by file first"; files are separate ErrorCtx passes in practice).
`engine.c` (used by the harness to read `test/baseline_results.ts`, nothing else): no let/const/var distinction,
property assignment on arrays dropped, `value_loose_eq` stringifies; irrelevant unless it grows a second user.
- Grammar errors vs parse errors (`error_ctx_grammar_error_d`, `SourceFile.has_parse_errors`): tsc drops checker grammar
  errors in a file with parse errors. Only the `using`/import attribute checks go through it; the older checker grammar
  errors (TS1155, TS1039, TS1254, modifier checks...) still report regardless. Conversely tttt's *parser* reports some
  of tsc's grammar errors (TS1308, TS2852, TS1491/1495), and those count as parse errors for `has_parse_errors`.
