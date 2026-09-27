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
  checker only *reads* a subset (the strict family, `target`, `jsx`, `exactOptionalPropertyTypes`, the unused/implicit
  checks, decorators, module interop...: `grep -o 'compiler_options()->[a-z_]*' src`); the rest is carried but inert.
  `compiler_options()` is a global like the
  `ErrorConfig` it replaced, set around load/resolve/check by `program.c`; becomes a `Checker`/`Program` field read
  through a context when signatures get one.
- `baseline_test_case.c: apply_setting`: harness-only directives that are still ignored: `@baselineFile`,
  `@fullEmitPaths`. Bad option values are ignored too
  (tsc's harness throws). A `tsconfig.json` unit is honoured for options and roots; option diagnostics
  (`program_validate_options`) are skipped when there is one, since tsc then puts them on the option's json span.
- `error_ctx_global_error_s`: diagnostics without a file are `Error`s with a null span, sorted first (by code, then
  message, like tsc), shown by the harness before the first file (`formatted_global_errors`). The CLI's
  `display_error` prints them without a location. `error_ctx_error_s` still drops (and logs) errors with an invalid
  span.
- `program_apply_comment_directives` (`@ts-expect-error`/`@ts-ignore`): suppression goes by tttt's error *phase*,
  not tsc's diagnostic kind. Only RESOLVE_NAMES/TYPECHECK errors are suppressed, so a tsc checker grammar error
  (`checkGrammar*`, TS1xxx) that tttt reports while parsing is not, and a tsc parse error that tttt reports while
  typechecking wrongly is. In-file program diagnostics (tsc's `getProgramDiagnostics`, eg a bad `/// <reference>`)
  aren't suppressed at all. Line breaks are `\n` only (tsc: also lone `\r`, U+2028/9) and `\s` is ASCII only.
  Directives are recorded by `hop_comment` (deduplicated in `source_file_add_comment_directive`), so this relies on
  every comment going through it and nothing that isn't a comment (JSX text) going through it.
- `mount_test_lib_folder` (harness): tsc's `tests/lib` is *copied* into each test's mock filesystem (react16.d.ts is
  2.7k lines) when the test mentions `/.lib/` or `@libFiles`, and parsed again by every such test. A read-only mount
  in `MockFs` (and caching the parsed files like the default lib) would be the real thing if it ever shows in suite
  time.
- JSX parsing (`parser.c`, plan 22): `parse_jsx_attributes` approximates tsc's `isInSomeParsingContext` (whether a
  token that can't start an attribute aborts the tag or is skipped) as "inside JSX children, always; elsewhere, when
  it could start a statement" (`jsx_children_depth` + a character list), predating the ParsingContext mask (plan 27):
  JSX attributes/children should become lists of it. JSX names with unicode escapes
  (`<\u0061>`, TS17021) are compared and classified by their value (`jsx_name_value`, an inline decode in
  `jsx_element_is_intrinsic`) but *resolved* by their raw text, like every other escaped identifier in tttt.
- `SourceFile.unchecked` + `error_ctx_remove_unchecked`: the JS gate is a *filter* run after name resolution and
  after typechecking (semantic errors in an unchecked file are removed from the ErrorCtx), not a skip: names are
  still resolved (imports from an unchecked file need its exports) and `block_typecheck` is skipped only for the
  whole-file case. `check_access()` keeps its own "JS file: no TS2339" gate on top (`is_js`), because expando
  members don't exist. `program_load` refuses a `.js` root without `allowJs` (TS6504) but a `.js` file reached
  through an import is refused by `program_load_module_ref` with `MODULE_REF_IS_JS` (TS7016-style), two paths.
- `TTTT_IGNORE_UNDERLINES=1` (`compare_outputs`): the types component compared without its `^^^` lines, a
  measurement of what the underlines still cost (73 tests on 2026-09-22), not a mode to save results in.
- Types baseline node reuse (`ty_display.c` `display_node()`/`display_annotated()`, see `documentation.md`),
  approximations of tsc's node builder (`tryReuseExistingTypeNode`, `expressionToTypeNode.ts`):
  - `name_accessible()` resolves the first name of an entity name from `enclosing`, which only knows file and namespace
    scopes (see `symbol_chain.c` below): a local of a function is never accessible (right: tsc's isDeclarationVisible), but a
    name shadowed inside a function isn't seen. Type parameters and parameters are always accessible.
  - Instantiated signatures: the declaration-level check is "the mapper left the type alone and it doesn't mention the
    signature's type parameters" (tsc: uninstantiated annotation type == instantiated type, with fresh type parameters
    tttt doesn't make, `mentions()` is conservative). `TyDisplay.mapped_by` (tsc's context.mapper, recovering mapped
    type parameters inside a reused node) is only used for type predicates.
  - `mentions_mapped()`: tsc doesn't instantiate a member signature that can't reference the mapped type parameters
    (getObjectTypeInstantiation + the syntactic isTypeParameterPossiblyReferenced); tttt does, and display then prints
    the declared signature instead. The real fix is in `instantiate.c` (keep the type when the declaration can't
    reference the mapper's sources, like the anonymous types' free params already do), which would also save work.
  - `T_1` renaming (`TyParamNames`): the type parameters in scope at the print location come from the traversal
    (`baseline_test_case_enter_generic()`, with tsc's resolveName exceptions for static members and computed member
    names re-stated by hand), not from a real scope chain. Mapped/conditional synthetic `T`s of
    tsc's node builder (distributive conditional rewriting, homomorphic mapped wrappers) aren't made.
  - Inaccessible names: `display_node_identifier()` names the target anew with `out_symbol_name()` and keeps the type
    arguments; when that gives `import("m").A` it patches the underline so `import(` and `).` are reused, byte-offset
    arithmetic that assumes an ASCII module name.
  - Computed member names that can't be named print their value (`display_node_computed_name()`); the rest of tsc's
    `ComputedPropertyName` handling (late-bound names, `trackComputedName`) isn't ported.
  - Syntactic typing (`single_return_expr()`, `TyDisplay.syntactic_function`): only returned or parameter-initializer
    *function expressions* (parameter declarations reused whole) and `as T` returns. tsc's `typeFromExpression` also
    does object literals, `null`/`undefined`, literals in const contexts; those still print from the `Ty`, which gives
    the same text but maybe other underlines. `FunctionImpl.syntactically_contextual` counts call expressions and
    annotated initializers (variables, parameters, class fields), not JSX.
  - Accessor properties: only object literal accessors (`Prop.function`), not class accessors printed structurally.
  - Index signatures, tuple element names, type parameter defaults: always synthetic (matches the probes).
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
- `statement_check_jump` / `TypecheckLabel`: labels are checked (TS1107/1115/1116, TS7028 unused), but duplicate
  labels (TS1114) aren't reported.
- `run_baseline_test_case: path_is_script`: virtual files that aren't TS/JS by extension are left out of the program
  entirely. Right for package.json/tsconfig.json, wrong for `.json` modules under `resolveJsonModule` (5 tests).
- `const` type parameters (`ty_is_const_type_variable`, `in_const_context` in expr.c): no mapped/conditional/variadic
  tuple cases of tsc's isConstTypeVariable. Array literals nested in an argument count as array literal candidates by
  walking the argument's syntax (`add_nested_array_literals`, call.c) because tuples are interned and can't carry
  tsc's ObjectFlags.ArrayLiteral. The rest arguments of `...args: T` get `T[i]` as contextual type only when const.
- `EnumDecl.is_const`: only decides the symbol flag (a const enum doesn't merge with a namespace); no TS2475 etc.
- `parse_generic_decl_item` parses and drops `in`/`out` type parameter modifiers (`const` is kept): they don't print
  (`<in T>` shows as `<T>`), don't set variances, and misplaced ones get no TS1274.
- `parse_if_else` looks ahead past a `;` for `else`, because statement terminators belong to
  `parse_append_statement_sequence` rather than to the statement; `ended_on_brace` there is the same kind of guess.
- `EXPR_REGEX` carries nothing but its span: flags are validated (`regex_check_flags`, TS1499), the pattern isn't
  (`/a(/` has no error).
- `parse_object_item`: `{ a = 1 }` (shorthand with default, only legal in destructuring assignment) keeps the default
  in `ObjectExprItem.shorthand_default` only to look its names up (not checked, not in the baselines); `?`/`!` before
  a method's `(`/`<` is swallowed without TS1162, and `{ b?: 1 }` doesn't parse at all (tsc: TS1162). `ObjectExprItem`
  methods are plain `EXPR_FUNCTION` values.
- Class accessors (`ClassItem.method.accessor`, `class_item_accessor_evaluate_type`): pairing is by name+staticness in
  `class_decl_lookup_names` (raw pointers between items of the same vec). No TS2300 for property-vs-accessor clashes,
  readonly only means "no setter". Interface/type-literal accessors are converted to properties in `parse_interface`.
- Private names (`#x`, `ty_private_name`): the lexical lookup (`PropertyExpr.private_class`) scans class items by
  text on every access (no per-class table). An access that no class declares looks up the plain `"#x"` atom, so a
  string-named property `"#x"` would be found (tsc: never). TS18015 (relations: "refers to a different member"), TS2806
  (set-only private accessor), private names in object literals (TS18016: `{ #y: 1 }` still fails to parse), for-in's
  `for (#x in o)` (TS1451 here, tsc TS2406) are missing.
- `in` (`ty_check_in_expression`): no TS2638 (a right operand that may be a primitive, hasEmptyObjectIntersection).
- Class field `x!: T`: the parser drops the `!` (`let x!: T` keeps it, `Binding.definitely_assigned`); the checks that
  need it (`class_instance_member` in class_decl.c, `check_used_before_initialization` in ty/check.c) peek at the
  source character after the field's name.
- Generators (`infer_generator_type`, `check_yield`): no next type is inferred from what the `yield`s are used as
  (tsc's `nextTypes` from the yield's contextual type): it's the contextual/declared TNext or `unknown`. No TS7055/TS7057
  (implicit any generators), no TS2505 (a generator annotated `void`), returns aren't checked against the annotation's
  TReturn.
- `infer_generator_type`: a `yield*` of something that isn't iterable prints its type, because tsc reports TS2488 from
  inside the return type inference and the message can find the return type in progress (makes `yield* foo` inside
  `foo` any, YieldExpression6_es6). Faithful to what tsc observably does, but it is a side effect of printing.
- Async functions (`FunctionImpl.is_async`, `promise_decl`): inferred return is `Promise<T>` built from the global
  `Promise` looked up by name during name resolution (`any` if the lib has none, eg target ES5 without `@lib`); return
  expressions are awaited (`ty_awaited`) and widened against the promised type of the contextual return type
  (`promised_context` in function.c: the members that `ty_awaited` unwraps); returns aren't checked against the
  declared type; no TS1064/TS2705. `ty_awaited` only unwraps types literally named `Promise`/`PromiseLike`.
- `await_is_operator`: decides keyword-vs-identifier from `ParserShared.in_async`/`function_depth` plus a peek at the
  next character. Top-level await never reports TS1375/TS1378 (needs module detection + `@module`/`@target`).
  Class field initialisers and parameter defaults count as "top level"/inherit the enclosing function's context.
- Decorators (`ClassDecl.decorators`, `ClassItem.decorators`, `FunctionArg.decorators`): parsed, name-resolved,
  typechecked and traversed as ordinary expressions. Placement is checked (TS1206/TS1249 in `class_decl_typecheck`),
  but nothing checks that they're valid decorators for their target (TS1238/TS1240/TS1241...).
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
  (escapes not processed).
- `engine.c` ignores every construct added in parser rounds 1 and 2 (returns undefined); its only user is the harness
  reading `test/baseline_results.ts` (see the comment catalogue).


## Parser error recovery (plan 27 stage 1, documentation.md "Error recovery")
- `list_abort_or_skip()`: a token that starts an element of the list itself is skipped, never ends the list (tsc's
  isInSomeParsingContext includes the current list, but tsc's element parser would have consumed that token). Right
  while tttt element parsers fail where tsc's commit; wrong for the element that genuinely can't be parsed.
- The fail cache ignores `ParserShared.parsing_context` (plan 27 stage 1 step 1 asked to verify this): a failure
  recorded under one mask can replay under another, which only changes recovery in invalid code. No test was seen to
  depend on it.
- `invalid_char_size()` treats every non-ASCII, non-whitespace character as invalid (TS1127 for any parse error that
  starts there), because identifiers are ASCII-only (plan 27 stage 3 makes them unicode aware and must narrow this).
- `tok_scan_end()` is a second, cheap notion of a token next to `hop_token()` (which parses literals): templates are
  one backtick, `>` sequences are scanned greedily like tsc's scanner before rescanning.
- `parse_list_member()` asks the member-start predicate only when the member parsed with errors
  (`errors_attempted`): a member tsc wouldn't start that tttt parses *cleanly* is still taken.
- The variable declarator loop (`parse_variable_statement`) sets the VariableDeclarations bit but isn't tsc's list;
  `parse_binding_content()`'s object/array patterns still break out at the first bad element.
- Committed arrow functions (`arrow_function_tristate()` TRUE) whose parameter list *still* fails fall back to the
  parenthesized expression like before; tsc never backtracks there.

## Baselines (plan 21)
- `symbol_chain.c` (tsc's getSymbolChain, used by both baselines) approximations:
  - Scopes: the chain sees the scopes the traversal tracks (file, module, namespace: `BaselineTestCase.enclosing`), not
    function/block/class scopes. Function/block locals never alias anything and have no parent, so naming them is the
    same (plan 21 stage 2.1, decided not needed); what's lost is needsQualification by a *type* shadowed inside a
    function, and class type parameters as a table.
  - tsc's module/namespace `locals` also hold the exported declarations (a local whose `exportSymbol` is the export):
    `try_locals_with_exports()` merges the block's locals with the exports declared in the same *file* (not the same
    block) by source position. A duplicate declaration's detached symbol is found the same way.
  - `getContainersOfSymbol` without the alternative containing modules (`getAlternativeContainingModules`: other
    modules that re-export the symbol) and without the `firstVariableMatch` (a variable whose type is the container's,
    only with meaning Value), and without the export='d-member-of-an-ambient-module case.
  - A module's `default` export symbol is named like its first named declaration (`C` for `export default class C`,
    tsc: `default`); `export_name()` gives it tsc's name for lookups and printing. `import("m")` specifiers are `module_name_for_error()`'s path, not
    tsc's getSpecifierForModuleSymbol (relative from the enclosing file, `node_modules` rules).
  - No accessibility cache (tsc's `accessibleChainCache`): each lookup walks the tables. Test-only (error messages
    have no enclosing scope), and the suite time didn't move.
- Member lines (traversal `member_text()`/`prop_text()`): a member's name is its container's chain plus the member's
  name as its first declaration writes it; tttt members aren't Symbols, so the container comes from the traversal
  (`member_container`, `class_symbol`) or the Prop's `parent`, and a namespace object's member is looked up as the
  namespace's export by the accessed name.
- `render_types_or_symbols_file` drops one trailing line break of every file but the test's last unit, because
  load_test_source() ends each unit's lines (tsc joins them). A last unit followed by `// @link` lines still comes out
  one line off (symbolLinkDeclarationEmitModuleNamesImportRef).
- `arguments` resolution per target (`scope_has_arguments`) has no TS2815 (`arguments` in a class field initializer).
- Symbols of property accesses (`collect_prop_symbol()` in the traversal): a union member that isn't an object type
  (`X | undefined`) is skipped for property accesses and only makes the property partial (no symbol, like tsc) for
  binding patterns (`strict_union`). Turning it on for accesses lost 40 tests whose object tttt narrows less than tsc
  (implicitConstParameters: a const parameter narrowed inside a closure): the leniency hides narrowing gaps.
- `__index` symbols (`index_symbol_compile_test_info()`): only for declared types and written type literals (tsc's
  getIndexSymbolFromSymbolTable finds none in what spreads synthesize), and never for an index signature declared in a
  lib: no tsc baseline has one, and useRegexpGroups has none where a local copy of the same interface does.
  Unexplained; revisit if a lib `__index` ever shows up.
- Declaration order (`symbol_declaration_spans()`): each block's function declarations first (tsc's
  bindEachFunctionsFirst), by the scope recorded in `SymbolDecl.block`; declarations without one (aliases, lib-merged
  type parameters) group by file. `block_add_names()` binds them first too now (plan 23), so this reordering may be
  mostly redundant; not re-checked. Lib file order (`SymbolConstructor` in lib.es2015.iterable before lib.es2015.symbol)
  isn't tsc's.

## Missing checks (plan 15, retired: `unused.c`, `ty/check.c`, `ty/helpers.c`, `statement.c`, `class_decl.c`)
- `Symbol.referenced` (unused locals) is written by the resolve walk of the file that uses a name, also for globals
  (a script's top-level declarations) that other files share: a data race once files resolve in parallel. Only under
  noUnusedLocals/noUnusedParameters. Class members aren't symbols, so their reads are a checker table keyed by the
  start of the member's declaration span (`ty_mark_member_referenced`).
- `mark_jsdoc_links()` (unused.c): JSDoc `{@link X}` names count as uses by a text scan of the file, looked up from
  the file's scope (tsc resolves them where the comment is).
- Type parameters of merged interface/class declarations are one symbol per name like in tsc
  (`scope_new_generic_merged`), except when the earlier declaration is the shared lib program's (a global
  augmentation of `Array<T>`): nothing is written to lib symbols, so those stay separate. unused.c still checks every
  declaration's type parameter at the same index (`type_parameter_is_unused`) for that case.
- `ty_check_index_constraints` on type literals only runs when the literal syntactically has an index signature and
  something to check against it (`type_literal_has_index_constraints`), and an alias body is evaluated through its
  alias first (`type_decl_typecheck`): evaluating a literal's type early gives it an id (union order) and loses the
  alias name. tsc checks every literal, lazily.
- The overload checks (`statements_check_overloads`, `class_check_overloads`) are syntactic per statement list /
  class body; tsc walks a symbol's declarations and compares parents. "Same parent" is approximated by "the list
  declares more of it later".
- TS2729: tsc's isPropertyInitializedInStaticBlocks runs control flow at the end of each static block;
  `initialized_in_static_blocks` looks for a top-level `this.x = ...` statement instead.
- `widen_nullable` (members.c, non-strict widening to any): tsc only widens its *widening* null/undefined (the types
  of the `null`/`undefined` expressions), tttt has one null type; spread properties of object literals are exempt as
  an approximation. A proper fix is tsc's nullWideningType/undefinedWideningType.
- TS2403 (`check_subsequent_declaration`) compares with the identity relation, which finds every type-core
  approximation between two spellings of one type: see triage "TS2403 false positives".
- `binary_op_typecheck` (instanceof): TS2358 isn't checked in JS files, where tttt doesn't read JSDoc types (a `@type
  {*}` variable is its initializer's primitive type here). Also the `[Symbol.hasInstance]` test matches the escaped
  name `__@hasInstance@<id>` by text (`instanceof_target_ok`).
- Emit helpers (`ty/helpers.c`, `ty_check_emit_helpers`): the call sites pass spans computed by hand where tsc uses
  getErrorSpanForNode: `decorator_span()` scans back from the expression to its `@`; `function_error_span()` cuts an
  arrow function at its first line; `yield*` is cut to its 5-char keyword; an import specifier's `full_span` is trimmed
  of trailing whitespace. "Ambient" is approximated per call site (`ClassDecl.is_ambient`, `Binding.ambient`,
  `BlockTypecheckCtx.ambient`); tsc has one NodeFlags.Ambient. `for await` (__asyncValues) isn't checked: the statement
  doesn't know its function.
- Named evaluation of anonymous class expressions (`ClassDecl.named_evaluation`) is marked by the parser at each
  construction site (`mark_named_evaluation()`); a new place that assigns an expression to a name needs a call there.
- TS1361/TS1362's valid use sites (`FlowBuilder.type_only_alias_ok`, `in_type_query`) are set by the constructs that
  contain the identifier, not derived from its parents like tsc's isValidTypeOnlyAliasUseSite.
- `class_check_member_overrides()` (TS2610 & co) reads member kinds off the class items of the declaring class
  (`class_instance_member()`, found by name atom), not off symbol flags; the `x!:` of a field is found by looking at
  the source text after its name (the parser drops the `!`). TS2612 skips every case where tsc's
  isPropertyInitializedInConstructor (flow analysis) would decide.
- `regex_check_flags()` and TS1042 (`report_misplaced_async()`) are grammar checks done where it was convenient:
  the flags in the checker's expression walk, `async` in the parser next to the other modifier checks (as parse
  errors, which suppress later grammar errors in the file; tsc's is a checker grammar error).

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
  module symbol itself, so that its type prints as `typeof ns`. With an enclosing scope the display now names the
  module (`symbol_members()`) through the symbol chain like tsc; error messages still print the gather symbol's name.
  The gather symbol could go (the target being the module) once messages name modules like tsc.
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
- `identifier_expr_lookup_names`: `arguments` has no symbol (`IdentifierExpr.is_arguments`, typed `IArguments` by
  name in `ty_identifier_reference_type`).
- `scope_declare_default_local()`: a compatible local declared *before* `export default function A` (`namespace A {}`)
  keeps the name `A` for itself; tsc resolves `A` to the default export for the meanings it has. `resolve_alias_target`: TS1192 is only suppressed for declaration files under
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
  check phase: the thread-local `current_checker`, see the top of this file.
- Exported and local declarations of one name live in two tables with no link between them (tsc has a local
  `exportSymbol` stub). TS2440 against an exported local is special-cased in `binder_link`, TS2395 (mixed exported
  and local declarations) is a pass of its own in `scope.c`; `export { b }` of a local that is also declared exported
  doesn't show the merged declarations (mergedDeclarationExports).
- `block_is_instantiated`: syntactic. `export { x }` looks `x` up through `Scope.block` of the enclosing scopes
  (`alias_target_state`), but a namespace found there is judged with the outer chain of the namespace being checked,
  not its own; cycles are cut by depth (16) rather than tsc's visited map.
- Class and interface *members* are not symbols (`Prop` has the identity the checker needs); duplicate member
  diagnostics come from a checker pass over the members (`ty_check_object_type_duplicate_members`, see below).
  `ty_check_class_duplicate_members` replays tsc's binder for the static side, sharing a table with the exports of
  namespaces merged with the class (per class declaration, in the merged symbol's declaration order), and
  `object_bind_members` (expr.c) does the same for object literals. Both are check-time imitations of binder
  conflicts: a namespace export that conflicts with a static member still sits in the exports and shows up in
  `typeof C` (tsc keeps only the first).
- `scope_export_default_expr`: the parser's statement spans end before the `;`, tsc's include it; patched there by
  looking for a `;` after the span (TS2528/TS2323 on `export default {...};`). Other statement-span errors (TS7027)
  may have the same off-by-one.
- `declare_symbol`: a declaration named like one of tttt's builtin type symbols (`class number`) gets a detached
  symbol when it conflicts (tsc has no symbol for the keyword), so a value `new number()` doesn't resolve.
- `symbol_table_collect` allocates; `ty_namespace_type` members and `extend_export_stars` go through it.
- One test logs "Declaration B acquired multiple times" (a node whose names get added twice; not found yet).
- `check_access` reports TS2339 on modules too now (`import * as ns; ns.missing`): where module resolution picks the
  wrong file (nodeModulesTypesVersionPackageExports) that shows as spurious TS2339 next to the TS2307s (plan 06).

## Control flow graph (`flow.c`, the builder parts of `expr.c`/`statement.c`/`function_impl.c`; plan 05)
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

## Narrowing (`ty/flow.c`, the hooks in `ty/expr.c`; plan 05 stage 2)
- Aliased conditions (`aliased_condition`, plan 08) only narrow identifier references (tsc also readonly properties).
- Not there (each has a TODO in `flow.c`): destructured discriminants (`const { kind } = x`), `x.constructor === C`, `[Symbol.hasInstance]` predicates,
  `"a" in x` intersecting with `Record<"a", unknown>` when no member is known to have the property, `unknown`
  narrowed by the cases of a `switch`,
  the key property map tsc uses for big unions (`getKeyPropertyName()`: an optimisation, but it also changes which
  members `getNarrowedType()` starts from), erasure of generic construct signatures in `constructor_instance_type()`.
  Destructured discriminants and the `in` rule show as spurious TS18048 / TS2339 (dependentDestructuredVariables,
  inKeywordTypeguard).
- `ty_check_element_implicit_any()` (TS7053 & co.) skips what `check_access()` skips for lack of expandos: callable
  objects (`fn["x"]` is TS7053 in tsc unless fn is a function declaration with expando assignments), mapped types over
  something generic, unresolved bases, JS files. `is_for_in_numeric_variable()` doesn't check that the access is inside
  the loop's body.
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
- `getNarrowableTypeForReference()` is partial (`narrowable_type`, see the expressions section), no
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

## The AST-side walk and what's left of the old type world
- `TYPE_LAZY`, `TYPE_REF`, the binding `TYPE_INDEXED`, `CallableArg`, `type_clone` and its whole family are gone:
  `TypeNode` is syntax only. Leftovers of that world: `owned_symbol` + `symbol_ref` on `GenericItem`/`InferredType`/
  mapped keys and `key.expr.owned` + `ref` on interface members only existed for clones, one pointer each would do;
  `LAZY_TYPE_FUNCTION` memoises `ty_function_type()`, which is interned already (the LazyType could go, its users
  are `value_entry_type`, `class_item_get_type` and the test traversal).
- `ty_binding_assigned_type`: not memoised per binding (the root's LazyType is, the accesses down a nested pattern
  are redone per query). What detects `[x = x]` is the default's LazyType; `ty_binding_default_type` checks a default
  with its contextual type (which bypasses the LazyType), so it keeps its own in-flight list (`defaults_in_flight`).
- `binding_element_context` (lower.c, tsc's getContextualTypeForBindingElement) takes the property through
  `ty_contextual_prop_type` (index signatures included); tsc's getTypeOfPropertyOfType doesn't look at them.
- `binding_add_padded_implicit_any_errors` (TS7031 for the elements a parameter's tuple initializer lacks, tsc's
  padTupleType) runs for unannotated parameters and defaults of nested patterns; tsc reports wherever
  checkDeclarationInitializer runs (also contextual-type lookups of annotated parameters).
- `contains_spread` (members.c, widening context): tsc's ObjectFlags.ContainsSpread approximated as "a property of the
  literal came from a spread" (`{ ...{} }` doesn't count). The padded `x?: undefined` properties are `undefined`, not
  tsc's missingType under exactOptionalPropertyTypes, and the widened literal stays an object literal type (tsc's isn't).
- `reorder_candidates` (call.c): overloads with a literal-typed parameter go first; tsc's reorderCandidates also orders
  the groups of merged declarations (later declarations first), which isn't done here.
- `FunctionImpl.constructed_class`: a class constructor's return type is `ty_class_instance_type()` of its class,
  set in `class_decl_lookup_names` (which also reports TS1093 and drops a written return type).
- `ty/check.c: check_access` (TS2339 and friends) is honest about unions, intersections, `null`/`undefined` and
  `unknown` objects since plan 05 stage 4, but lets through what it knows the type core can't answer yet: **JS files**
  (members declared by assignment: `this.x = 1`, `module.exports`, prototypes), **callables without construct
  signatures** (expando functions, `function f() {} f.x = 1`, also in TS), **generic mapped types** (`Pick<T, K>`,
  `Readonly<T>`: members only through the constraint) and **classes with a base `class_base()` can't fully see**
  (`ty_has_unresolved_base()`: mixins `class extends B` with `B: T`, unresolvable `extends` expressions, the static
  side of any expression base). Bracket access never reports a missing property.
- `expr_typecheck()` `EXPR_PROPERTY`: everything after the `?.` of a chain is checked against the non-nullable type
  of its object, because the `undefined` of the short circuit isn't told apart from one the property has itself
  (tsc: `optionalType` marker): `a?.b.c` with `b?: X` misses its TS18048.
- `class_base()` (`ty/members.c`): an `extends` that isn't a class name is the return type of the *first* construct
  signature of the expression's type; no written type arguments, no static side, no intersection with the base
  constraint for mixins (hence the gate above).
  A source that is `any` is let through to anything, even `never` (`check_assignable`).
- `TY_RELATION_ASSIGNABLE_STRICT_NULLS`: what the operator checks in `expr.c`/`ty/expr.c` mean by "extends number"
  (null/undefined don't, whatever strictNullChecks says; HACK comment in ty.h). tsc checks operands differently.
- `generic_params_expr_typecheck` (`f<A>` outside a call): constraints are checked against the first signature only.
- `for_each_loop_statement_evaluate_var_type`: `for..in` is `string` (no `Extract<keyof T, string>`).
- Iteration (`ty/iterate.c`): no TS2763-2765 (the next type of an iterator vs what for-of/spread/destructuring send),
  no TS2489/TS2490 (an iterator without `next()`/`value`: it just isn't iterable), errors are only reported at the top
  (tsc adds related info). Array destructuring *assignments* (`[a, b] = it`) check against the iterated type but the
  array literal target still types as an array, not a tuple (tsc's inDestructuringPattern), and a binding pattern
  isn't the contextual type of its initializer (`var [a, [b]] = [1, [2]]` gives `(number | number[])[]`).
- `ty_function_return_type`: a return type that is asked for while it's being inferred makes it `any` (tsc's failed
  popTypeResolution), without TS7023.
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


## Calls, inference and property access (`ty/call.c`, `ty/infer.c`, `ty/access.c`)
The algorithms are meant to stay; what's listed is where they stop short of tsc.
- Member modifiers (plan 13): `PropertyExpr` carries four resolve-walk pointers for them (`enclosing_class`,
  `in_constructor_of`, `this_parameter`, `this_function`), 32 bytes per property access that only matter for class
  members; a `Scope const*` would do (scopes live as long as the binder) if memory ever matters. `member_refs()`
  (check.c) duplicates the member walk of `access()` (access.c) to get at the `Prop`s rather than their types; one walk
  that returns both (tsc's getPropertyOfType giving a symbol) would be cleaner. It special-cases numeric names on
  tuples so as not to resolve tuple members the element access never needed: *creating types in a check changes type
  ids and so union/keyof display order* (valibot messages reordered until it didn't). Unions: a member without the
  property but with an index signature is looked up with the index type `string`, not the name.
- `infer_from` (infer.c): reverse mapped types are made eagerly (bounded by `Checker.reverse_mapped_depth`, HACK in
  ty.h; tsc's are lazy), no `InferencePriority` flags beyond naked-type-variable, homomorphic, mapped-constraint and
  return-type (they are tsc's bit values and OR together now).
- `ty_union_signatures` (relate.c, tsc's getUnionSignatures): union signatures aren't cached per union type (built on
  every call). `resolve_call` still falls back to member-by-member resolution when
  some member has no signatures of the kind (tsc: TS2349 "Not all constituents of type ... are callable", tttt reports
  the member's TS2348).
- `super(...)` (`resolve_into` in call.c): the heritage clause's type arguments are passed as explicit type arguments
  of the base constructor's signatures; without written ones they're inferred where tsc instantiates with defaults
  (getInstantiatedConstructorsForTypeArguments).
- `super` legality (`resolve_super` in expr.c) walks scopes instead of tsc's containers; a bare `super` (TS1034, a parse
  error in tsc) isn't reported. `check_before_super` (TS17009/17011) only looks at `this`/`super` whose nearest function
  scope is the constructor, so an IIFE's `this` isn't checked (tsc neither: the arrow is its container). No TS2401/TS2376
  (where `super()` must be with initialized properties or parameter properties), no TS2659 (object literal `super`
  below ES2015).
- `report_arity`: the span for too few arguments is the callee's `inner_span`, not tsc's name-of-the-property-access
  rule.
- Heritage checks (`ty_check_heritage`, `class_heritage_typecheck`, `interface_heritage_typecheck`): TS2320 for
  interfaces only (`inherited_props_identical`: a class implementing two bases doesn't get
  it), no tsc-specific elaborations ("The types returned by 'toSeq()' are incompatible between these types", "'X' is
  assignable to the constraint of type 'this', but..."). The interface checks create types earlier than before, which
  moved union member order / type parameter names in 3 types baselines (tsc orders by type id).
- `skipLibCheck` makes declaration files `unchecked` (their semantic diagnostics are dropped, like tsc's
  skipTypeChecking); `skipDefaultLibCheck` isn't looked at.
- Contextual `this` (`ty_contextual_this_type`, plan 10 stage 2): the object literal's contextual type is remembered
  when it is checked (`KEY_LITERAL_CONTEXT`, the last one wins) and so is its enclosing literal (`KEY_LITERAL_PARENT`),
  since there are no parent pointers; a `this` asked for before the literal was checked falls back to the literal's
  own type. `ThisType<T>` is recognised by name (a global `ThisType`), and its argument isn't instantiated with the
  inference in progress (tsc: getMapperFromContext(getInferenceContext(literal))). No TS7041 (`this` captured by an
  arrow function at the top level), no TS7017 on `typeof globalThis`.
- `clone_type_params` (members.c, plan 11): an instantiated generic signature gets fresh type parameters (tsc's
  cloneTypeParameter) only for those with a constraint or default (`ty_param_is_bounded`), or when the mapper brings
  type variables in (`then<U>` of `P<U>`: `T := U` would capture the method's own U). Otherwise unbounded ones stay
  shared with the target (indistinguishable, and keeps `Box<A>.map` vs `Box<B>.map` a plain comparison instead of an
  inference). Something that relies on two instantiations *not* sharing an unbounded type parameter would notice.
- Return mapper (`infer_type_arguments` in call.c, HACK comment): tsc's `returnMapper` comes from its own inference
  from the contextual type as the outer call's return mapper says; tttt only uses that inference for type parameters
  the main inference (from the outer-instantiated contextual type, silent never for what the outer call doesn't know)
  has no candidates for. Using it for everything was tsc-exact on the tests but cost valibot 9%. The first pass gives
  a non-context-sensitive *function* argument its parameter type fully instantiated with the return mapper (tsc
  instantiates each nested contextual type as it is asked for); other arguments only get the top level
  (`instantiate_instantiable`), since full instantiation broke arrayLiteralInference. The second pass uses the return
  mapper while no type parameter has candidates (tsc's instantiateContextualType).
- `TYF_NON_INFERRABLE` (tsc's ObjectFlags.NonInferrableType): set on the silent never and `ty_any_function_type()`,
  propagated through unions, intersections, references, object literals and aliased anonymous instantiations.
- Skip-sensitive first pass (plan 19 R10, `ExprCtx.skip_sensitive`): an object/array/JSX-attributes argument with a
  context-sensitive function in it is checked with those functions as `ty_any_function_type()` and inferred from.
  Missing from tsc's version: the returnOnlyType (a function whose parameters are annotated but whose return
  expression is context sensitive still becomes the any-function), and the first overload round's applicability
  check still skips such arguments entirely (`first_unfit_argument`). isPartiallyInferableType only exists as the
  PartialHomomorphicMappedType priority of a reverse mapped inference from a non-inferrable source.
- Intra-expression inference (`infer_intra_expression`, call.c): HACK, only arrow functions are sites (plus nested
  literals, spread literals). tsc takes methods and function expressions too, but their `this` goes by the literal's
  contextual type while inference isn't finished, and tttt memoizes the `this` it evaluated (tsc recomputes
  checkThisExpression every time): checking them early broke thisTypeInObjectLiterals2. intraExpressionInferences
  still fails on its method cases.
- `ty_generator_context` (iterate.c): a union return annotation / contextual type is filtered to the members a
  generator could be assignable to (tsc's checkGeneratorInstantiationAssignabilityToReturnType); tsc filters a
  non-union contextual type too (to never if it doesn't fit).
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
- `ty_access`: `null`/`undefined` members of a union object are skipped silently (TS18047/18048 come from
  `check_access` instead), optional chaining isn't known here.

## Expressions (`ty/expr.c`), contextual typing, the call passes
`ty_check_expr(expr, contextual)` is meant to stay; what's listed is where it stops short of tsc.
- **Assignment targets** (`assignment_target_typecheck`, expr.c): a property target is checked against its *read*
  type; tsc uses the write type (`getWriteTypeOfSymbol`), so divergent accessors (`get x(): number; set x(v: number |
  string)`) give a false TS2322 (divergentAccessors*, getterSetterSubtypeAssignment). Needs a write type on `Prop`.
  Array pattern rest elements, object rest and non-array iterables aren't checked (null source), and `{ a = 1 } = o`
  isn't checked at all (`ObjectExprItem.shorthand_default` is only name-resolved; the HACK comment in
  `assignment_target_typecheck` still says the parser drops it). Expando assignments to functions (`foo.constructor =
  1`, nullPropertyName) are checked against the existing member.
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
- Excess property checks (plan 12, `has_excess_properties` in relate.c): no TS2561 (spelling suggestion). The keys of a generic mapped type count as known
  (`members->mapped`), the intersection-target re-check of nested properties (tsc's structuredTypeRelatedTo, "Nested
  excess property") isn't there. The discriminant reduction of a union target is tsc's discriminateTypeByDiscriminableItems
  without the key-property map. Contextual types of union members are reduced (`string | "a"` is `string`, tsc keeps
  both with noReductions), so a discriminant can widen and pick the wrong member (`contextualTypeShouldBeLiteral`).
  The for-of destructuring assignment check (statement.c) relates the regular element type (HACK: tsc checks a
  pattern property by property).
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
  fixes everything it mentions, except that an object/array literal only fixes through its context-sensitive
  functions (`fixed_by_expr`), and a function against a union goes by the contextual signature). tsc fixes lazily, when a contextual parameter type is actually asked for. Inference from the contextual return type only
  happens when that type has no type variables (no outer inference context, no return mapper).
- `resolve_call` still runs twice per call (type, then errors) and nothing is cached per `CallExpr`; argument memos make
  the second run cheap but not free.
- `ty_non_nullable` of a type parameter or deferred type whose constraint allows null/undefined returns the stripped
  *constraint* (tsc: `T & {}`), which loses `T`.
- `ty_unresolved`: an unresolved type name lowers to an error-`any` that prints as the written name (tsc does the same);
  it's a `TY_INTRINSIC` with `TYF_ANY | TYF_ERROR`, interned by name in the literal table.
- `check_array`: tuples only from a tuple-like contextual type, a destructuring pattern or `as const`.
- `compute`: tagged templates take the return type of a lone non-generic signature (no call resolution: a generic tag
  is `any`), binary arithmetic is always `number` (no bigint: `b * 2n` is `number`; unary `-`/`~`/`++`/`--` follow
  tsc's getUnaryResultType), `+` goes by `TY_RELATION_ASSIGNABLE_STRICT_NULLS`. Bigint values are doubles
  (`Value.bigint`), so `2n ** 64n` literals lose precision. `satisfies` contextually types its operand but the result
  isn't narrowed by it.
- `ty_apply_type_arguments` (instantiation expressions): maps the type parameters of every signature that takes at
  least that many type arguments; signatures that don't fit aren't dropped, and nothing reports TS2635.

## Type core (`src/ty/`): known gaps
- `Awaited<T>` (call.c, 2026-09-27): `ty_awaited()` wraps a generic result that could be a promise in the lib's
  `Awaited<T>` (tsc's getAwaitedType), but iteration and generator types use `ty_awaited_no_alias()` and returns of
  async functions `ty_awaited_returned()`, which strips `Awaited<X>` back to X. tsc has `Awaited<T>` in those places
  too and relates it to T through the default constraint of the conditional type (structuredTypeRelatedTo on a
  conditional source), which tttt's relation can't; once it can, use ty_awaited() everywhere and drop both.
  `ty_awaited` still only unwraps `Promise`/`PromiseLike` by name, not any thenable.
- Substitution types (`ty_enter_true_branch`, check.c): only the indexed access *type* check knows that `A` is
  `A & X` in the true branch of `A extends X ? ... : ...`, from a stack the typecheck walk keeps in the Checker
  (`true_branch_check`, 16 deep). Lowering itself has no substitution types (tsc's getSubstitutionType), so a
  constraint implied by a conditional is invisible everywhere else.
- Divergent accessors (members.c, access.c): `Prop.write_node` is a class or interface setter's annotated parameter;
  object literal accessors have none (tsc gives them one too). The symbols baseline of an interface's second accessor
  and its setter parameter come from `InterfaceMember.second_name`/`setter_param`, spans the parser keeps for that.
- Namespace imports of `export =` modules (`namespace_import_type`, lower.c): `ty_identifier_reference_type()` asks
  `ty_type_of_symbol()` about the alias symbol rather than its target whenever the name found an alias, so that
  function can tell a namespace import from the value it names. Only identifiers: `import("./m")` and
  `typeof import` don't get the interop shapes. The synthetic `default` property prints as `default` in the symbols
  baseline where tsc names it after the module (`Symbol("package")`).
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
- Relations to a generic mapped type (`generic_mapped_target_related`, relate.c) follow tsc's isGenericMappedType
  rule, but only a source without type variables gets its answer: a generic one that fails it falls through to the
  member-by-member comparison, which a mapped type without members passes. That permissive fallback stands in for
  homomorphic `Readonly<T>` to `T` and the like (generic mapped to generic mapped is tsc's mappedTypeRelatedTo,
  `mapped_related`, and decides on its own). The mapped
  type parameter P gets no constraint when lowered, so `S[P]` is compared as `S[Q]` (with an `as` clause a stand-in
  P' constrained to Q is used, `mapped_key_param_mapper`, cached per MappedInfo). Target `T[K]` uses the write type
  (intersection over a union constraint of T, `write_constraint_of_indexed`).
- `class_base`: a base class *name* is found syntactically (`extends Name`, `extends Name<Args>`, `extends ns.Name`),
  anything else takes the first construct signature (see the AST-side section; the HACK comment on it predates that).
  The static side inherits properties from the base's static side and, without a constructor of its own, its
  construct signatures.
- `this` types (plan 10 stage 1): `Prop.thisless` follows tsc's syntactic isThisless* rules, so every class member
  without annotations is instantiated once per class (and per derived class: inherited members are the base's with
  this-argument = the derived `this`). No `this is T` as a *property* type (`isFSO: this is FileSystemObject`
  doesn't parse, typeGuardOfFormThisMember), no TS2526 for `this` outside a class/interface, `this` in a static member
  is the static side (tsc: error), relations compare a reference with a this-argument structurally with the plain one
  (tsc: the this-argument is one more covariant type argument).
- `ty_resolve_enum` / `evaluate` (enums.c, also the constant folding of template expressions): no property accesses of
  `const` objects, `const` variables in another file count as constant without tsc's resolvedOtherFiles flag, members
  of ambient enums without initializer auto-increment (tsc: computed).
- `relate.c` gaps: no optionality of parameters (`undefined` isn't added under strictNullChecks), signatures/index
  signatures of a source intersection aren't combined (properties are; HACK comment in `structured_related`).
  Array<S> -> ReadonlyArray<T> is a shortcut on the element types (`structured_related`, HACK comment).
- `has_conflicting_discriminants` (checker.c): tsc reduces an intersection with disjoint discriminants to never lazily
  (getReducedType), tttt when the intersection is made. That can be in the middle of lowering one of the properties
  or an alias its type names (`type B = X & { e: A }; type A = I | B`, the corpus' BindableStatic*Expression), so a
  property that is in flight or whose node reaches an alias in flight (through unresolved aliases,
  `ty_node_reaches_alias_in_flight`) isn't judged, and the answer isn't remembered. The intersection itself is
  interned with that answer. The real fix is a lazy reduced view of intersections.
- `get_variances` / `get_alias_variances`: arrays, tuples and synthetic targets are hardcoded covariant (tsc does the
  same for arrays and tuples). `VARIANCE_UNRELIABLE` is set from a checker-wide flag whenever a deferred type or mapped
  type is compared while *any* variance is being measured (tsc: only when a marker is reached through it), and
  remembered in the relation cache (`CACHE_UNRELIABLE`). There is no `Unmeasurable` (tsc: `-?` mapped types, which
  then need identical arguments): it's treated as unreliable, so the variance fast path can accept pairs tsc would only
  accept if identical. `in`/`out` modifiers on type parameters are parsed and dropped (tsc takes the variance from
  them). Alias variances apply to object and conditional types (like tsc), keyed by the alias Symbol; the marker
  instantiations are remembered by type id (`KEY_ALIAS_MARKER`, tsc's markerTypes).
- The identity relation (`TY_RELATION_IDENTITY`, `identity_structured` in relate.c): tuples of different shapes are
  never identical (tsc compares their properties, so labels don't matter there either); two references to one target
  compare type arguments only (no structural fallback); the two-conditional rule in `structured_related` doesn't infer
  the source's `infer` type parameters from the target's extends type. Inference matches identical members with it
  (`is_identical` in infer.c, pointers first).
- Relation cache keys are plain (source id, target id); tsc's key postfix for in-scope type parameters and
  intersection state isn't there. `RelationState.overflow` (100 deep) reports through `instantiation_limit_hit`,
  which only `check_assignable_or_error_internal` turns into TS2589.
- `ty_conditional_resolve`: without a permissive instantiation, a failed check with a type variable nested in either
  side stays deferred (tsc would pick the false branch if it fails even with them replaced by `any`). Whether one is
  nested is a walk over members (`mentions_type_variables`, up to 8192 types / 48 deep, "unsure" beyond), since
  TYF_COULD_CONTAIN_TYPE_VARIABLES is set on every function and object literal type. A generic mapped type anywhere
  counts as mentioning one. A constraint of an `infer` that mentions type variables (`Foo<infer X, infer Y>` with `U
  extends T`) isn't checked against the inference (tsc instantiates it with the other inferences first).
  `ty_implied_infer_constraint` (tsc's getInferredTypeParameterConstraint) lacks the last rule: an `infer` template
  of a mapped extends type whose check type is a mapped type.
- `TyCondBranch` / `ty_deferred_reference`: conditional branches and array/tuple nodes are only lazy when the node
  syntactically reaches the alias being resolved (`node_reaches_alias_in_flight*`); tsc defers by position. A named
  generic reference to the alias in flight outside of those (`type T = Foo<T>` is an error in tsc, but `type T =
  { a: T } | Foo<T>`-style through other aliases isn't always) still becomes the error type.
- `expand_mapped`: mapped types expand to properties/index signatures once the key type is a union of literals and
  primitives. Modifiers come from `MappedInfo.modifiers_type` only when the key type was written as `keyof T`.
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
- `lower_identifier`: the `object` keyword is recognised by being a builtin declaration named "object".
- `lower` (composites): skips the `any` placeholder `parse_type` makes for a leading `&`.
- `lower_tuple`: rest elements keep the written type (`...T[]` holds `T[]`), tsc holds the element type.
- `collect_free_params` / `Ty.anonymous.free_params`: which outer type parameters an anonymous type's node mentions,
  found by walking the node when the type is created. Parameters declared inside the node (signatures, mapped keys,
  `infer`) are taken out at the end rather than tracked by scope, and it gives up (`free_params_unknown`, = assume all) on `typeof`, `this`, computed keys,
  parameters with initializers, and for every function and merged value type (a body can mention any type parameter in scope). `TyMapper` is always flat (no composed/merged forms yet,
  inference will want them). `LowerCtx.generic` is only a "don't bother walking" hint.
- `signature_resolve` (instantiated): return types are lazier than the rest (`signature_return_type`), because
  they're often inferred from a body that is being checked. Fresh type parameters: see `clone_type_params` above.
- `builder_add_interface_member`: index signature value types are lowered eagerly (property and signature types are
  lazy); computed keys that don't name a literal or unique symbol are dropped (tsc: late bound index signature, see
  "Computed member names" above).
- `ty_union`: no subtype reduction (that's `ty_union_subtype_reduced`, asked for explicitly), doesn't drop
  `null`/`undefined` without strictNullChecks. TS2456 (circular alias) is only a TODO in `lower.c`.

## JavaScript: JSDoc and CommonJS (plan 26 stage 1)
- **Unresolved JSDoc names are silent** (`identifier_type_lookup_names()`, HACK): a JSDoc type's lookup errors go to a
  dropped ErrorCtx, so `@typedef`/`@callback`/`@template`/`@import` names (stage 3) don't give TS2304/TS2503. tsc does
  report a genuinely unknown JSDoc name (jsdocResolveNameFailureInTypedef, jsdocPropertyTagInvalid). Remove when
  stage 3 declares those names. The types printer already doesn't reuse such names (tsc's rule), which costs the
  typedef tests their types component until then.
- **JSDoc is consumed at parse time, not stored**: `jsdoc_before()` re-parses a host's comment into transient
  `JsDocTag`s and moves types into the host's slots; unused tags are dropped. Stage 3 (typedef, template) needs tags
  that outlive the host's parse: store them (per host, or a per-file list) instead of re-deriving them.
- Only the *last* JSDoc comment before a host is read (tsc: all of them, the last one's tags plus earlier `@overload`s),
  and only a comment that ends right before the host's first token (a `//` comment in between hides it).
- JSDoc parse errors are dropped (tsc reports them for checkJs files as jsDocDiagnostics).
- The `argN` names of JSDoc function type parameters are spans pointing at static strings, not into the file
  (`jsdoc_parameter_name()`); `span_is_written()` tells them apart (display, TS6210). Anything that computes a
  position from a parameter's name span would read garbage.
- `@type {F}` on a function *moves* the signature's parameter/return TypeNodes into the function
  (`jsdoc_apply_type_signature()`), only for a written function type or `{(...): R}` literal, not a type reference;
  an arrow function under a variable's `@type` gets it contextually instead (tsc applies getSignatureOfTypeTag to it
  too: only the types baseline's reuse marks differ).
- The CommonJS module indicator is syntactic (parser, `saw_commonjs`, also set by failed attempts); tsc's binder sees
  only bound nodes. `module`/`exports`/`require` declared as locals of the file are respected, a global `exports`
  isn't (like tsc), but `exports.x =` in nested functions/blocks isn't declared (only the file's top-level statements,
  `statement_add_names()`): tsc binds them anywhere.
- `module.exports.x = v` after `module.exports = v0` declares an export of the file next to `export=`; tsc merges it
  into the export= type (getInitializerTypeFromAssignmentDeclaration's member copy). `Object.defineProperty(exports,
  ...)` isn't a declaration yet (stage 2).
- `require()` declarations are aliases through `PortItem`s the parser synthesizes on the `InitializedBinding`; only
  flat object patterns (no defaults, rest, nesting) qualify, anything else stays a variable of the module's type.
- The symbols baseline's `getSpecialPropertyAssignmentSymbol` shapes are an override hook in the baseline case
  (`symbol_overrides`, `commonjs_symbol_overrides()`), keyed by span; `module.exports` is overridden to tsc's synthetic
  `module.exports` property the same way.
- `readonly_assignment()` never reports a declaring `exports.x =` (an alias export's target may be a const).
- `identifier_type_check_arity()`: TS2315 ("not generic") on a class/function declared in a JavaScript file is skipped
  (HACK until `@template` makes them generic, stage 3).
- Gates still in place (plan 26 asks to list them): `check_access()` / `ty_check_element_implicit_any()` skip JS files
  (no TS2339/TS7053: expandos, stage 2), `binary_op_typecheck` skips TS2358 in JS, `scope_add_port()` has no
  `export as namespace` alias in JS, `expr.c` (a JS value module in `check_declared_before_use`-like code) and
  `several_types.c:257` (JS skip in a type check). None was removed by stage 1: they're about expandos and TS-only
  syntax.

## Module resolution (`src/module_resolution.c`, `src/tsconfig.c`, plan 06)
- A resolved `.json` module is `any` (`MODULE_REF_IS_JSON`): the file isn't parsed into a type, `path_is_script` keeps
  it out of the harness' program too. `requireOfJson*`, `jsonFileImport*` (~35 tests).
- A `.js` file that resolves with `allowJs` is loaded and parsed **as TypeScript** (plus JSDoc and CommonJS, plan 26);
  without `checkJs` its semantic diagnostics are dropped (`SourceFile.unchecked`). See "JavaScript" below.
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
- `interface_decl_typecheck`: an `extends` target isn't checked for TS2312.
- `check_access`: indexing with a type parameter / deferred index type returns silently instead of TS2536.
- `lower_type_args` (lower.c): a missing required type argument without default becomes the error type without TS2314
  (TS2314 is only reported syntactically in several_types.c). `lower_identifier`: `import("./a").B.C` with a missing
  intermediate member gives `any`, no TS2339. `binding_rest_elements`: a rest binding from a readonly array/tuple
  loses `readonly`. `append_qualified_name`: always fully qualifies (the traversal has the accessibility walk, error
  messages don't).
- `scope_add_port`: `export as namespace N` never reports TS2686 on use; `export` inside a namespace body is dropped
  silently instead of TS1194. `scope_export_default_expr`/`scope_init_declaration`: a default export outside a module isn't diagnosed (TS1319).
- `ty_identifier_reference_type`: a bare `this` where nothing declares one is `undefined` (tsc: `typeof globalThis`
  or implicit any).
- `prop_type` (ty/members.c): an in-flight (cyclic) property type is the error type silently, no TS2502.
- `for_in_accepts` (statement.c): a type parameter should be judged by its constraint (spurious TS2407 possible).
- `binding_add_names`: literal types on an exported binding aren't widened the way tsc does for
  `defaultNamedExportWithType1`.
- `instantiate_anonymous` (ty/instantiate.c): instantiation expressions are never instantiated a second time.
- `ty_union_subtype_reduced` (ty/expr.c): subtype reduction bails above 64 members (quadratic check).
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
  errors in a file with parse errors. The newer grammar checks go through it (~40 sites: labels, decorators, regex
  flags, import attributes, `using`...); older ones reported with `error_ctx_error_d` (TS1254, some of TS1155/TS1039,
  modifier checks...) still report regardless. Conversely tttt's *parser* reports some of tsc's grammar errors: the
  ones in `is_grammar_code()` (modifiers, TS1020, TS1162, TS1255) don't count as parse errors and are dropped in a
  file with parse errors, others (TS1308, TS2852, TS1491/1495, TS1019...) still count as parse errors.
- Error elaboration (plan 16, `elaborate_error` & co in ty/check.c): tsc re-checks the elaborated expressions
  (checkExpressionForMutableLocationWithContextualType, checkArrayLiteral with forceTuple); tttt reads the memos instead
  (`ty_mutable_location_type`, `ty_array_literal_as_tuple`), so an expression whose memo was typed under a different
  contextual type (generic inference passes) can show a different source type. `ty_is_generic_object` approximates
  isGenericObjectType. `ty_best_matching_type` (relate.c): findMatchingDiscriminantType lacks the key-property map,
  findMatchingTypeReferenceOrTypeAliasReference only matches references by target and anonymous types by alias, and
  findMostOverlappyType counts shared property names instead of intersecting `keyof`s.
- `add_implementation_success` (call.c) probes the implementation signature with `resolve_with_signatures`, which
  re-types the arguments; they are re-checked against the failed candidate afterwards to restore their memos.
- Related information positions: `type_decl_span` (check.c) is tsc's target.symbol.declarations[0] for declared types,
  function type nodes (FunctionImpl.full_span) and type literals (Interface.span) only. Mapped properties of an `as`
  clause lose their declarations when the clause renamed the key (per key, tsc decides per mapped type: remapping vs
  filtering, `expand_mapped` in members.c).
- `ty_check_object_type_duplicate_members` (check.c): `MemberTables` is a fixed 128-entry array searched linearly;
  members past the 128th of a class are never checked for duplicates (TS2300). Quadratic in the member count too.
- `intersect_unions` (checker.c): tsc's TS2590 ("Expression produces a union type that is too complex to represent")
  for a cross product of 100000+ isn't reported, the intersection is just the error type.
- `jsx_signatures_of` (call.c): the fake signature of a string-literal tag that isn't in JSX.IntrinsicElements takes
  the error type, without tsc's TS2339 from createSignatureForJSXIntrinsic.
- `Checker.jsx_element`: the element being checked, so `report_relation_error` can apply tsc's JSX head rule
  (`ty_jsx_skips_head`); tsc passes the error node instead. Relations about attributes outside `ty_check_jsx_element`
  don't see it.
- `ty_check_type_reference_constraints` (check.c, TS2344 on `T<A>` type references): arguments that could contain type
  variables aren't checked. Checking them gave ~60 false TS2344s: tttt's relation can't yet show `Diff<keyof U, K>`
  assignable to `keyof U` (indexedAccessRetainsIndexSignature), a deferred conditional to its constraint
  (conditionalTypeClassMembers, homomorphicMappedTypeNesting), or a merged declaration's constraint
  (interfaceMergedUnconstrainedNoErrorIrrespectiveOfOrder). Remove the gate as those get fixed (each is a relation bug
  of its own).
- `jsx_lookup_factory` (expr.c) writes `SourceFile.jsx_fragment_factory_looked_up` through a const cast: the resolve
  walk has no per-file state of its own, and TS2879 is once per file in tsc (getJSXFragmentType's cache).
  `ModuleRef.jsx_runtime_reported` does the same job for TS2875 on the ref.
- `export =`/`export default` of a namespace- or type-only name (`identifier_expr_lookup_names` branch on
  `flow->type_only_alias_ok`): the identifier gets `symbol` but no `target`, so the checker sees an untyped name there
  (tsc types it from the alias). Fine for the baselines that exercise it so far.
- `narrowable_type` (`ty/expr.c`): tsc types the initializer of a pattern with a rest element a second time for the
  rest (CheckMode.RestBindingElement: no binding-pattern contextual type, so a generic `T` isn't narrowed to its
  constraint and the rest stays `Omit<T, ...>`). tttt has one type per expression, so a pattern with a rest element
  (`pattern_type_has_rest`) is never a contextual type there, for the other elements either.
- `binding_element_implied_type` (`ty/lower.c`): a default evaluated for a pattern's contextual type runs under
  `flow_uncached` so that its sibling references (`any` then, `contextual_patterns`) aren't memoised; tsc's
  checkIdentifier isn't cached per node so it doesn't need that. Anything else the default mentions is evaluated
  uncached too (not remembered), which costs time on big defaults.
- HACK comments not covered elsewhere: `check_subsequent_property` (ty/check.c) skips TS2717 between two
  `unique symbol` types (one per declaration in tttt, one per merged property symbol in tsc);
  `ty_elides_default_type_args` (iterate.c) always prints `Iterable<T>` for the four 3-parameter iteration interfaces,
  where tsc prints what was written when all arguments were; `interface_decl_lookup_names` turns every `symbol`
  property of the global `SymbolConstructor` into a `unique symbol` (tsc does the same for pre-3.0 libs);
  `TyDisplay.widen_literals` prints literal types inside expression-made object types as their primitive (tsc widens
  them when typing the literal).
