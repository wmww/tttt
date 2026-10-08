# Hacks
Suboptimal-but-working code, by subsystem. Each entry: where (function names), what is approximated, what the real
fix would be. Missing diagnostics and features belong in `bugs/*.md`, triage.md or a plan, not here. `grep -rn HACK
src` finds the code-side markers; every significant one has an entry here.

## Global state, memory, parallelism
- `reexec_with_huge_pages` (`main.c`) (permanent: 5-20% wall time until the AST has arenas that madvise like the
  checker's, `bugs/perf_leads.md`): glibc reads `GLIBC_TUNABLES` only at startup, so tttt re-execs itself with
  `glibc.malloc.hugetlb=1` (not under valgrind or `TTTT_NO_REEXEC`).
- `shrink_list()`/`list_push()` (`parser.c`) (permanent: memory for ~0.7% of nest's instructions, free once lists are
  built in a scratch stack and copied out exact, perf_leads.md "Memory"): finished parser lists realloc away their
  slack; the first push reserves one element, not the vec's 4.
- `identifier_expr_name()` (`expr.h`) (permanent: memory layout): an `IdentifierExpr` has no name of its own, it reads
  its `Expr`'s inner_span through `offsetof` (never copy one out of its Expr). Its rare pointers (`module_ref_`,
  `this_function_`, `evolving_index_`, `extends_entity_`) share a union picked by keyword/require_call/evolving_op; a
  new writer must keep those discriminants exclusive.
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
  binder, tttt could in declare). Replayed in program order: suggestion counting, expandos into a non-private host
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
- Once-per-program diagnostics (permanent: K checkers each report what tsc's one reports once): `Error.once_key`,
  `error_ctx_keep_first_once()` keeps the earliest file's. Tagged: TS2343/TS2807 (`ty_check_emit_helpers`, keyed by
  module) and TS2488 on a union (`ty_iterated_type_sending`, tsc's cached noIterationTypes: once per checker by a
  links key, across checkers by a hash of the message, which prints a union alike in every checker). A new "said
  once" checker memo must be tagged too.
- Per-checker caches assumed pure in the type ids (permanent: tsc caches the same): `ty_union` of two types
  (KEY_UNION_PAIR), `ty_intersection` without an alias (KEY_INTERSECTION_OF), flow's `assignment_reduced_type`
  (KEY_ASSIGNMENT_REDUCED). A result computed while something it reads is in flight (constraints in
  `remove_constrained_type_variables`, members for empty-object checks) is pinned. First suspects of an
  order-dependent union or narrowing bug: disable these.
- Each checker measures variances and builds relation caches on its own (permanent: K checkers), and a TS2589 count is
  charged to whichever node first needs them (tsc pays once): a TS2589 near the 5M limit could depend on `--checkers`
  (see also "Instantiation limit" under Type core).

## Parser
- `unique symbol` placement (TS1335, `parse_type_inner` and `expect_annotation_type`): checked by the parser through
  `unique_allowed_at`, not on the tree like tsc's checkGrammarTypeOperatorNode, and never in JSDoc, where tsc goes to
  the tag's host (a variable or property declaration allows it).
- Strings are UTF-8 (permanent: tttt's strings are UTF-8): a lone surrogate escape becomes U+FFFD
  (`append_code_point()`), so all lone surrogates are one string and `"\u{D83D}\u{DE00}"` isn't the emoji.
  `regex_check_literal()` works in UTF-16 to port tsc's scanner; a position between surrogates is the 3rd UTF-8 byte
  (`utf8_byte_utf16_units()`). A NUL byte ends the file (scannerUnexpectedNullCharacter1,
  scannerStringLiteralWithContainingNullCharacter1).
- Escaped identifiers (`a`) resolve by value through `name_cooked()` (span.h): a global registry of cooked names
  behind a mutex, keyed by where the raw name sits in a file with a `\u`, each lookup a linear scan of that file's
  cooked names (quadratic for a file with thousands). Symbol tables cook at every lookup (a `memchr` for normal
  names). Comparisons against literals (`span_equals(name, "constructor")`) stay raw (`bugs/escaped_identifiers.md`).
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
- JSDoc types outside JSDoc (`jsdoc_type_outside_jsdoc()`): `?T`/`T!` are a `TYPE_JSDOC` wrapper; `*`, `?`,
  `function(...)` report TS8020 without a node. Unnamed `function(...)` parameters (and `jsdoc_parameter_name()`) are
  `argN` spans into static strings (`span_is_written()`); position arithmetic on them reads garbage.
- Top-level await (`parse_file()`): the whole file is reparsed with the statements that saw an `await` identifier as
  await contexts (`ParserShared.await_statements`); module-ness guessed by `parsed_file_is_module()`, a copy of
  program.c's rules.
- `triple_slash_invalid_reference()` re-implements tsc's pragma regexes next to `parse_source_directive()`.
- `undefined` is a literal; `expr_lookup_names` rewrites it into an identifier when a non-global declaration is in
  scope (`Expr.undefined_assigned` for assignment targets). Should be an identifier resolved to undefinedSymbol.
- TS2574 in `type_typecheck()` (type.c, HACK) skips generic types other than type parameters. tsc's isArrayLikeType
  sees a homomorphic mapped type over an array-constrained `T[K]` as array-like (getResolvedApparentTypeOfMappedType
  in the relation); tttt's relation doesn't, and deleting the skip loses
  homomorphicMappedTypeWithNonHomomorphicInstantiationSpreadable1. Real fix: that apparent type in relate.c.

## Binder and name resolution (`scope.c`)
- `scope_release()` (permanent: plan 43's memory): walk-only scopes (parameter list, return type, block statement,
  type literal, interface `this`, computed name/decorator) are popped off the binder arena when still its latest
  allocation and no pending report (`PendingNotFound`, `PendingExportMerge`) took them. The proof is "everything that
  can point at a scope is allocated after it in the same arena": a new place that stores a `Scope*` outside the arena
  (AST, malloc'd lists) must block the release like the pending reports do. Check by poisoning (memset 0xA5) instead
  of popping.
- Scope chains are shorter than the AST's nesting: `if` branches that are blocks, loops, `switch`, `try`, `return`...
  get no block scope of their own (`branch_names_scope()`); a `declare` declaration in an ambient context gets no
  `scope_new_self()` scope unless unused checks are on; a JSDoc `@param` type flags the parameter list's scope
  (`jsdoc_parameter_type`) while it is walked instead of a scope of its own.
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
  `module_resolved_exports()`. `check_export_context_merges()` reads a declaration's explicit `export`/`declare` from
  the start of its `full` span's text (SymbolDecl has no bits for them).
- `block_is_instantiated`: a nested namespace is judged with its parent's `outer` scope.
- Class/interface members aren't symbols (`Prop` is the identity). `bind_member_symbols` + `class_decl_lookup_names`
  apply binder rules at lookup; computed keys' `name_string` is source text, so accessor pairs and overloads match
  computed entity-name keys by text whatever their type (tsc late-binds only literal/unique-symbol names, and `["a"]`
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
- TS7011/TS7018 reportability (`ty_function_widening_reportable`) reads the contextual signature before
  `ty_fix_contextual_signature()` (`KEY_CONTEXT_UNFIXED`, tsc's getContextualSignature: rename is plan 51 stage 3 item
  4) and `KEY_CONTEXT_WIDENING_ABSENT` (a first check under inference without a signature view, standing in for tsc
  computing the return type under resolvingSignature): plan 51 stage 3 item 1 decides it where the return type is
  computed. `ty_function_context_is_self` (TS7024) reads `KEY_CONTEXT_RETURN_SELF` (written by `infer_return_type()`)
  but still needs `KEY_CONTEXT_NONE` / `KEY_CONTEXT_FIRST_ABSENT` (stage 3 item 2).
- `infer_from_objects()`, a generic mapped target `{ [P in C]: T[P] }` (the homomorphic `T[K]` shortcut, standing in
  for reverse mapping): the source goes to T as if homomorphic unless C is a conditional (Omit's
  `Exclude<keyof T, X>`, where tsc infers nothing), intersection-limited or a deferred `keyof X` whose X isn't
  inferred. A union with literal keys (`"a" | Exclude<keyof T, X>`) still takes that path (tsc infers from the
  properties those keys give: complicatedIndexesOfIntersectionsAreInferencable). Pick's K gets the source's keys only
  outside return-type inference (with `return_inference_mapper()` K2 of twiceNestedKeyofIndexInference would be
  `never`). `infer_to_mapped()` finding nothing returns (tsc goes per property; neutral).
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
- Return mapper (`infer_type_arguments`, call.c HACKs): the contextual inference through the outer return mapper fills
  only type parameters without candidates (tsc's always-separate returnContext loses
  contextualParamTypeVsNestedReturnTypeInference1-4 and nestedGenericSpreadInference, gains
  inferFromGenericFunctionReturnTypes3; valibot +9%). `literal_arg_context()` pre-instantiates with the return mapper
  (higher-order needs it). A pattern's return mapper drops inferred `any` (tsc's nonInferrableAnyType), rest patterns
  give none; `ty_pattern_unclone` copies the pattern type per call. Nested literals instantiate context only in
  `check_for_mutable_location()`. Plan 51 keeps these until someone explains the four tests under tsc's exact
  returnMapper (a plan 37 lead).
- `Inference.circularity`: `visited` records no priorities, so a pair is circular only while still on the stack. Real
  fix: store the circularity with the visited pair, as tsc's invokeOnce stores the inference priority.
- Reverse mapped types of arrays/tuples are eager like tsc's, but cut at depth 4 (`Checker.reverse_mapped_depth`, HACK:
  tsc's reverseMappedSourceStack/isDeeplyNestedType needs a mapped-type `Ty`). Object ones are lazy (plan 42 stage 1).
  Instantiating a generic reverse mapped type goes through `{target, mapper}`, not tsc's instantiateReverseMappedType.
  Inference priorities lack SpeculativeTuple, NoConstraints, AlwaysStrict. `inference_call_result()`: combined
  covariant candidates literal-reduced (tsc: subtype-reduced).
- Intra-expression sites (`ty_add_intra_expression_site()`): not for JSX attributes; `context_is_tuple_like()` misses
  call spreads.
- JSDoc type parameters (`meets_jsdoc_node_in()` in `assignment_mentions()`): fixing only, "in scope" by source
  position; tsc instantiates every JSDoc node for its outer type parameters (triage).
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
- Printing while resolving, to imitate tsc's eager error messages: `missing_property_type()` (ty/expr.c HACK,
  `Checker.print_probe`): tsc prints the object type of a TS2339 while checking, which can find an inferred return
  type in progress (`this.x` in an object literal method). tttt reports it later (ty/check.c), so evaluation prints the
  type too while something is being resolved; a return type found in progress then gets TS7023 but keeps its type
  (tsc's errors checker makes it any). A signature that saw it isn't memoized. function.c HACK: a non-iterable
  `yield*` prints its type while the return type is in progress, making `yield* foo` in `foo` any. Errors reported
  during evaluation (one pass, like tsc) would remove both.
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
- `implementation_signature()` (tsc's getImplementationSignature): clones a generic signature for a call inside the
  function that declares its type parameters, by span containment of `sig->function`; skips construct signatures
  (tsc goes by the class declaration for `new C()` inside C; tttt doesn't track where class type parameters live).

## Expressions and contextual typing (`ty/expr.c`, `ty/access.c`)
- `access()` any-key case (`ACCESS_ELEMENT`, HACK): only an element access *expression* reads an intersection's index
  signatures for an `any` key; an `X[any]` *type* on an intersection still gives any. tsc reads them there too, but
  then jquery's `NamespacedTypeToTriggeredEventMap<...>[any]` becomes the intersection of every event and tttt fails
  to relate those intersections during JQuery's variance measurement (JQuery<T> invariant, 13 DT sweep packages
  regress). The real bug is the intersection-source relation. Repro: `declare const a: JQuery<HTMLElement>; const b:
  JQuery<Node> = a;` with jquery's and sizzle's index.d.ts, `TTTT_DEBUG_RELATE_NESTED=1`.
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
- `KEY_CONTEXT_NONE` / `KEY_CONTEXT_FIRST_ABSENT` (expr.c): "the first check had no contextual signature / type", so
  the return type wasn't decided then (tsc's contextuallyCheckFunctionExpressionOrObjectLiteralMethod only computes it
  with a signature). NONE only under a call's resolution, FIRST_ABSENT also for anchored functions checked after it.
  Three slices (with `KEY_CONTEXT_BLOCKED`) of tsc's one rule: plan 51 stage 3 item 2 (each alone still loses: NONE 2
  tests + effect 2, FIRST_ABSENT flagflip -22 TS7023); merging `KEY_CONTEXT_DONE`/`BLOCKED` is item 4. A blocked
  function still takes `this` from the last contextual signature, which tsc asks lazily.
- `assign_contextual_signature_as`: once per function (`KEY_CONTEXT_DONE`), from the first usable context; generic
  contextual signatures aren't instantiated. `KEY_CONTEXT_PARAM_PATTERN` marks pattern fallbacks for the symbols
  traversal. `KEY_CONTEXT_IIFE_RETURN`: recorded at the IIFE's first contextual check, an earlier return type misses it
  (permanent: tsc's getContextualType(iife) is fixed at the first check too, plan 51).
- Apparent contextual types: `intersection_prop_context` intersects per-member answers (tsc: the intersection's
  apparent type once, distributing over union constraints; only union/intersection constraints go through
  `context_map`); `get_prop_context`/`contextual_signature` take apparent types at use. `context_map` drops `unknown`
  parts when others contribute (tsc: noReductions).
- Excess properties (`has_excess_properties`): a generic mapped target knows the lower bound of its constraint's keys
  (`generic_mapped_knows()`, a partial tsc's resolveMappedTypeMembers: no intersection/conditional lower bounds, no
  keyofStringsOnly); no nested re-check for intersection targets; `discriminated_union_target` has no key-property map.
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

## Relations (`ty/relate.c`)
- `find_prop()` of an intersection: tttt keeps optional properties' types without undefined and has readers add it,
  where tsc's getTypeOfSymbol includes it. An intersection property that is optional (all its *members'* properties
  are) but has a non-member part (a namespace's function, `typeof f & { m?: X }`) is `F & (X | undefined)` = `F & X`
  in tsc, without undefined; tttt drops PROP_OPTIONAL from it then (decided by flags, not a relation), which also
  makes it required as a target (tsc: optional). Real fix: optional property types that carry their undefined.
- relate.c sits at GCC's `inline-unit-growth` limit: code added anywhere in it can cost a hot inlining elsewhere
  (`find_prop` into `props_related_impl`, `global_type` into `apparent_type`). The reporting helpers (`report_line`,
  `display_for_report`...) are `[[gnu::cold]]` so the message building in `if (report)` branches doesn't eat the
  budget; rare relation paths go out of line as `[[gnu::noinline, gnu::cold]]` (`source_indexes_related()`). Check
  `-fopt-info-inline-missed` for "inline-unit-growth limit reached" when an innocent change shows up in
  `./instrcount.sh`.
- **Constraint functions are approximations** (the real fix for several entries below: tsc's getConstraintOfType vs
  getBaseConstraintOfType as two separate, cached, circularity-aware functions):
  - `base_constraint()` cuts off at depth 8 instead of tsc's circularity stack, so its memo (KEY_BASE_CONSTRAINT) only
    holds depth-0 answers; a display path (`display_mapped` -> `expand_generic_homomorphic` -> `ty_apparent_type`)
    relies on the cutoff to stop (`Compute<A[K]>`, computedTypesKeyofNoIndexSignatureType). perf_leads.md.
  - `base_constraint()` of an indexed access builds the access of the constraints even where it isn't valid (`T[P]`
    with `T extends object`, `P extends keyof T`), where tsc's getIndexedAccessTypeOrUndefined gives none. Null there
    breaks xstate (TS2344 on `NonNullable<TStateValue[K]>` in a mapped type), so only two users check validity first:
    `report_relation_error_now()` (the "could be instantiated" line) and `spread_base_constraint()` (check.c HACK, the
    spread check). Some other tsc rule is missing.
  - Distributive constraint of a conditional source (`structured_related_members`'s TY_CONDITIONAL case, HACK): tsc
    takes the check type's immediate constraint (`(any[] & T)[K]` -> `(any[] & T)[keyof T]`), tttt's
    `distributive_constraint()` its base constraint. The relation tries the base one first, then the immediate one
    (`distributive_constraint_as(.., true)`, cached per conditional) only when a base one exists (standing in for
    tsc's hasNonCircularBaseConstraint, else react-hook-form's Path types hit TS2589), and reports through the
    immediate one like tsc. While measuring variances it reports the measurement unreliable instead (cost).
    `base_constraint()` and the other users keep the base version: the immediate one there stops at unions of
    conditional types.
  - `apparent_type`: indexed/conditional without base constraint stays itself (tsc: `{}`); an intersection member whose
    apparent type is any/unknown stays itself (HACK) because `base_constraint()` of a conditional keeps `any` branches
    (webpack's `Reflect.get(...) & any[]`).
- `structured_related_members` TY_CONDITIONAL source (HACK): the first try of the true/false branches pushes the
  target on the deep-nesting stack too, where tsc relates its default constraint with RecursionFlags.Source only (the
  other generic-source constraint steps follow tsc). tsc's flags cost ~10% on the react corpus (react-hook-form's
  nested `T[a][b]...` chains related level by level, Maybe results not cached; 9831d7ea).
- Error reporting (plan 34): `flush_incompatible()` reads the property name back out of its message; `headMessage`
  approximated by `RelationState.at_top`; `report_truncate()` doesn't restore `skipped_source/target`,
  `override_next_head`. `intersection_props_related`'s TS2741 member choice fitted to one probe
  (`test/probes/plan19/valibot/overload_omit_intersection_missing_prop.ts`).
- Elaboration (`elaborate_error`) reads memos (`ty_mutable_location_type`, `ty_array_literal_as_tuple`) instead of
  re-checking; `ty_best_matching_type` has no key-property map; `type_decl_span` covers declared types, function type
  nodes and type literals only (renaming `as` properties lose declarations).
- `RelationState.intersection_source` (HACK): tsc keeps IntersectionState.Source into properties; doing so hit zod's
  instantiation limits.
- `structured_related_members`: the distributive-constraint fallback for intersection targets covers conditional
  sources only.
- `props_related`: the same-symbol skip is by `Prop` identity plus a rule for object literal accessors of one function
  (tsc: same symbol).
- `generic_mapped_target_related` (HACK): a generic source failing isGenericMappedType falls through to member
  comparison, which memberless mapped types pass (`Readonly<T>` to `T`); arrays, non-variadic tuples and objects
  without properties don't (tsc: nothing but a mapped source does). What still needs it: zod's `SortItem<output<T>>`
  against a Flatten-like `{ [K in keyof A | keyof B]: (A & B)[K] }`, `[...Foo<T>]` for a lone spread argument (tsc's
  getSpreadArgumentType has `Foo<T>`, restTypeRetainsMappyness). `write_constraint_of_indexed` (HACK) lacks
  NoIndexSignatures.
- Array<S> -> ReadonlyArray<T> relates element types (HACK); `is_rest_array` (HACK) takes any non-type-parameter as a
  rest array.
- Variances: arrays/tuples/synthetic targets hardcoded covariant; `VARIANCE_UNRELIABLE` from a checker-wide flag
  (`variance_reports`, set when a compared type mentions a marker), cached as `CACHE_UNRELIABLE`. Alias markers by type
  id (`KEY_ALIAS_MARKER`).
- Identity (`identity_structured`): differently shaped tuples never identical, no `infer` in the two-conditional rule.
  TS2403 (`check_subsequent_declaration`) uses it and exposes every approximation between two spellings.
- Cache keys are (source id, target id) plus two intersection-state bits. `RelationState.overflow` reports TS2321
  like tsc, but trips at 100 nested recursive_related() calls (`st->depth`), where tsc counts its source and target
  stacks separately (a call that skips a push doesn't count): tttt overflows at least as early.
- `this` types: `Prop.thisless` follows isThisless; a reference with a this-argument relates structurally.
- Alias variance (`in out` annotations): the reporting pass falls back to the interface's own variances on an
  invariant failure only for two references to the same target; other shapes keep the old path. The null-base TS2417
  check takes the first `extends` of the first merged interface, not getBaseTypes.
- `TY_INDEXED`'s IncludeUndefined (noUncheckedIndexedAccess) is dropped wherever an indexed access is rebuilt from its
  parts (`simplified_parts`, `ty_actual_type_variable`).
- **Every anonymous object type has an inferable index** (`has_inferable_index`): tsc's isObjectTypeWithInferableIndex
  wants a symbol (an object literal, type literal, ...) and excludes types the checker makes without one. tttt only
  knows `anonymous.no_inferable_index` (`OBJECT_NO_INFERABLE_INDEX`: the literal-keyof inference object) and the
  global empty object; other symbolless synthetic types (spreads? reverse mapped types?) may wrongly relate to index
  signatures.

## Type core (`src/ty/`)
- `name_type_filters()`/`peek_cond_branch()` (`ty/members.c`): whether a mapped type's `as` clause filters or remaps
  (tsc's getMappedTypeNameTypeKind, which decides whether its properties keep their declarations and so their stable
  order) is told by the declared clause's shape (each union member / conditional branch is the key, never, or an
  intersection with it), not by relating it to the key as tsc does: relating, or even lowering a self-referential
  branch, reaches the members being resolved (mappedTypeAsClauseRecursiveNoCrash1). A pending lazy branch counts as
  remapping. The fix is a relation that can't resolve these members (or tsc's resolution order).
- Stable type ordering (`ty/order.c`, tsc 6's compareTypes): conditional types compare by their root's node position
  (`TyConditionalOuter.node`, the TernaryType span) and deferred references by their node's first token
  (`type_node_start()`: identifier, tuple, type literal, through arrays and union/intersection members) before the
  mapper, like tsc's compareNodes; fresh literals sort after their regular twin; unique symbols without a position go
  after positioned ones, then by owner name. Still tsc-id ties that fall back to tttt's ids: roots not lowered from a
  node (no `outer`), deferred references whose node has no cheap position (a parenthesized or keyword element type),
  two nodes starting at the same token (`(A extends B ? 1 : 2) extends C ? ...` against its check type), unique
  symbols of the same name without positions, bigint literals, intrinsic variants and every final tie
  (same-symbol anonymous types with equal mappers). noImplicitAnyUnionNormalizedObjectLiteral1 is not a comparator
  rule: tttt's nullable-widening pass keeps one member's id where tsc's widening recreates both (triage, agent D third
  pass).
  `Ty.order_key` caches the comparison prefix (8 bytes per type).
- Member order (`builder_sort_stable()`, members.c, tsc's getNamedMembers): applied to object literals, mapped types,
  namespaces and merged values only; class/interface members keep tttt's own-then-inherited order (matches the
  contained/non-contained split in every case seen), `builder_prototype_last()` moves a static side's synthetic
  `prototype` after the rest. Props without a declaration sort by name bytes (tsc: escapedName, then symbol id).
- `KEY_AUTO_FLOW_ISSUE` (`identifier_flow_type()`): TS7005/TS7034 for a reference whose flow type was still auto when
  first evaluated inside a loop's incomplete pass (tsc reports from checkExpression there), remembered per expression.
- `conditional_branch_lacks_constraint()` (expr.c, is_literal_of_contextual_type): tsc's base constraint of a union
  needs every member's, ty_base_constraint() leaves unconstrained members out (relations depend on that: making it strict
  broke doNotElaborateAssignabilityToTypeParameters). The literal-room check special-cases a conditional's union
  branches only; other places asking "what is this at least" for `T | string` may be too permissive.
- `instantiate_inner()` TY_INDEXED (HACK): an indexed access distributed over a union index keeps its alias (tsc's
  getIndexedAccessTypeOrUndefined), except when the result is a union of unit types (`ConditionalKeys<B, C>` of
  type-fest): the aliased identity made every mapped type over the keys (Pick, ConditionalPick) resolve again (+2%).
  tsc prints those key unions by the alias.
- `UnaryOpExpr.no_type_params_in_scope` approximates getResolvedSignature's instantiation for `yield` (TS7057): only a
  bare type parameter counts as a call's, a contextual `T[]` is taken as is.
- Union origins (`Ty.composite.origin`): not instantiated, dropped by union-mapping helpers (plan 37 B stage 2).
- `ty_realias`: a renamed object type is a second `Ty` (members resolved twice); renamed deferred types are copies. No
  "local alias" exception. `renames` in `lower`: whether an alias instance keeps the name is syntactic.
- `ty_conditional_resolve` (conditional.c HACK): an unsatisfied `infer X extends C` falls back to C only if C is then
  non-generic, and `infer` constraints mentioning type variables are otherwise unchecked (tsc falls back to a generic
  constraint too, which leaks valibot's LazyPath `TValue` here because deferred conditionals don't keep their
  inferences: `inferred_true_branch()` recomputes; `test/probes/plan19/zod/ark_infer_constraint.ts`).
  `ty_implied_infer_constraint` lacks the mapped-extends rule. Permissive/restrictive instantiations only replace what
  `mentions_type_variables` finds (8192 types / 48 deep, then "unsure": defer), since TYF_COULD_CONTAIN_TYPE_VARIABLES
  is on every function/object literal; `mentions_type_variables` treats an object literal getter whose return type is
  in flight as unsure (asking would re-infer it below a call's barrier: zod's recursive getters became TS7023).
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
- `ty_indexed` stays deferred beyond props, index signatures and list elements.
- `class_base()` (members.c HACK): base class names found syntactically; other expressions take the first fitting
  construct signature (TS2510 by `ty_class_base_constructors_differ()`). A base constructing `any` is no base;
  `ty_has_unresolved_base()` gates TS2339/TS7053 instead (tsc: `[x: string]: any`). Type-parameter bases take the
  constraint's members at declaration. `ty_non_augmenting_base()` from syntax. See also `ClassDecl.jsdoc_base`.
- A bare `C`/`this` inside generic `C` isn't its own instantiation (lower.c TODO).
- Enums (`ty_resolve_enum`, `evaluate`): no `const` object property accesses; other files' `const`s count as constant;
  ambient non-const members auto-increment (tsc: computed). `member_value()` (enums.c HACK) counts a reference to a
  string enum member as syntactically a string (TS18055 under isolatedModules): tsc keeps the referenced member's own
  isSyntacticallyString.
- `never_reduced_prop` (checker.c): `ty_reduced()` (tsc's getReducedType) doesn't judge members flagged
  `TYF_COULD_CONTAIN_TYPE_VARIABLES` unless `is_known_object_for_reduction` (tsc's isNeverReducedProperty looks at
  every member; judging them cost valibot +29%: find why tsc doesn't pay that first).
  `check_related`'s fast path runs on the unreduced types like tsc's isTypeRelatedTo (`any` is assignable to an
  intersection that is never), is_related_to() then reduces both sides.
- `reduce_type_variable_intersection` (checker.c HACKs): type parameters only, not indexed accesses (valibot's
  `NonNullable<TSchema['~types']>` became never via a premature `undefined` base constraint); constraints with a
  deferred type aren't reduced (`primitive_constraint_of`, tsc asks the base constraint, 5% of valibot). Real fix:
  cheaper conditional base constraints (bugs/perf_leads.md).
- Generic tuples: `base_constraint` replaces only variadics with array constraints; `normalized_tuple` makes
  `...readonly T[]` into `...T[]`; `lower_tuple` keeps the written `T[]` for rests (tsc: `T`).
- `lower_identifier`: `object` is a builtin declaration named "object".
- `builder_add_interface_member`: index signature types lowered eagerly; non-literal computed keys dropped (tsc:
  late-bound index signatures).
- `ty_union` doesn't subtype-reduce (`ty_union_subtype_reduced` on request) or drop nullables without
  strictNullChecks. Cross-product caps give the error type silently: `intersect_unions` and `ty_template_literal`
  (100k, TS2590 unreported), `normalized_tuple` (10k).
- `append_mapped_code_point()` (template.c HACK): Uppercase & co. cover ASCII, Latin-1, basic Greek, Cyrillic;
  `apply_mapping`'s placeholders step code points (JS: UTF-16 units). `${symbol}` templates are silently `string`.
- Unique symbols: escaped names (`__@sym@12`) mapped back through `ty_unique_symbol_of_name`, so a string property
  `__@x@3` passes as a symbol; `instanceof_target_ok` matches `__@hasInstance@<id>` by text; one unique symbol per
  declaration (tsc: per merged property), so `check_subsequent_property` (check.c HACK) skips TS2717 between them.
  `SymbolConstructor`'s `symbol` properties are unique (interface_decl.c HACK, tsc's too).
- Private names (`ty_private_name`): the lexical lookup scans class items by text (`class_decl_find_private_name`);
  an undeclared one looks up the plain `"#x"` atom. Object literal accessors have no `write_node`
  (`prop_decl_rare()`).
- `Checker.current_node` (TS2589/TS2615/TS4110/TS1062 location): `lower()` makes a generic reference current while
  lowering it (a guess at tsc's order). `Foo<X>` isn't lowered in the walk (resolving every alias changed type ids).
- Instantiation limit: what an exhausted budget produced is memoized like tsc's (errorType stays in links/instantiation
  caches: later users are silently any, `test/probes/instantiation_limit/`), but tttt instantiates more eagerly than
  tsc, so a tttt-only TS2589 (variance measurement, eager members) poisons what tsc would still compute. A cut-short
  conditional instance is re-reported only within its epoch; later hits only bump `instantiation_limit_hits`.

## Checks (`ty/check.c`, `unused.c`, `statement.c`, `class_decl.c`)
- `source_file_check_verbatim_exports()` (TS1287) finds the `export` keyword by scanning the statement text
  (`export_keyword_of()`): statements don't keep their modifiers' spans. `check_alias_isolated_modules()` puts a
  default import's error on its name where tsc spans the whole import clause (`A, { b }`).
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
- `ty_check_object_type_duplicate_members`: `MemberTables` is a fixed 128-entry linear array; later members unchecked.
- `ty_check_class_duplicate_members()` (HACK): a late-bound member name (`[k]`, `[Symbol.x]`) conflicts with what the
  member table has (the static `prototype`, tsc's source of that TS2300 unknown) without joining it; tsc also reports
  an early `a` against a later `get [k]` (k = "a") at both.
- `ty_check_heritage`: TS2320 for interfaces only (`inherited_props_identical`).
- Emit helpers (`ty_check_emit_helpers`): spans by hand (`decorator_span()`, `function_error_span()`); "ambient"
  approximated per site (`ClassDecl.is_ambient`, `Binding.ambient`, `BlockTypecheckCtx.ambient`; want
  NodeFlags.Ambient); `for await` unchecked.
- `unused_mark_jsdoc_links()` (HACK): `{@link X}` is a use by text scan, looked up from the file's scope (real fix:
  resolve them from parsed tags). Member reads are a table keyed by declaration span start
  (`ty_mark_member_referenced`).
- `for_in_accepts`: type parameters and deferred types accepted outright (TS2407 missed).
- `program_apply_comment_directives`: suppression by tttt's error *phase*, not tsc's diagnostic kind; `\n` and ASCII
  `\s` only; relies on every comment going through `hop_comment`.
- `SourceFile.unchecked` + `error_ctx_remove_unchecked`: the JS/skipLibCheck gate filters after checking. Plain JS is
  checked in full and filtered to plainJSErrors (as tsc).
- `error_ctx_global_error_s`: file-less diagnostics have a null span, sorted first; `error_ctx_error_s` drops invalid
  spans.
- `port_statement_typecheck()`: TS2837/TS2858 (non-string import attribute values) don't stop the import clause checks
  that tsc's checkExternalImportOrExportDeclaration returning false skips.
- `ty_check_destructured_member` (check.c HACK, `from_this`): `({ m } = this)` inside method m isn't a read of private m
  (tsc's markPropertyAsReferenced with rightIsThis); "inside" is by position, so a function nested in m counts as m.

## JavaScript and JSDoc (plan 26)
- A `@returns`/`@throws` type is only resolved in tsc (never checkSourceElement'd), but `function_impl_typecheck`
  runs `type_typecheck` on it, skipping just the constraint check (`IdentifierType.jsdoc_unchecked` in
  `identifier_type_check_arity()`). Real fix: a resolution-only walk (arity errors must stay:
  jsdocArrayObjectPromiseNoImplicitAny).
- `ClassDecl.jsdoc_base` (a mismatched or empty `@augments`, tsc's getEffectiveBaseTypeNode) is only what
  `class_base()` resolves; the other readers of `ClassDecl.extends` (base constructor checks, arity, display) still
  see the clause. Dotted tag names (`@augments {ns.A}`) keep the clause.
- JSDoc is consumed at parse time, not stored: `jsdoc_before()` re-parses the last comment before the host into
  transient `JsDocTag`s (parse errors kept for JS files through `ParserShared.jsdoc_errors`).
  `jsdoc_bind_comments()` parses every `@typedef`/`@callback`/`@import` comment again and claims it by innermost
  statement list (claims from failed attempts stay); `@import` only at list top level; dotted/nameless typedefs
  skipped; typedefs sharing a comment share its templates. `ParserShared.jsdoc_enum` is a one-slot handoff that
  statements outside a list drop.
- `@type` on assignments reaches the symbol via `SymbolDecl.jsdoc_type`, threaded through every assignment-declaration
  site in scope.c (new sites must pass it; chains: outermost only). `jsdoc_apply_type_signature()` *moves* the
  TypeNodes of a written function type. `jsdoc_prototype_type_param()` sees F's type parameters anywhere in a
  prototype method. `Statement.expr_jsdoc_type` shares a union with `expr`: every site building an expression
  statement must clear it.
- No `SymbolFlags.Assignment`: `global_merges_by_assignment()` rescans top-level statements
  (`js_file_assigns_namespace()`) on global conflicts; TS2403 looks for `SYMBOL_DECL_EXPANDO`. Global expando
  namespaces (`global_expando_namespace()`) go straight into shared globals during binding (tsc: per file, merged):
  `declare_symbol()` merges anything with an expando-only global; `binder_link_global_expandos()` gives initializers
  their members after binding.
- Expandos: statement level in the declare step, nested in expressions via the resolve walk
  (`scope_add_nested_expando()`: `f.x.y = v` doesn't see a nested `f.x = {}`). `scope_add_expandos()` (in
  `block_add_names()`) and `scope_add_this_property()` write file-level symbols from the resolve walk (want per-file
  lists merged after); only expression statements and `var` initializers are scanned. `Symbol.late_bound_key` keys
  `this[k] = v` by source text.
- CommonJS binding: the module indicator is syntactic (`saw_commonjs`, failed attempts too); `exports.x =` only from
  top-level statements and their blocks (`add_nested_commonjs_exports()`; in functions only IIFEs, see
  missing_checks.md); `Object.defineProperty` only as an expression statement; `is_exports_alias()` module-level only
  (queue of 16); `require()` is parser-synthesized `PortItem`s (flat patterns only); `readonly_assignment()` never
  reports `exports.x =`. Exported class expressions alias the class (`scope_reparent`, top level only).
  `exports.x = undefined` then `exports.x = a` (moduleExportDuplicateAlias*): tsc makes every one an alias, tttt a
  property + alias symbol.
- CommonJS `export =`: absorbs the module's other exports in place at link time (`merge_commonjs_export_equals()`,
  tsc's getCommonJsExportEquals clone), with two of tsc's laziness artifacts emulated by rules: a variable's
  function/class initializer lacks the merged exports (`is_commonjs_merged_export()`), and `export import a = x.c`'s
  TS2708 ignores a value meaning only the merge gave (`symbol_is_commonjs_merged_value_only()`).
  `merged_export_equals_type()` (lower.c) keeps a typed view (`export_equals_view`) under its own keys; conflicting
  same-named exports stay unmerged (only TS's `check_export_equals_merge()` reports); a module target
  (`module.exports = require("./y")`) isn't merged; `exports.x = a.b` isn't an alias; `import * as ns` of CommonJS has
  the module's type.
- Constructor functions: function type plus a construct signature (`resolve_members()`, `ty_js_constructor_class()`
  prints `typeof F`). Its class symbol is the variable's/property's (`set_function_class_symbol()`; tsc: the function
  expression's), patched in `ty_js_assigned_class_name()`, `symbol_chain_append_entity()`,
  `this_compile_test_info()`. `F.prototype = {...}` members are added at member resolution
  (`builder_add_assignment_members()`), not merged symbols (mergeJSSymbols): `F.prototype.m` and `this.m` are one
  member, prototype accessors don't merge with `this.x` (jsDeclarationsFunctionLikeClasses2). Reads in a constructor
  *function* aren't auto-typed (`constructor_declared_read()` is class-only); every plain JS function gets a return
  flow node in case it's a constructor.
- `add_this_property()` (scope.c): a JS function expression with a JSDoc `@this` gets no class symbol of its own for
  `this.x = v` (tsc binds one). tsc reports a member's widening errors (TS7008) only when something resolves its type,
  and with `@this` nothing does (`this.x` reads the declared this type); tttt reports eagerly, so not binding it stands
  in for the laziness (webpack's `@this {Module} function errors() { this._errors = [] }`).
- TS2738 (`IdentifierExpr.this_shadows_outer`) is decided in the resolve walk.
- JS syntax errors (TS8xxx, `js_syntax_error()`) come from the parser; what tsc skips inside is erased after
  (`js_syntax_skip_since()`): every skipped construct needs its call. Node array spans run first to last element.

## Declaration diagnostics (`ty/declaration_diagnostics.c`, plan 40)
- `fallback_begin`/`fallback_end`: where the syntactic builder falls back anywhere in a declaration, the *whole*
  declaration's checker type is printed for the tracker (tsc keeps the syntactically typed parts as written). The
  written parts' names are checked separately; a difference shows only when the checker's type names something the
  syntax didn't.
- The node builder's re-entry is ported in two spots only: an expando variable's function initializer
  (`report_variable`) and string/number/symbol-named class methods (`check_class`, createLateBoundIndexSignatures). tsc
  re-runs the builder for every signature/property of the file a printed fallback type mentions; TS9xxx inside those
  aren't reported.
- Late painting (`paint`, `take_painted`, late passes in `declaration_diagnostics_check`): tsc transforms imports in
  its first pass, so TS9026 sees only what was painted before the import; other statements are painted from any
  point. A painted namespace reaches `symbol_chain.c` through a thread-local hook (`symbol_chain_set_painted()`, which
  also enables a per-walk accessibility memo and alias index). tsc's painting persists across files; here per file.
- `track_computed_name` takes the computed name's *text* (`[a.b]`) and resolves its first identifier from the
  enclosing scope; the fallback is assumed to be a module's local (CannotBeNamed from another module, NotAccessible
  otherwise), what tsc's resolveName gives for top-level module declarations.
- TS7056 only for wide types: the printer's global MAX_DISPLAY_DEPTH elides before tsc's per-symbol depth of 10 would,
  so deep recursive types (declarationEmitPrivatePromiseLikeInterface) stay under the 1M characters.
- `symbol_chain.c` accessibility: `decl_visible()` fails open (visible) where it doesn't know a declaration's
  statement list (no `block`, an ambient module, a namespace block it can't match).
- `namespace_import_is_clone()` / `export_equals_namespace_import_is_clone()` (`symbol_chain.c`, HACK): under
  esModuleInterop tsc resolves `import * as ns` of a module with a `default` (or signatures) to a clone symbol, so
  `ns` names neither the module nor anything but itself. tttt has no clone symbol: make_gather_target()'s per-alias
  symbol stands in, and for `export =` modules `namespace_import_type()` (lower.c) makes a `TyAnonymous.module_clone`
  type (`display_module_clone()`). Both predicates are syntactic, approximating namespace_import_type()'s typed one;
  swapping in `ty_namespace_import_original() != null` breaks transformNestedGeneratorsWithTry (tsc's clone keeps the
  module's members reachable for naming). Real fix: a clone symbol.
- `module_specifiers.c` leaves out getModuleSpecifierOverride (a reused `import("...")` node keeps its specifier in
  tsc), redirectTargetsMap, output files named by package.json `imports`, AMD `moduleName`, auto-import preferences.
  Alternative containers (`alternative_modules()`) are cached per walk and per (symbol, program) on each thread;
  outside walks (types baseline, messages) the file's imports are rescanned every time.
- `is_optional_parameter` ignores getMinArgumentCount's trailing `void` parameters; `Place.contextual` is only set
  through type assertions (the builder never descends into calls or JSX, the other isContextuallyTyped ancestors).
- `declaration_diagnostics_wanted`: isSourceFileFromExternalLibrary is "loaded by an import and the path has
  `node_modules/`"; JavaScript is skipped (no JS declaration emit, missing_checks.md "Declaration emit").
- The printed `import("...")` text is still tttt's module name (the specifier only decides TS2742 and the container
  order); nothing compares the printed declaration text.

## Modules and program (`module_resolution.c`, `tsconfig.c`, `program.c`, `main.c`)
- `path_is_drive_rooted` (`module_resolution.h`): a DOS drive path (`c:/x`) is a directory `c:` of the current
  directory everywhere (mock fs, canonical paths, `join_rooted`/`option_path`), what POSIX does with it; tsc treats it
  as a root of its own. Enough for the harness's drive tests (TS5009 compares roots by name, `path_root_of`).
  (permanent: tttt is POSIX-only; a path type with roots if Windows ever matters)
- `resolve_uncached()` (module_resolution.c HACK): node10 and export-map packages resolve in two passes (TypeScript
  extensions first), reproducing tsc's selfNameModuleAugmentation behaviour (microsoft/TypeScript#50762) by other
  means. `try_load_input_file_for_path` lets the TypeScript pass take a JavaScript input anyway (with allowJs), since
  tsc's one pass maps an exports target to its input before the target's `.d.ts`: a patch on the patch.
- `/// <reference lib>`: the CLI loads it after the program's files (`program_follow_lib_references()`; tsc: libs
  first). The harness adds it to the cached lib list up front by text scan (`load_test_source`).
  `program_load_reference_path()` probes extensions by hand. (Unknown lib names, TS2726, and `@libReplacement` are
  missing: harness.md)
- `tsconfig.c`: an unreadable root config is one `failable` message; `include` globs always skip dot and package
  directories (tsc: only under `**`). The walk runs before `compiler_options_resolve()`, so
  `allows_js()`/`resolves_json()` restate the allowJs/resolveJsonModule defaults; `follow_links()` uses libc's
  realpath, not the `Fs` (the mock fs never links configs).
- Config syntax (`json.c`, `program_set_config_file()`): the config's syntax errors come from parsing it as a JSON
  source file with tttt's parser, while what's read comes from `json_parse()`, whose recovery (missing commas/colons,
  unquoted keys, unterminated strings and containers, non-values dropped) only approximates parseJsonText's. (What
  isn't reported: harness.md)
- Project references (`project.c`, plan 41): a `ProjectSet` reads every config twice (options, then root files with a
  scratch `CompilerOptions`); the redirect map is rebuilt per program. `main.c` counts config diagnostics among the
  "parse errors" of its summary. (TS6059/TS6307 explanation chains and transitive reference diagnostics: harness.md)
- Reference sources' typed bodies (`skip_typed_body()`/`skip_balanced_braces()` in parser.c,
  `function_impl_lookup_names_as()`'s `skip_body`, plan 41 stage 2): skipped by a hand scanner whose regex-or-division
  rule is a heuristic (gives up after `)`/`}`/`++`/`--`). What a skipped body would have done to the outside is lost:
  a `let` assigned only in it isn't `is_assigned`, `Symbol.referenced`. The parser can't tell an IIFE, so an annotated
  IIFE's body is skipped there (resolve then walks an empty body).
- `-b`'s `--build-jobs` default (`build()`, main.c) is 2 at 4+ threads, picked on one corpus (tsc-src). (permanent:
  a tuning constant; a memory-aware scheduler would size by projects' file counts)
- Message paths (`source_file_set_message_cwd()`, `path_for_message()`): the CLI sets a process-wide working directory
  so message text names files absolutely like tsc's CLI; unset, the harness's display paths are used. Global because
  messages are built where no `Program` is at hand (scope.c, statement.c).

## Display and baselines (`ty_display.c`, `symbol_chain.c`, `baseline_test_case.c`)
- `program_module_display_name()`: import types and module symbols named from a place use
  `program_module_specifier()`, uncached (tsc caches per symbol, file and mode); under `outFile` it falls back to the
  file path without extension instead of tsc's non-relative specifier from the common source directory, and JSON
  files are special-cased by observation. The baseline traversal skips omitted array elements by shape (an undefined
  literal with an empty span in `EXPR_ARRAY`), not by a node kind of their own.
- `TyDisplay.decl_literal` / `annotates_name_at()`: tsc reuses a `unique symbol` annotation only inside the enclosing
  declaration (the printed node's parent); the types baseline has no parent node, so "the literal is the annotation of
  the name at `at`" is decided from the source text between them (only `?`/`:`).
- `symbol_chain.c` (getSymbolChain) sees only scopes the traversal tracks (`BaselineTestCase.enclosing`), so
  function-local shadowing isn't qualified; `try_locals_with_exports()` merges by position; the accessibility memo
  exists only in declaration-diagnostics walks. No firstVariableMatch (getContainersOfSymbol's search for a variable
  of an interface's type): `out_unique_symbol_name` (HACK) knows only the `XConstructor` -> `X` pattern.
  `append_qualified_name` (ty/lower.c) always fully qualifies in messages. `baseline_test_case_module_alias_text` only
  handles chains ending at the export.
- Node reuse (`display_node()`/`display_annotated()`): `name_accessible()` resolves from `enclosing` only; signatures
  reuse the declaration when the mapper left it alone (`mentions()` conservative). `T_1` renaming (`TyParamNames`,
  `baseline_test_case_enter_generic()`) restates resolveName by hand. `display_node_identifier()` assumes ASCII module
  names. Syntactic typing (`single_return_expr()`, `FunctionImpl.syntactically_contextual`, which counts any JSX
  element as contextual; `single_return_keyword()` only does primitive literals of tsc's typeFromExpression); index
  signatures, tuple names, defaults always synthetic. `jsdoc_reference_reusable()`: `@import` aliases are never
  reused (tsc's observed behaviour, importTag* need it; mechanism not found).
- `property_compile_test_info()` (HACK): the `default` of `import * as ns` of a JSON module prints as the module's
  name (`Symbol("package")`): tsc's is an alias (createDefaultPropertyWrapperForModule, aliasTarget the module) that
  the symbol lookup resolves, other modules' synthetic default a plain property. tttt's synthetic default isn't an
  alias, so the harness restates it per module kind.
- `index_components_printed()` (HACK): isEntityNameVisible for `{ [k]: T }` approximated (function-declaration
  parameters visible even nested; no shadowing check since `TyDisplay.enclosing` misses enclosing parameters).
- `ty_elides_default_type_args` (iterate.c): tsc's `Iterable<T>` elision, minus its exception for a reference written
  with all its type arguments (tsc's `type.node`; tttt types don't keep their node).
- The `any` underline rule reads whether tsc's `.errors.txt` exists, not whether tttt reported errors (permanent: the
  baseline's underlines follow tsc's errors; using tttt's would fail types components for errors already counted).
  `BaselineTestCase.any_keeps_underline` leaks to the next line if no types line follows.
- Symbols lines: container from the traversal or `Prop.parent`; `collect_prop_symbol()` counts a non-object union
  member as lacking the property only for binding patterns: the object of a property access is sometimes not narrowed
  as far as tsc's (doing it everywhere loses 70 symbols components: optional chains, logicalAssignment*, nonPrimitive*).
  Span-keyed overrides (`commonjs_symbol_overrides()`, `prototype_symbol_overrides()`,
  `this_property_symbol_overrides()`) stand in for getSpecialPropertyAssignmentSymbol. JSDoc tag Decls get no leading
  trivia (sniffed). A module's `default` export symbol is named like its first declaration.
- JS class member merges (prototype assignments into class-body members, `F.prototype = {...}` members into `this.x`
  members) only append declaration spans to the checker's `Prop` (`builder_add_assignment_members`); the harness reads
  them back in `prototype_symbol_overrides()`, `this_property_symbol_overrides()` and the object literal walk. Enum
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
