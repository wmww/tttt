# Hacks
Suboptimal-but-working code, by subsystem. Each entry: where (function names), what is approximated, what the real
fix would be. Missing diagnostics and features belong in `bugs/*.md`, triage.md or a plan, not here. `grep -rn HACK
src` finds the code-side markers; every significant one has an entry here.

## Global state, memory, parallelism
- `check_share()` / `checker_memory()` (`program.c`, `ty/checker.c`) (permanent, plan 67): recycling relies on no
  diagnostic depending on a checker's history, which is false in a few places that tsgo shares
  (undefinedProperties member order: 4 vscode `.d.ts`) and a few it doesn't (bugs/sourcesweep_checker.md "History
  dependences recycling exposes"). `checker_memory()` leaves out the LazyMemo pages so the cut points don't depend on
  the parser threads' slot numbering: they are real memory the budget doesn't see. The budget policy (256 MB total,
  128 floor) is a wall-time compromise measured on 8 corpora, no warm-up model.
- `reexec_with_huge_pages` (`main.c`) (permanent: 5-20% wall time until the AST has arenas that madvise like the
  checker's, `bugs/perf_leads.md`): glibc reads `GLIBC_TUNABLES` only at startup, so tttt re-execs itself with
  `glibc.malloc.hugetlb=1` (not under valgrind or `TTTT_NO_REEXEC`).
- `source_file_id()` (`span.h`) / `span_files` (`span.c`) (permanent, plan 60 stage 2: Span is 16 bytes): spans hold a
  file index into a process-wide table that is never shrunk (indexes aren't reused; a dropped file's entry is null,
  chunks are mmapped so leak checks ignore them). `source_file_id()` reads SourceFile's first word because span.h only
  sees the incomplete type (static_assert in source_file.c). Expr's parenthesis offsets are 24-bit (`expr_set_outer_span()`
  panics past 16 MB of parentheses and trivia).
- `shrink_list()`/`list_push()` (`parser.c`) (permanent: memory for ~0.7% of nest's instructions, free once lists are
  built in a scratch stack and copied out exact, perf_leads.md "Memory"): finished parser lists realloc away their
  slack; the first push reserves one element, not the vec's 4.
- `identifier_expr_name()` (`expr.h`) (permanent: memory layout): an `IdentifierExpr` has no name of its own, it reads
  its `Expr`'s inner_span through `offsetof` (never copy one out of its Expr). Its rare pointers (`module_ref_`,
  `this_function_`, `evolving_index_`, `extends_entity_`) share a union picked by keyword/require_call/evolving_op; a
  new writer must keep those discriminants exclusive.
- `property_dot_new()` (`expr.c`) (permanent, plan 63: memory): a dot `PropertyExpr` is malloc'd only up to the end
  of `name_` (120 of 184 bytes), the bytes of the bracket form's `index` Expr aren't there: never copy a PropertyExpr by
  value or read `index` without checking `form` (`property_key_span()` and friends do). Synthetic accesses
  (`synthetic_access()`, lower.c) are whole arena structs.
- Shared all-zero stand-ins for absent parts (plan 63: memory): `binding_type()` returns `type_node_none` when a
  binding has no annotation and `scope_locals()` `symbol_table_empty`, both cast or read as mutable by some callers:
  only write through `binding_set_type()` / `scope_locals_mut()`. `type_lookup_names()` and the inferred-names walk
  check the pointer, not just `has_explicit_type` (a parser path that sets the flag without a type would otherwise
  write into the shared node).
- Two kinds of `links` (plan 60 stage 1, permanent: memory): `Checker.node_links` (`ty_link_get()`/`ty_link_set()`)
  holds 11 two-word key kinds with the key inline, value-based (no slot to keep); every other kind is still in
  `links`, whose `intern_slot()` slots stay valid. A kind may only move once all its uses are plain gets/sets, and
  `link_key()` packs the kind into the top byte of the subject's address (user space addresses are below 2^47).
  Interned unions, intersections and references key their member lists by pointer, and their TyLists point into the
  key block (`intern_key_list()`): never write through one. Pointer keys hash differently per run (ASLR): only probe
  lengths change (callgrind counts move by ~0.01%), nothing iterates the tables.
- `LazyMemo` holds type ids, not pointers (`lazy_memo_evaluated()`): every Ty is in its checker's id table
  (`lazy_slots_add_type()` in ty_alloc()), so a type made outside ty_alloc() can't be a memo's result.
- `current_checker` / `current_lazy_slots` (thread locals, `checker_make_current`): how the AST-side walk
  (`*_typecheck`, `*_evaluate_type`, `ty_of_node`) finds its checker; left set after `program_typecheck` for the test
  traversal. `compiler_options()` is a thread local too. Both want a passed check context (checker + errs + options).
  Many parsed options are inert (`grep -o 'compiler_options()->[a-z_]*' src` lists what's read).
- `Checker.return_context`: the contextual type of `return`s is checker state saved around bodies
  (`function_impl_enter_return_context`), where tsc walks up to the function (getContextualReturnType).
- LazyType slots (`lazy_slot_new`): one process-wide counter, never reused, taken eagerly: sparse tables cost ~6% RSS,
  panic after 1G slots; per-file numbering would work. `lazy_type_drop` resets a slot if a checker is current
  (assumed a temporary); AST slots are dropped by `checker_drop` before the AST: an implicit ordering rule.
- **Parallelism contract** (permanent: what the parallel resolve walk shares by design; documentation.md "Resolve
  names"). A JavaScript file's walk defers every assignment declaration to its replay (`DEFERRED_JS_*`), so the rest of
  its walk never sees what they declare (a `this.x = v`'s SYMBOL_CLASS, members, an expando's
  `FunctionImpl.class_symbol`; nothing reads them there); the `@constructor` mark of a function's local stays in the
  walk. Files sharing merged type parameters walk serially (`binder_mark_serial_resolves()`; tsc makes them in its
  binder, tttt could in declare). Replayed in program order: not-found reports (their suggestions), expandos into a non-private host
  (`expando_host_is_private()` is conservative). Cross-file writes: `Symbol.referenced` (atomic OR),
  `has_assignment_flow_containers` (atomic store), `flow_start` (only the first declaring file writes it;
  `outer_reference` treats a symbol first declared elsewhere as outer without reading it). A walk's scratch `ErrorCtx`
  can't be a deferred report's target: `shard_lasting_errs()` sends it to the shard's discarded one, right only while
  scratch contexts in the walk are dropped, never merged. Guards: the program's ctx is `frozen` and `Binder.frozen`
  set during the walk, so a report there, a new binder symbol or a first alias resolution `log_error()`s. Not a
  proof: TSan on the corpora and on `TTTT_PARALLEL_RESOLVE=1 baseline_tests --threads 4` (`ulimit -s 4000000`).
- Load pool (`program_load_ahead_from()`) (permanent: a wrong guess only wastes a parse, `TTTT_LOAD_POOL_STATS=1`):
  guesses what a file will load with a simplified copy of `program_load_module_ref()`/`program_handle_statement()`.
  Sizes from `stat()` in `main.c` (harness: FIFO).
- Once-per-file diagnostics another checker may also say (permanent: K checkers): `Error.once_key`,
  `error_ctx_keep_first_once()` keeps the one of the checker whose current file comes first. Tagged: TS2343/TS2807
  (`ty_check_emit_helpers`, keyed by file; tsgo keeps only the owning checker's, tttt approximates it by file order).
  A new "said once" checker memo must be tagged too. (The JSX runtime module one is untagged since plan 62 stage 1:
  tsgo says it per file at the file's first tag, so every checker says the same thing and the merge dedupes it.)
- Per-checker caches assumed pure in the type ids (permanent: tsc caches the same): `ty_union` of two types
  (KEY_UNION_PAIR), `ty_intersection` without an alias (KEY_INTERSECTION_OF), flow's `assignment_reduced_type`
  (KEY_ASSIGNMENT_REDUCED). A result computed while something it reads is in flight (constraints in
  `remove_constrained_type_variables`, members for empty-object checks) is pinned. First suspects of an
  order-dependent union or narrowing bug: disable these.
- Each checker measures variances and builds relation caches on its own (permanent: K checkers), and a TS2589 count is
  charged to whichever node first needs them (tsc pays once): a TS2589 near the 5M limit could depend on `--checkers`
  (see also "Instantiation limit" under Type core).
  The variances themselves can differ too: circular measurements (tsc's Unknown for one in flight) depend on which
  type is measured first, which follows the files a checker saw. (vue's vModel.ts:446 used to depend on it; it is
  stable at every `--checkers` since an in-flight measurement answers Unknown like tsgo, not Maybe: 2026-10-12)

- **Lazy name indexes trust their owners' discipline** (2026-10-14 perf session): `MembersBuilder.by_name`
  (ty/members.c) maps names to positions in `props`, so whatever reorders `props` must call `builder_unindex()`
  (builder_late_bound_last, builder_sort_stable_from, builder_prototype_last do; the tuple reorder in the tuple members
  is followed only by builder_finish). A new reordering site that forgets it makes `builder_find_prop()` miss a member
  in classes/interfaces past 16 members. `LiteralBuilder.slots` (ty/expr.c) relies on `props` only growing through
  literal_builder_add() and copies going through literal_builder_clone(); `ClassDecl.namesake_*` (class_decl.c) are
  built once when names are looked up and assume the items never change after. ttttLargeDeclarationDuplicates is the
  regression test.

## Parser
- `unique symbol` placement (TS1335, `parse_type_inner` and `expect_annotation_type`): checked by the parser through
  `unique_allowed_at`, not on the tree like tsc's checkGrammarTypeOperatorNode, and never in JSDoc, where tsc goes to
  the tag's host (a variable or property declaration allows it).
- Strings are UTF-8 (permanent: tttt's strings are UTF-8), a lone surrogate escape kept as its WTF-8 bytes
  (`append_code_point()`, which joins an escaped pair; the printer escapes it). Code that decodes UTF-8 strictly may
  still drop those bytes (`cutf8_encode` refuses surrogates). `regex_check_literal()` works in UTF-16 to port tsc's scanner; a position between surrogates is the 3rd UTF-8 byte
  (`utf8_byte_utf16_units()`). A NUL byte is a character where tokens and lists are concerned (at_eof()); some
  lookaheads still stop at it.
- Escaped identifiers (`a`) resolve by value through `name_cooked()` (span.h): a global registry of cooked names
  behind a mutex, keyed by where the raw name sits in a file with a `\u`, each lookup a linear scan of that file's
  cooked names (quadratic for a file with thousands). Symbol tables cook at every lookup (a `memchr` for normal
  names). Comparisons against literals (`span_equals(name, "constructor")`) stay raw (`bugs/binder_modules.md` "Escaped identifiers").
  `EnumCase.name` of a string member is the raw text. Escaped keywords (`hop_escaped_keyword()`) report TS1260
  wherever the probe matches, even speculatively.
- Two notions of "reserved": `parse_identifier()` refuses only `is_reserved_identifier()` (plus `var` in JS) because it
  also parses identifier names and keyword types; `parse_binding_identifier()` and enum names refuse all of
  `is_reserved_word()`. The fix is per caller: pick tsc's parseIdentifier / parseIdentifierName /
  parseBindingIdentifier (refusing all in `parse_identifier()` breaks identifier names).
- Conflict markers (TS1185): `Decl()` positions don't skip back over one; none after U+2028/U+2029.
- No missing-identifier node: `written_name()` prints `(Missing)` for empty zero-width names; bindings use
  `BINDING_NOOP` where tsc declares `(Missing)`. `parse_throw()`'s missing expression is a zero-width implicit any
  (TS1142 finds the keyword by `strlen("throw")`); `statement_is_bare_return()`: `return;` is an `undefined` literal
  (want a `has_value` bit).
- Missing nodes and trivia (`token_full_start()`, `unhop_trivia()`): `ParserShared.last_trivia_start/end` remember only
  the last whitespace hop. `expect_expr()` moves back so the parent ends at the previous token; missing expressions
  span the token start, missing names the full start; the symbols baseline skips ASCII whitespace forward for
  `Symbol(unknown)` (a comment breaks it). `Expr.missing` tells missing from absent. `expect_expr()` continues a
  missing operand with binary operators and `.`, not `[`/`(`.
- Spans rebuilt by scanning text: full starts by skipping spaces backwards (TS1433, TS1385-TS1388; comments not
  skipped); `question_dot_after()` scans forward for `?.`; an object literal missing `}` ends by walking back over
  whitespace (`parse_object`), a bodyless accessor's TS1005 likewise. A `span_from` on a parent's anchor inside a child
  parser before `parse_success` truncates the span.
- Error recovery (plan 27): `list_abort_or_skip()` skips a token that starts a list element instead of ending the list
  (right while tttt's element parsers fail where tsc's commit). The fail cache ignores
  `ParserShared.parsing_context`. `parse_list_member()` asks the member-start predicate only after errors;
  `parse_binding_content()` stops at the first bad element; a committed arrow whose parameters fail falls back to a
  parenthesized expression. `tok_scan_end()` is a second cheap token notion next to `hop_token()`.
  `parse_jsx_attributes` approximates isInSomeParsingContext (`jsx_children_depth` + a character list).
- Statements: `statement_takes_semicolon()` lists by tag who calls parseSemicolon(). Statement spans include the `;`,
  but port items' and class members' full spans don't (`span_with_semicolon()` at their error sites). `IfStatement`
  flattens `else if` (no `Statement` for the nested if: no TS7027 on it; `Statement.flow` costs 8 bytes per statement
  for TS7027 only, labeled statements have none). `statement_start_needs_check()` gates isStartOfStatement by first
  word (a new rejecting case must be added).
- Dropped syntax: `in`/`out` type parameter modifiers never print; private names outside classes have no flag
  (`object_item_is_private_name`, `interface_member_is_private_name`, `enum_decl_typecheck` check the first character;
  such an enum member is declared and prints `__missing`).
- Accessors: class accessors pair by name+staticness in `class_decl_lookup_names` (raw pointers in one vec); readonly
  means "no setter". Interface accessors become properties in `parse_interface` (`InterfaceMember.write_type`,
  `second_name`, `setter_param`; `interface_declaration_kind()` in check.c re-reads `get`/`set` from the span text).
  `{ a = 1 }` keeps `ObjectExprItem.shorthand_default`. Computed keys: see "Class/interface members aren't symbols"
  under Binder.
- `await_is_operator`, `yield_is_operator_outside_generator`, `yield_await_is_keyword_here` (permanent: the parser is
  scannerless): `ParserShared` flags plus a character peek where tsc looks ahead a token
  (nextTokenIsIdentifierOrKeywordOrLiteralOnSameLine; a comment after `yield` is taken as "no operand").
- `mark_named_evaluation()` (permanent: no parent pointers): every construction site that names an anonymous class
  must call it (tsc's isNamedEvaluation walks up).
- Parsed and dropped (the lost diagnostics are in `bugs/missing_checks.md`): computed non-literal enum keys
  (`EnumCase.computed_name`, member prints `(typeof E)["[e]"]`, tsc `E`); non-string import specifiers; a `ModuleRef`
  from an abandoned attempt stays listed (`add_module_ref`). `global x` is `NamespaceDecl.is_shorthand` typed any
  (`namespace_decl_compile_test_info()`). `ModifiedNode.name` stands in for getErrorSpanForNode (TS1248).
- Heritage clauses (`parse_heritage_clauses()`): type-position elements are parsed as expressions, then again as types
  if entity names (`parse_heritage_element()`); TS2864/TS2840 recognised by text; unchecked parts in
  `unchecked_*`/`invalid_*` fields resolved silently for baselines; TS1172-TS1176/TS1097 from the parser.
  `BaselineTestCase.hidden_type_spans` hides what isPartOfTypeNode drops.
- JSDoc types outside JSDoc (`jsdoc_type_outside_jsdoc()`): `?T`/`T!` are a `TYPE_JSDOC` wrapper; `*` reports TS8020
  without a node.
- Top-level await (`parse_file()`): the whole file is reparsed with the statements that saw an `await` identifier as
  await contexts (`ParserShared.await_statements`); module-ness guessed by `parsed_file_is_module()`, a copy of
  program.c's rules.
- `triple_slash_invalid_reference()` re-implements tsc's pragma regexes next to `parse_source_directive()`.
- `undefined` is a literal; `expr_lookup_names` rewrites it into an identifier when a non-global declaration is in
  scope (`Expr.undefined_assigned` for assignment targets). Should be an identifier resolved to undefinedSymbol.

- `json_check_value()` (parser.c): the TS1328 of a missing or omitted JSON value is placed at its full start by walking
  back over whitespace only; a comment between `:` and the missing value puts tsc's at the comment's end.

## Binder and name resolution (`scope.c`)
- `scope_release()` (permanent: plan 43's memory): walk-only scopes (parameter list, return type, block statement,
  type literal, interface `this`, computed name/decorator) are popped off the binder arena when still its latest
  allocation and no pending report (`PendingNotFound`, `PendingExportMerge`) took them. The proof is "everything that
  can point at a scope is allocated after it in the same arena": a new place that stores a `Scope*` outside the arena
  (AST, malloc'd lists) must block the release like the pending reports do. Check by poisoning (memset 0xA5) instead
  of popping.
- Scope chains are shorter than the AST's nesting: `if` branches that are blocks, loops, `switch`, `try`, `return`...
  get no block scope of their own (`branch_names_scope()`); a `declare` declaration in an ambient context gets no
  `scope_new_self()` scope unless unused checks are on.
- `binder_redirect` / `Binder.overlays`: a program adding to a shared-lib symbol gets a clone (`clone_parent_symbol`);
  the checker maps through `canonical()` (`ty/lower.c`): anything new keyed by `Symbol*` must too. Fallout: an
  augmented `Console` shows both `log` signatures (wants getIntersectedSignatures); `maps_merged_type_param()`
  (ty_display.c) misses an interface merged with a lib one, whose type parameter is the lib symbol's
  (promisePermutations x3).
- `make_gather_target`: namespace imports, `import x = require()` and `export as namespace` alias a namespace symbol of
  their own (`Symbol.gather_module`) so the type prints `typeof ns`; could go once messages name modules like tsc.
  Node16+ ESM `import * as ns` of CommonJS: it copies the module's resolved exports at link time (right only because
  nothing merges into module exports after `binder_link()`), plus `Symbol.synthetic_default`; in `members.c` the
  synthetic default's `prop->decl->symbol` is the alias rather than the module, so the symbols baseline prints
  `d.default`. Related: `namespace_import_is_clone()` under Declaration diagnostics.
- `merge_symbol()` returns early for alias sources and other binders' targets (one TODO): module augmentations
  (`merge_augmentations`, `retarget_decl`) are never merged into shared-lib symbols, an alias inside one meeting an
  existing name isn't merged (`export default x` of an alias in an augmentation), and `add_pattern_declarations`
  (tsc's unidirectional merge of a pattern ambient module into a name's augmentation, `Binder.pattern_augmentations`)
  only copies the flags and declarations of an export both declare (the rest through the exports table's parent), not
  a nested namespace's exports. Real fix: a `merge_symbol` that can clone its target. Names added through an
  `export *` module reach only that module.
- `export =`: named imports see only the namespace side; `namespace_import_type` asks `ty_type_of_symbol()` about the
  alias to spot namespace imports (identifiers only), so `import("./a")` of an `export =` module prints `typeof ns`
  where tsc spells out the synthetic `{ ...; default: ... }`, and the synthetic `default` prints as `default` in
  symbols (tsc: the module name).
- `export = a.b` / `export default a.b` is an alias (`resolve_entity_alias()`, falling back to a value symbol of the
  expression): a class static merged with a namespace interface is an alias whose `value_target` is the static, and
  `module_export_equals()` returns the alias itself (callers assuming a non-alias beware). tsc makes them one symbol
  with both declarations; tttt keeps statics out of the class's exports (exportEqualsProperty2 symbols).
- `globalThis` is a namespace over `Binder.globals`: every global is a property (tsc: `var`s and functions);
  `ty_namespace_type` builds ~2k props on first touch.
- `arguments` has no symbol (`IdentifierExpr.is_arguments`, `IArguments` by name).
- `scope_declare_default_local()`: a compatible local before `export default function A` keeps `A`.
- Exported and local declarations of a name are two unlinked tables (tsc: `exportSymbol`; `SymbolRare.export_symbol`
  only for `export default` locals): TS2440 special-cased in `binder_link`, TS2395 a separate pass. TS2441, TS1203,
  TS2308, TS2661, TS2669 come from binder/link (tsc: checker); TS2308 re-walks `export *` apart from
  `module_resolved_exports()`.
- `block_is_instantiated`: a nested namespace is judged with its parent's `outer` scope.
- Class/interface members aren't symbols (`Prop` is the identity). `bind_member_symbols` + `class_decl_lookup_names`
  apply binder rules at lookup; computed keys' `name_string` is source text, so accessor pairs and overloads match
  computed entity-name keys by text whatever their type (a class's pairs replay the binder's join rule over the
  members with that text, `computed_symbol_first()`; object literals don't) (tsc late-binds only literal/unique-symbol names, and `["a"]`
  pairs with `a`: computedPropertyNames2): needs lazy pairing in the checker. `ty_member_name()` still evaluates every
  computed key. `ty_check_class_duplicate_members` / `object_bind_members` replay the binder at check time.
- `declare_symbol` (permanent: builtin types are symbols in tttt's globals, not in tsc's): a declaration named like a
  builtin type (`class number`) gets a detached symbol.
- Merged interface/class type parameters share one symbol per name (`scope_new_generic_merged`, see the parallelism
  contract) except against the shared lib; `type_parameter_is_unused` compensates.
- TS2301/TS2844: fields are walked before the constructor's scope exists, so `constructor_locals()` (class_decl.c)
  collects its names with `function_collect_local_value_names()`/`statement_collect_var_names()`, a read-only copy of
  `statement_hoist_names()`: keep in sync.
- TS1361/TS1362 valid sites (`FlowBuilder.type_only_alias_ok`, `in_type_query`) set by enclosing constructs, not
  derived. `export =`/`export default` of a type-only name gets `symbol` without `target`. TS1376/TS1377's related
  span (`span_from_type_keyword()`, from `port_item_add_type_only_related()`) scans back from the name for `type`:
  PortItem has no span of the whole specifier.
- Null-ErrorCtx resolution because the resolve walk reports checker errors: the body of `with`
  (`statement_resolve_names_reachable`; binder errors in it are lost too; the baseline walk still evaluates its types
  under `BaselineTestCase.in_with`) and `yield x` outside a generator (`UnaryOpExpr.outside_generator`). Real fix: the
  resolve walk stops reporting checker errors.

- `SymbolRare.literal_member` (`add_expando_member()`, `namespace_export_prop()`, the walker's `prop_text()`): tsgo
  declares a `{}` container's expandos in the object literal's symbol, a table of its own; tttt shares the host's
  exports, so an expando whose name a dotted JSDoc typedef's namespace already declared (`@typedef {T} Host.a.B` next
  to `Host.a = {}`) merges into that namespace symbol. The value side is then the expando's alone (its property
  drops the namespace declarations and the qualification), but a type-side reference shows both declarations.

- `SymbolRare.merge_detached` (`merge_into_exports()`, `ty_type_of_symbol()`): an alias a cross-file merge went
  through (`export { Knex }` in a namespace, augmented) keeps tsgo's view of it, whose aliasTarget is the target as it
  was before the merge: only its flags are remembered, so a target without a value makes the alias errorType, but one
  with a value gives the merged target's type (tsgo: the unmerged one's). The symbol chain skips the alias in tables.

- `attribute_type_accepts()`/`attributes_subtype()`/`best_pattern_module()` (`scope.c`): which pattern ambient module
  (`declare module "*.css" with { type: "css" }`) an import with attributes is, decided by the binder on the syntax
  rather than by tsgo's isTypeAssignableTo/isTypeStrictSubtypeOf of attribute types: string literal annotations exact,
  unions of them as sets, `string` and anything needing types (a type reference, a template) take any value, the rest
  none. Only erroneous code (TS1555) sees the approximation. `import("m", options)` of a non-literal options asks the
  checker (`ty_dynamic_import_module()`), whose answer the resolve walk's TS2307 doesn't see. Candidates capped at 16.

## Control flow and narrowing (`flow.c`, `ty/flow.c`)
- Array mutations: `flow_array_mutation()` makes nodes only where `may_evolve()` (permanent: tsc makes one for every
  narrowable reference and ignores it unless the variable is auto-typed); only an `x.push(...)` callee marks an
  evolving-array target (tsc's isEvolvingArrayOperationTarget: `f(x.push)` too). `TY_EVOLVING_ARRAY`: switch clauses
  narrow the final array, not the evolving type.
- `expr_is_narrowable_reference()`: `a = b` counts whatever the left side (tsc: a LeftHandSideExpression; differs only
  after parser recovery).
- Body-less functions get no start node (permanent: only their parameter defaults would flow, TS2371 anyway).
- Destructured discriminants and dependent parameters (`discriminant_access()`, `ty_destructured_narrowed_type()`,
  `ty_parameter_narrowed_type()`) query flow with a placeholder reference (`FlowQuery.pattern`/`parameters_of`) for
  tsc's pseudo references: one extra query per read.
- `narrowable_type`: constraint positions marked by the resolve walk (`constraint_position`); element access for
  literal indices only. tsc checks a rest pattern's initializer twice (CheckMode.RestBindingElement): tttt types
  the rest element of a root pattern from `ty_rest_binding_initializer_type` (the reference computed again without
  context), not nested patterns' rests; call.c's return mapper still ignores a pattern with a rest element.
- Auto types: `IdentifierExpr.outer_reference` decided by the resolve walk; isNeverInitialized is
  `!symbol->is_assigned`; "outer variable assumed initialized" decided at the start node (`Symbol.flow_start`).
- `ty_flow_expr_type()` loop results are cached per back-edge walk by generation in `Checker.links`: stale generations
  stay until the checker dies. `LazyMemo.hits`: 8 re-entrant evaluations, then the error type (runaway guard).

## Declarations and bindings (AST-side walk)
- Plan 43 side structs (`InitializedBindingRare`, `FunctionImplRare`, `initialized_binding_value()`): absent ones read
  as shared read-only statics (`initialized_binding_no_value` lives in rodata: a stray write segfaults); `*_rare_mut()`
  allocates on first write, also from the resolve walk (safe while one thread resolves a given AST). Packed spans
  (`FunctionSpan`, `Binding`'s `explicit_type_*`/`element_*`) are offsets in one file and panic if set from another.
  (permanent: AST memory layout)
- `ty_binding_assigned_type` (not memoised): tsc unions the undefined-stripped element with the default (subtype
  reduction) and widens after; tttt widens the default first, then reduces (a fresh object literal default is reduced
  fresh and widened in place, `[{ flag } = {}]`; other defaults whose freshness matters would still differ).
  `ty_binding_default_type` bypasses its LazyType (a pattern's names on the resolution stack stop `[x = x]`);
  `binding_element_implied_type` runs defaults under `flow_uncached` (with `include_pattern`).
- Array pattern rest (`binding_element_of`): the parent type is widened, so "any parent is any" peeks at the root
  initializer's expression type for a nullable. Real fix: keep the unwidened parent type (tsc's
  getTypeForBindingElementParent).
- `class_decl_lookup_names` drops a constructor's return annotation after TS1093 (tsc keeps and ignores it).
- `field_type_from_assignments()`: the first real `this.x =` stands in for tsc's synthetic flow reference of the
  property (getFlowTypeInConstructor).
- `ty_merged_value_type`: a second `Ty` next to the unmerged one (`ty_static_type_of`, what static `this` sees). Real
  fix: one type per merged symbol (tsc's getTypeOfFuncClassEnumModule).
- Optional parameters: `T` in the signature, `| undefined` added at use (`value_entry_type()`,
  `ty_optional_param_type()`); tsc's parameter symbol type has it (addOptionality).
- `ty_report_pattern_iterable()` (`ty/lower.c`, from `initialized_binding_evaluate_assigned_type`): TS2318 'Iterable'
  for an empty or rest-only array pattern goes where tsc makes createIterableType, but tttt computes an initializer's
  pattern contextual type eagerly, so it's gated on the initializer being an array or object literal. A lazily
  computed contextual type would decide it the way tsc does.

## Calls and inference (`ty/call.c`, `ty/infer.c`)
- `Checker.outer_inference` (the call inferring now) is dynamic state, where tsc finds a node's inference context
  among its ancestors (getInferenceContext): every evaluation of something that isn't part of the argument must clear
  it, or its contextual types are instantiated with the wrong call's inferences. Cleared by `function_return_type()`
  (unless the function is the argument), a variable's initializer (`initialized_binding_evaluate_assigned_type()`, not
  a parameter default) and `ty_infer_type_predicate()`. A new cached evaluation reached from inside inference must
  clear it too (trpc's order-dependent `any`, plan 62).
- `visited_add()` (infer.c) keys an inference's visited pairs by (source, target) alone; tsgo's invokeOnce key also has
  the priority, contravariant and bivariant, and it routes alias-to-alias inference (inferFromAliasTypeArguments)
  through invokeOnce too, where tttt's alias branch of `infer_from()` recurses unguarded. Keying like tsgo walks more
  (effect +0.4%); guarding aliases makes karakeep measure fewer alias variances (perf_leads.md, agent A's leftovers).
- TS7023/TS7024's own-context circularity (`ty_function_context_is_self`) and TS7011/TS7018 reportability are
  decided where the return type is inferred (`ty_function_decide_widening_reportable()`, tsc's getReturnTypeFromBody):
  through the anchor once the call is resolved, else the first contextual signature (`KEY_CONTEXT_RETURN`), not the
  one the check of the moment has.
- `infer_from_objects()`, a generic mapped target `{ [P in C]: T[P] }` (the homomorphic `T[K]` shortcut, standing in
  for reverse mapping): the source goes to T as if homomorphic unless C is a conditional (Omit's
  `Exclude<keyof T, X>`, where tsc infers nothing), intersection-limited or a deferred `keyof X` whose X isn't
  inferred. A union with literal keys (`"a" | Exclude<keyof T, X>`) still takes that path (tsc infers from the
  properties those keys give: complicatedIndexesOfIntersectionsAreInferencable). Pick's K gets the source's keys only
  outside return-type inference (with `return_inference_mapper()` K2 of twiceNestedKeyofIndexInference would be
  `never`). `infer_to_mapped()` finding nothing returns (tsc goes per property; neutral). Deleting the shortcut
  (tsc always builds the reverse mapped type, which is lazy in tttt too since plan 42 stage 1) cost those two tests in
  plan 42's prototype (2026-10-05); both pass now with the shortcut, so re-measure before trying again.
- Skip-sensitive rounds (`resolve_with_signatures`): tsc's SkipContextSensitive round checks every argument once, its
  context-sensitive functions as anyFunctionType; tttt leaves them out of `check_args_until_unfit`
  (`skipped_function_unfit()` relates anyFunctionType for bare functions) and relates the literal ones afterwards
  (`first_unfit_skipped_literal()`, regularised, nested generic calls skipped), so a literal is related after the
  arguments behind it. `return_only_type()` gives generators anyFunctionType; `first_unfit_argument` skips such
  arguments in the first overload round.
- `ExprCtx.sig_contextual` instantiated only where `may_want_signature()` (permanent: perf, instantiating it for every
  expression changes no baseline but costs +2.7% instructions on xstate, +1.7% valibot)
- Higher-order (`inference_propagate_generic`, `Arg.generic_function`/`generic_call`, `waits_for_sensitive`): a failed
  call is re-inferred without them, then arguments re-checked against the failed candidate only to restore memos
  (`add_implementation_success` likewise). `ty_generic_function_in_context` (HACK) isn't tsc's branch.
- Return mapper (`infer_type_arguments`): tsgo's returnMapper (its own inference from the contextual type instantiated
  with the outer call's createOuterReturnMapper, made once per outer inference: `outer_return_mapper()`), except that a
  call whose return source is the main inference's source reuses the main inference. `literal_arg_context()`
  pre-instantiates with the return mapper (higher-order needs it). A pattern's return mapper drops inferred `any`
  (tsc's nonInferrableAnyType), rest patterns give none; `ty_pattern_unclone` copies the pattern type per call. Nested
  literals instantiate context only in `check_for_mutable_location()`.
- Declaration emit's display depth limit (`ty_display`, 8 * 16 levels) reports TS7056 like the length limit tsc has
  instead: a recursive reverse mapped type's index signatures make a new type per level, which tsc prints until the
  text is a million characters long. A finite type nested deeper than that would get a spurious TS7056.
- Reverse mapped types of arrays/tuples are eager like tsc's, but cut at depth 4 (`Checker.reverse_mapped_depth`, HACK:
  tsc's reverseMappedSourceStack/isDeeplyNestedType needs a mapped-type `Ty`). Object ones are lazy (plan 42 stage 1).
  Instantiating a generic reverse mapped type goes through `{target, mapper}`, not tsc's instantiateReverseMappedType.
  Inference priorities lack SpeculativeTuple, NoConstraints, AlwaysStrict. `inference_call_result()`: combined
  covariant candidates literal-reduced (tsc: subtype-reduced).
- Intra-expression sites (`ty_add_intra_expression_site()`): not for JSX attributes; `context_is_tuple_like()` misses
  call spreads.
- `const` type parameters: nested array literals found by syntax (`add_nested_array_literals`) plus
  `Inference.array_literals` (ty_internal.h HACK) because interned tuples can't carry ObjectFlags.ArrayLiteral.
- `clone_type_params`: fresh type parameters only when bounded or the mapper brings type variables, else shared with
  the target (permanent: perf, indistinguishable)
- `ty_union_signatures` isn't cached (tsc: the union's resolved members). `resolve_call` falls back per member for
  `super`, decorators and JSX when a union member has no signatures; tsc errors for decorators, takes the union
  signatures for `super` (getSignaturesOfType) and has getUninstantiatedJsxSignaturesOfType's own union rule for JSX.
- `super(...)` of a TypeScript base whose type parameter has no default and no written type argument (TS2314 on the
  clause): inferred (`super_filled_type_arguments()` returns null); tsc's getConstructorsForTypeArguments drops the
  constructor.
- `tagged_template_call()` (HACK): a stand-in CallExpr with a shallow copy of the tag as callee
  (`CallInfo.tagged_template`); a TaggedTemplate node or argument-list abstraction would avoid it.
- `signature_resolve`: function expressions take the contextual signature's type parameters only if resolved after
  the context was assigned.
- `missing_property_type` (HACK) also decides the type of a failed access: errorType under conditions approximating
  check.c's reporting (object type, not `typeof globalThis`, no unresolved base, not JS; unchecked JS's "declared in
  another global file" by the object name's file). tsc: errorType exactly when it reports.
- Async: `ty_awaited()` wraps generics in `Awaited<T>`, but generators use `ty_awaited_no_alias()` and async returns
  `ty_awaited_returned()` (call.c HACKs) because tttt can't relate `Awaited<T>` to T through the conditional's default
  constraint (with the alias in generators: 8 suite tests lost, asyncImportedPromise_es6, unknownControlFlow...).
- Contextual `this` (`ty_contextual_this_type`): a literal's context through its anchor once the call is resolved,
  else remembered at check (`KEY_LITERAL_CONTEXT`, last wins: tsc's pushed contextual type while the call infers; a
  push/pop stack is plan 51 stage 3 item 3); `ThisType<T>` by name, instantiated with `ty_instantiate_fixing()`.
  `KEY_LITERAL_PARENT` is a parent side table (permanent: ThisType markers of outer literals, plan 51).

## Expressions and contextual typing (`ty/expr.c`, `ty/access.c`)
- **"Which declarations make one member symbol" has several copies** (triage 2026-10-14): class_decl.c's
  bind_member_symbols/computed_symbol_first, the checker's MembersBuilder, check.c's MemberTables, the symbols
  baseline's computed_member_symbol_items. The symbols baseline should ask the checker's Prop decl spans everywhere, as
  late_member_declarations() and ty_object_literal_member_symbol() do.
- `ty_deferred_reference()`: TYF_COULD_CONTAIN_TYPE_VARIABLES only when a type parameter the node mentions is unmapped
  or mapped to something generic (tsc's couldContainTypeVariables says yes for any reference with a node). Precise
  where conditional.c's "infer constraint that stays generic" HACK needs it (react-relay); drizzle -24%, valibot +2%.
- Instantiation expressions (`apply_type_arguments_part`, HACK): signatures whose type arguments violate their
  constraints are dropped when some other signature was dropped by arity; tsc keeps them uninstantiated. The mapper
  can't say "kept as is": mapping their type parameters to themselves breaks `collectValues<A>` inside collectValues
  (effect). Needs a kept-signature list on the instantiation (`ty_apply_type_arguments_mapper`, `instantiate_members`).
- Expression memo (`check_with`, `LazyMemo.context`): an `Expr`'s memo is the last contextual check's (permanent: tsc's
  final Normal-mode check writes the node type, first-wins loses 19 tests, plan 51). `kind_uses_context` decides
  rechecks; `initial_or_assigned_type()` re-derives the context for `x = e` and annotated declarations, compound /
  logical assignments and destructuring defaults don't.
- Context anchors (`ContextAnchor`, `ty_record_call_anchors()`, plan 51 stage 2): after a call resolves, its function
  arguments' contextual return and its literal arguments' contextual type (for `this`) are derived through the
  resolved signature along a syntactic path (arguments, literal members, array elements, ternary branches, returns;
  not through calls, first call wins), tsc's getContextualType up to links.resolvedSignature. Before resolution the
  push-down values stand in (`KEY_CONTEXT_RETURN`, `KEY_LITERAL_CONTEXT`). Not recorded for a non-generic call of one
  signature (`CallResolution.exact`). Derivations cached per anchor unless transient (`AnchorRead.transient`).
  Generators' returns aren't anchored.
- `KEY_CONTEXT_CHECKED` (expr.c, tsc's ContextChecked): the first check that doesn't skip a function decides its
  contextual signature for good (`context_checked_none` without one); a function without one still takes `this` from
  the last contextual signature, which tsc asks lazily.
- `assign_contextual_signature_as`: once per function (`KEY_CONTEXT_CHECKED`), at its first check; generic
  contextual signatures aren't instantiated. `KEY_CONTEXT_PARAM_PATTERN` marks pattern fallbacks for the symbols
  traversal. `KEY_CONTEXT_IIFE_RETURN`: recorded at the IIFE's first contextual check, an earlier return type misses it
  (permanent: tsc's getContextualType(iife) is fixed at the first check too, plan 51).
- Apparent contextual types: `intersection_prop_context` intersects per-member answers (tsc: the intersection's
  apparent type once, distributing over union constraints; only union/intersection constraints go through
  `context_map`); `get_prop_context`/`contextual_signature` take apparent types at use. `context_map` drops `unknown`
  parts when others contribute (tsc: noReductions).
- Excess properties (`has_excess_properties`): a generic mapped target knows the lower bound of its constraint's keys
  (`generic_mapped_knows()`, a partial tsc's resolveMappedTypeMembers: no intersection/conditional lower bounds); no nested re-check for intersection targets; `discriminated_union_target` has no key-property map.
- Destructuring assignments (`assignment_target_typecheck`): for-of destructuring (statement.c HACK) relates the
  widened element type (tsc: per property, so no excess property checks); array literal targets are arrays, not
  tuples.
- Expandos on functions are checked against existing members; `is_container_declaration()` approximates
  isAssignmentDeclaration for `ns.a = {}`.
- `ty_access` skips nullish union members silently (TS18047/18048 come from `check_access`). `member_refs()` (check.c)
  duplicates `access()`'s walk to get `Prop`s without creating types.
- `PropertyExpr` keeps four pointer-sized resolve-walk fields (`flow`, `context`, `declares`, a
  `private_class_`/`for_in_loop_` union): 32 bytes per access.
- JSX: `jsx_signatures_of` types unknown intrinsic tags as error (tsc: TS2339); the head rule (`ty_jsx_skips_head` via
  `Checker.jsx_element`) only inside `ty_check_jsx_element`; attribute types skip the regular object literal state.
- `assignment_value_context` (HACK): `TYF_BINDING_PATTERN` is both tsc's object literal `pattern` and
  isFromBindingPattern, so an object assignment pattern only marks its context when the value isn't a call (else
  `({ p: this.p } = f())` would infer nothing from it). A flag for "from a binding pattern" would separate them.
- `contains_spread` (members.c HACK, ObjectFlags.ContainsSpread): read off the properties (one not the literal's own),
  so `{ ...{} }` doesn't count.

- `x satisfies T` (`ty/expr.c` EXPR_AS_TYPE): relates right away and, when that fails, prints T and throws the text
  away, because tsc builds TS1360's message there and printing T resolves what it resolves (a return type in flight:
  `const f = () => 1 satisfies typeof f` is TS7023). The diagnostic itself is still the walk's. A message built for
  its side effects; the general fix is reporting satisfies where it's checked (tsgo's checkSatisfiesExpression).

- `alias_is_local()` (lower.c): tsc's isLocalTypeAlias is "has a containing function"; tttt uses TypeDecl.local (any
  block or function body), which also counts a type alias in a top-level block.

## Relations (`ty/relate.c`)
- Two passes where tsc has one: tttt relates a pair without explaining, then again to explain, where tsc's
  checkTypeRelatedTo explains during its only run. The failures the first run cached on its way would make the
  reporting run's non-reporting sub-questions (an overload tried after the first) fail early where tsc's run finds the
  pair still on its maybe stack, so `check_related()` logs them (Checker.failed_log) and the reporting run of the same
  pair forgets them first (complexRecursiveCollections). A caller whose first run isn't immediately followed by the
  reporting run of the same pair keeps them; tsc's own isTypeAssignableTo-then-checkTypeAssignableTo callers have them
  too, so that's mostly right.
- `canonical_signature()` (tsc's getCanonicalSignature) returns the signature itself when none of its type parameters
  is an unconstrained clone, where tsc instantiates it anyway (erasing its type parameters). tttt's inference treats a
  generic contextual signature's type parameters differently from a non-generic one's: with the erasing instantiation,
  vue's `toHandlerKey` (`<T extends string>(str: T) => T extends '' ? ...` against its annotation) loses tsc's TS2322,
  which tttt only gets through that difference (tsgo infers T := the annotation's T and still fails the conditional
  relation; why wasn't found). Real fix: find where tsgo's relation of the two conditionals fails.
- `find_prop()` of an intersection: optional property types are kept without undefined (readers add it; tsc's
  getTypeOfSymbol includes it). An optional intersection property with a non-member part (`typeof f & { m?: X }`) is
  `F & X` in tsc; tttt drops PROP_OPTIONAL from it by flags, which also makes it required as a target (tsc: optional).
  Real fix: optional property types that carry their undefined.
- relate.c sits at GCC's `inline-unit-growth` limit (permanent: a build constraint): code added anywhere can cost a
  hot inlining elsewhere (`find_prop` into `props_related_impl`). Reporting helpers are `[[gnu::cold]]`, rare paths
  `[[gnu::noinline, gnu::cold]]`; check `-fopt-info-inline-missed` when an innocent change shows in `scripts/instrcount.sh`.
- `effective_intersection_constraint()` follows constraints through type parameters, keyof and conditional types at most
  32 steps (tsc: until it reaches something else; a safety cap, never seen hit). `apparent_type_of_mapped_at()` stops 8
  modifiers types deep (tsc: unbounded, relying on mapped type circularity checks).
- `conditional_source_related()` relates a conditional source's distributive constraint without tsc's
  hasNonCircularBaseConstraint(source) gate: computing that base constraint inside an alias's resolution lowers branches
  that reach the alias in flight (spurious TS2456 in type-fest's `_ConditionalPickDeep`, sourcesweep: crawlee and 3 more).
  tsc's alias references are lazier. Real fix: deferred alias references, or that gate after the alias is resolved.
- `resolved_base_constraint()` simplifies indexed accesses before computing (tsc: every type), and conditional types
  only when `may_simplify_conditional()` says their branches as written could make them `T extends U ? T : never`-shaped
  (simplifying every conditional instantiates both branches, which resolves what they mention: mikro-orm/seeder 2.3x
  types). Misses a branch written as another type parameter that the mapper turns into the check type.
- Base constraints of types without type variables are themselves (`resolved_base_constraint()`) except string mappings,
  template literals and substitution types: tsc also computes unions/intersections containing those
  (`Uppercase<string> | number` is `string | number` to tsc, itself to tttt).
- `conditional_source_related()` (permanent: perf): the default-constraint step pushes the target on the deep-nesting
  stack too (tsc: RecursionFlags.Source only); tsc's flags cost +8.6% instructions on react (react-hook-form's
  `T[a][b]...` chains).
- Error reporting (plan 34, tsgo's chain since plan 54): `TyReportLine` has no message arguments, so
  `report_property_incompatible()` reads paths back out of messages (`incompatible_line_path()`) and
  `relation_line_said()` compares a missing property line's text with the pair's (tsgo's chainArgsMatch compares
  argument strings); `headMessage` approximated by `RelationState.at_top` and `head_code`.
  `relation_line_said()` never lets a missing-property line about a mapped type with generic keys replace the head
  (tsgo would name its apparent type; vitest's TS2344).
  `intersection_props_related`'s TS2741 member choice fitted to one probe
  (`test/probes/plan19/valibot/overload_omit_intersection_missing_prop.ts`).
- Elaboration (`elaborate_error`) reads memos (`ty_mutable_location_type`, `ty_array_literal_as_tuple`) instead of
  re-checking; `ty_best_matching_type` has no key-property map; `type_decl_span` covers declared types, function type
  nodes and type literals only (renaming `as` properties lose declarations).
- `RelationState.intersection_source`: tsc keeps IntersectionState.Source down into the members' properties, tttt only
  for the members themselves. Keeping it (the one-line change in `structured_related_impl`) is clean on the suite but
  gives vue a spurious TS2769 (`h(Suspense, ...)` in runtime-core/__tests__/hydration.spec.ts).
- `structured_related_members`: the whole-source fallback for an intersection target (tsc: any instantiable source
  goes on to its constraints) covers conditional sources only.
- `props_related`: the same-symbol skip is by `Prop` identity plus a rule for object literal accessors of one function
  (tsc: same symbol).
- `generic_mapped_target_related` (HACK): a generic source failing it falls through to member comparison, which
  memberless mapped types pass (`Readonly<T>` to `T`); arrays, non-variadic tuples and objects without properties
  don't (tsc: only a mapped source goes on). Returning its result outright loses circularConstrainedMappedTypeNoCrash,
  restTypeRetainsMappyness (`[...Foo<T>]` of a lone spread, tsc's getSpreadArgumentType has `Foo<T>`),
  mappedTypeRelationships, and zod's `SortItem<output<T>>` against a Flatten-like mapped type.
- `is_rest_array` (HACK) takes any non-type-parameter as a rest array; real fix: normalize `[...X]` (X no type
  variable) when the tuple is made, like tsc's createNormalizedTupleType.
- Variances: `VARIANCE_UNRELIABLE` comes from a checker-wide flag (`variance_reports`, set when a compared type mentions
  a marker), cached as `CACHE_UNRELIABLE`; alias markers by type id (`KEY_ALIAS_MARKER`).
- Identity (`identity_structured`): differently shaped tuples never identical, no `infer` in the two-conditional rule.
  TS2403 (`check_subsequent_declaration`) uses it and exposes every approximation between two spellings.
- `RelationState.overflow` (TS2321) trips at 100 nested recursive_related() calls (`st->depth`); tsc counts source
  and target stacks separately (a call that skips a push doesn't count): tttt overflows at least as early.
- `this` types: a reference with a this-argument relates structurally.
- Alias variance (`in out` annotations): the reporting pass falls back to the interface's own variances on an
  invariant failure only for two references to the same target. The null-base TS2417 check takes the first `extends`
  of the first merged interface, not getBaseTypes.
- `TY_INDEXED`'s IncludeUndefined (noUncheckedIndexedAccess) is dropped wherever an indexed access is rebuilt from its
  parts (`simplified_parts`, `ty_actual_type_variable`).
- **Every anonymous object type has an inferable index** (`has_inferable_index`): tsc's isObjectTypeWithInferableIndex
  wants a symbol and excludes types the checker makes without one. tttt only knows `anonymous.no_inferable_index`
  (`OBJECT_NO_INFERABLE_INDEX`) and the global empty object; other symbolless synthetic types (spreads? reverse mapped
  types?) may wrongly relate to index signatures.

- relate.c's tuple-to-tuple branch relates the number index signatures of tuples of different targets (tsc's
  indexSignaturesRelatedTo after propertiesRelatedTo) only while a class's `extends` expression is evaluated
  (`Checker.base_constructors_in_flight`): the relation is implied by the elements, tsc's only observable effect is
  relating a source element to another position's target element, which can need the class's bases (TS2506, effect's
  TaggedClass). Done always it cost valibot 1.4%.
- `ty_resolving_cycle_across_calls()`: bases re-entered below a call's resolving_start are marked circular from their
  entry up, standing for tsc's second round (which re-enters, then finds the cycle above its resolutionStart). Only
  for RESOLVING_BASES; the other kinds keep the barrier.

## Type core (`src/ty/`)
- The eager-base runaway model (`probe_eager_bases()`, documentation.md "Eager base runaways") is fidelity to a tsgo
  runaway, with approximations: (1) probes only run when the reference's arguments structurally mention a type whose
  members are in flight or whose bases mention itself at depth >= 2 (`mentions_declared()`: valibot's
  `BaseIssue<T> extends Config<BaseIssue<T>>` at depth 1 is left out, probing those cost valibot 0.6%); (2) the guard
  is "the resolving stack is unchanged since the re-entered members started", with `RESOLVING_MAPPED_CONSTRAINT`
  pushed around a mapped type's constraint instantiation and key expansion for tsc's key-parameter base constraint
  (zod's recursive-types.test.ts is where tsgo stops at that guard and tttt otherwise reported 3 spurious TS5115);
  (3) tsgo's poisoned caches after the runaway (error types) aren't modelled (vue's two TS7006, missing_checks.md).
- Class and interface statements' errors at the current node go at their name (`CurrentNode.error_span`, tsc's
  getErrorRangeForNode); other declarations (functions, enums, type aliases, namespaces) still at the whole statement.
- A type parameter source against a union target takes its hoisted constraint (`constraint & T`, tsc's
  getEffectiveConstraintOfIntersection in structuredTypeRelatedTo) only when that relation is a definite True
  (`structured_related_impl()`, TY_TYPE_PARAM): tttt relates a generic intersection to a union by its members, so
  `string & T` meets the outer T relation in flight as Maybe, which tsc's whole-intersection comparison never does.
- Function expression types are instantiated by what their resolved signature mentions (`ty_function_type_mentions()`,
  a `mentions_type_variables` collect walk), where tsc keys by the outer type parameters: tttt's contextually typed
  parameters can carry a call's type parameters (plan 51), so the syntactic set isn't safe. Asked before the signature
  is resolved and the return type inferred (or when the walk is unsure, or two function types mention each other),
  it falls back to keying by the whole mapper, which made docmost 2.2M types. Object literal types go the same way
  (`object_literal_type_mentions()`, conditional.c), once every property type is known, except a literal with a
  getter: whether its mentions are known changes when the getter resolves, so one literal has instances of both kinds
  and zod's recursive-types.test.ts depends on which meet (a related probe fails on ai-main already: perf_leads.md,
  agent A's leftovers).
- `name_type_filters()`/`peek_cond_branch()` (`ty/members.c`): whether a mapped type's `as` clause filters or remaps
  (tsc's getMappedTypeNameTypeKind, deciding whether properties keep their declarations and stable order) is told by
  the clause's shape (each union member / conditional branch is the key, never, or an intersection with it), not by
  relating it to the key: relating, or lowering a self-referential branch, reaches the members being resolved
  (mappedTypeAsClauseRecursiveNoCrash1). A pending lazy branch counts as remapping.
- Stable type ordering (`ty/order.c`, tsgo's compareTypes): ties that still fall back to tttt's ids: roots not
  lowered from a node (no `outer`), deferred references whose node has no cheap position (`type_node_start()`:
  parenthesized or keyword element types), two nodes starting at the same token (`(A extends B ? 1 : 2) extends C ?
  ...`), unique symbols of one name without positions, intrinsic variants, same-symbol anonymous types
  with equal mappers.
- Pseudo types (`src/ty/pseudo.c`, plan 55): tsgo walks parents for isContextuallyTyped/IsInConstContext, tttt passes
  them down from flags the resolve walk sets (`ObjectExprItem.pseudo_contextual`/`pseudo_const`,
  `FunctionImpl.syntactically_contextual`); `pseudo_of_variable()` is always given a non-contextual place (a variable
  inside a callback is contextual to tsgo). Const-ness when printing (`Pseudo.in_const`) is syntactic only, where tsgo
  asks the checker's isConstContext (equal in practice: post-inference contextual types aren't const type variables).
  `pseudo_enter_signature()` treats an instantiated signature's own type parameters as fresh (tsgo clones them), by
  mention, not by identity. `reused_node_length()` counts whitespace before a reused node, tsgo's range also has
  comments. `pseudo_member_prop()` finds a member's property by declaration span, not by name. `pseudo_type()` of an
  Inferred expression whose type is a fresh object literal is null (never equivalent): tsgo re-checks the expression and
  gets a new type, tttt interns literal types by shape, so this is decided by the flag rather than by identity.
- Declaration emit's writer (`declaration_emit.c`, plan 64, the `emit_*()` functions): a second walk with its own
  per-node-kind functions next to the diagnostics walk's `check_*()` ones, sharing visibility, painting and
  `serialize_*()`: what the two decide about members (privates, overloads, dynamic names, late bound index members:
  `late_index_members()` serves both) can drift apart. Known gaps: an index signature's extra parameters are dropped;
  the unique-name generator is approximated (`file_has_word()`: a whole-word search of the file text, tsc keeps the
  set of identifiers the scanner saw; `undefined` is special-cased as resolvable in `emit_expando_namespace()`);
  `w_literal_value()` rewrites `(typeof E)["x"]` text into `E["x"]` after printing the type; the leading comments of
  a statement are written first and taken back if nothing follows (`w_leading()`/`w_unless_written()`), a class's
  `_base` variable is inserted before them (`Walk.statement_start`). CRLF output is a pass over the finished text.
  Import type options are copied from the source with brace spacing normalized (`display_node_identifier()`, invalid
  ones `{ with: {} }` by a text check, `import_type_options_valid()`). Preserved `/// <reference path>` directives keep
  their relative path with the extension changed (tsgo makes it relative to the output directory). The harness
  matches tttt's files to the baseline's by output name (not tsgo's emit order) and doesn't do `@fullEmitPaths`.
  A cloned template literal type (`display_pseudo()` PSEUDO_STRING_LITERAL) escapes tabs and CRs in the source text
  rather than reprinting the cooked value.
- Declaration emit's comments (`ty/decl_comments.c`): tsc's printer decides by node positions (`pos`, `end`,
  containerPos) on synthesized nodes; tttt calls the comment functions at the places it knows a node starts or ends
  (`span.buf`, span ends trimmed back over trivia by `decl_comments_end()`), so a node kind not hooked writes no
  comments (union and tuple members, whose TypeNodes have no span; type-level trailing comments like
  `a: string /** c */;`). The comment ranges come from what the lexer hopped (`SourceFile.comments`, only recorded
  where declarations are emitted): backtracking may record a comment twice (deduplicated by start) and the JSDoc type
  parser's hops aren't recorded. TyDisplay prints a written type literal's accessors and an object type's class
  accessors as `get`/`set` only in multiline (declaration emit) mode, `TyDisplay.transformer_node` approximates what
  tsc's declaration transformer visits (written annotations) against what its node builder reuses.
- isolatedDeclarations reporting (`declaration_emit.c`, plan 55): tttt has no parents, so `locate()` finds a
  reported node by walking the declaration being serialized along the pseudo builder's descent (for
  findNearestDeclaration and createExpressionError's parent); a node the printer reports from another declaration of
  the same file gets no related information. `print_part()` prints an Inferred part as `ty_pseudo_type()` (tsgo:
  serializeTypeForDeclaration of the node's declaration); NoResult parts print nothing (their TS4xxx would be missed).
  Printing stops at truncation (TS7056), so properties past it don't report (tsgo's do: monkeytype contracts).
  A destructuring declaration's RequiresWidening is TYF_FRESH_OBJECT | TYX_CONTAINS_WIDENING.
  `interface_declares_method()` matches a merged interface's method by name only (tsgo: getSignaturesOfSymbol).
  `declared_item()` (ty_display.c) follows mapped_from and mapped_intersection to an object literal member; some deeper
  chains (spreads of zod shapes, knip's schema/configuration.ts) still lose it.
- Member order (`builder_sort_stable()`, tsc's getNamedMembers): object literals, mapped types, namespaces and merged
  values only; class/interface members keep tttt's own-then-inherited order, `builder_prototype_last()` moves a static
  side's `prototype` last. Props without a declaration sort by name bytes (tsc: escapedName, then symbol id).
- `KEY_AUTO_FLOW_ISSUE` (`identifier_flow_type()`): TS7005/TS7034 for a reference whose flow type was still auto in
  a loop's incomplete pass (tsc reports from checkExpression there), remembered per expression.
- `UnaryOpExpr.no_type_params_in_scope` approximates getResolvedSignature's instantiation for `yield` (TS7057): only a
  bare type parameter counts as a call's, a contextual `T[]` is taken as is.
- Union origins (`Ty.composite.origin`): not instantiated, dropped by union-mapping helpers (plan 37 B stage 2).
- `ty_realias`: a renamed object type is a second `Ty` (members resolved twice); renamed deferred types are copies. No
  "local alias" exception. `renames` in `lower`: whether an alias instance keeps the name is syntactic.
- Through an import (`lower_identifier()`'s `through_import`), a generic alias's instance takes the import's alias like
  tsgo's getTypeFromTypeAliasReference only for a union/intersection, deferred conditional, indexed access or deferred
  reference body. Not for an instantiated anonymous or mapped body (tsgo's getObjectTypeInstantiation keys those on the
  alias too): realiasing them made arktype's `finalizeTree` instantiations run away into 3 spurious TS5114 (2026-10-14,
  not investigated), so `import { L }` with `type L<T> = O<T>` still is the O<X> type there.
- Conditional types (`conditional.c`): deferred conditionals don't keep their inferences (`inferred_true_branch()`
  recomputes); `ty_implied_infer_constraint` lacks the mapped-extends rule. Permissive/restrictive instantiations only
  replace what `mentions_type_variables` finds (8192 types / 48 deep, then "unsure": defer; it resolves signatures tsc
  never would, `callable_definitely_related()` short-cuts the two "is it a function" idioms, bugs/type_core.md), since
  TYF_COULD_CONTAIN_TYPE_VARIABLES is on every function/object literal; an object literal getter whose return type is
  in flight counts as unsure (asking re-infers it below a call's barrier: zod's recursive getters became TS7023).
- `instantiate_conditional`: outer parameters from `collect_free_params`, falling back to the uncached full-mapper path
  on `TyConditionalOuter.unknown`. `collect_free_params` gives up on `typeof`, a non-class `this`, parameter
  initializers, bodies without a return annotation, JSDoc callbacks. `TyMapper` is flat.
- Substitution types: the true-branch chain is `TypeNode.flow` from the resolve walk; no `number | ${number}` mapped
  key rule.
- Mapped types: homomorphism (`homomorphic_variable`) needs a syntactic `[K in keyof X]`; `expand_mapped` expands once
  keys are literal unions, modifiers only for written `keyof T`; `mentions_mapped()` (ty_display.c) compensates for
  instantiating member signatures that can't mention the parameters (bugs/display.md).
- `expand_mapped()` (members.c HACK): a homomorphic mapped type over `T extends A | B` gets the union's properties but
  not its index signatures (tsc gives both; they cost valibot 3.6% because `T[string]`'s constraint comes out unknown).
  Real fix: that constraint (`T[string]` of a union of index-signature objects is the union of their values).
- `class_base()` (members.c HACK): base class names found syntactically; other expressions take the first fitting
  construct signature (TS2510 by `ty_class_base_constructors_differ()`). A base constructing `any` is no base;
  `ty_has_unresolved_base()` gates TS2339/TS7053 instead (tsc: `[x: string]: any`). Type-parameter bases take the
  constraint's members at declaration. `ty_non_augmenting_base()` from syntax.
- A bare `C`/`this` inside generic `C` isn't its own instantiation (lower.c TODO).
- `never_reduced_prop` (checker.c) (permanent: perf, judging them with `is_known_object` costs zod +4.1%, drizzle
  +0.9% instructions): `ty_reduced()` (getReducedType) doesn't judge members flagged `TYF_COULD_CONTAIN_TYPE_VARIABLES`
  unless `is_known_object_for_reduction` (a written type literal mentioning no type parameter); tsc looks at every
  member.
- `primitive_constraint_of` (checker.c HACKs) takes a deferred type in a type parameter's declared constraint as no
  primitive (permanent: perf; asking its base constraint like tsc costs valibot +6% instructions), and an indexed access
  into a conditional type with generic branches too (`NonNullable<ComponentPropsWithRef<C>["ref"]>` in @types/react:
  its base constraint distributes over every intrinsic element, react +15%).
- Generic tuples: `base_constraint` replaces only variadics with array constraints; `lower_tuple` keeps the written
  `T[]` for rests (tsc: `T`).
- `lower_identifier`: `object` is a builtin declaration named "object".
- `builder_add_interface_member`: index signature types lowered eagerly; non-literal computed keys dropped (tsc:
  late-bound index signatures).
- Cross-product caps give the error type silently: `normalized_tuple` (10k).
- `apply_mapping`'s placeholders step code points (JS: UTF-16 units). `${symbol}` templates are silently `string`.
- Unique symbols: escaped names (`__@sym@12`) mapped back through `ty_unique_symbol_of_name`, so a string property
  `__@x@3` passes as a symbol; `instanceof_target_ok` matches `__@hasInstance@<id>` by text; one unique symbol per
  declaration (tsc: per merged property), so `check_subsequent_property` (check.c HACK) skips TS2717 between two.
  (`SymbolConstructor`'s `symbol` properties being unique, interface_decl.c: permanent, tsc does the same.)
- Private names (`ty_private_name`): the lexical lookup scans class items by text (`class_decl_find_private_name`);
  an undeclared one looks up the plain `"#x"` atom. Object literal accessors have no `write_node`
  (`prop_decl_rare()`).
- `Checker.current_node` (TS2589/TS2615/TS4110/TS1062 location): `lower()` makes a generic reference current while
  lowering it (a guess at tsc's order). `Foo<X>` isn't lowered in the walk (resolving every alias changed type ids).
- Instantiation limit: what an exhausted budget produced is memoized like tsc's (`test/probes/instantiation_limit/`),
  but tttt instantiates more eagerly than tsc, so a tttt-only TS2589 (variance measurement, eager members) poisons what
  tsc would still compute. A cut-short conditional instance is re-reported only within its epoch.
- `lower_node()` TYPE_VALUE makes a literal's fresh twin, which nothing needs (permanent: perf, unexplained; without it
  the tsc corpus costs 1% more instructions in union_with_origin).
- Deep nesting identity (isDeeplyNestedType's recursion identity) uses ObjectFlags.FromTypeNode only for array/tuple
  nodes, not generic interface references (commit "Arrays and tuples written as type nodes are their own recursion
  identity"): a partial port.

- `item_is_literal()` (`ty_display.c`): an object literal property whose value is an array/object literal isn't printed
  from its syntax in the literal's unwidened type. tsgo's array literal types carry ContainsObjectOrArrayLiteral and
  are other types than their widened copies; tttt interns tuples and arrays without a fresh twin, so it's told
  syntactically.
- `OBJECT_WIDENED_COPY` (`widen_nullable_copy()`, members.c): only the non-strict nullable widening of a union remakes
  unchanged object literal members (tsgo's getWidenedTypeOfObjectLiteral remakes every one it widens, which orders
  the union by creation); the strict-mode object widening doesn't, and tttt's structural intern otherwise hands the
  original back.
- `declared_inside()` (`instantiate_anonymous()`): an object literal/function expression type whose mentions are
  unknown drops mappings of type parameters declared inside its span, standing in for tsgo's outer-type-parameter
  key; anything else tsgo's syntactic isTypeParameterPossiblyReferenced filters is still in the key.
- `ty_display()`'s depth safety net: 1024 where nothing truncates (tsgo has none but the 1M length), 128 in
  declaration emit, 44 in messages. A runaway print still stops; a genuine deep alias argument list beyond 1024 would
  print `...`.

## Checks (`ty/check.c`, `unused.c`, `statement.c`, `class_decl.c`)
- `check_duplicate_declarations()` (TS2300 for a property declared again) reproduces a tsgo checking-order artifact:
  in a class, a member before the first late-bound one counts its early symbol's declarations only
  (`MemberEntry.early_total`: tsgo's getSymbolOfDeclaration before late binding merged anything), keyed on the
  `private_names` parameter, which only the class check passes. If something in tttt ever resolves class members
  before this check the way tsgo might, revisit.
- `checker_defer_function_body()` (ty/checker.c): a function expression's body is queued from its first check
  only when it is in the file being checked. tsc's checkNodeDeferred also queues one checked from *another* file into
  that file's set before the file is checked, so those come first: excalidraw's bounds.ts under
  noUncheckedIndexedAccess (flagflip excalidraw/nuia) has getArrowheadPoints queued by an earlier file, and its once-per
  type TS2488 lands at 818 in tsc, at 943 in tttt (getLinearElementRotatedBounds is queued first, from a call at 175).
  Needs per-file pending queues per checker, and is only exact at `--checkers 1` (other checkers' evaluations aren't
  seen): decide with checkerdiff in mind.
- `source_file_check_verbatim_exports()` (TS1287) finds the `export` keyword by scanning the statement text
  (`export_keyword_of()`): statements don't keep their modifiers' spans.
- `ty_check_static_heritage()` relates to a synthetic object literal (tsc's getTypeWithoutSignatures, which keeps the
  class symbol and so prints `typeof Base`); `Checker.display_of`/`display_as` make messages print the named static
  side for it while explaining. A synthetic anonymous type that remembers the symbol it prints as would replace it.
  It also adds `& K` to the source (HACK) because `ty_static_type_of` doesn't make `X & K` bases' static side
  `typeof C & K`.
- `check_access` (TS2339, HACKs): lets through generic mapped types whose members `expand_mapped()` found none (a
  union constraint's common properties for `Mutable<T>` with `T extends A | B`, `Pick<T, K>` with K generic; tsc
  reports what resolveMappedTypeMembers doesn't find) and unresolved bases; bracket access never reports; fires on
  modules too (wrong resolution shows as TS2339 next to TS2307). `ty_check_element_implicit_any` (TS7053, HACK) skips
  the same.
- `ty_check_index_constraints` on type literals only when syntactically needed (`type_literal_has_index_constraints()`):
  the others aren't lowered at their check, and an alias's literal body is made as the alias's type first (else it
  loses the alias name).
- Syntactic stand-ins: overload grouping (`statements_check_overloads`, `class_check_overloads`), TS2729
  (`initialized_in_static_blocks`, HACK: top-level `this.x =` instead of flow), member kinds in
  `class_check_member_overrides()` (`class_instance_member()`, not symbol flags; TS2612 skips what
  isPropertyInitializedInConstructor decides), `check_property_modifiers` (TS2687, HACK) skips methods and accessors.
- `ty_check_class_duplicate_members()` (HACK): a late-bound member name (`[k]`, `[Symbol.x]`) conflicts with what the
  member table has (the static `prototype`, tsc's source of that TS2300 unknown) without joining it; tsc also reports
  an early `a` against a later `get [k]` (k = "a") at both. `MemberTables` lookup is a linear scan.
- Emit helpers (`ty_check_emit_helpers`): spans by hand (`decorator_span()`, `function_error_span()`); "ambient"
  approximated per site (`ClassDecl.is_ambient`, `Binding.ambient`, `BlockTypecheckCtx.ambient`; want
  NodeFlags.Ambient).
- `unused_mark_jsdoc_links()` (HACK): `{@link X}` is a use by text scan, looked up from the file's scope (real fix:
  resolve them from parsed tags). Member reads are a table keyed by declaration span start
  (`ty_mark_member_referenced`).
- `program_apply_comment_directives`: suppression by tttt's error *phase*, not tsc's diagnostic kind; `\n` and ASCII
  `\s` only; relies on every comment going through `hop_comment`.
- `SourceFile.unchecked` + `error_ctx_remove_unchecked`: the skipLibCheck / noCheck / JS gate filters after checking,
  so skipLibCheck'd declaration files are checked in full for nothing (tsc's skipTypeChecking doesn't check them: a
  perf lead, as long as checking them first doesn't change what the checked files see). Plain JS is checked in full
  and filtered to plainJSErrors (as tsc).
- `error_ctx_global_error_s`: file-less diagnostics have a null span, sorted first; `error_ctx_error_s` drops invalid
  spans (permanent: tttt's diagnostic representation)
- `ty_check_destructured_member` (check.c HACK, `from_this`): `({ m } = this)` inside method m isn't a read of private m
  (tsc's markPropertyAsReferenced with rightIsThis); "inside" is by position, so a function nested in m counts as m.
- `EMIT_HELPER_EXTENDS/ASSIGN/GENERATOR/VALUES/READ/SPREAD_ARRAY/MAKE_TEMPLATE_OBJECT` are never requested any more
  (their checks were ES5 / downlevelIteration only); `helpers.c`'s table is indexed by bit, so they stay in the enum.

## JavaScript and JSDoc (bugs/js.md)
- A `@returns`/`@throws` type is only resolved in tsc (never checkSourceElement'd), but `function_impl_typecheck`
  runs `type_typecheck` on it, skipping just the constraint check (`IdentifierType.jsdoc_unchecked` in
  `identifier_type_check_arity()`). Real fix: a resolution-only walk (arity errors must stay:
  jsdocArrayObjectPromiseNoImplicitAny).
- TS8023 (`ClassDecl.jsdoc_augments`) only compares a tag whose rightmost name differs from the clause's; tsgo
  compares every tag's type with the base type (`@augments A<string>` on `extends A<number>` would be TS8023 too).
- JSDoc is consumed at parse time, not stored: `jsdoc_before()` re-parses the last comment before the host into
  transient `JsDocTag`s (parse errors kept for JS files through `ParserShared.jsdoc_errors`).
  `jsdoc_bind_comments()` parses every `@typedef`/`@callback`/`@import` comment again and claims it by innermost
  statement list (claims from failed attempts stay); `@import` only at list top level; dotted typedefs
  skipped; typedefs sharing a comment share its templates.
- `@type` on assignments reaches the symbol via `SymbolDecl.jsdoc_type`, threaded through every assignment-declaration
  site in scope.c (new sites must pass it; chains: outermost only). `jsdoc_apply_type_signature()` *moves* the
  TypeNodes of a written function type. `Statement.expr_jsdoc_type` shares a union with `expr`: every site building an expression
  statement must clear it.
- No `SymbolFlags.Assignment`: TS2403 looks for `SYMBOL_DECL_EXPANDO`. A function's or JS class's expandos are marked
  by a nameless `SYMBOL_DECL_EXPANDO` (`mark_expando_host()`) whose decl flags say "its value has the members"
  (value_entries() merges them in); the symbol gets no namespace meaning (like tsgo, which only adds exports), so
  namespace lookups through an alias also accept a target with expandos (`symbol_namespace_member()`).
- Expandos: statement level in the declare step, nested in expressions via the resolve walk
  (`scope_add_nested_expando()`: `f.x.y = v` doesn't see a nested `f.x = {}`). `scope_add_expandos()` (in
  `block_add_names()`) and `scope_add_this_property()` write file-level symbols from the resolve walk (want per-file
  lists merged after); only expression statements and `var` initializers are scanned. `Symbol.late_bound_key` keys
  `this[k] = v` by source text.
- CommonJS binding: the module indicator is syntactic (`saw_commonjs`, failed attempts too); exports are found by a
  whole-file `ast_walk_statement()` in `scope_add_commonjs_module()` (only for CommonJS JS files); `require()` is
  parser-synthesized `PortItem`s (flat patterns only); `readonly_assignment()` never reports `exports.x =`. Exported
  class expressions alias the class (`scope_reparent`, top level only: nested ones are properties).
  `exports.x = undefined` then `exports.x = a` (moduleExportDuplicateAlias*): tsgo makes every one an alias, tttt a
  property + alias symbol; an alias declaration keeps its value (`SymbolDeclJs.commonjs_value`) for the union. `module.exports` of the `module` variable gets its symbols line from a span override in
  the traversal (`EXPR_PROPERTY` case: tsgo's `exports` member symbol of `module`, which tttt doesn't make).
- `export =` next to other exports: tsgo 7.1's model for every module (`bind_type_exports()`, tsgo's
  bindCommonJSTypeExports: the type/namespace exports move onto the `export=` symbol, which becomes a namespace and
  stops being a pure alias), but run at link time (binder_link()) rather than per file in the binder, so the TS2309
  check before it (`export_equals_shadows_namespace()`) recomputes whether the promotion will happen. A named import
  from such a module drops the module's own exports with a value meaning (tsgo's getExportsOfModuleWorker) in
  resolve_alias_target(), not through module_resolved_exports().
- Package deduplication (`program_load_resolved()`): the second copy of a package (SourceFile.package_redirect) is still
  parsed and bound with its own AST (tsc's redirect file has the first copy's AST and binds nothing), only unchecked;
  its imports share the first copy's resolutions by path text (`program_share_redirect_ref()`), falling back to its
  own resolution when the copies differ or the first is still loading. Its globals (`declare global`, `export as
  namespace`, script declarations) still merge.
- Named class expressions a CommonJS module exports are named by their own name inside their body only through a
  thread-local the baseline traversal sets (`symbol_chain_enter_class()`): error messages name from the file.
- tsc reports a member's widening errors (TS7008) only when something resolves its type,
  `BaselineTestCase.any_keeps_rendering` leaks to the next line if no types line follows.
- Symbols lines: container from the traversal or `Prop.parent`; `collect_prop_symbol()` counts a non-object union
  member as lacking the property only for binding patterns: the object of a property access is sometimes not narrowed
  as far as tsc's (doing it everywhere loses 70 symbols components: optional chains, logicalAssignment*, nonPrimitive*).
  Span-keyed overrides (`this_property_symbol_overrides()`) stand in for getSpecialPropertyAssignmentSymbol. JSDoc tag Decls get no leading
  trivia (sniffed). A module's `default` export symbol is named like its first declaration.
- JS class member merges (`this.x` into class-body fields) only append declaration spans to the checker's `Prop`
  (`builder_add_assignment_members`); the harness reads them back in `this_property_symbol_overrides()`. Enum
  members take their symbol's spans the same way. `append_decl()` sniffs a JSDoc typedef's dotted name (after `}` or
  `@typedef`) to drop leading trivia. `symbol_declaration_spans()` drops a JSDoc namespace declaration at the same start
  as another declaration (tsc: the same identifier node). `KEY_UNDEFINED_PROPERTY` (members.c) only feeds declaration
  spans, per checker.
- Class members aren't `Symbol`s, so tsc's member-table merges are rebuilt for the symbols baseline: a field and the
  parameter property/interface member of its name take the class type's `Prop` declarations
  (`class_prop_declarations()`), and a class/interface type parameter with an instance member of its name is one
  symbol through `BaselineTestCase.merged_declarations` (`register_type_parameter_member_merges()`, keyed by the first
  declaration span, registered when the class is walked: a use earlier in the file misses it). The checker doesn't
  merge them (nothing it checks is known to depend on it).
- tttt's binder splits symbols tsc's has as one, and the symbols baseline merges their declarations back
  (`baseline_symbol_declaration_spans()`): a module/namespace local with the exported declarations of its name in its
  block (tsc's declareModuleMember local symbol), a function's type parameter with a parameter/local of its name
  (`Scope.type_params_function`). Computed member names are grouped by `computed_member_symbol_items()` /
  `object_member_name()` (late-bound names through the checker's key type). A union/intersection property's symbol
  dedupes its members' properties by (first declaration, mapper), treating an instantiated property whose type equals
  the declared one as uninstantiated (tsc's instantiateSymbol keeps a symbol whose resolved type has no type variables:
  lazy in tsc, so tsc may differ when the type wasn't resolved yet).
- `unknown_symbol_text()` (baseline_test_traversal.c): the symbols baseline names tsc's unknownSymbol (names after an
  import of an unresolved module, in `import x = a.B` paths and heritage `extends a.B`) by the first alias of an
  unresolved import in the innermost table, rebuilt in the harness: tttt has no unknownSymbol to run the symbol chain on.
- `ty_report_enclosing()` / `statement_scope_at()` (`ty_display.c`): tsc prints a relation message's type from its
  value declaration when that is a non-context-sensitive expression (getTypeNamesForErrorDisplay), which turns on
  annotation reuse and qualifies names from there. tttt types don't know their expression: a function expression is
  printed from its first parameter's scope (none: the file), an object literal from the innermost namespace / function
  declaration body / block around a property written in it (function expressions aren't entered), a literal of spread
  properties only from nowhere (tsc: `typeof stuff.klass` for a spread namespace member). Context sensitivity of a
  literal is guessed from its members' types. Real fix: the type of a literal or function expression knowing its node
  (or a scope pointer stored on the Expr by the resolve walk).

## Harness and tools
- `identifier_expr_compile_test_info`: a name resolving straight to a file's module symbol is `exports` (declared by
  the whole file) only when its text is `exports`; any other such name (a UMD global merged into its module) is shown
  like any symbol. Decided by text because nothing else marks the CommonJS `exports` reference.
- `namespace_decl_compile_test_info`: an ambient module's name uses symbol_text() (an alias in scope, `import * as Foo`)
  unless that comes back quoted, then the quoted path. A quote test on the printed text, not a chain query.
- Conditional tail recursion (`ty_instantiate_tail`) is C recursion where tsc loops (getConditionalType's tailCount):
  1000 steps need ~16 MB, so `main()` raises the soft RLIMIT_STACK to 64 MB (`raise_stack_limit`, workers get
  `worker_stack_size`); a hard limit below that still overflows. Real fix: a loop.
- `initialized_in_static_blocks` (check.c): tsc's isPropertyInitializedInStaticBlocks asks control flow at the end of
  each block; this looks for a `this.x =`/`C.x =` statement syntactically (any static block before the use counts for
  an any/unknown property, whose flow type never has undefined).
- Deliberate harness/tool simplifications (permanent): `apply_setting` ignores emit-only options (`@baselineFile`,
  `@fullEmitPaths`) and bad values (tsc's harness refuses those tests); `mount_test_lib_folder` copies tsc's
  `tests/lib` per test and `baseline_test_cache_get_program_with_libs` keeps one lib `Program` per lib list, never
  evicted; `@noTypesAndSymbols` counts symbols/types as passed; `TTTT_IGNORE_UNDERLINES=1` is measurement only;
  `engine.c` (reads `test/baseline_results.ts`) has no let/const/var distinction, drops array property assignment,
  `value_loose_eq` stringifies; `failstats.py`'s `line_ratio` is multiset overlap, not a diff. Commits
  f3b836b..f899e4e don't compile (fixed by 7f2a7fa): skip when bisecting.
- `may_have_readonly_member` (check.c): a perf gate before writing through a union of keys, exact where it answers
  false, conservative (true) for unions, intersections, tuples and callables. (permanent: perf, not an approximation)
- The CLI is `tsc --noEmit` by default (`run()` in main.c adds `--noEmit true` unless the command line names noEmit;
  not for `-b`, which is tsc -b's and emits). `scenarios.py` drops the emit-only codes (`EMIT_ONLY_CODES`:
  TS5011/5055/5056) from tsgo's expected output of emitting commands.
- `TTTT_TYPESCRIPT_VERSION` (main.c, `--version`) is hand-written: "7.1.0-dev", what the pinned tsgo prints. Bump
  with the oracle.
- `declaration_diagnostics_wanted()`: JavaScript files only get the full walk with isolatedDeclarations
  (TS9010/TS9013...); otherwise only their CommonJS exports' declarations (`check_js_commonjs_exports()`, three
  shapes serialized as variables, `Emitting.name_not_node` for `exports.x = e`). tsgo reports TS4xxx for all of JS
  declaration emit, but the walk doesn't know JSDoc namespaces/typedefs (jsDeclarationsImportAliasExposedWithinNamespace
  got a spurious TS4078 with JS enabled).
- `resolve_with_signatures()`'s `super(...)` TS2346: JavaScript never reports it (superCallInJSWithWrongBaseTypeArgumentCount*
  say so), though tsgo's resolveCallExpression filters the base constructors by the clause's type argument count
  without a JS exemption. Not understood why tsgo's resolveCall reports nothing there.
- `SourceFile.expando_containers` (JS `{}` containers, read by `check_object()`): one entry per (literal, binder), the
  checker takes its own binder's. A suite run never re-binds a literal under another binder (instrumented 2026-10-09:
  only the same host again), so the per-binder key is belt and braces; it assumes no two programs bind the same
  SourceFile concurrently (the array is realloc'd by the binding walk).
- `recursion_identity()` (relate.c) / `reference_from_type_node()` (lower.c): tsc's ObjectFlags.FromTypeNode is only
  ported for array and tuple type nodes; generic interface/class references written as nodes keep the target identity
  because tttt's signature relations don't instantiate fresh references per level (performanceComparisonOf...Signatures
  would hit TS2589). Also order-dependent like tsc's (first creator of the interned reference wins).
- Distributed type parameters (TS 7.1, `ty_distributed_type_param()`, IdentifierType.distributed set in type.c's
  TYPE_TERNARY lookup): tsgo distributes the conditional's own check type node too; tttt keeps the original there,
  because its many `conditional.check ==` identity tests (mapper sources, outer params, nested-conditional tails)
  broke 12 tests otherwise. What that would have given is emulated: conditional_source_related() relates the
  branches with the twins mapped back (tsgo's first distributive constraint T' -> T), instantiate_conditional() keeps
  identity mappings of params that have a twin (tsgo maps every outer parameter), and the identity relation treats a
  twin as its original (also inside a substitution) since tttt's merged declarations have per-declaration params.
- `ty_tuple_target()`: every tuple target's element type parameters are the first n of `Checker.tuple_params`, one
  list shared by all targets (tsgo makes fresh ones per target; type-fest's IntRange made 4.5M of them). Sound while a
  target's parameters only appear in its own generic members, mapped by that target's arguments: a type mentioning two
  targets' raw parameters at once would conflate them. Element props share the read-only `ty_prop_decl_value_name`
  (writing to it faults: `prop_decl_shared()` is the check before giving a prop a declaration of its own).
- `expand_generic_intersection_homomorphic` (members.c) only carries index signatures over from a part that is itself
  a generic homomorphic mapped type (`Nullable<Vals<K>>`, nuqs). tsc's resolveMappedTypeMembers takes every part's
  (forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType of the modifiers type: `S & object` of a conditional's true
  branch, `Pick<X, keyof X>` too), but doing so changed localTypeParameterInferencePriority's inference candidates
  (an extra `Pick<UnrollOnHover<S>, ...>` candidate): something downstream treats the extra index signature differently.
- `never_index_read` (checker.c): an anonymous object indexed by never is never, members unresolved (tsc reads an
  index signature: `{ [k: number]: X }[never]` is X). Taking it made xstate's recursive StateId (`Values<{ [K in never]:
  StateId<...> }>`) hit TS5115 under nosnc/sloppy flagflip cells (+7): resolving those keyless mapped types' members,
  even ty_mapped_info_unexpanded's constraint of the instantiation, instantiates enough to tip the circularity
  heuristic. Only a mapped type whose keys its declaration tells (a primitive, or a type parameter the instantiation
  maps to string/number/symbol: `Record<string, T>[never]` is T, es-toolkit's sampleSize) reads its index signature.
  Type literals with index signatures still don't. Fix properly by making the TS5115/instantiation accounting not
  depend on such member resolution, or a mapped type's template lazy when its keys are none.

## Content mappers (`content_mapper.c`, documentation.md "Content mappers")
- `alias_original_names()`: tsgo substitutes an alias segment's original text for the *arguments* of a diagnostic
  that exactly covers it (displayMessageArgs); tttt's messages are formatted already, so the quoted virtual name is
  replaced in the message text. Wrong if the name appears quoted for another reason.
- `span_cmp()` compares a content-mapped file and its original text (ContentMapped.original, same path) by offset:
  tsgo sorts the mapper's (original-coordinate) and the compiler's (virtual-coordinate) diagnostics of a file together.
- `program_parse_ahead()` leaves content-mapped files to the loader (transformed in program order) so the failure budget
  (TS18070 after 5) is deterministic; tsgo counts in goroutine order, so with 6+ failing files which 5 it reports
  differs from run to run (tttt's are the first 5 in program order). Costs the transforms their parallelism.
- `drop_diagnostic_kind()` (main.c) recognizes a mapper's program diagnostics (TS18099/TS18070 without a location) by
  code to keep them next to syntactic errors (tsgo's GetDiagnosticsOfAnyProgram appends them there).
- `append_compiler_options()` sends the options tttt has (booleans, target, module, moduleResolution, jsx, jsx
  factories), not tsgo's whole CompilerOptions JSON (paths, lib, strings...): a mapper reading those gets nothing.
- Not done: the `locale` initialize parameter, mapper protocol/stderr logging, declaration emit names for mapped files
  (tttt doesn't emit), `-b`'s tsbuildinfo `contentMapperIdentities` (tttt writes none).
