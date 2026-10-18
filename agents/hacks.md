# Hacks
Suboptimal-but-working code, by subsystem. Each entry: where (function names), what is approximated, the correct rule,
and the oracle that would show it wrong. How things work belongs in documentation.md; missing diagnostics and features in
`bugs/*.md`, triage.md or a plan. `grep -rn HACK src` finds the code-side markers.

## Global state, memory, parallelism
- `ast_free()` / `ast_delete()` (ast_alloc.c): the AST arenas trust the size they're given, and a too-big one hands
  the next allocation of that size someone else's bytes. `ast_delete()` takes `sizeof(*p)`, so a box allocated short
  (`property_dot_new()`, `require_ports` arrays) needs its real size at every free; nothing checks it. Correct: a
  size-checked debug build (a header word per block). Oracle: the suite under a build that poisons and verifies.
  Also: dropped arenas' addresses are never reused (virtual space grows by every AST a process ever parsed: the test
  harness, `-b`), `ast_owns()` assumes 47-bit user addresses, and free blocks left when a file ends are lost (3%).
- `huge_pages_take()` (prelude.c): whether an arena chunk is madvised for huge pages is decided by a budget of the free
  2 MB blocks `/proc/buddyinfo` listed at the first call, a guess at what the kernel can give without compacting
  synchronously. Correct: whatever the kernel would tell us (nothing does, short of watching `compact_stall` in
  `/proc/vmstat`). Oracle: `compact_stall` and `thp_fault_alloc` around a vscode run (measurements.md "Plan 63").
- `source_text_map()` (source_file.c): source text is a MAP_PRIVATE mapping of the file wrapped in a hand-built long
  `cstr` (STC's private layout: `cstr_l_set_cap`), and `source_texts_release()` drops its pages at every recycle. A
  file changed by another process mid-run is read changed, a truncated one is SIGBUS (clang's MemoryBuffer has the same
  trade); only tttt's own declaration writes are guarded (`source_text_detach()`, a linear scan of every mapping per
  written file). Anything that drops or mutates `SourceFile.contents` must check `text_mapping` first. Correct: a
  `csview` text field. Oracle: the corpora and sweeps (the harness's mock fs never maps).
- `check_share()` / `checker_memory()` (program.c, ty/checker.c): checker recycling assumes no diagnostic depends on a
  checker's history; false in a few places (undefinedProperties member order: 4 vscode `.d.ts`; bugs/sourcesweep_checker.md
  "History dependences recycling exposes"). `checker_memory()` leaves out LazyMemo pages (real memory the budget doesn't
  see), and the 256 MB/128 floor budget is a wall-time compromise. Correct: no history dependence. Oracle:
  `scripts/kdiff.py -k 1 -m 0`.
- `current_checker` / `current_lazy_slots` / `compiler_options()` (thread locals, `checker_make_current`): how the AST
  side (`*_typecheck`, `*_evaluate_type`, `ty_of_node`) finds its checker; left set after `program_typecheck` for the
  test traversal. Correct: a passed check context (checker + errs + options). Many parsed options are inert (`grep -o
  'compiler_options()->[a-z_]*' src`).
- `Checker.return_context` (`function_impl_enter_return_context`): the contextual type of `return`s is saved checker
  state where tsc walks up to the function (getContextualReturnType). Wrong for a `return` evaluated outside its body's
  check (shows as types-baseline/TS2322 differences on returns).
- LazyType slots (`lazy_slot_new`): process-wide numbers, never reused, taken eagerly (panic past 1G slots: vscode
  takes ~12M, re-materialized bodies take new ones). Handed out in per-thread blocks of a memo page, a fresh one per file
  (2026-10-17), the pages a checker touches are still only ~60% used (vscode: ~7 MB a checker at 4 checkers, ~20 MB
  at one): what's left of the sparseness is memos never evaluated, small. Per-file numbering would end the panic. `lazy_type_drop` resets a slot only if a checker is current, and AST
  slots rely on `checker_drop` running before the AST is freed.
- Once-per-file diagnostics (`Error.once_key`, `error_ctx_keep_first_once()`; TS2343/TS2807 from
  `ty_check_emit_helpers`): with K checkers tttt keeps the one of the checker whose file comes first; tsgo keeps the
  owning checker's. A new "said once" checker memo must be tagged. Oracle: `scripts/kdiff.py`, `checkerdiff.py`.
- Per-checker variances and relation caches: a TS2589 count is charged to whichever node first needs them (tsc pays
  once), so a TS2589 near the 5M limit, and circular variance measurements (which follow the files a checker saw), can
  depend on `--checkers`. Oracle: `scripts/kdiff.py`, `checkerdiff.py`.

- Deferred bodies (plan 78; how they work and what a drop puts back: documentation.md "Deferred bodies"). The
  approximations:
  - Body walk outputs are read through materializing accessors (`function_impl_walked()`, trailing `_` on the raw
    fields); left raw: an untyped uninitialized field's `ts_assigned_` (`class_untyped_field_ts_assigned()`, the parser
    keeps that constructor eager), `outer_this_function`, `uses_arguments` (JS, eager). A new raw reader of a body's
    output is a silent order dependence. Oracle: the suite against `--no-defer-bodies`, kdiff/checkerdiff.
  - What isn't deferred is decided by syntax (`skip_body()`, `undefer_body()`): each case is a names-phase reader of the
    body (parameters with expressions, a `(` after the parameter list, IIFEs, constructors of classes with fields,
    `import`/`require` in a body, regex-or-division ambiguity, non-element `<` in .tsx). Correct: those readers lazy, or
    answered from the parse. Cost: vscode undefers 5.3k of 96k bodies (`TTTT_DEFER_STATS=1`).
  - A new write outside the bodies from their walk must be undone at a drop (`deferred_bodies_undo()`) or a new way of
    keeping a materialization alive must allocate it lasting (`AstArena.lasting`; owner materializations carried cost
    vscode K1 +155 MB). Oracle: `TTTT_AST_POISON=1 --checker-memory 0` on the corpora, `kdiff.py -k 1 -m 0`.
  - A dropped materialization leaks its parser's malloc'd strings (~8 MB per round of vscode's bodies). Correct: those
    strings in the AST arena. Re-materialization costs ~2% of vscode's instructions (bugs/perf_leads.md).
  - Signature/body name collisions and body syntax errors rerun the whole program without deferral
    (`program_defer_failed()`). Correct for collisions: walk the signature again after the body's names. Oracle:
    `defer: restarts` on the sweeps (0 on the corpora).

## Parser
- `unique symbol` placement (TS1335): checked while parsing through `ParserShared.unique_allowed_at`
  (`parse_type_inner`, `expect_annotation_type`), not on the tree (checkGrammarTypeOperatorNode), and never in JSDoc,
  where tsc asks the tag's host. Oracle: JS suite tests / mutate.py.
- Strings are UTF-8: a lone surrogate escape is kept as WTF-8 bytes (`append_code_point()`), which strict decoders
  (`cutf8_encode`) drop; `regex_check_literal()` ports tsc's UTF-16 scanner (`utf8_byte_utf16_units()`). A NUL byte is a
  character for tokens and lists (`at_eof()`) but some lookaheads still stop at it.
- Escaped identifiers (`name_cooked()`, span.c `name_cooked_slow`): a global mutex-guarded registry, each lookup a linear
  scan of the file's cooked names (quadratic for thousands). Comparisons against literals (`span_equals(name,
  "constructor")`) and `EnumCase.name` of string members stay raw (bugs/binder_modules.md "Escaped identifiers");
  `hop_escaped_keyword()` reports TS1260 even speculatively. Correct: cook once at scan time into the node.
- Two notions of "reserved": `parse_identifier()` refuses only `is_reserved_identifier()` (it also parses identifier
  names and keyword types), `parse_binding_identifier()` and enum names all of `is_reserved_word()`. Correct: per caller,
  tsc's parseIdentifier / parseIdentifierName / parseBindingIdentifier. Oracle: syntaxstats.py.
- Conflict markers (TS1185): `Decl()` positions don't skip back over one; none recognised after U+2028/U+2029.
- No missing-identifier node: `written_name()` prints `(Missing)` for empty zero-width names, bindings use `BINDING_NOOP`
  where tsc declares `(Missing)`; `parse_throw()`'s missing expression is a zero-width implicit any (TS1142 finds the
  keyword by `strlen("throw")`); `return;` is an `undefined` literal (`statement_is_bare_return()`, wants a `has_value`).
- Missing nodes and trivia (`token_full_start()`, `unhop_trivia()`): `ParserShared.last_trivia_start/end` remember only
  the last whitespace hop; `expect_expr()` moves back so the parent ends at the previous token and continues a missing
  operand with binary operators and `.` only (not `[`/`(`); the symbols baseline skips ASCII whitespace forward for
  `Symbol(unknown)` (a comment breaks it).
- Spans rebuilt by scanning text: full starts by skipping spaces backwards (TS1433, TS1385-TS1388, comments not
  skipped), `question_dot_after()`, an object literal missing `}` (`parse_object`), a bodyless accessor's TS1005,
  `json_check_value()`'s TS1328 (tsc: after a comment). A `span_from` on a parent's anchor inside a child parser before
  `parse_success` truncates. Correct: real token full starts. Oracle: errtextdiff.py positions.
- Error recovery (plan 27; documentation.md "Error recovery"): `list_abort_or_skip()` skips a token that starts an
  element of the list itself; the fail cache ignores `parsing_context`; `parse_binding_content()` stops at the first bad
  element; a committed arrow whose parameters fail falls back to a parenthesized expression; `tok_scan_end()` is a second
  token notion next to `hop_token()`; `parse_jsx_attributes` approximates isInSomeParsingContext. Oracle:
  syntaxstats.py.
- Statements: `statement_takes_semicolon()` lists by tag who calls parseSemicolon(); statement spans include the `;`,
  port items' and class members' don't (`span_with_semicolon()`). `IfStatement` flattens `else if` (no Statement for
  the nested if: no TS7027 there, statement.c TODO); `statement_start_needs_check()` gates isStartOfStatement by first
  word (a new rejecting case must be added).
- Private names outside classes have no flag: `object_item_is_private_name`, `interface_member_is_private_name`,
  `enum_decl_typecheck` check the first character (such an enum member is declared, prints `__missing`).
- Class accessors pair by name+staticness in `class_decl_lookup_names` (raw pointers in one vec); readonly means "no
  setter". `{ a = 1 }` keeps `ObjectExprItem.shorthand_default`.
- `yield`/`await` operand lookahead (`await_is_operator`, `yield_is_operator_outside_generator`,
  `yield_await_is_keyword_here`): a character peek where tsc looks ahead a token; a comment after `yield` is taken as
  "no operand".
- Parsed and dropped (lost diagnostics in bugs/missing_checks.md): computed non-literal enum keys
  (`EnumCase.computed_name`, member prints `(typeof E)["[e]"]`, tsc `E`); non-string import specifiers; a `ModuleRef`
  from an abandoned attempt stays listed (`add_module_ref`); `global x` is `NamespaceDecl.is_shorthand` typed any;
  `ModifiedNode.name` stands in for getErrorSpanForNode (TS1248).
- Heritage clauses (`parse_heritage_clauses()`, `parse_heritage_element()`): elements parsed as expressions, then again
  as types if entity names; TS1172-TS1176/TS1097 from the parser; unchecked parts in `unchecked_*`/`invalid_*` fields
  resolved silently for baselines; `BaselineTestCase.hidden_type_spans` hides what isPartOfTypeNode drops. Correct: one
  ExpressionWithTypeArguments node.
- `jsdoc_type_outside_jsdoc()`: `?T`/`T!` are a `TYPE_JSDOC` wrapper; `*` reports TS8020 without a node.
- Top-level await (`parse_file()`): the whole file is reparsed with the statements that saw an `await` identifier as
  await contexts (`ParserShared.await_statements`); module-ness guessed by `parsed_file_is_module()`, a copy of
  program.c's rules (keep in sync).
- `triple_slash_invalid_reference()` re-implements tsc's pragma regexes next to `parse_source_directive()`.
- `undefined` is a literal; `expr_lookup_names` rewrites it into an identifier when a non-global declaration is in
  scope (`Expr.undefined_assigned` for targets). Correct: an identifier resolved to undefinedSymbol.

## Binder and name resolution (`scope.c`)
- `binder_redirect` / `Binder.overlays`: a program adding to a shared-lib symbol gets a clone (`clone_parent_symbol`),
  the checker maps through `canonical()` (ty/lower.c): anything new keyed by `Symbol*` must too. Fallout: an augmented
  `Console` shows both `log` signatures (wants getIntersectedSignatures); `maps_merged_type_param()` (ty_display.c)
  misses an interface merged with a lib one (promisePermutations x3).
- `make_gather_target`: namespace imports, `import x = require()` and `export as namespace` alias a namespace symbol of
  their own (`Symbol.gather_module`) so the type prints `typeof ns`. Node16 `import * as ns` of CommonJS copies the
  module's exports at link time (right only because nothing merges into module exports after `binder_link()`), plus
  `Symbol.synthetic_default`, whose `prop->decl->symbol` is the alias (symbols baseline prints `d.default`). Related:
  `namespace_import_is_clone()` under Declaration diagnostics.
- `merge_symbol()` returns early for alias sources and other binders' targets: module augmentations
  (`merge_augmentations`, `retarget_decl`) never merge into shared-lib symbols, an alias in an augmentation meeting an
  existing name isn't merged, `add_pattern_declarations` copies only flags and declarations of an export both declare
  (not a nested namespace's exports); names added through an `export *` module reach only that module. Correct: a
  `merge_symbol` that can clone its target.
- `export =`: named imports see only the namespace side; `namespace_import_type` asks `ty_type_of_symbol()` about the
  alias (identifiers only), so `import("./a")` of an `export =` module prints `typeof ns` where tsc spells out
  `{ ...; default: ... }`, and the synthetic `default` prints as `default` in symbols (tsc: the module name).
- `export = a.b` / `export default a.b` is an alias (`resolve_entity_alias()`): a class static merged with a namespace
  interface is an alias whose `value_target` is the static, and `module_export_equals()` may return the alias itself.
  tsc makes one symbol with both declarations (exportEqualsProperty2 symbols).
- `arguments` has no symbol (`IdentifierExpr.is_arguments`, `IArguments` by name).
- Exported and local declarations are two unlinked tables (tsc: `exportSymbol`; `SymbolRare.export_symbol` only for
  `export default` locals, `scope_declare_default_local()`): TS2440 special-cased in `binder_link`, TS2395 a separate
  pass; TS2441, TS1203, TS2308, TS2661, TS2669 come from binder/link (tsc: checker); TS2308 re-walks `export *` apart
  from `module_resolved_exports()`.
- `block_is_instantiated` (statement.c): a nested namespace is judged with its parent's `outer` scope.
- Class/interface members aren't symbols (`Prop` is the identity): `bind_member_symbols` + `class_decl_lookup_names`
  apply binder rules at lookup (written names only); `ty_class_member_symbols()` (check.c) is the checker's declareSymbol
  + lateBindMember for a class (computed names by the key's type), `object_bind_members` replays the binder for object
  literals, where computed keys still pair by source text; `ty_member_name()` evaluates every computed key. See
  "member symbols" under Expressions.
- Merged interface/class type parameters share one symbol per name (`scope_new_generic_merged`, serial walk) except
  against the shared lib; `type_parameter_is_unused` compensates.
- TS2301/TS2844: fields are walked before the constructor's scope exists, so `constructor_locals()` (class_decl.c)
  collects names with `function_collect_local_value_names()`/`statement_collect_var_names()`, a read-only copy of
  `statement_hoist_names()`: keep in sync.
- TS1361/TS1362 valid sites (`FlowBuilder.type_only_alias_ok`, `in_type_query`) set by enclosing constructs, not
  derived; `export =`/`export default` of a type-only name gets `symbol` without `target`; TS1376/TS1377's related span
  (`span_from_type_keyword()`) scans back for `type` (PortItem has no span of the whole specifier).
- Null-ErrorCtx resolution because the resolve walk reports checker errors: the body of `with`
  (`statement_resolve_names_reachable`, binder errors there lost; `BaselineTestCase.in_with`) and `yield x` outside a
  generator (`UnaryOpExpr.outside_generator`). Correct: the resolve walk stops reporting checker errors.
- `SymbolRare.literal_member` (`add_expando_member()`, `namespace_export_prop()`, the walker's `prop_text()`): tsgo
  declares a `{}` container's expandos in the literal's own symbol; tttt shares the host's exports, so an expando named
  like a dotted JSDoc typedef's namespace (`@typedef {T} Host.a.B` next to `Host.a = {}`) merges into it. The value side
  is the expando's alone, a type-side reference shows both. Correct: a literal symbol table (jsEnumCrossFileExport).
- `attribute_type_accepts()`/`attributes_subtype()`/`best_pattern_module()`: which pattern ambient module (`declare
  module "*.css" with { type: "css" }`) an import with attributes matches is decided syntactically, not by tsgo's
  isTypeAssignableTo/isTypeStrictSubtypeOf (literal annotations exact, unions as sets, `string`/type references accept
  anything); only erroneous code (TS1555) sees it. `import("m", options)` with non-literal options asks the checker
  (`ty_dynamic_import_module()`), invisible to the resolve walk's TS2307. Candidates capped at 16.
- `program_mark_init_resolved_imports()` (`ModuleRef.resolved_before_ambient`): tsc's initializeTypeChecker resolves the
  base names of global thisless interfaces before scripts' ambient modules merge, so imports those go through can't be
  ambient modules (DT ember/v2). Followed syntactically only (a local, qualified or non-interface base ends the walk;
  tsc follows resolved bases). `InterfaceDecl.contains_this` ignores `this is T` predicates: counting them makes
  ReadonlyArray non-thisless and loses dependentDestructuredVariablesFromNestedPatterns' tuple inference.

## Control flow and narrowing (`flow.c`, `ty/flow.c`)
- Evolving arrays: switch clauses narrow the final array, not the `TY_EVOLVING_ARRAY`. Oracle: suite evolving-array
  tests, flagflip noImplicitAny cells.
- `expr_is_narrowable_reference()`: `a = b` counts whatever its left side; tsc wants a LeftHandSideExpression (differs
  only after parser recovery).
- `narrowable_type`: element access narrows for literal indices only. A rest pattern's initializer is checked once
  more for the root pattern only (`ty_rest_binding_initializer_type`, tsc's CheckMode.RestBindingElement does every
  level); call.c's return mapper ignores a pattern with a rest element. Oracle: suite destructuring tests' types.

## Declarations and bindings (AST-side walk)
- `ty_binding_assigned_type` (not memoised): tsc unions the undefined-stripped element with the default (subtype
  reduction), then widens; tttt widens the default first, then reduces. Fresh object literal defaults are special-cased
  (`[{ flag } = {}]`); other defaults whose freshness matters differ. Oracle: suite `.types` of pattern defaults.
- Array pattern rest (`binding_element_of`): the parent type is widened, so "any parent is any" peeks at the root
  initializer for a nullable. Correct: keep the unwidened parent (tsc's getTypeForBindingElementParent).
- `class_decl_lookup_names` drops a constructor's return annotation after TS1093; tsc keeps and ignores it.
- `field_type_from_assignments()`: the first real `this.x =` stands in for tsc's synthetic flow reference of the
  property (getFlowTypeInConstructor). Oracle: JS suite tests.
- `ty_merged_value_type`: a second `Ty` beside the unmerged one (`ty_static_type_of`, what static `this` sees). Correct:
  one type per merged symbol (tsc's getTypeOfFuncClassEnumModule).
- Optional parameters are `T` in the signature, `| undefined` added at use (`value_entry_type()`,
  `ty_optional_param_type()`); tsc's parameter type has it (addOptionality). Same root as optional properties below (plan 82).
- `ty_report_pattern_iterable()`: TS2318 'Iterable' gated on an array/object literal initializer because tttt computes
  the pattern's contextual type eagerly (tsc's createIterableType only when asked).

## Calls and inference (`ty/call.c`, `ty/infer.c`)
- `Checker.outer_inference` is dynamic state where tsc finds a node's inference context among its ancestors
  (getInferenceContext): every evaluation that isn't part of the argument must clear it (`function_return_type()`
  unless the function is the argument, variable initializers, `ty_infer_type_predicate()`), as must any new cached
  evaluation reachable from inference. Symptom: order-dependent `any` (trpc), found by `scripts/kdiff.py`.
- `visited_add()` keys visited pairs by (source, target) only; tsgo's invokeOnce key adds priority/contra/bivariant and
  also guards alias-to-alias inference, which tttt's alias branch of `infer_from()` recurses into unguarded. tsgo's key
  costs effect +0.4%; guarding aliases changes karakeep's alias variances (perf_leads.md).
- TS7023/TS7024 own-context circularity (`ty_function_context_is_self`) and TS7011/TS7018 reportability
  (`ty_function_decide_widening_reportable()`) are decided where the return type is inferred, through the anchor or
  else the first contextual signature (`KEY_CONTEXT_RETURN`), not the check of the moment's.
- Skip-sensitive rounds (`resolve_with_signatures`): tsc's SkipContextSensitive round checks every argument, sensitive
  functions as anyFunctionType; tttt leaves them out of `check_args_until_unfit` (`skipped_function_unfit()`) and
  relates literal ones afterwards (`first_unfit_skipped_literal()`), so a literal is related after the arguments behind
  it (observable in which argument TS2345 lands on). Generators: `return_only_type()`, `first_unfit_argument`.
- Higher-order (`inference_propagate_generic`, `Arg.generic_function`/`generic_call`, `waits_for_sensitive`,
  `ty_generic_function_in_context`): a failed call is re-inferred without them, then arguments are re-checked against
  the failed candidate only to restore memos (`add_implementation_success` likewise). tsc resolves once.
- Return mapper (`infer_type_arguments`, `outer_return_mapper()`): tsgo's returnMapper, except a call whose return
  source is the main inference's reuses the main inference. A pattern's return mapper drops inferred `any` (tsc's
  nonInferrableAnyType), rest patterns give none; `ty_pattern_unclone` copies the pattern type per call; nested literals
  instantiate context only in `check_for_mutable_location()`.
- TS7056 from a display depth limit (`ty_display`, 8 * 16 levels with a tracker) where tsc only has the 1M-character
  limit (recursive reverse mapped types); a finite type nested deeper gets a spurious one. Oracle: dtsdiff.
- Reverse mapped arrays/tuples are eager but cut at depth 4 (`Checker.reverse_mapped_depth`; tsc's
  reverseMappedSourceStack/isDeeplyNestedType). Generic reverse mapped types instantiate via `{target, mapper}`, not
  instantiateReverseMappedType. Inference priorities lack SpeculativeTuple, NoConstraints, AlwaysStrict.
  `inference_call_result()` literal-reduces combined covariant candidates (tsc: subtype-reduces).
- `context_is_tuple_like()` misses call spreads (tsc's intra-expression sites through spreads).
- `const` type parameters: nested array literals found syntactically (`add_nested_array_literals`) plus
  `Inference.array_literals`, because interned tuples can't carry ObjectFlags.ArrayLiteral.
- `ty_union_signatures` isn't cached. `resolve_call` falls back per member for `super`, decorators and JSX when a union
  member has no signatures; tsc errors (decorators), uses the union signatures (`super`) or its own JSX rule.
- `tagged_template_call()`: a stand-in CallExpr with a shallow copy of the tag (`CallInfo.tagged_template`). Correct: an
  argument-list abstraction over CallExpr and TaggedTemplate.
- `signature_resolve`: a function expression takes its generic contextual signature's type parameters only if its
  signature is resolved after the context was assigned (memoised otherwise).
- `missing_property_type`: errorType for a failed access, decided apart from check.c's reporting (tsc's
  checkPropertyAccessExpressionOrQualifiedName does both at once). What's left of the approximation: unchecked JS
  (isUncheckedJSSuggestion) goes by the object identifier's declaration rather than leftType.symbol and ignores its
  class exclusions, and a type parameter whose apparent type is any is the error type (tsc: isAnyLike, any). Both
  print `any`, so no oracle sees it; the any-base and generic mapped gates are gone (2026-10-17).
- Contextual `this` (`ty_contextual_this_type`): through the anchor once the call is resolved, else the literal's last
  context (`KEY_LITERAL_CONTEXT`, last wins; tsc's pushed contextual type, a push/pop stack is plan 51 stage 3 item 3);
  `ThisType<T>` found by name, instantiated with `ty_instantiate_fixing()`.

## Expressions and contextual typing (`ty/expr.c`, `ty/access.c`)
- Member symbols: a class's are `ty_class_member_symbols()` (check.c: TS2300, the builder's joins, overload
  groups, computed accessor pairs via `ty_class_accessor_first/getter/setter()`), the binder's for written names
  (bind_member_symbols, symbol_first/accessor_first: syntactic like tsc's). Left on top: the builder's rules for a member
  whose namesake comes from *another* declaration (an interface merged with the class: `builder_add_class_prop_joined`'s
  interface joins and the late pass's meanings test; `builder_add_interface_member`'s own member_kinds, and check.c's
  class + interface table `check_class_interface_members` is separate). The builder's method overloads go by the table
  for late-bound names (`class_methods_same_symbol`, by text first: a key naming its own class can't be typed while
  its members are built). A late-bound accessor merged into an early accessor's symbol isn't paired with it
  (bugs/type_core.md "Classes and declarations").
- `ty_deferred_reference()`: TYF_COULD_CONTAIN_TYPE_VARIABLES only when a mentioned type parameter is unmapped or mapped
  to something generic (tsc: any reference with a node). Needed by conditional.c's "infer constraint stays generic"
  (react-relay); drizzle -24% types.
- Instantiation expressions (`apply_type_arguments_part`): signatures violating their constraints are dropped when
  another was dropped by arity; tsc keeps them uninstantiated. Needs a kept-signature list on the instantiation
  (`ty_apply_type_arguments_mapper`, `instantiate_members`); identity-mapping them breaks effect's `collectValues<A>`.
- Context re-derivation (`initial_or_assigned_type()`) covers `x = e` and annotated declarations, not compound/logical
  assignments or destructuring defaults. Anchors (`ty_record_call_anchors()`) don't go through calls (first call wins),
  skip non-generic one-signature calls (`CallResolution.exact`) and generators' returns; tsc's getContextualType walks
  every parent up to links.resolvedSignature.
- `KEY_CONTEXT_CHECKED`/`assign_contextual_signature_as`: the first non-skipping check fixes a function's contextual
  signature; generic contextual signatures aren't instantiated; a function without one still takes `this` from the
  last contextual signature (tsc asks lazily).
- Apparent contextual types: `intersection_prop_context` intersects per-member answers (tsc: the intersection's
  apparent type once, distributing over union constraints); `context_map` drops `unknown` parts when others contribute
  (tsc: noReductions).
- Excess properties (`has_excess_properties`): `generic_mapped_knows()` has no intersection/conditional lower bounds
  (tsc's resolveMappedTypeMembers); no nested re-check for intersection targets; `discriminated_union_target` and
  `ty_best_matching_type` have no key-property map.
- Destructuring assignments (`assignment_target_typecheck`): for-of destructuring relates the widened element type
  (tsc: per property, so no excess property checks); array literal targets are arrays, not tuples.
- Expandos on functions are checked against existing members; `is_container_declaration()` approximates
  isAssignmentDeclaration for `ns.a = {}`.
- `member_refs()` (check.c) duplicates `access()`'s walk to get `Prop`s without creating types (two walks to sync).
- JSX: the head rule (`ty_jsx_skips_head` via `Checker.jsx_element`) applies only inside `ty_check_jsx_element`;
  attribute types skip the regular object literal state.
- `assignment_value_context`: `TYF_BINDING_PATTERN` is both tsc's object literal `pattern` and isFromBindingPattern, so
  an object assignment pattern only marks its context when the value isn't a call. Correct: a separate flag.
- `x satisfies T`: on failure prints T and discards the text, for the printing's side effects (tsc builds TS1360 there:
  `const f = () => 1 satisfies typeof f` is TS7023). Correct: report where checked (checkSatisfiesExpression).
- `definitely_unrelated()`'s `mentions_type_variables()` walk resolves signatures' return types when
  `ty_function_free_params()` can't tell syntactically (unannotated non-literal parameter defaults, unannotated returns
  with bodies); tsc's getPermissiveInstantiation is lazy, so tttt can leave a conditional deferred for good when that
  return type needs what's being resolved (mikro-orm's `toJSON(strict = true): Omit<EntityDTO<this>, I>`).

## Relations (`ty/relate.c`)
- Reporting runs: tsc's error-node checkTypeAssignableTo callers run once, explaining (check.c relate_explaining(),
  ty_check_assignable()); the checkTypeRelatedToAndOptionallyElaborate ones (check_assignable()) and the overload
  error reports run plainly first, like tsc's. One one-run caller still runs plainly first: the static side
  (`ty_check_static_heritage`, TS2417: it relates to each intersection member plainly to name the one that fails, which
  tsc's single run does inside typeRelatedToEachType).
- **Optional property types lack their undefined** (`find_prop()`/`find_prop_in()`, also optional parameters above):
  readers add it; tsc's getTypeOfSymbol has it. All-optional intersection parts intersect read types only when one has
  undefined and all are one type (react-select), not under exactOptionalPropertyTypes; a non-member part (`typeof f &
  { m?: X }`) drops PROP_OPTIONAL (required as a target). Always intersecting read types (tsc) is clean except a spurious
  TS2590 in tiptap/demos' NodeViewContent.tsx:19 (a part differing in identity). Correct: types that carry undefined (plan 82).
- `conditional_source_related()` relates the distributive constraint without tsc's hasNonCircularBaseConstraint(source)
  gate: computing it inside an alias's resolution gives spurious TS2456 (type-fest's `_ConditionalPickDeep`, crawlee).
  Correct: lazier alias references, or the gate after the alias resolves. Its default-constraint step also pushes the
  target on the deep-nesting stack (tsc: RecursionFlags.Source only; tsc's way costs react +8.6%).
- `resolved_base_constraint()` simplifies indexed accesses, and conditionals only when `may_simplify_conditional()`
  sees a `T extends U ? T : never` shape as written (all conditionals: mikro-orm/seeder 2.3x types); misses a branch
  that becomes the check type through the mapper. Types without type variables are their own base constraint except
  string mappings, templates, substitutions; tsc also computes unions/intersections containing those
  (`Uppercase<string> | number`).
- Error reporting: `TyReportLine` has no message arguments, so `report_property_incompatible()` parses paths out of
  messages (`incompatible_line_path()`) and `relation_line_said()` compares texts (tsgo's chainArgsMatch compares
  arguments); headMessage is `RelationState.at_top` + `head_code`. Correct: lines with arguments.
- Elaboration (`elaborate_error`) reads memos (`ty_mutable_location_type`, `ty_array_literal_as_tuple`) instead of
  re-checking; `type_decl_span` covers declared types, function type nodes and type literals only (renaming `as`
  properties lose declarations).
- `structured_related_members`, an intersection source against a union target: its members are related alone (unless
  all are object types; never a type parameter member, which loops back to the effective constraint), standing in for tsc's
  getEffectiveConstraintOfIntersection, which `effective_intersection_constraint()` doesn't always match
  (intersectionConstraintReduction's `K1 & K2 & Extract<K1, keyof any>` fails without the members). tsc never relates
  an intersection's members alone to a union. Correct: the effective constraint as tsc computes it, then drop the
  stand-in.
- `recursive_related()` relates a pair under IntersectionState.Source through its twin without it (cached or on the
  maybe stack) unless the twin went through an inferable index signature (`CACHE_INDEX_INFERRED`), the only thing the
  state changes; tsgo computes both (zod +1.6% instructions with the cache half alone, within 0.4% of the old
  member-only state with both). The maybe-stack half rechecks a pair whose own comparison went through one (`no_twin`), but not a twin outside that comparison: a Source pair cached as part of
  an inner success whose twin further out used an inferable index could be wrongly related (never seen; the recheck
  itself ran 6 times on vue, 0 on the other corpora).
- `structured_related_members`: the whole-source fallback for an intersection target covers conditional sources only
  (tsc: any instantiable source goes on to its constraints).
- `props_related` skips the same property by `Prop` identity plus a rule for object literal accessors of one function
  (tsc: same symbol).
- `generic_mapped_target_related`: a generic source failing it falls through to member comparison, which memberless
  mapped types pass (`Readonly<T>` to `T`); tsc lets only a mapped source go on. Returning outright loses
  circularConstrainedMappedTypeNoCrash, restTypeRetainsMappyness, mappedTypeRelationships and zod's `SortItem`.
- `is_rest_array` takes any non-type-parameter as a rest array. Correct: normalize `[...X]` when the tuple is made
  (tsc's createNormalizedTupleType).
- Identity (`identity_structured`): differently shaped tuples never identical (tsc compares their properties), no
  `infer` in the two-conditional rule. TS2403 (`check_subsequent_declaration`) exposes every gap.
- The depth-100 backstop (`MAX_RELATION_DEPTH`, assumed related) counts every `recursive_related()` level (`st->depth`);
  tsc counts sourceStack and targetStack separately (a side-only push doesn't count): tttt gives up earlier.
- Alias variance (`in out`): the reporting pass falls back to the interface's own variances on an invariant failure
  only for references to the same target. The null-base TS2417 check takes the first `extends` of the first merged
  interface (`class_decl_merged_interface_base`), not getBaseTypes.
- `TY_INDEXED`'s include_undefined (noUncheckedIndexedAccess) is dropped where an indexed access is rebuilt from parts
  (`simplified_parts`, `ty_actual_type_variable`). Oracle: flagflip noUncheckedIndexedAccess.
- `has_inferable_index`: every anonymous object type has an inferable index except `OBJECT_NO_INFERABLE_INDEX` and the
  global empty objects (a blacklist of tsgo's symbolless or non-literal-symbol `newAnonymousType` sites: binding
  patterns, string-literal inference, decorator contexts, CommonJS `module`), where tsc's isObjectTypeWithInferableIndex
  reads the symbol's flags. Probed 2026-10-17 and matching: spreads (of interfaces, classes, unions with undefined,
  functions, tuples, enums: getSpreadType gives the literal's symbol), object rests, reverse mapped, mapped types,
  instantiation expressions, JSX attributes, JS literals/expandos/constructor functions, `export =` module clones.
  A new synthetic anonymous type needs the flag if tsc makes it without an ObjectLiteral/TypeLiteral symbol.
- Artifact models (each fitted to one real project or probe, kept because real code needs it until the underlying
  rule is found): `canonical_signature()` skips erasing (vue `toHandlerKey` TS2322), tuple number-index relation only under `Checker.base_constructors_in_flight`
  (effect TS2506), `ty_resolving_cycle_across_calls()` (tsc's second resolution round, RESOLVING_BASES), the mapped-type
  rule in `relation_line_said()` (vitest TS2344), `intersection_props_related`'s TS2741 member choice (a valibot probe).

## Type core (`src/ty/`)
- **A mapped type's `as` clause kind by shape, then by relation unless it mentions `keyof`**
  (`mapped_name_type_filters()`, members.c): tsc's getMappedTypeNameTypeKind relates the clause to the key; tttt
  first reads the shape (`name_type_filters()`), then relates (cached per declared mapped type, KEY_MAPPED_FILTERING)
  only when the clause's source text has no `keyof` token: relating `${K}.${keyof Self<T[K]>}` from inside member
  resolution resolves members of mapped types in flight, which tttt then keeps incomplete
  (mappedTypeAsClauseRecursiveNoCrash1 loses keys). A keyof clause the shape can't read, or a pending lazy branch (`peek_cond_branch()`), counts as remapping (no
  declarations linked: member order by name). Fix: make member resolution re-entrant like tsc's empty-members-upfront (plan 83).
- **An indexed access type node's missing key prints its object to a dropped string** (`lower()` TYPE_INDEXED,
  lower.c): tsc reports the key inside getIndexedAccessType, and the typeToString of its message resolves an
  anonymous object's member types while the alias resolves (`type A = { a: A }["x"]` is TS2456). tttt reproduces
  the side effect by printing; the TS2339 itself is still type_typecheck()'s. Artifact model like the eager bases.
- Eager-base runaway model (`probe_eager_bases()`, documentation.md "Eager base runaways"; a deliberate artifact
  model: webpack and zod need it).
  Approximations: probes run only when the reference's arguments mention a type whose members are in flight or whose
  bases mention itself at depth >= 2 (`mentions_declared()`, perf: valibot's depth-1 `BaseIssue<T>` is skipped); the
  guard is "the resolving stack is unchanged since the re-entered members started", with `RESOLVING_MAPPED_CONSTRAINT`
  standing for tsc's key-parameter constraint entry; tsgo's poisoned caches after the runaway aren't modelled (vue's
  two TS7006, missing_checks.md). Right rule: none (tsgo artifact); the corpora (zod, vue, valibot) show drift.
- `structured_related_impl()` TY_TYPE_PARAM: a type parameter source against a union takes its hoisted constraint
  (`constraint & T`) only on a definite True, because tttt relates a generic intersection member by member (meeting the
  outer T relation as Maybe). Right rule: tsc's whole-intersection comparison; flagflip/sweep would show misses.
- Instantiation keys by mentions, not by outer type parameters (tsc's getObjectTypeInstantiation):
  `ty_function_type_mentions()` / `object_literal_type_mentions()` (contextual parameters can carry a call's type
  parameters, plan 51); unknown mentions fall back to the whole mapper (perf: docmost 2.2M types), and
  `declared_inside()` (`instantiate_anonymous()`) drops only parameters declared inside the node. An object literal
  with a getter has instances of both kinds (zod's recursive-types.test.ts depends on which meet). Right rule: key by
  outer parameters once contextual parameters stop carrying call type parameters; perfsweep types ratio and zod show it.
- Stable type ordering (`ty/order.c`) ties still broken by tttt ids: roots without `outer`, deferred references with no
  cheap node position (`type_node_start()`), two nodes at one token, position-less unique symbols, intrinsic variants,
  same-symbol anonymous types with equal mappers. Shows as union order (displaydiff, expecttypes "union order").
- Pseudo types (`ty/pseudo.c`): `pseudo_of_variable()` always gets a non-contextual place (tsgo: a variable inside a
  callback is contextual); `Pseudo.in_const` is syntactic (tsgo: isConstContext); `pseudo_enter_signature()` treats an
  instantiated signature's type parameters as fresh by mention; `reused_node_length()` ignores comments in the range;
  `pseudo_member_prop()` matches by declaration span; a fresh object literal's `pseudo_type()` is null by flag, not
  identity. Shown by dtsdiff and the suite's dts/types components.
- Declaration emit writer (`declaration_emit.c`, `emit_*()`): a second walk next to the `check_*()` one, sharing
  `serialize_*()`, so their member decisions can drift. Known gaps: an index signature's extra parameters dropped; the
  unique-name generator is a whole-word text search (`file_has_word()`; tsc keeps the scanner's identifiers),
  `undefined` special-cased in `emit_expando_namespace()`; `w_literal_value()` rewrites `(typeof E)["x"]` text after
  printing; leading comments are written then taken back (`w_leading()`/`w_unless_written()`, `Walk.statement_start`);
  CRLF is a pass over the text; import type options copied from source text (`display_node_identifier()`,
  `import_type_options_valid()`); `track_computed_name()` resolves from the file's scope (`Walk.scope`, plus the root
  function's parameters), not tsc's enclosingDeclaration; a cloned template literal (`display_pseudo()`) escapes source
  tabs/CRs rather than reprinting the cooked value. dtsdiff and the dts component show these.
- Declaration emit comments (`ty/decl_comments.c`): tsc decides by synthesized nodes' positions; tttt hooks the places
  it knows a node starts/ends (`decl_comments_end()`), so unhooked kinds write none (union/tuple members, type-level
  trailing comments). Backtracking may record a comment twice (deduplicated by start); JSDoc type parser hops aren't
  recorded. Accessors print as `get`/`set` only in multiline mode; `TyDisplay.transformer_node` approximates what the
  transformer visits vs what the node builder reuses. dtsdiff shows it.
- Declaration maps (`ty/decl_map.c`): a raw 0x01 in copied output would be misread as a mark; `w_members()`'s `{}` check
  and `w_literal_value()`'s `(typeof ` check only see unmarked text. Ranges the AST lacks are rescanned from source
  (`decl_map_type_end()`: one flag for nested parens, `((A))` maps one pair; `source_modifiers()`;
  `decl_map_back_trivia()`). Reused nodes tsgo prints anew (trpc's `infer T extends object`, mixin constructors) map
  where tsgo doesn't (12 dtsdiff projects by maps alone); a node reused from another file is mapped with its own file's
  offsets (tsgo drops such ranges; tttt can't tell a TypeNode's file). dtsdiff `--names` lists them.
- isolatedDeclarations reporting (`declaration_emit.c`): no parents, so `locate()` re-walks the serialized declaration;
  a node reported from another declaration gets no related info; `print_part()` prints Inferred parts as
  `ty_pseudo_type()`, NoResult parts print nothing (missed TS4xxx); printing stops at TS7056 truncation (tsgo's
  later properties still report: monkeytype); `interface_declares_method()` matches by name only (tsgo:
  getSignaturesOfSymbol); `declared_item()` loses deep chains (zod shape spreads, knip). Source sweep/clidiff show it.
- Member order (`builder_sort_stable()`, tsc's getNamedMembers): only object literals, mapped types, namespaces, merged
  values; class/interface members keep own-then-inherited order (`builder_prototype_last()` for `prototype`);
  declaration-less props sort by name bytes (tsc: escapedName then symbol id). Shows in types/symbols baselines.
- `UnaryOpExpr.no_type_params_in_scope` (TS7057 `yield`): only a bare type parameter counts as a call's; a contextual
  `T[]` is taken as is. Right rule: getResolvedSignature's instantiation of the contextual type.
- `ty_realias`: a renamed object type is a second `Ty` (members resolved twice); renamed deferred types are copies; no
  "local alias" exception; whether an alias instance keeps its name (`renames` in `lower`) is syntactic.
- `lower_identifier()` `through_import`: a generic alias instance takes the import's alias only for union/intersection,
  deferred conditional, indexed access or deferred reference bodies, not instantiated anonymous/mapped ones (tsgo keys
  those on the alias too); doing it made arktype's `finalizeTree` run away into 3 spurious TS5114 (uninvestigated).
- Conditional types (`conditional.c`): deferred conditionals don't keep inferences (`inferred_true_branch()`
  recomputes); `ty_implied_infer_constraint` lacks the mapped-extends rule. Permissive/restrictive instantiations
  replace only what `mentions_type_variables` finds (8192 types / 48 deep, else "unsure": defer), resolving signatures
  tsc never would (`callable_definitely_related()` short-cuts two idioms, bugs/type_core.md); a getter whose return
  type is in flight counts as unsure (zod recursive getters otherwise TS7023). Substitution types: no
  `number | ${number}` mapped key rule.
- `instantiate_conditional`: outer parameters from `collect_free_params`, which gives up on `typeof`, non-class `this`,
  parameter initializers, bodies without return annotation, JSDoc callbacks (then the uncached full-mapper path,
  `TyConditionalOuter.unknown`). `TyMapper` is flat.
- Mapped types: homomorphism (`homomorphic_variable`) needs a syntactic `[K in keyof X]`; `expand_mapped` expands once
  keys are literal unions, modifiers only for a written `keyof T`; `mentions_mapped()` (ty_display.c) compensates for
  instantiating member signatures that can't mention the parameters (bugs/display.md).
- `expand_mapped()` HACK (perf): homomorphic over `T extends A | B` takes the union's properties but not its index
  signatures (valibot +3.6% because `T[string]`'s constraint comes out unknown). Fix: `T[string]` of a union of
  index-signature objects is the union of their values.
- `class_base()` HACK: class names found syntactically; other expressions take the first fitting construct signature
  (TS2510 by `ty_class_base_constructors_differ()`); a base constructing `any` is no base (its sides get tsc's
  anyBaseTypeIndexInfo, `class_base_is_any()`); type-parameter bases take the constraint's
  members at declaration; `ty_non_augmenting_base()` is syntactic. A bare `C`/`this` inside generic `C` isn't its
  own instantiation (lower.c TODO).
- `never_reduced_prop` (permanent, perf: zod +4.1%, drizzle +0.9%): `ty_reduced()` skips members flagged
  `TYF_COULD_CONTAIN_TYPE_VARIABLES` unless `is_known_object_for_reduction`; tsc looks at every member.
- `primitive_constraint_of` HACKs (permanent, perf): a deferred type in a declared constraint is no primitive (valibot
  +6% otherwise), nor an indexed access into a conditional with generic branches (react +15%).
- Generic tuples: `base_constraint` replaces only variadics with array constraints; `lower_tuple` keeps the written
  `T[]` for rests (tsc: `T`). `apply_mapping` steps code points (JS: UTF-16 units); `${symbol}` is silently `string`.
- `builder_add_interface_member`: index signature types lowered eagerly; non-literal computed keys dropped (tsc:
  late-bound index signatures; declaration emit has its own `late_index_members()`).
- Unique symbols: escaped names (`__@sym@12`) mapped back by `ty_unique_symbol_of_name`, so a string property `__@x@3`
  passes as a symbol; `instanceof_target_ok` matches `__@hasInstance@<id>` by text; one unique symbol per declaration
  (tsc: per merged property), so `check_subsequent_property` skips TS2717 between two.
- Private names (`ty_private_name`): lexical lookup scans class items by text (`class_decl_find_private_name`); an
  undeclared one looks up the plain `"#x"` atom. Object literal accessors have no `write_node` (`prop_decl_rare()`).
- `Checker.current_node` (TS2589/TS2615/TS4110/TS1062 location): `lower()` makes a generic reference current while
  lowering it (a guess at tsc's order); `Foo<X>` isn't lowered in the walk (resolving every alias changed type ids).
- Instantiation limit: an exhausted budget's results are memoized like tsc's (`test/probes/instantiation_limit/`), but
  tttt instantiates more eagerly (variance measurement, eager members), so a tttt-only TS2589 poisons what tsc would
  still compute; a cut-short conditional instance is re-reported only within its epoch.
- `item_is_literal()` (ty_display.c): an object literal property valued by an array/object literal isn't printed from
  syntax in the unwidened type, told syntactically because tttt's tuples/arrays have no fresh twin (tsgo:
  ContainsObjectOrArrayLiteral). A real rule, not an artifact (without it only
  declarationEmitUsingAlternativeContainingModules1/2 lose their types). Correct fix: fresh array/tuple literal types.
- `reference_from_type_node()` / `recursion_identity()`: ObjectFlags.FromTypeNode is only ported for tuple type nodes;
  interface/class references written as nodes keep the target identity because signature relations don't instantiate
  fresh references per level (performanceComparisonOf...Signatures would hit TS2589). Order-dependent like tsc's.
- `ty_distributed_type_param()`: tsgo distributes the conditional's own check type node too; tttt keeps the original
  (its `conditional.check ==` identity tests broke 12 tests) and emulates the rest: `conditional_source_related()`
  maps twins back, `instantiate_conditional()` keeps identity mappings of params with a twin, the identity relation
  treats a twin as its original. Correct: distribute the node and replace the identity tests.
- `ty_tuple_target()`: all tuple targets share `Checker.tuple_params` (tsgo: fresh per target; type-fest's IntRange
  made 4.5M). Unsound if a type mentions two targets' raw parameters at once. Element props share the read-only
  `ty_prop_decl_value_name` (`prop_decl_shared()` guards writes).
- `expand_generic_intersection_homomorphic` (members.c): index signatures come only from a part that is itself a
  generic homomorphic mapped type; tsc's resolveMappedTypeMembers takes every part's, but that added an inference
  candidate in localTypeParameterInferencePriority. Shown by: that test when generalized. Such an expansion is marked
  `index_signatures_incomplete` (MembersRare) and check_access/TS7053 don't report what it seems to lack (react-
  navigation's `Partial<{ [E in keyof (EventMap & Core)]: X }>[string]`, better-auth); its accesses are still the
  error type where tsc has `X[string] | undefined`. Fixing the inference removes both.
- `never_index_read` (checker.c): an anonymous object indexed by never is never, except a mapped type whose keys its
  declaration tells (`Record<string, T>[never]` is T); tsc reads index signatures everywhere, but resolving xstate's
  keyless mapped types tipped TS5115 (flagflip nosnc/sloppy +7). Correct: instantiation accounting that doesn't
  depend on member resolution, or a lazy template for keyless mapped types.
- `rest_literal()` (ty/lower.c) / `object_symbol()` (ty/order.c): a destructuring rest's binding symbol, which orders
  it in unions (tsc's getRestType symbol), lives in the anonymous type's `literal_expr` union slot (`rest_binding`,
  flagged `rest_of_binding`), free for synthetic objects; it is also kept in KEY_REST_SYMBOL links. Readers of
  `literal_expr` must keep checking `object_literal` first. Correct: a symbol on anonymous types like tsc's. Oracle: the
  suite's union order (ttttRestTypeUnionOrder).
- `ty_keyof()` (ty/checker.c): a mapped type with an `as` clause over `keyof X` gets its keys from
  ty_mapped_type_name_keys() (tsc's getIndexTypeForMappedType), other renamed mapped types still from their expanded
  members (an `any` name there would be a string index signature). Oracle: ttttKeyofMappedAsAnyName.

## Checks (`ty/check.c`, `unused.c`, `statement.c`, `class_decl.c`)
- `check_duplicate_declarations()` (TS2300): artifact of tsgo's checking order: in a class, a member before the first
  late-bound one counts only its early symbol's declarations (`MemberEntry.early_total`), keyed on the `private_names`
  parameter only the class check passes. Revisit if tttt ever resolves class members before this check.
- `checker_defer_function_body()`: a function expression's body is queued only from the file being checked; tsc's
  checkNodeDeferred also queues bodies first checked from another file, so they come first (flagflip excalidraw/nuia:
  once-per-type TS2488 at a different line). Needs per-file queues per checker, exact only at `--checkers 1`.
- `source_file_check_verbatim_exports()` (TS1287) finds `export` by text scan (`export_keyword_of()`): statements don't
  keep modifier spans.
- `ty_check_static_heritage()` relates a synthetic object literal (tsc's getTypeWithoutSignatures keeps the class
  symbol, printing `typeof Base`); `Checker.display_of`/`display_as` patch the messages. HACK: adds `& K` to the source
  because `ty_static_type_of` doesn't make `X & K` bases' static side `typeof C & K`. Fix: a synthetic anonymous type
  remembering the symbol it prints as.
- `check_access` (TS2339): bracket access never reports there (`ty_check_element_implicit_any`, TS7053, mirrors
  getPropertyTypeForIndexType's errors separately); said to fire on modules too (TS2339 next to TS2307: not
  reproduced with namespace/default imports of a missing module, 2026-10-17).
- `ty_check_index_constraints` on type literals only when syntactically needed (`type_literal_has_index_constraints()`):
  the others aren't lowered at their check, and an alias's literal body must be made as the alias's type first.
- Syntactic stand-ins: overload grouping (`statements_check_overloads`, `class_check_overloads`); TS2729
  (`initialized_in_static_blocks`: top-level `this.x =`/`C.x =`, not tsc's control flow at each block's end; mutate.py shows it); member kinds in `class_check_member_overrides()`
  (`class_instance_member()`, not symbol flags; TS2612 skips isPropertyInitializedInConstructor);
  `check_property_modifiers` (TS2687) skips methods and accessors.
- `ty_check_class_duplicate_members()` HACK: a late-bound name (`[k]`, `[Symbol.x]`) conflicts with the member table
  (static `prototype`) without joining it; tsc also reports an early `a` against a later `get [k]` (k = "a") at both.
- Emit helpers (`ty_check_emit_helpers`): spans by hand (`decorator_span()`, `function_error_span()`); "ambient" per
  site (`ClassDecl.is_ambient`, `Binding.ambient`, `BlockTypecheckCtx.ambient`; want NodeFlags.Ambient).
  `EMIT_HELPER_EXTENDS/ASSIGN/GENERATOR/VALUES/READ/SPREAD_ARRAY/MAKE_TEMPLATE_OBJECT` are dead (ES5/downlevelIteration
  only) but stay in the enum: `helpers.c`'s table is indexed by bit.
- `unused_mark_jsdoc_links()` HACK: `{@link X}` is a use by text scan, looked up from the file's scope (fix: resolve
  parsed tags). Member reads are keyed by declaration span start (`ty_mark_member_referenced`).
- `program_apply_comment_directives`: suppression by tttt's error phase, not tsc's diagnostic kind; `\n` and ASCII
  `\s` only; relies on every comment going through `hop_comment`.
- `SourceFile.unchecked` + `error_ctx_remove_unchecked`: skipLibCheck/noCheck/JS filtering happens after checking, so
  skipLibCheck'd `.d.ts` are checked in full for nothing (tsc's skipTypeChecking doesn't): a perf lead, if checking
  them first doesn't change what checked files see. Plain JS is checked in full and filtered (as tsc).
- `ty_check_destructured_member` HACK (`from_this`): `({ m } = this)` inside method m isn't a read of private m;
  "inside" is by position, so a function nested in m counts as m.

## JavaScript and JSDoc (bugs/js.md)
- `IdentifierType.jsdoc_unchecked` (`identifier_type_check_arity()`): tsc only *resolves* `@returns`/`@throws`/`@augments`
  types (never checkSourceElement); tttt runs `type_typecheck` and skips just the constraint check. Correct: a
  resolution-only walk that keeps arity errors (jsdocArrayObjectPromiseNoImplicitAny). Shown by: mutate.py on JS tests.
- TS8023 (`jsdoc_apply_class()`): only checked when the tag's rightmost name differs from the clause's; tsgo's
  checkJSDocAugmentsTagMatchesExtends compares every tag's type with the base (`@augments A<string>` on
  `extends A<number>` is TS8023 too). Shown by: a probe / mutate.py.
- `jsdoc_bind_comments()`: claims made by failed parse attempts stay; `@import` only at a list's top level; dotted
  `@typedef` names skipped; typedefs sharing a comment share its `@template`s. Correct: tsgo's per-tag reparse.
  Shown by: jsstats.py, the sweep's JS packages.
- Fragile JSDoc threading (correct: JSDoc kept on the node, like tsgo's reparser): `SymbolDecl.jsdoc_type` is passed
  through every assignment-declaration site in scope.c (new sites must pass it; chains: outermost only);
  `jsdoc_apply_type_signature()` *moves* a written function type's TypeNodes; `Statement.expr_jsdoc_type` shares a
  union with `expr`, so every site building an expression statement must clear it.
- No `SymbolFlags.Assignment`: TS2403 looks for a `SYMBOL_DECL_EXPANDO` declaration instead, and since expando hosts get
  no namespace meaning an alias's namespace lookup also accepts a target with expandos (`symbol_namespace_member()`).
- Expandos: statement level only scans expression statements and `var` initializers (`scope_add_expandos()`);
  `scope_add_nested_expando()` (`f.x.y = v`) doesn't see a nested `f.x = {}`; it and `scope_add_this_property()` write
  file-level symbols from the resolve walk (should be per-file lists merged after); `Symbol.late_bound_key` keys
  `this[k] = v` by source text (tsc: the key's type). Shown by: sweep/sourcesweep JS projects.
- CommonJS: `saw_commonjs` is set by failed parse attempts too; `require()` ports are flat patterns only;
  `readonly_assignment()` never reports `exports.x =`; exported class expressions alias the class at top level only
  (`scope_reparent()`); `exports.x = undefined; exports.x = a` (moduleExportDuplicateAlias*) is a property + alias
  where tsgo makes every one an alias (`SymbolDeclJs.commonjs_value` keeps the value for the union).
- `bind_type_exports()` (tsgo's per-file bindCommonJSTypeExports) runs at link time (`binder_link()`), so the TS2309
  check `export_equals_shadows_namespace()` recomputes whether the promotion will happen, and a named import drops the
  module's value exports in `resolve_alias_target()` instead of `module_resolved_exports()`. Correct: per file.
- Package deduplication (`program_load_resolved()`): the second copy (`SourceFile.package_redirect`) is still parsed
  and bound with its own AST (tsc: the first copy's AST, nothing bound), only unchecked; its imports share the first
  copy's resolutions by path text (`program_share_redirect_ref()`, own resolution as fallback) and its globals still
  merge. Shown by: sourcesweep projects with duplicated packages (duplicate global declarations).
  Its representative is decided when the first resolution with the package id arrives, in load order: tsgo loads every
  file first, gives each the id of any resolution that reached it, then keeps the first file in DFS order with that id
  (filesparser.go collectFiles) and skips the redirects' imports. So a workspace source loaded id-less as a root and
  self-referenced only later would lose to its real-path copy reached through node_modules (a project opened through a
  symlinked directory, plan 84). HACK `sources_by_package_id` (program.c): a file outside node_modules loaded without an
  id is assumed to be reached by its package's own `exports` later (`module_resolver_source_package_id()`), so a new
  file with that id is its redirect. Wrong when nothing self-references it: tsc keeps both copies (effect through a
  symlinked checkout: 5 such files, ~20 diagnostics apart; it was 3165 against 5 before). Correct: plan 84's two-phase load.
- `symbol_chain_enter_class()`: a thread-local the baseline traversal sets names a CommonJS-exported named class
  expression by its own name inside its body; error messages still name it from the file.
- `ty_report_enclosing()` / `statement_scope_at()` (ty_display.c): tsc prints a relation message's type from its value
  declaration when that is a non-context-sensitive expression (getTypeNamesForErrorDisplay: annotation reuse, names
  qualified from there). tttt types don't know their expression: a function expression prints from its first
  parameter's scope (else the file), an object literal from the namespace/function body/block around a property written
  in it, a spread-only literal from nowhere; context sensitivity is guessed from member types. Correct: the literal's
  type knows its node (or the resolve walk stores a scope on the Expr). Shown by: errtextdiff.py header buckets.

### Symbols baseline reconstruction (harness: tttt has no tsc-shaped member symbols)
- `collect_prop_symbol()`: a non-object union member lacks the property only for binding patterns, because a property
  access's object is sometimes narrowed less than tsc's (applying it everywhere loses 70 symbols components). Correct:
  narrow like tsc, then drop the gate.
- Span overrides stand in for tsc's symbols: `this_property_symbol_overrides()` (getSpecialPropertyAssignmentSymbol;
  `builder_add_assignment_members()` and enum members append spans to the checker's `Prop` for it), `module.exports`'s
  `exports` member (span override in the traversal's `EXPR_PROPERTY` case), `append_decl()` sniffing a JSDoc typedef's
  dotted name / tag Decl trivia from the text, `symbol_declaration_spans()` dropping a JSDoc namespace declaration at
  another's start, `KEY_UNDEFINED_PROPERTY` (members.c, declaration spans only).
- Member-table merges rebuilt in the harness only (the checker doesn't merge): `class_prop_declarations()` (field +
  parameter property/interface member), `register_type_parameter_member_merges()` (`BaselineTestCase.merged_declarations`,
  registered when the class is walked: an earlier use misses it), `baseline_symbol_declaration_spans()` (namespace local
  + its exported declarations; a function's type parameter + a parameter/local, `Scope.type_params_function`). A union property's symbol dedupes members by (first declaration, mapper), treating
  an instantiation whose type equals the declared one as uninstantiated (tsc's lazy instantiateSymbol may differ).
- `unknown_symbol_text()`: tsc's unknownSymbol is named by the first alias of an unresolved import in the innermost table.
- `namespace_decl_compile_test_info`: an ambient module's name is `symbol_text()` unless that comes back quoted (a text
  test, not a chain query).
- `BaselineTestCase.any_keeps_rendering` leaks to the next types line if the node it was set for adds none.

## Harness and tools
- Deliberate simplifications (permanent, see documentation.md "baseline_tests"): `apply_setting`, the lib `Program`
  cache, `@noTypesAndSymbols`, `engine.c`, `failstats.py`'s `line_ratio`.
- The CLI checks a config that would emit JavaScript as `noEmit` (`Options.implicit_no_emit`): its emit-only option
  diagnostics (TS5055/5056 of .js outputs) are missing and noEmitOnError's exit code is 2 where tsc's is 1;
  `scenarios.py` drops `EMIT_ONLY_CODES` (TS5011/5055/5056) from emitting commands. Lifts with JS emit (plan 64).
- JavaScript declaration diagnostics (`declaration_diagnostics_check()`): `check_commonjs_exports()` repeats the
  CommonJS exports' serialization (for painting) into a scratch context, `check_js_commonjs_exports()` having reported
  them. `export_specifier_local()` (check_entity_name()): a module export merging `export { x }` with a dotted
  `@typedef`'s namespaces is checked as x's local, where tsgo's binder merges the namespaces. Shown by:
  jsDeclarationsImportAliasExposedWithinNamespace.
- JavaScript declaration emit guesses (one or two suite tests each): a default-exported function's `@overload`s keep
  the statement's comments, other functions' don't (`emit_function_declaration()`); a constructor's `@overload`s have no
  comments; `@property` description margins follow parseTagComments by observation (the first line's text column, or
  the tag's column when the description starts on the next line; `out_jsdoc_property_description()`, `{@link}` copied
  as written); `ty_jsdoc_name_is_type()` copies tsgo eliding the import of a JSDoc name its fallback resolves as a
  value (tsgo's own `.d.ts` then doesn't resolve).
- JavaScript declaration emit (`declaration_emit.c`, plan 64 stage 6): CommonJS exports are found by a syntactic
  walk (`collect_commonjs()`, `ast_walk_statement()`), not the binder's export symbols; whether a member's name
  "resolves elsewhere" (tsgo's GetReferencedValueDeclaration) is looked up from the file's scope wherever the
  assignment is; a class expression's self-reference (tsgo's watchedClassSymbol) is found by writing the class into a
  scratch string first (`cjs_class_self_referenced()`); `this["x"]`'s quotes are re-read from source text
  (`this_property_literal_name()`); a JSDoc type literal member's span reaches the next tag but the map trims trivia
  before it (query's root.tsdown.config maps); `export default`'s `@type` is printed as its type, `module.exports =`'s reused (by
  observation of tsgo's quotes); a function typed by a generic JSDoc `@type {F}` gets its type parameter list cut out
  of the printed signature (`w_signature()`). Shown by: `baseline_tests --dts-js`, dtsdiff.
- `raise_stack_limit` (main.c): conditional tail recursion (`ty_instantiate_tail`) is C recursion where tsc loops
  (1000 steps ~16 MB), so the soft RLIMIT_STACK is raised to 64 MB (`worker_stack_size`); a lower hard limit still
  overflows. Correct: a loop. Shown by: a crash on type-fest's IntRange under `ulimit -Hs 8192`.

## Content mappers (`content_mapper.c`, documentation.md "Content mappers")
- `alias_original_names()`: tsgo substitutes an alias segment's original text in the *arguments* of a diagnostic
  covering it (displayMessageArgs); tttt replaces the quoted virtual name in the formatted text. Wrong if that name
  is quoted for another reason.
- `append_compiler_options()` sends only the options tttt has, not tsgo's whole CompilerOptions JSON (paths, lib,
  strings): a mapper reading those gets nothing.
- Not done: the `locale` initialize parameter, mapper protocol/stderr logging, declaration output names for mapped
  files (unchecked since tttt emits declarations, plan 64), `-b`'s tsbuildinfo `contentMapperIdentities`.
- `node_in_mapped_file()` (ty_display.c, declaration maps): a reused TypeNode doesn't know its file; found from one of
  its spans, a keyword by comparing the text at its offset. Correct: TypeNodes knowing their file. Shown by: dtsdiff.py.
