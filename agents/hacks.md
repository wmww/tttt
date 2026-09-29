# Hacks
Suboptimal-but-working code, by subsystem. Each entry: where (function names), what is approximated, what the real
fix would be. Missing diagnostics and features belong in `bugs/*.md`, triage.md or a plan, not here. `grep -rn HACK
src` finds the code-side markers; every significant one has an entry here.

## Global state, memory, parallelism
- `reexec_with_huge_pages` (`main.c`): glibc reads `GLIBC_TUNABLES` only at startup, so tttt re-execs itself with
  `glibc.malloc.hugetlb=1` (not under valgrind or `TTTT_NO_REEXEC`): 5-20% wall time. An own malloc / AST arenas that
  madvise like the checker arena would remove it (`bugs/perf_leads.md`).
- `shrink_list()` (`parser.c`): every parser list is shrunk when done (AST elements are big: a parameter is 568 bytes):
  -20% RSS for +1.5% instructions. Real fix: smaller nodes. PERF notes: `CallExpr` argument types recomputed per use,
  `Value.object` refcount churn, `CallableType`/`FunctionImpl` two allocations.
- `current_checker` / `current_lazy_slots` (thread locals, `checker_make_current`): how the AST-side walk
  (`*_typecheck`, `*_evaluate_type`, `ty_of_node`) finds its checker; left set after `program_typecheck` for the test
  traversal. `compiler_options()` is a thread local too. Both want a passed check context (checker + errs + options).
  Many parsed options are inert (`grep -o 'compiler_options()->[a-z_]*' src` lists what's read).
- `Checker.return_context`: the contextual type of `return`s is checker state saved around bodies
  (`function_impl_enter_return_context`); `statement_typecheck` & co. take no parameter for it.
- LazyType slots (`lazy_slot_new`): one process-wide counter, never reused, taken eagerly: sparse tables cost ~6% RSS,
  panic after 1G slots; per-file numbering would work. `lazy_type_drop` resets a slot if a checker is current
  (assumed a temporary); AST slots are dropped by `checker_drop` before the AST: an implicit ordering rule.
- **Parallelism contract** (documentation.md "Resolve names"), what still writes shared state. Declare: script globals
  and `declare global` into `Binder.globals`, ambient modules into `Binder.ambient_modules` (want per-file tables
  merged at link); one `Binder.arena` and `Binder.symbols`; one `ErrorCtx`. Resolve: shared arena; writes
  `is_assigned*`/`last_assignment_end`, `Scope.flow`, `Symbol.referenced` (also on shared script globals: a race once
  files resolve in parallel), `scope_add_expandos()`/`scope_add_this_property()` (file-level symbols), const-cast
  writes of `SourceFile.jsx_fragment_factory_looked_up` / `ModuleRef.jsx_runtime_reported`. Check: per-checker
  `Checker.errs`; the program's ctx is `frozen` and `Binder.frozen` set, so a report there, a new binder symbol or a
  first alias resolution `log_error()`s. Runtime guards, not a proof (TSan on the corpora is the other net).
- Load pool (`program_load_ahead_from()`): guesses what a file will load with a simplified copy of
  `program_load_module_ref()`/`program_handle_statement()`; a wrong guess only wastes a parse
  (`TTTT_LOAD_POOL_STATS=1`). Sizes from `stat()` in `main.c` (harness: FIFO).
- Once-per-program diagnostics (`Error.once_key`, `error_ctx_keep_first_once()`): with K checkers each reports and the
  merge keeps the earliest file's. Only TS2343/TS2807 (`ty_check_emit_helpers`) are tagged; a new "said once" checker
  memo must be tagged too.
- `ModuleResolver` caches are per `Program`, unsynchronised; resolutions aren't cached (tsc: per directory+name+mode).

## Parser
- Strings are UTF-8: a lone surrogate escape becomes U+FFFD (`append_code_point()`), so all lone surrogates are one
  string and `"\u{D83D}\u{DE00}"` isn't the emoji. `regex_check_literal()` works in UTF-16 to port tsc's scanner; a
  position between surrogates is the 3rd UTF-8 byte (`utf8_byte_utf16_units()`). A NUL byte ends the file
  (scannerUnexpectedNullCharacter1, scannerStringLiteralWithContainingNullCharacter1).
- Escaped identifiers (`\u0061`) resolve by value through `name_cooked()` (span.h): a global registry of cooked names
  behind a mutex, keyed by where the raw name sits in a file with a `\u`, each lookup a linear scan of that file's
  cooked names (quadratic for a file with thousands). Symbol tables cook at every lookup (a `memchr` for normal
  names). Comparisons against literals (`span_equals(name, "constructor")`) stay raw (`bugs/escaped_identifiers.md`).
  `EnumCase.name` of a string member is the raw text. Escaped keywords
  (`hop_escaped_keyword()`) report TS1260 wherever the probe matches, even speculatively (not seen over-reporting).
- Two notions of "reserved": `parse_identifier()` refuses only `is_reserved_identifier()` (plus `var` in JS) because it
  also parses identifier names and keyword types; `parse_binding_identifier()` and enum names refuse all of
  `is_reserved_word()`. Refusing all in `parse_identifier()` broke 68 tests: each caller must pick tsc's
  parseIdentifier / parseIdentifierName / parseBindingIdentifier.
- Conflict markers (TS1185) only after `\n` in `hop_whitespace_slow()` and at file start (not after a lone `\r`);
  `Decl()` positions don't skip back over one. `parse_error_in_literal()` bypasses the TS1127 conversion.
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
  parser before `parse_success` truncates the span (was a bug in `parse_import_equals()`; may recur).
- Error recovery (plan 27): `list_abort_or_skip()` skips a token that starts a list element instead of ending the list
  (right while tttt's element parsers fail where tsc's commit). The fail cache ignores
  `ParserShared.parsing_context`. `parse_list_member()` asks the member-start predicate only after errors;
  `parse_binding_content()` stops at the first bad element; a committed arrow whose parameters fail falls back to a
  parenthesized expression. `tok_scan_end()` is a second cheap token notion next to `hop_token()`.
  `parse_jsx_attributes` approximates isInSomeParsingContext (`jsx_children_depth` + a character list).
- Statements: `parse_if_else` looks past a `;` for `else` (terminators belong to `parse_append_statement_sequence`);
  `statement_takes_semicolon()` lists by tag who calls parseSemicolon(). Statement spans exclude the `;`
  (`span_with_semicolon` patches `scope_export_default_expr`; TS7027 may be one off). `IfStatement` flattens `else
  if` (no `Statement` for the nested if: no TS7027 on it). `statement_start_needs_check()` gates isStartOfStatement by
  first word (a new rejecting case must be added).
- Dropped syntax the checker re-peeks: class field `x!: T` (`definite` in class_decl.c and
  `check_used_before_initialization` look at the character after the name); `in`/`out` type parameter modifiers never
  print; private names outside classes have no flag (`object_item_is_private_name`,
  `interface_member_is_private_name`, `enum_decl_typecheck` check the first character; such an enum member is
  declared and prints `__missing`).
- Accessors: class accessors pair by name+staticness in `class_decl_lookup_names` (raw pointers in one vec); readonly
  means "no setter". Interface accessors become properties in `parse_interface` (`InterfaceMember.write_type`,
  `second_name`, `setter_param`). Computed keys' `name_string` is source text: accessor pairs and overloads match by it
  (tsc: late-bound symbol, `["a"]` pairs with `a`). `{ a = 1 }` keeps `ObjectExprItem.shorthand_default`.
- `await_is_operator`, `yield_is_operator_outside_generator`, `yield_await_is_keyword_here`: `ParserShared` flags plus a
  character peek; `parse_class_item_inner` saves/restores them inline (want a `FunctionCtx` helper).
- `mark_named_evaluation()`: every construction site that names an anonymous class must call it.
- Grammar vs parse errors (`is_grammar_code()`, `SourceFile.has_parse_errors`): tsc drops checker grammar errors in
  files with parse errors. Older checks (TS1254, some TS1155/TS1039, modifiers) use `error_ctx_error_d` and always
  report. `is_grammar_code()` classifies ~50 codes: a code both parser and checker report can't be both, and a new
  parser grammar check counts as a parse error until listed. Fix: a `parse_grammar_error()` flagging the Error itself.
- Parsed and dropped: decorators before an import in `parse_decl_statement()`'s fallback (TS1206 lost); decorators
  without a declaration (MissingDeclaration: TS1128 at `@` instead of TS1146); index signature parameter initializers
  (`parse_index_signature()`, not a `FunctionArg` list: TS2304 in `[a = b]` lost); computed non-literal enum keys
  (`EnumCase.computed_name`, member prints `(typeof E)["[e]"]`, tsc `E`); non-string import specifiers; a
  `ModuleRef` from an abandoned attempt stays listed (`add_module_ref`). `global x` is `NamespaceDecl.is_shorthand`
  typed any (`namespace_decl_compile_test_info()`). `ModifiedNode.name` stands in for getErrorSpanForNode (TS1248).
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
- Small: TS2574 (`type_typecheck()`) checks generic variadics only if type parameters; a misplaced `TYPE_PREDICATE`
  resolves only its type's names.

## Binder and name resolution (`scope.c`)
- `binder_redirect` / `Binder.overlays`: a program adding to a shared-lib symbol gets a clone (`clone_parent_symbol`);
  the checker maps through `canonical()` (`ty/lower.c`): anything new keyed by `Symbol*` must too. Fallout: an
  augmented `Console` shows both `log` signatures (wants getIntersectedSignatures).
- `make_gather_target`: namespace imports, `import x = require()` and `export as namespace` alias a namespace symbol of
  their own (`Symbol.gather_module`) so the type prints `typeof ns`; could go once messages name modules like tsc.
- Module augmentations (`merge_augmentations`, `retarget_decl`): never merged into shared-lib symbols; an alias inside
  one meeting an existing name isn't merged; names added through an `export *` module reach only that module.
  `export =`: named imports see only the namespace side, `import("./a")` has no
  synthetic `{ default }`; `namespace_import_type` asks `ty_type_of_symbol()` about the alias to spot namespace
  imports (identifiers only; the synthetic `default` prints as `default` in symbols, tsc the module name).
- `export = a.b` / `export default a.b` is an alias (`resolve_entity_alias()`, falling back to a value symbol of the
  expression): a class static merged with a namespace interface is an alias whose `value_target` is the static, and
  `module_export_equals()` returns the alias itself (callers assuming a non-alias beware). tsc makes them one symbol
  with both declarations; tttt keeps statics out of the class's exports (exportEqualsProperty2 symbols).
  `merge_symbol()` still returns early for alias sources (`export default x` of an alias in an augmentation).
- Node16+ ESM `import * as ns` of CommonJS: `make_gather_target()` copies the module's resolved exports at link time
  (right only because nothing merges into module exports after `binder_link()`), plus `Symbol.synthetic_default`;
  in `members.c` the synthetic default's `prop->decl->symbol` is the alias rather than the module, so the symbols
  baseline prints `d.default`.
- `globalThis` is a namespace over `Binder.globals`: every global is a property (tsc: `var`s and functions);
  `ty_namespace_type` builds ~2k props on first touch.
- `arguments` has no symbol (`IdentifierExpr.is_arguments`, `IArguments` by name).
- `scope_declare_default_local()`: a compatible local before `export default function A` keeps `A`.
  `resolve_alias_target`: TS1192 suppressed only for declaration files under allowSyntheticDefaultImports.
- Exported and local declarations of a name are two unlinked tables (tsc: `exportSymbol`): TS2440 special-cased in
  `binder_link`, TS2395 a separate pass. TS2441, TS1203, TS2308, TS2661, TS2669 come from binder/link (tsc: checker);
  TS2308 re-walks `export *` apart from `module_resolved_exports()`. `ty_emits_commonjs_like()` could use
  `source_file_emit_module_kind()`.
- `block_is_instantiated`: syntactic; a namespace found via `Scope.block` is judged with the wrong outer chain.
- Class/interface members aren't symbols (`Prop` is the identity). `bind_member_symbols` + `class_decl_lookup_names`
  apply binder rules at lookup but pair computed entity-name keys by text whatever their type (tsc late-binds only
  literal/unique-symbol names: computedPropertyNames2): needs lazy pairing in the checker. `ty_member_name()` still
  evaluates every computed key. `ty_check_class_duplicate_members` / `object_bind_members` replay the binder at check
  time.
- `declare_symbol`: a declaration named like a builtin type (`class number`) gets a detached symbol.
- Merged interface/class type parameters share one symbol per name (`scope_new_generic_merged`) except against the
  shared lib; `type_parameter_is_unused` compensates.
- TS2301 (`function_collect_local_value_names()`/`statement_collect_var_names()`) is a read-only copy of
  `statement_hoist_names()`: keep in sync.
- TS1361/TS1362 valid sites (`FlowBuilder.type_only_alias_ok`, `in_type_query`) set by enclosing constructs, not
  derived. `export =`/`export default` of a type-only name gets `symbol` without `target`. `check_type_only_value()`
  scans back for `type` (PortItem has no clause span).
- Null-ErrorCtx resolution because the resolve walk reports checker errors: the body of `with`
  (`statement_resolve_names_reachable`; binder errors in it are lost too; the baseline walk still evaluates its types
  under `BaselineTestCase.in_with`) and `yield x` outside a generator (`UnaryOpExpr.outside_generator`). Real fix: the
  resolve walk stops reporting checker errors.
- `is_for_in_numeric_variable` (`ty/expr.c`): "the index is the for-in variable" by position, not checked to be inside
  the loop body.

## Control flow and narrowing (`flow.c`, `ty/flow.c`)
- Array mutation nodes (`flow_array_mutation`) only where `may_evolve`; `x.push` only as a callee (tsc: `f(x.push)`
  too). `TY_EVOLVING_ARRAY`: switch clauses narrow the final array, not the evolving type.
- Class *expression* methods are FUNCTION_IS_DECLARATION; `export { x }` doesn't mark the local assigned.
  `expr_is_narrowable_reference()`: `a = b` counts whatever the left side. `expr_is_optional_chain()` is quadratic.
- Nested destructuring assignments get assignment nodes from the outer pattern only; `pattern_path_type()` falls back
  to the declared type for rests, computed keys, iterables.
- Body-less functions get no start node (their parameter defaults have no flow). `Statement.flow` costs 8 bytes per
  statement for TS7027 only; labeled statements have none.
- `function_impl_check_end()`: anonymous `function` reported at its parameters (tsc: keyword); annotated
  async/generator functions and TS7030 on bare `return;` skipped.
- `aliased_condition` narrows identifiers only (tsc: readonly properties too).
- Destructured discriminants and dependent parameters (`discriminant_access()`, `ty_destructured_narrowed_type()`,
  `ty_parameter_narrowed_type()`) query flow with a placeholder reference (`FlowQuery.pattern`/`parameters_of`) for
  tsc's pseudo references: one extra query per read.
- Not ported (ty/flow.c TODOs): `x.constructor === C`, `[Symbol.hasInstance]`, `"a" in x` intersecting
  `Record<"a", unknown>`, `unknown` narrowed by switch, generic construct signature erasure, literal regular types.
- `chain_object_type()` (HACK): every `undefined` of an object continuing an optional chain is taken for tsc's marker,
  so `a?.b.c` with optional `b` has no TS18048. Wants an `optional` intrinsic printing as `undefined`.
- `narrowable_type`: constraint positions marked by the resolve walk (`constraint_position`); element access for
  literal indices only. A pattern with a rest element is never a contextual type (`pattern_type_has_rest`, expr.c
  HACK; tsc checks the initializer twice, CheckMode.RestBindingElement).
- `non_null_flow_type`: the `x!` rule for identifiers only. At flow depth 2000 queries silently give the error type.
- Auto types: `IdentifierExpr.outer_reference` decided by the resolve walk; isNeverInitialized is
  `!symbol->is_assigned`; "outer variable assumed initialized" decided at the start node (`Symbol.flow_start`).
- `ty_flow_expr_type()` loop results are cached per back-edge walk by generation in `Checker.links`: stale generations
  stay until the checker dies. `LazyMemo.hits`: 8 re-entrant evaluations, then the error type (runaway guard).
- `prop_has_explicit_type()` (HACK): a mapped type's property counts as explicitly typed; JSDoc-typed ones never do.
- `ty_infer_type_predicate`: synthetic condition nodes and a reference `Expr` per unannotated signature.
- Assertion signatures: a callee name without explicit type is silently not an assertion (no TS2775/TS2776).

## Declarations and bindings (AST-side walk)
- Old type world leftovers: `owned_symbol` + `symbol_ref` on `GenericItem`/`InferredType`/mapped keys,
  `key.expr.owned` + `ref` on interface members; `LAZY_TYPE_FUNCTION` memoises the already interned
  `ty_function_type()`.
- `ty_binding_assigned_type`: not memoised; a default replaces only an all-undefined element (tsc: undefined removed
  first). `ty_binding_default_type` bypasses its LazyType (`defaults_in_flight`); `binding_element_implied_type` runs
  defaults under `flow_uncached`. `binding_element_context` includes index signatures (tsc's getTypeOfPropertyOfType
  doesn't).
- Array pattern rest (`ty_binding_type`): the parent type is widened, so "any parent is any" peeks at the root
  initializer's expression type.
- `binding_add_padded_implicit_any_errors` (TS7031) only for unannotated parameters and nested defaults;
  `binding_rest_elements` loses `readonly`; `binding_add_names` doesn't widen literals of exported bindings.
- AST written by walks: `FunctionImpl.constructed_class` in `class_decl_lookup_names` (also TS1093),
  `FunctionImpl.private_ambient` by `class_decl_typecheck` just before checking the member.
- `field_type_from_assignments()`: the first real `this.x =` stands in for tsc's synthetic flow reference; `declare
  x;` doesn't take the base property's type.
- `expr_requires_scope_change()` (TS2373): hand-written expression walk without JSX; there's no generic visitor.
- `generic_params_expr_typecheck`: constraints checked against the first signature only.
- `ty_function_return_type`: a return type asked for while inferred is `any`, no TS7023.
- `lower` TYPE_TYPEOF: `typeof x` is the narrowed type, not the declared one (lower.c TODO).
- `ty_merged_value_type`: a second `Ty` next to the unmerged one (`ty_static_type_of`, what static `this` sees).
- TS1254 (`variable_statement_typecheck`): any identifier/property access counts as a literal enum reference.
- Optional parameters: `T` in the signature, `| undefined` added at use (`value_entry_type()`,
  `ty_optional_param_type()`); tsc shows it in the signature too.

## Calls and inference (`ty/call.c`, `ty/infer.c`)
- `first_unfit_skipped_literal()` (`resolve_with_signatures`): tsc's SkipContextSensitive round still relates a
  context-sensitive object/array literal argument (its sensitive functions as anyFunctionType) to the parameter; tttt
  skips the whole argument in that round, and does the literal check only for non-generic overloads of a call with
  several, against a parameter type without type variables. Doing it for every candidate (generic ones against the
  skip-round instantiation) broke typeInferenceWithExcessProperties & co. and vue (47 -> 443): checking the literal
  with another contextual type leaves memos of its members behind. Real fix: side-effect-free speculative checks
  (plan 38 stage 3's resolve-once).
- Repeated resolution stand-ins (plan 38 stage 3 would resolve once): tttt resolves a call several times (error pass,
  outer rounds, overloads). `KEY_ARG_FIXES`/`ArgFixes` replay what each argument fixed and to what;
  `LazyMemo.speculative` (`lazy_type_set_speculative()`) keeps an outer round's call from replacing the memo made
  under the instantiated context (xstate's `DoNotInfer` leak). `KEY_CALL_RESOLUTION` (`ty_call_typecheck`) skips
  re-resolving when the last resolution would report nothing, set by hand via `mark_would_report` at every error site
  of `resolve_call`/`resolve_with_signatures` (new diagnostics there must set it). `KEY_CONTEXT_RETURN` is rewritten
  per check (`set_contextual_return()`); `ExprCtx.sig_contextual` only where `may_want_signature()`.
- Higher-order (`inference_propagate_generic`, `Arg.generic_function`/`generic_call`, `waits_for_sensitive`): a failed
  call is re-inferred without them, then arguments re-checked against the failed candidate only to restore memos
  (`add_implementation_success` likewise). `ty_generic_function_in_context` (HACK) isn't tsc's branch.
- Return mapper (`infer_type_arguments`, HACK): the contextual inference through the outer return mapper fills only
  type parameters without candidates (using it for all: tsc-exact on tests, valibot +9%). Non-sensitive function
  arguments get their parameter type fully instantiated, others top level (`instantiate_instantiable`). A pattern's
  return mapper drops inferred `any` (tsc's nonInferrableAnyType) and rest patterns give none; `ty_pattern_unclone`
  copies the pattern type per call. Nested literals instantiate context only in `check_for_mutable_location()`.
- `Inference.circularity`: `visited` records no priorities, so a pair is circular only while still on the stack.
- Reverse mapped types are eager (`Checker.reverse_mapped_depth`, HACK; tsc lazy), four priorities only.
  `infer_from_objects()`'s homomorphic shortcut stands in for reverse mapping, skipped for a deferred `keyof X` whose X
  isn't inferred (stricter gates lost three tests); `infer_to_mapped()` finding nothing returns (tsc goes per
  property; neutral). `inference_call_result()`: combined covariant candidates literal-reduced (tsc: subtype-reduced).
- Skip-sensitive first pass: `return_only_type()` gives generators anyFunctionType; `first_unfit_argument` skips such
  arguments in the first overload round; isPartiallyInferableType only as a priority. Intra-expression sites
  (`ty_add_intra_expression_site()`): not for JSX attributes; `context_is_tuple_like()` misses call spreads.
- JSDoc type parameters (`meets_jsdoc_node_in()` in `assignment_mentions()`): fixing only, "in scope" by source
  position; tsc instantiates every JSDoc node for its outer type parameters (triage).
- `const` type parameters: no mapped/conditional/variadic cases; nested array literals found by syntax
  (`add_nested_array_literals`) plus `Inference.array_literals` (ty_internal.h HACK) because interned tuples can't carry
  ObjectFlags.ArrayLiteral; `...args: T` gets `T[i]` context only when const.
- `clone_type_params`: fresh type parameters only when bounded or the mapper brings type variables; else shared.
- `ty_union_signatures` not cached; `resolve_call` falls back per member when one has no signatures (TS2348 for tsc's
  TS2349). `reorder_candidates`: literal overloads first, no merged-group order.
- `super(...)` (`resolve_into`): heritage type arguments passed as explicit, inferred when unwritten (tsc: defaults).
  `resolve_super` walks scopes; `check_before_super` only when the nearest function scope is the constructor.
- `report_arity`: too few arguments at the callee's `inner_span`; generic rest arity ignores spreads/candidates.
- `tagged_template_call()` (HACK): a stand-in CallExpr with a shallow copy of the tag as callee
  (`CallInfo.tagged_template`); a TaggedTemplate node or argument-list abstraction would avoid it.
- `signature_resolve`: function expressions take the contextual signature's type parameters only if resolved after
  the context was assigned.
- `ty_check_decorator`: signature rebuilt per check (each decorator is checked once, so harmless so far).
- Generators: no next type from `yield` use; `ty_generator_context` filters only unions. function.c HACK: a
  non-iterable `yield*` prints its type while the return type is in progress, making `yield* foo` in `foo` any.
- Async: `Promise`/`PromiseLike` by name (no thenables). `ty_awaited()` wraps generics in `Awaited<T>`, but generators
  use `ty_awaited_no_alias()` and async returns `ty_awaited_returned()` (call.c HACKs) because tttt can't relate
  `Awaited<T>` to T through the conditional's default constraint; once it can, use ty_awaited() everywhere.
- Contextual `this` (`ty_contextual_this_type`): literal context/parent remembered at check
  (`KEY_LITERAL_CONTEXT`/`KEY_LITERAL_PARENT`, last wins); `ThisType<T>` by name, instantiated with
  `ty_instantiate_fixing()`.

## Expressions and contextual typing (`ty/expr.c`, `ty/access.c`)
- Expression memo (`check_with`, `LazyMemo.context`): an `Expr`'s memo is the last contextual check's;
  `kind_uses_context` decides rechecks. Rests on "whoever knows the context checks first";
  `initial_or_assigned_type()` re-derives it for `x = e` and annotated declarations, compound/logical assignments and
  destructuring defaults don't. Union order depends on who asks first.
- `assign_contextual_signature_as`: once per function (`KEY_CONTEXT_DONE`), from the first usable context; generic
  contextual signatures aren't instantiated. `KEY_CONTEXT_PARAM_PATTERN` marks pattern fallbacks for the symbols
  traversal. `KEY_CONTEXT_IIFE_RETURN`: recorded at the IIFE's first contextual check; an earlier return type misses it.
- Apparent contextual types: `intersection_prop_context` intersects per-member answers (tsc: the intersection's
  apparent type once, distributing over union constraints); `get_prop_context`/`contextual_signature` take apparent
  types at use; `apparent_contextual_type` maps only for `this` in literals. `context_map` drops `unknown` parts when
  others contribute (tsc: noReductions). Contextual union members are reduced (`string | "a"` is `string`).
  `discriminate_by_object_members` skips optional properties a literal omits.
- Excess properties (`has_excess_properties`): generic mapped keys count as known; no nested re-check for
  intersection targets; `discriminated_union_target` has no key-property map.
- Destructuring assignments (`assignment_target_typecheck`): object rest unchecked; for-of destructuring (statement.c
  HACK) relates the regular element type (tsc: per property); array literal targets are arrays, not tuples.
- `missing_property_type` (HACK): a failed access is errorType under conditions approximating check.c's reporting
  (object type, not `typeof globalThis`, no unresolved base, not JS; unchecked JS's "declared in another global file" by
  the object name's file). tsc: errorType exactly when it reports.
- Expandos on functions are checked against existing members; `is_container_declaration()` approximates
  isAssignmentDeclaration for `ns.a = {}`.
- `check_array`: tuples only from tuple-like context, patterns or `as const`. Bigints are doubles (`Value.bigint`).
  `satisfies` doesn't narrow. Literal ids follow tsc only where made in the same order (`1 | 0` vs `0 | 1`).
- `ty_apply_type_arguments` (instantiation expressions): ill-fitting signatures kept; `instantiate_anonymous`
  (instantiate.c HACK) skips object types that also mention the parameters; display is the expanded object.
- `ty_non_nullable` (checker.c HACK): a generic whose constraint allows nullish gives the stripped constraint (tsc:
  `T & {}` printed `NonNullable<T>`). The real getNonNullableType is `ty_non_nullish_part()` (flow only): switch the
  ~24 callers and print `{}` intersections as `NonNullable<T>`.
- `ty_access` skips nullish union members silently (TS18047/18048 come from `check_access`). `member_refs()` (check.c)
  duplicates `access()`'s walk to get `Prop`s without creating types.
- `PropertyExpr`'s four resolve-walk pointers cost 32 bytes per access; a `Scope const*` would do.
- JSX: `jsx_signatures_of` types unknown intrinsic tags as error (tsc: TS2339); the head rule (`ty_jsx_skips_head` via
  `Checker.jsx_element`) only inside `ty_check_jsx_element`; attribute types skip the regular object literal state.
- `contains_spread` (members.c HACK, ObjectFlags.ContainsSpread): `{ ...{} }` doesn't count.
- `TY_RELATION_ASSIGNABLE_STRICT_NULLS` (ty.h HACK): the operator checks' "extends number" never takes nullables.
- `ty_unresolved`: unresolved type names are intrinsics with `TYF_ANY | TYF_ERROR` interned by name.

## Relations (`ty/relate.c`)
- Distributive constraint of a conditional source (`structured_related`'s TY_CONDITIONAL case): tsc takes the check
  type's immediate constraint (getConstraintOfType: `(any[] & T)[K]` -> `(any[] & T)[keyof T]`), tttt's
  `distributive_constraint()` its base constraint. The relation tries the base one first, then the immediate one
  (`distributive_constraint_as(.., true)`, cached per conditional) only when a base one exists (a stand-in for tsc's
  hasNonCircularBaseConstraint: without that gate react-hook-form's Path types in bulletproof-react hit TS2589), and
  reports through the immediate one like tsc. Not while measuring variances (it walked react-hook-form's Path types
  through marker instantiations, +12% instructions on bulletproof-react): the measurement is reported unreliable instead. `base_constraint()` and the other users keep the base version: the
  immediate one there stops at unions of conditional types (flow's constraint substitution showed them). Real fix:
  tsc's constraint functions (getConstraintOfType vs getBaseConstraintOfType) as two separate, cached functions.
- `structured_related_members` TY_CONDITIONAL source (HACK): the first try of the true/false branches pushes the
  target on the deep-nesting stack too, where tsc relates its default constraint with RecursionFlags.Source only (the
  other generic-source constraint steps follow tsc). With tsc's flags react-hook-form's Path/FieldPathValue chains
  explore ~10% more of the react corpus; the indexed-access step's tsc flags still cost it 4%: nested `T[a][b]...`
  constraint chains are related level by level, the Maybe results inside aren't cached, and each variant is computed
  about 12 times (see the commit "Conditional sources' first branch check pushes the target again").
- Error reporting (plan 34): `flush_incompatible()` reads the property name back out of its message; `headMessage`
  approximated by `RelationState.at_top`; `report_truncate()` doesn't restore `skipped_source/target`,
  `override_next_head`. `intersection_props_related`'s TS2741 member choice fitted to one probe
  (`test/probes/plan19/valibot/overload_omit_intersection_missing_prop.ts`).
- Elaboration (`elaborate_error`) reads memos (`ty_mutable_location_type`, `ty_array_literal_as_tuple`) instead of
  re-checking; `ty_best_matching_type` has no key-property map; `type_decl_span` covers declared types, function type
  nodes and type literals only (renaming `as` properties lose declarations).
- `RelationState.intersection_source` (HACK): tsc keeps IntersectionState.Source into properties; doing so hit zod's
  instantiation limits.
- `structured_related_members`: union targets are related against `ty_widened_objects(source)` (tsc only
  regularizes); the distributive-constraint fallback for intersection targets covers conditional sources only.
  **Failed port**: relating generic source constraints with RecursionFlags.Source only (`is_related_to_side(..., 2)`,
  6c7de767, reverted) cost bulletproof-react +14% instructions, valibot +1%; tsc does the same walk, so tttt probably
  misses a cache. Redo with a profile.
- `props_related`: the same-symbol skip is recognised only for object literal accessors of one function.
- `generic_mapped_target_related` (HACK): a generic source failing isGenericMappedType falls through to member
  comparison, which memberless mapped types pass (`Readonly<T>` to `T`); arrays, non-variadic tuples and objects
  without properties don't (tsc: nothing but a mapped source does). What still needs it: zod's `SortItem<output<T>>`
  against a Flatten-like `{ [K in keyof A | keyof B]: (A & B)[K] }`, `[...Foo<T>]` for a lone spread argument (tsc's
  getSpreadArgumentType has `Foo<T>`, restTypeRetainsMappyness). `write_constraint_of_indexed` (HACK) lacks
  NoIndexSignatures. A TY_KEYOF target over a generic mapped type (HACK) lacks getApparentMappedTypeKeys.
- Conditional targets: skipFalse yes, skipTrue no (HACK): `ty_definitely_unassignable`'s wildcard instantiation is
  exponential on zod's union-to-tuple types because shared subterms are re-instantiated under fresh mappers (tsc caches
  by mapped outer type arguments). Fix the sharing, add skipTrue, then drop `first_unfit_argument`'s HACK (no `[]`
  check for a missing generic rest; xstate's `ConditionalRequired`).
- `apparent_type`: indexed/conditional without base constraint stays itself (tsc: `{}`); an intersection member whose
  apparent type is any/unknown stays itself (HACK) because `base_constraint()` of a conditional keeps `any` branches
  (webpack's `Reflect.get(...) & any[]`). Fix base_constraint().
- Array<S> -> ReadonlyArray<T> relates element types (HACK); `is_rest_array` (HACK) takes any non-type-parameter as a
  rest array.
- Variances: arrays/tuples/synthetic targets hardcoded covariant; `VARIANCE_UNRELIABLE` from a checker-wide flag set
  when any deferred/mapped type is compared during measurement (tsc: only via a marker), cached as
  `CACHE_UNRELIABLE`; no Unmeasurable. Alias markers by type id (`KEY_ALIAS_MARKER`).
- Identity (`identity_structured`): differently shaped tuples never identical, references compare arguments only, no
  `infer` in the two-conditional rule. TS2403 (`check_subsequent_declaration`) uses it and exposes every
  approximation between two spellings.
- Cache keys are plain (source id, target id). `RelationState.overflow` (100 deep) reports TS2589 (HACK; tsc TS2321).
- `this` types: `Prop.thisless` follows isThisless; a reference with a this-argument relates structurally.
- `check_assignable` (check.c HACK): source `any` passes even to `never` (not for arguments any more). Letting only the
  error type through cost 25-30 tests (narrowing/exhaustiveness give any where tsc has never).
- Alias variance (`in out` annotations): the reporting pass falls back to the interface's own variances on an
  invariant failure only for two references to the same target; other shapes keep the old path. The null-base TS2417
  check takes the first `extends` of the first merged interface, not getBaseTypes.
- `TY_INDEXED`'s IncludeUndefined (noUncheckedIndexedAccess) is dropped wherever an indexed access is rebuilt from its
  parts (`simplified_parts`, `ty_actual_type_variable`).

## Type core (`src/ty/`)
- `UnaryOpExpr.no_type_params_in_scope` approximates getResolvedSignature's instantiation for `yield` (TS7057): only a
  bare type parameter counts as a call's, a contextual `T[]` is taken as is.
- Union origins (`Ty.composite.origin`): not instantiated, dropped by union-mapping helpers (plan 37 B stage 2).
  `dedupe_unions` merges same-member unions with different origins in intersections (display only).
- `ty_realias`: a renamed object type is a second `Ty` (members resolved twice); renamed deferred types are copies. No
  "local alias" exception. `renames` in `lower`: whether an alias instance keeps the name is syntactic.
- `ty_conditional_resolve`: an unsatisfied `infer X extends C` falls back to C only if C is then non-generic
  (conditional.c HACK: the generic fallback leaked valibot's `TValue` because deferred conditionals don't keep their
  inferences, `inferred_true_branch()` recomputes; `test/probes/plan19/zod/ark_infer_constraint.ts`).
  Permissive/restrictive instantiations only replace what `mentions_type_variables` finds (8192 types / 48 deep, then
  "unsure": defer), since TYF_COULD_CONTAIN_TYPE_VARIABLES is on every function/object literal. `infer` constraints
  mentioning type variables unchecked; `ty_implied_infer_constraint` lacks the mapped-extends rule.
- `instantiate_conditional`: outer parameters from `collect_free_params`, falling back to the uncached full-mapper path
  on `TyConditionalOuter.unknown`. `collect_free_params` gives up on `typeof`, `this`, computed keys, initializers,
  every function and merged value type. `TyMapper` is flat.
- Substitution types: the true-branch chain is `TypeNode.flow` from the resolve walk; no `number | ${number}` mapped
  key rule. `mentions_type_variables` treats substitutions as unsure (precision sent react-hook-form into overflows).
- Mapped types: homomorphism (`homomorphic_variable`) needs a syntactic `[K in keyof X]`; `expand_mapped` expands once
  keys are literal unions, modifiers only for written `keyof T`; `mentions_mapped()` (ty_display.c) compensates for
  instantiating member signatures that can't mention the parameters. `instantiate_mapped_over_list`: syntactic, bails
  for rest/variadic tuples, list unions, `any`, list-constrained parameters.
- `ty_simplified` only for indexed accesses; `ty_keyof` of primitives only for intrinsics/literals; `ty_indexed` stays
  deferred beyond props, index signatures and list elements.
- `class_base()` (members.c HACK): base class names found syntactically; other expressions take the first fitting
  construct signature (no TS2510). A base constructing `any` is no base; `ty_has_unresolved_base()` gates
  TS2339/TS7053 instead (tsc: `[x: string]: any`). Type-parameter bases take the constraint's members at declaration.
  `ty_non_augmenting_base()` from syntax. `ty_check_static_heritage` (check.c HACK) adds `& K` to the source because
  `ty_static_type_of` doesn't make `X & K` bases' static side `typeof C & K`.
- A bare `C`/`this` inside generic `C` isn't its own instantiation (lower.c TODO).
- Enums (`ty_resolve_enum`, `evaluate`): no `const` object property accesses; other files' `const`s count as constant;
  ambient non-const members auto-increment (tsc: computed).
- `has_conflicting_discriminants` (checker.c HACK): intersections reduce to never when made, not lazily; in-flight
  props (`ty_node_reaches_alias_in_flight`, `members_judgeable`) are skipped and the answer not remembered though the
  intersection is interned. Real fix: a lazy reduced view.
- `reduce_type_variable_intersection` (checker.c HACKs): type parameters only, not indexed accesses (valibot's
  `NonNullable<TSchema['~types']>` became never via a premature `undefined` base constraint); constraints with a
  deferred type aren't reduced (cost valibot 5%, `primitive_constraint_of`). Real fix: cheaper conditional base
  constraints (bugs/perf_leads.md).
- Generic tuples: `base_constraint` replaces only variadics with array constraints; `normalized_tuple` makes
  `...readonly T[]` into `...T[]`; `lower_tuple` keeps the written `T[]` for rests (tsc: `T`).
- `lower_identifier`: `object` is a builtin declaration named "object". lower.c HACK: composites skip the placeholder
  `parse_type` makes for a leading `&`/`|`.
- `builder_add_interface_member`: index signature types lowered eagerly; non-literal computed keys dropped (tsc:
  late-bound index signatures).
- `ty_union` doesn't subtype-reduce (`ty_union_subtype_reduced` on request) or drop nullables without
  strictNullChecks. Cross-product caps give the error type silently: `intersect_unions` and `ty_template_literal`
  (100k, TS2590 unreported), `normalized_tuple` (10k).
- `apply_mapping` (template.c HACK): Uppercase & co. cover ASCII, Latin-1, basic Greek, Cyrillic; placeholders step
  code points (JS: UTF-16 units). `${symbol}` templates are silently `string`.
- Unique symbols: escaped names (`__@sym@12`) mapped back through `ty_unique_symbol_of_name`, so a string property
  `__@x@3` passes as a symbol; `instanceof_target_ok` matches `__@hasInstance@<id>` by text; one unique symbol per
  declaration (tsc: per merged property), so `check_subsequent_property` (check.c HACK) skips TS2717 between them.
  `SymbolConstructor`'s `symbol` properties are unique (interface_decl.c HACK, tsc's too).
- Private names (`ty_private_name`): lexical lookup scans class items by text per access; an undeclared one looks up
  the plain `"#x"` atom. Object literal accessors have no `Prop.write_node`.
- `Checker.current_node` (TS2589/TS2615/TS4110/TS1062 location): `lower()` makes a generic reference current while
  lowering it (a guess at tsc's order). `Foo<X>` isn't lowered in the walk (resolving every alias changed type ids).
- Instantiation limit: property types computed past `ty_instantiation_limit_reached` aren't memoized, but signatures,
  members and variances keep what an exhausted budget produced: one TS2589 can poison later users.

## Checks (`ty/check.c`, `unused.c`, `statement.c`, `class_decl.c`)
- `check_access` (TS2339, HACKs): lets through generic mapped types and unresolved bases; bracket access never reports;
  fires on modules too (wrong resolution shows as TS2339 next to TS2307). `ty_indexed_access_index_ok` lacks TS4105.
  `ty_check_element_implicit_any` (TS7053, HACK) skips generic mapped types and unresolved bases.
- `ty_check_type_reference_constraints` (HACK): deferred conditional arguments aren't checked for TS2344 (default
  constraint lacks inferConditionalConstraintMappedMember; 2000 conditionals vs a 2000-member union exhaust the budget).
- `ty_check_index_constraints` on type literals only when syntactically needed: evaluating a literal early changes
  union order and loses the alias name.
- Syntactic stand-ins: overload grouping (`statements_check_overloads`, `class_check_overloads`), TS2729
  (`initialized_in_static_blocks`, HACK: top-level `this.x =` instead of flow), member kinds in
  `class_check_member_overrides()` (`class_instance_member()`, not symbol flags; TS2612 skips what
  isPropertyInitializedInConstructor decides), `check_property_modifiers` (TS2687, HACK) skips methods and accessors.
- `ty_check_object_type_duplicate_members`: `MemberTables` is a fixed 128-entry linear array; later members unchecked.
- `ty_check_heritage`: TS2320 for interfaces only (`inherited_props_identical`).
- Emit helpers (`ty_check_emit_helpers`): spans by hand (`decorator_span()`, `function_error_span()`); "ambient"
  approximated per site (`ClassDecl.is_ambient`, `Binding.ambient`, `BlockTypecheckCtx.ambient`; want
  NodeFlags.Ambient); `for await` unchecked.
- `unused_mark_jsdoc_links()` (HACK): `{@link X}` is a use by text scan (JSDoc is parsed now: resolve from tags).
  Member reads are a table keyed by declaration span start (`ty_mark_member_referenced`).
- `for_in_accepts`: type parameters and deferred types accepted outright (TS2407 missed).
- `program_apply_comment_directives`: suppression by tttt's error *phase*, not tsc's diagnostic kind; `\n` and ASCII
  `\s` only; relies on every comment going through `hop_comment`.
- `SourceFile.unchecked` + `error_ctx_remove_unchecked`: the JS/skipLibCheck gate filters after checking;
  `skipDefaultLibCheck` unread. Plain JS is checked in full and filtered to plainJSErrors (as tsc).
- `error_ctx_global_error_s`: file-less diagnostics have a null span, sorted first; `error_ctx_error_s` drops invalid
  spans; `error_cmp` orders by span only.

## JavaScript and JSDoc (plan 26)
- JSDoc is consumed at parse time, not stored: `jsdoc_before()` re-parses the last comment before the host into
  transient `JsDocTag`s; JSDoc parse errors dropped (no TS8032). `jsdoc_bind_comments()` parses every
  `@typedef`/`@callback`/`@import` comment again and claims it by innermost statement list (claims from failed attempts
  stay); `@import` only at list top level; dotted/nameless typedefs skipped; typedefs sharing a comment share its
  templates. `ParserShared.jsdoc_enum` is a one-slot handoff that statements outside a list drop.
- `@type` on assignments reaches the symbol via `SymbolDecl.jsdoc_type`, threaded through every assignment-declaration
  site in scope.c (new sites must pass it; chains: outermost only). `jsdoc_apply_type_signature()` *moves* the
  TypeNodes of a written function type. `jsdoc_prototype_type_param()` sees F's type parameters anywhere in a
  prototype method. `Statement.expr_jsdoc_type` shares a union with `expr`: every site building an expression
  statement must clear it. `jsdoc_reference_reusable()`: `@import` aliases aren't reused by the printer.
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
- CommonJS: module indicator syntactic (`saw_commonjs`, failed attempts too). `exports.x =` from top-level statements
  and their blocks (`add_nested_commonjs_exports()`), in functions only in IIFEs; `Object.defineProperty` only as a
  statement; `is_exports_alias()` module-level only (queue of 16); `require()` is parser-synthesized `PortItem`s (flat
  patterns only); `readonly_assignment()` never reports `exports.x =`. `export=` absorbs other exports in place (tsc
  copies: typedefCrossModule3); `exports.x = a.b` isn't an alias; `import * as ns` of CommonJS has the module's type.
  Exported class expressions alias the class (`scope_reparent`, top level only).
- Constructor functions: function type plus a construct signature (`resolve_members()`, `ty_js_constructor_class()`
  prints `typeof F`). Its class symbol is the variable's/property's (`set_function_class_symbol()`; tsc: the function
  expression's), patched in `ty_js_assigned_class_name()`, `symbol_chain_append_entity()`,
  `this_compile_test_info()`. Object-literal functions assigning `this.x` aren't classes (tsc: they are;
  objectPropertyAsClass). `F.prototype = {...}` members are added at member resolution
  (`builder_add_assignment_members()`), not merged symbols (mergeJSSymbols): `F.prototype.m` and `this.m` are one
  member, prototype accessors don't merge with `this.x` (jsDeclarationsFunctionLikeClasses2). Reads in a constructor
  *function* aren't auto-typed (`constructor_declared_read()` is class-only); every plain JS function gets a return
  flow node in case it's a constructor. TS2417 doesn't see JS class expandos.
- Spurious TS2339 in JS (~15 tests) from binder gaps: `Object.defineProperty` declarations, class-static expandos,
  `var X = m.X = {}`, `exports["e"] =`, `module.exports.a.cat =`, prototype-literal members seen from `this`.
- TS2738 (`IdentifierExpr.this_shadows_outer`) is decided in the resolve walk. A JS literal assigned to an any-typed
  target is closed only for identifier targets (`check_binary`). `scope_add_port()`: no `export as namespace` in JS.
- JS syntax errors (TS8xxx, `js_syntax_error()`) come from the parser; what tsc skips inside is erased after
  (`js_syntax_skip_since()`): every skipped construct needs its call. Node array spans run first to last element.

## Modules and program (`module_resolution.c`, `tsconfig.c`, `program.c`, `main.c`)
- `.js` files under allowJs are parsed as TypeScript; a `.js` root without allowJs (TS6504, `program_load`) and a
  refused import (`MODULE_REF_IS_JS`) are two paths.
- `module_resolver_resolve` (HACK): node10 and export-map packages resolve in two passes (TypeScript extensions
  first), reproducing tsc's selfNameModuleAugmentation behaviour (microsoft/TypeScript#50762) by other means.
- `apply_realpath` loses the original path; no package ids (no duplicate-package handling).
- `/// <reference lib>`: the CLI loads it after the program's files (`program_follow_lib_references()`; tsc: libs
  first; unknown names silently ignored, tsc TS2726). The harness adds it to the cached lib list up front by text scan
  (`load_test_source`); `@libReplacement: true` is unimplemented (libTypeScriptOverrideSimple & co.).
  `program_load_reference_path()` probes extensions by hand.
- `tsconfig.c`: config errors are one `failable` message (no TS5xxx); `include` globs always skip dot and package
  directories (tsc: only under `**`); the allowJs gate depends on option read order.
- `main.c`: strict by default only without a tsconfig; `default_libs` when nothing picks `--lib`/`--target`;
  `--keep-going` unvalidated.

## Display and baselines (`ty_display.c`, `symbol_chain.c`, `baseline_test_case.c`)
- `symbol_chain.c` (getSymbolChain) sees only scopes the traversal tracks (`BaselineTestCase.enclosing`), so
  function-local shadowing isn't qualified; `try_locals_with_exports()` merges by position; no alternative containing
  modules, firstVariableMatch or accessibility cache. `append_qualified_name` always fully qualifies in messages.
  `out_unique_symbol_name` (HACK) knows only the `XConstructor` variable pattern. `baseline_test_case_module_alias_text`
  only handles chains ending at the export.
- Node reuse (`display_node()`/`display_annotated()`): `name_accessible()` resolves from `enclosing` only; signatures
  reuse the declaration when the mapper left it alone (`mentions()` conservative). `T_1` renaming (`TyParamNames`,
  `baseline_test_case_enter_generic()`) restates resolveName by hand. `display_node_identifier()` assumes ASCII module
  names. Syntactic typing (`single_return_expr()`, `FunctionImpl.syntactically_contextual`): no JSX; index
  signatures, tuple names, defaults always synthetic; enum member annotations print `E` (tsc: `E.x`).
- `index_components_printed()` (HACK): isEntityNameVisible for `{ [k]: T }` approximated (function-declaration
  parameters visible even nested; no shadowing check since `TyDisplay.enclosing` misses enclosing parameters).
- `TyDisplay.widen_literals` (HACK) prints literals in expression-made object types as primitives.
  `ty_elides_default_type_args` (iterate.c HACK) always prints `Iterable<T>`. `is_nested_expando_function`: `typeof
  f` only for non-local expando functions (observed, mechanism unknown).
- The `any` underline rule reads whether tsc's `.errors.txt` exists; `BaselineTestCase.any_keeps_underline` leaks to
  the next line if no types line follows.
- Symbols lines: container from the traversal or `Prop.parent`; `collect_prop_symbol()` skips non-object union members
  (strict lost 40 tests by exposing narrowing gaps); `index_symbol_compile_test_info()` never for lib index signatures
  (unexplained); `symbol_declaration_spans()` puts functions first per block (maybe redundant). Span-keyed overrides
  (`commonjs_symbol_overrides()`, `prototype_symbol_overrides()`, `this_property_symbol_overrides()`) stand in for
  getSpecialPropertyAssignmentSymbol. JSDoc tag Decls get no leading trivia (sniffed). A module's `default` export
  symbol is named like its first declaration.
- `callee_compile_test_info`: `f<T>` gets its own line only when `?.` follows (source peek); `a<b>?.()` keeps an
  `EXPR_GENERIC_PARAMS` callee.
- `render_types_or_symbols_file` drops one trailing line break per unit (a last unit before `// @link` lines is off).
- `display_type` depth guard (`DISPLAY_TYPE_MAX_DEPTH`): probably dead; check and delete.

## Harness and tools
- `apply_setting`: `@baselineFile`, `@fullEmitPaths`, bad values ignored; option diagnostics skipped when a
  `tsconfig.json` unit exists.
- `mount_test_lib_folder` copies tsc's `tests/lib` per test; `baseline_test_cache_get_program_with_libs` keeps one lib
  `Program` per lib list, never evicted.
- `@noTypesAndSymbols` counts symbols/types as passed (~900 free passes). `TTTT_IGNORE_UNDERLINES=1` is measurement
  only.
- `engine.c` (reads `test/baseline_results.ts`): no let/const/var distinction, drops array property assignment,
  `value_loose_eq` stringifies. `string_to_double()` (value.c) isn't correctly rounded.
- `failstats.py`'s `line_ratio` is multiset overlap, not a diff. `bench/make_corpus.py` replicates passing tests
  (flatters tttt).
- Commits f3b836b..f899e4e don't compile (fixed by 7f2a7fa): skip when bisecting.
