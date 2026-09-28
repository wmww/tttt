# Hacks
Suboptimal-but-working code, by subsystem. Each entry: where (function names), what is approximated, what the real
fix would be. Missing diagnostics and features belong in `bugs/*.md`, triage.md or a plan, not here.

## Global state, memory, parallelism
- `reexec_with_huge_pages` (`main.c`): glibc only reads `GLIBC_TUNABLES` at startup, so tttt execs
  `/proc/self/exe` once with `glibc.malloc.hugetlb=1` (skipped when the variable is set, under valgrind, or with
  `TTTT_NO_REEXEC`). 5-20% of wall time on every corpus. A malloc of our own (or arenas for the AST, see
  `bugs/perf_leads.md`) that madvises its memory like the checker arena does would make it unnecessary.
- Parser lists `shrink_list()` (`parser.c`) when done, because STC's first push reserves 4 elements and the AST's
  list elements are big (a parameter is 568 bytes): one realloc per list (nest +1.5% instructions) for -20% RSS. The
  real fix is smaller nodes.
- `current_checker` / `current_lazy_slots` (thread locals, `checker_make_current`): how the AST-side walk
  (`*_typecheck`, `*_evaluate_type`, `ty_of_node`, the `Ty const*` displayable) finds its checker. Stays set after
  `program_typecheck` because the test traversal and debug display evaluate types afterwards. The type core takes a
  `Checker*`; the walk should get one too (a check context with checker + errs + options).
- `compiler_options()` (`compiler_options.c`): a global set around load/resolve/check by `program.c`; should become a
  `Checker`/`Program` field read through that context. Every tsc option parses, but the checker reads only a subset
  (`grep -o 'compiler_options()->[a-z_]*' src`); the rest is carried but inert.
- `lazy_slot_new`: LazyType slot numbers come from one process-wide atomic counter, never reused, taken eagerly by
  everything that might need a memo. Sparse slot tables cost page fragmentation (bench RSS +6%), panics after 1G slots.
  Every LazyType is in some file's AST now, so per-file numbering would work; a long-running process needs reuse.
- `lazy_type_drop`: a slotted LazyType dropped while a checker is current resets its slot (assumed a check-time
  temporary); the AST's own slots are dropped by `checker_drop` (run by `program_drop` before the AST). Implicit rule.
- `lazy_type_get((LazyType*)...)` const casts (`ty/expr.c`, `ty/lower.c`, `ty/members.c`): harmless (the memo is in
  the checker); go when `lazy_type_get` takes a const pointer.
- `Checker.return_context`: the contextual type of `return` expressions is checker state saved/restored around function
  bodies (`function_impl_enter_return_context`), because `statement_collect_return_types`/`statement_typecheck` have
  no parameter for it.
- **Parallelism contract** (documentation.md "Resolve names"), what still writes shared state:
  declare phase: scripts and `declare global` declare straight into `Binder.globals`, ambient modules into
  `Binder.ambient_modules` (want per-file tables merged in link, like module augmentations; `report_conflicts` tells
  cross-file conflicts apart by comparing the source of the first declaration); every symbol/table/scope comes from the
  one `Binder.arena` and `binder_new_symbol` appends to `Binder.symbols` (want per-file arenas + alias lists); one
  `ErrorCtx`. Resolve phase: function/block scopes, their symbols and flow nodes are made from the same shared arena;
  it writes `is_assigned`/`is_assigned_in_closure`/`last_assignment_end` on `is_local` symbols and `Scope.flow` of the
  file's own persistent scopes; `Symbol.referenced` (noUnused*) is written also on script globals other files share
  (a data race once files resolve in parallel); `scope_add_expandos()`/`scope_add_this_property()` (see JavaScript)
  write file-level symbols; `jsx_lookup_factory` writes `SourceFile.jsx_fragment_factory_looked_up` through a const
  cast (`ModuleRef.jsx_runtime_reported` likewise). Check phase (plan 36 stage 2 fixed the audit's list): each
  checker reports into its own `ErrorCtx` (`Checker.errs`); the program's is `frozen` and `Binder.frozen` is set while
  checking, and a report into the frozen ctx, a new binder symbol or a first alias resolution then `log_error()`s
  (none on the suite or the corpora). These are runtime guards, not a proof: a path the suite doesn't reach can still
  write shared state; TSan on the corpora (stage 3) is the other net. cleat's `return ok;` sink is a thread-local
  (`thirdparty/cleat-failable-patch.txt`).
- **Once-per-program diagnostics under K checkers** (`Error.once_key`, `error_ctx_keep_first_once()`): tsc reports
  some diagnostics once per program, from whichever file asks first (a checker-wide memo). With K checkers each one
  reports it; the merge keeps the report made while checking the earliest file. Only the emit helper checks
  (TS2343/TS2807, `ty_check_emit_helpers`) are tagged, because only they showed up (`baseline_tests --checkers 2/4`
  against K=1, dumps compared modulo quoted text). Other memoised reports (TS2589, circularity, TS2318) are either
  global (deduped by the merge) or reported where the memo is evaluated, which the suite didn't show differing. A new
  "said once" memo in the checker must be tagged too.
- The resolver's caches (`ModuleResolver`, per `Program`) are unsynchronised; no cache of resolutions themselves (tsc:
  per directory + name + mode; not visible in profiles so far).

## Parser
- Escaped identifiers (`\u0061`) are scanned (`identifier_char_size()`) but named by their raw text: `\u0061` and `a`
  are different names (`bugs/escaped_identifiers.md`), and escaped keywords are identifiers (no TS1260). JSX names
  are compared by value (`jsx_name_value`) but resolved by raw text too.
- Two notions of "reserved": `parse_identifier()` refuses only the words of `is_reserved_identifier()` (return,
  throw, typeof, this, case, in, super, default) because it's also what parses identifier *names* and keyword types in
  many places; `parse_binding_identifier()` (bindings, class names) and the enum name refuse all of
  `is_reserved_word()`. Making `parse_identifier()` refuse them all broke 68 tests (`as const`, ...): each caller
  would have to say whether it wants tsc's parseIdentifier, parseIdentifierName or parseBindingIdentifier.
- Conflict markers (TS1185) are only looked for after a `\n` in `hop_whitespace_slow()` and at the file's start
  (`parse_file()`): anything else that consumes a newline (a string, template, comment, JSX text) is where tsc's
  scanner wouldn't look either, but a lone `\r` line ending isn't a newline to `hop_whitespace`. `Decl()` positions in
  symbols don't skip back over a marker (conflictMarkerTrivia1: tsc's full start is before it).
- Source text containing a NUL character ends the file (`const char*` everywhere): scannerUnexpectedNullCharacter1,
  scannerStringLiteralWithContainingNullCharacter1.
- `parse_error_in_literal()`: escape errors skip `parse_error()`'s "an error at an invalid character is TS1127"
  conversion (a `\` inside a string isn't a token start); TS1490 (binary file) uses it for the file's start.
- `parse_throw()`'s missing expression (a line break after `throw`) is a zero-width implicit any, and the checker's
  TS1142 finds the keyword's end as `statement span + strlen("throw")`.
- `written_name()` (symbol_chain.c) prints `(Missing)` for a symbol whose written name is empty and zero-width
  (tsc's missing identifier); tttt has no missing-identifier node, bindings use `BINDING_NOOP` (no symbol) where tsc
  declares `(Missing)` (objectBindingPatternKeywordIdentifiers0x symbols).
- Object literals and accessors: a missing `}` makes the literal end at its last token by walking back over ASCII
  whitespace (not comments) in `parse_object`; the TS1005 `'{' expected` of a bodyless accessor/method finds
  its last character the same way (`class_decl_typecheck`, the `EXPR_OBJECT` typecheck).
- Error recovery (plan 27): `list_abort_or_skip()` skips a token that starts an element of the current list instead
  of ending it (right while tttt element parsers fail where tsc's commit). The fail cache ignores
  `ParserShared.parsing_context` (a failure under one mask can replay under another; only affects invalid code).
  `parse_list_member()` asks the member-start predicate only when the member had errors (`errors_attempted`). The
  declarator loop of `parse_variable_statement` uses `list_abort_or_skip()` but checks its terminators itself after
  each declarator; `parse_binding_content()` patterns stop at the first bad element. A committed arrow function
  (`arrow_function_tristate()` TRUE) whose parameters still fail falls back to a parenthesized expression.
- `tok_scan_end()` is a second, cheap notion of a token next to `hop_token()` (`>` sequences scanned greedily; a
  template is scanned to its end or its first `${`).
- `parse_jsx_attributes` approximates tsc's `isInSomeParsingContext` as "inside JSX children always, elsewhere when the
  token could start a statement" (`jsx_children_depth` + a character list); JSX attributes/children should become
  ParsingContext lists.
- `parse_if_else` looks ahead past a `;` for `else`, because terminators belong to
  `parse_append_statement_sequence`; `ended_on_brace` there is the same kind of guess. `statement_takes_semicolon()`
  lists which statements end with tsc's parseSemicolon() by tag rather than by what each statement parser consumes.
- Statement spans end before the `;` (tsc's include it): `span_with_semicolon` patches it in
  `scope_export_default_expr`; other statement-span errors (TS7027) may be one off.
- `statement_is_bare_return()`: `return;` is an `undefined` literal whose span starts with `return`; a `has_value` bit
  would be honest.
- `IfStatement` is a flattened `else if` chain: the nested if has no `Statement`, so TS7027 can't be reported on it and
  `last_assignment_end` can't use its span.
- Class field `x!: T`: the parser drops the `!` (`let x!: T` keeps it in `Binding.definitely_assigned`); `class_decl.c`
  (`definite`, used by `class_instance_member`/override checks) and `check_used_before_initialization` peek at the
  source character after the field's name.
- `parse_generic_decl_item`: `in`/`out` type parameter modifiers give the variance (`get_variances`, relate.c) but
  never print (only `const` does; tsc's getTypeParameterModifiers prints all three).
- `parse_object_item`: `{ a = 1 }` keeps the default in `ObjectExprItem.shorthand_default` (resolved, checked by
  `assignment_target_typecheck`, not in the types baseline). Object literal methods are plain `EXPR_FUNCTION` values.
- Class accessors (`ClassItem.method.accessor`, `class_item_accessor_evaluate_type`): paired by name+staticness in
  `class_decl_lookup_names` (raw pointers between items of one vec); readonly only means "no setter".
  Interface/type-literal accessors become properties in `parse_interface` (`InterfaceMember.write_type`,
  `second_name`, `setter_param` kept for the write type and the symbols baseline).
- `ClassItem.method/field.name_string` of a computed key is the key's source text: what accessor pairs and overloads
  are matched by (tsc matches the late bound symbol, so `["a"]` and `a` pair there).
- `EnumCase.name`/`full_name`: string-literal member names are the source text between the quotes (escapes not
  processed).
- `regex_check_literal()` (`regex_check.c`, tsc's checkGrammarRegularExpressionLiteral) converts each regex literal
  to UTF-16 to port tsc's scanner 1:1. A position between the two surrogates of an astral character (errors on a lone
  surrogate without the u flag) is the third byte of its UTF-8 sequence; `utf8_byte_utf16_units()` makes columns and
  the test harness's squiggles count that. Group names with escapes are compared decoded but re-encoded as UTF-8, and
  spelling suggestions measure UTF-8 bytes.
- `await_is_operator`, `yield_is_operator_outside_generator`, `yield_await_is_keyword_here`: tsc's
  isAwaitExpression/isYieldExpression/isIdentifier from `ParserShared.in_async`/`in_generator`/`in_static_block`/
  `function_depth` plus a character peek (not a token). Parameter defaults inherit the enclosing function's context;
  class field initialisers save/restore the three flags inline in `parse_class_item_inner` (a `FunctionCtx` helper for
  "outside yield/await context" would be cleaner).
- Private names outside classes (`{ #x: 1 }`, `interface I { #x: T }`, `enum E { #x }`, `const #x`) have no AST flag:
  `object_item_is_private_name`, `interface_member_is_private_name` and `enum_decl_typecheck` look at the first
  character of the name span, and `ty_display` prints an enum member named `#...` as `__missing` (tsc's binder names it
  InternalSymbolName.Missing and puts it in no table; tttt still declares it as a member of E).
- `mark_named_evaluation()`: anonymous class expressions that get a name are marked by the parser at each construction
  site (`ClassDecl.named_evaluation`); a new place that assigns an expression to a name needs a call.
- Grammar vs parse errors (`error_ctx_grammar_error_d`, `SourceFile.has_parse_errors`, `is_grammar_code()`): tsc drops
  checker grammar errors in a file with parse errors. Newer checks use `error_ctx_grammar_error_d`; older ones
  (TS1254, some TS1155/TS1039, modifier checks) use `error_ctx_error_d` and always report. Parser-reported grammar
  codes in `is_grammar_code()` don't count as parse errors; others (TS1308, TS2852) still do. `is_grammar_code()`
  classifies by *code* (about 50 now, grown by every stage 2 cluster): a code tsc reports both from its parser and
  from its checker can't be both, and a new parser-side grammar check silently counts as a parse error until its code
  is listed. The fix is a `parse_grammar_error()` that marks the error itself (the Error would carry the flag).
- `parse_decl_statement()`'s import/export fallback (`export public import a = b`) drops decorators in front of an
  import (TS1206 unreported: nothing keeps them for the baselines' walk). `ModifiedNode.name` stands in for tsc's
  getErrorSpanForNode (TS1248). `statement_start_needs_check()` gates tsc's isStartOfStatement by the first words
  that need it, a perf shortcut that a new rejecting case has to be added to.
- `parse_index_signature()`: the parameter list isn't a real `FunctionArg` list: names are identifiers only (no
  patterns, no `this`), initializers are parsed and dropped (tsc still checks them: TS2304 in `[a = b]` is lost), and
  parameters after the first keep only name/type for the baselines. Parameter grammar errors are reported from the
  parser as grammar codes; the type ones (TS1337/TS1268) and TS1021 from `interface_member_typecheck()`.
- Enum members with computed non-literal names (`[e] = 1`, `EnumCase.computed_name`): the key expression is parsed
  and dropped (tsc's types baseline shows `e : any`), and the member's type displays as `(typeof E)["[e]"]` where tsc
  says `E`.
- Heritage clauses (`parse_heritage_clauses()`): an element where a type is meant (`implements`, an interface's
  `extends`) is parsed as tsc's expression first and, if it is an entity name, parsed *again* as a type
  (`parse_heritage_element()`), so the AST keeps `TypeNode`s there; a keyword name (`implements number`) becomes a
  keyword type and the checker recognises TS2864/TS2840 by the element's text. What tsc parses but never checks (a
  second clause, `extends A, B`'s `B`, non-entity expressions) sits in `unchecked_*`/`invalid_*` fields that are only
  resolved (silently, `errs` null) for the baselines. TS1172-TS1176/TS1097 are checker grammar errors reported by the
  parser (only the first, like tsc); `checkGrammarModifiers` failing doesn't suppress them as it does in tsc. The
  baseline walk hides the types lines tsc's isPartOfTypeNode drops with `BaselineTestCase.hidden_type_spans`.
- Parser anchors: a `span_from` on an anchor of the parent parser inside a child parser before `parse_success`
  truncates the span (was a bug in `parse_import_equals()`); the pattern may exist elsewhere.
- `parse_import_type`/`add_module_ref`: a `ModuleRef` made in an abandoned parse attempt stays on the file's list
  (one more file looked for).
- `CallExpr` argument types recomputed per use; `Value.object` refcount churn; `IdentifierType` could be split into a
  small common case; `CallableType`/`FunctionImpl` are two allocations (PERF comments).

## Binder (`scope.c`)
- Identifiers keep `symbol` (what the name found, maybe an alias) and `target` (aliases resolved, cached at resolve
  time) instead of one pointer plus an accessor.
- `binder_redirect` / `Binder.overlays`: a program that adds to a symbol of the shared lib program gets a clone
  (`clone_parent_symbol`); the checker maps symbols through the redirect at its entry points (`canonical()` in
  `ty/lower.c`). Anything new that keys checker state by `Symbol*` must do the same or lib-internal references get a
  second type. Fallout: an augmented `Console` makes `console.log = (...args) => {}` see both `log` signatures (wants
  tsc's getIntersectedSignatures).
- `make_gather_target`: `import * as ns` / `import x = require()` / `export as namespace N` alias a namespace symbol
  of their own (`Symbol.gather_module`, looked through by `symbol_members()`) so the type prints `typeof ns`; error
  messages print the gather symbol's name. Could go once messages name modules like tsc.
- Module augmentations (`merge_augmentations`, `retarget_decl`): not merged into a symbol of the shared lib program,
  nor an alias declared inside an augmentation that meets an existing name. A name added through an `export *` module
  goes into that module's resolved table, not those of modules resolved earlier that `export *` from it. Named imports
  from an `export =` module only see the namespace side of what is assigned; `export = a.b` is a variable (loses the
  type meaning); `import("./a")` of an `export =` module has no synthetic `{ default }`.
- `binder_module_of_ref()` memoises in the AST's `ModuleRef` (safe because all are resolved in the link phase).
- `globalThis` is a namespace symbol over `Binder.globals`: every global is a property (`let`/`const` and types'
  values too; tsc: only `var`s and functions), and `ty_namespace_type` builds all ~2k props on first touch.
- `arguments` has no symbol (`IdentifierExpr.is_arguments`, typed `IArguments` by name in
  `ty_identifier_reference_type`).
- `scope_declare_default_local()`: a compatible local declared before `export default function A` keeps the name `A`
  (tsc resolves `A` to the default export). `resolve_alias_target`: TS1192 only suppressed for declaration files under
  `allowSyntheticDefaultImports`.
- Exported and local declarations of one name live in two tables with no link (tsc: `exportSymbol` stub). TS2440 is
  special-cased in `binder_link`, TS2395 is a pass of its own; `export { b }` of a local also declared exported
  doesn't show the merged declarations.
- `block_is_instantiated`: syntactic; `export { x }` is looked up through `Scope.block` of enclosing scopes
  (`alias_target_state`) but a namespace found there is judged with the outer chain of the namespace being checked;
  cycles cut by depth 16.
- Class/interface members aren't symbols (`Prop` has the identity). `ty_check_class_duplicate_members` replays tsc's
  binder for the static side (sharing a table with merged namespaces' exports) and `object_bind_members` for object
  literals: check-time imitations, so a namespace export conflicting with a static member still shows in `typeof C`.
- `declare_symbol`: a declaration named like a builtin type symbol (`class number`) gets a detached symbol on conflict,
  so `new number()` doesn't resolve.
- `symbol_table_collect` allocates (`ty_namespace_type` members, `extend_export_stars`).
- Type parameters of merged interface/class declarations are one symbol per name (`scope_new_generic_merged`) except
  when the earlier declaration is the shared lib program's (nothing is written to lib symbols); `type_parameter_is_unused`
  (unused.c) checks every declaration's parameter at the same index for that case.
- TS2441, TS1203, TS2308, TS2661, TS2669 are reported from the binder/link step (tsc: checker). TS2308 re-walks the
  `export *` graph separately from `module_resolved_exports()`. `ty_emits_commonjs_like()` predates
  `source_file_emit_module_kind()` and could be rebuilt on it.
- TS2301 (`function_collect_local_value_names()` / `statement_collect_var_names()`, statement.c) is a read-only copy of
  `statement_hoist_names()` (the constructor's scope doesn't exist when earlier fields resolve): keep in sync. Adds
  `Scope.instance_field`, `constructor_locals`, `constructor_local_count`; `IdentifierExpr.invalid_initializer_reference`
  only keeps the symbols baseline target.
- `Scope.expression_name_of`: the name scope of a function expression, only for its name's await/yield context.
- TS1361/TS1362 valid use sites (`FlowBuilder.type_only_alias_ok`, `in_type_query`) are set by the enclosing
  constructs, not derived from parents like tsc's isValidTypeOnlyAliasUseSite. `export =`/`export default` of a
  namespace- or type-only name gets `symbol` but no `target` (`identifier_expr_lookup_names`), so the checker sees an
  untyped name there. `check_type_only_value()` (expr.c) finds the start of a type-only default import's clause by
  scanning back from the name for `type` (PortItem has no span for the import clause).
- `statement_resolve_names_reachable` (`STATEMENT_WITH`): the body of a `with` is bound and resolved (scopes, flow,
  hoisted `var`s) but with a null ErrorCtx, since tsc never checks it and tttt reports many checker errors (TS2304...)
  from the resolve walk. Whatever resolve-time errors tsc reports from its binder (duplicate labels...) are lost in
  there too; TS1101 and unreachable code go through `flow->errs` and survive. The real fix is the
  resolve walk not reporting checker errors. The baseline walk prints the body with `BaselineTestCase.in_with` (every
  type the error type, no symbols) but still evaluates the types it then throws away.
- `UnaryOpExpr.outside_generator` (`yield x` outside a generator: TS1163): the operand is resolved with a null ErrorCtx
  and not typechecked, the same trick as `with` (tsc's checkYieldExpression returns before checking it).

## Control flow graph (`flow.c`, builders in `expr.c`/`statement.c`/`function_impl.c`)
- No array mutation flow nodes (`x.push()`, `x[i] = v`): only evolving arrays need them (see Narrowing).
- Class *expression* methods are FUNCTION_IS_DECLARATION (tsc: narrowing continues outside); `export { x }` doesn't
  mark the local as assigned (tsc's markNodeAssignments).
- `expr_is_narrowable_reference()`: `a = b` counts whatever the left side is (flow.c TODO).
  `expr_is_optional_chain()` re-walks the chain from the top per link (quadratic, chains are short).
- A destructuring assignment nested in another pattern (`[[a] = [1]] = e`) gets assignment nodes from the outer
  pattern only.
- Body-less functions (overloads, signatures, the libs) get a `FlowBuilder` but no start node: their parameter
  defaults have no flow nodes.
- `Statement.flow` costs 8 bytes per statement for TS7027 after `never` calls (only under `allowUnreachableCode:
  false`). Labeled statements have none.
- `function_impl_check_end()`: an anonymous `function` expression is reported at its parameter list (tsc: the keyword);
  async functions and generators with a return annotation are skipped (TODO), as is TS7030 on bare `return;`.

## Narrowing (`ty/flow.c`, hooks in `ty/expr.c`)
- Aliased conditions (`aliased_condition`) only narrow identifier references (tsc also readonly properties).
- Not ported (TODOs in `ty/flow.c`): destructured discriminants (`const { kind } = x`, spurious TS18048 in
  dependentDestructuredVariables), `x.constructor === C`, `[Symbol.hasInstance]` predicates, `"a" in x` intersecting
  with `Record<"a", unknown>` (inKeywordTypeguard), `unknown` narrowed by switch cases, erasure of generic construct
  signatures in `constructor_instance_type()`, narrowing by a literal's regular type.
- `chain_object_type()` (ty/expr.c): tsc marks the `undefined` of an optional chain that ended (a marker type) and
  strips only that; here every `undefined` of an object continuing a chain is taken for the marker, so `a?.b.c` with
  an optional `b` has no TS18048. Wants an `optional` intrinsic that prints as `undefined`.
- `remove_nullable_by_intersection()`: always intersects with `{}` (tsc: `{} | null` / `{} | undefined` when the other
  nullable is still possible).
- `narrowable_type` (tsc's getNarrowableTypeForReference): constraint positions are marked by the resolve walk
  (`constraint_position` on IdentifierExpr/PropertyExpr); tsc's element-access rule is approximated by "literal index
  only". A pattern with a rest element (`pattern_type_has_rest`) is never a contextual type there (tsc types the
  initializer twice, CheckMode.RestBindingElement).
- No `x!` rule of getFlowTypeOfReference; no evolving arrays: `let x = []` and `x = []` on an auto-typed `let x;` are
  `any[]` (`initialized_binding_evaluate_assigned_type`, binding.c and ty/flow.c HACKs; triage "Evolving arrays").
  At flow depth 2000 the query answers the error type silently.
- `pattern_path_type()`: assigned types in destructuring *assignments* go through `ty_access()` by index/name; rest
  elements, computed keys and iterables fall back to the declared type.
- Auto types (`TY_AUTO`): `IdentifierExpr.outer_reference` is decided by the resolve walk (a hoisted `var` declared
  after the closure isn't outer); tsc's isNeverInitialized is approximated by `!symbol->is_assigned`.
- The "outer variable is assumed initialized" rule is decided when the walk reaches a start node (`Symbol.flow_start`,
  `FlowQuery.left_container`), not up front.
- `ty_flow_expr_type()` results computed during a loop are cached per back-edge walk under a generation number in
  `Checker.links`: entries of finished generations are garbage until the checker dies.
- `LazyMemo.hits`: expression memos allow 8 re-entrant evaluations before answering the error type. A runaway guard,
  not a design.
- `prop_has_explicit_type()`: a property of a mapped type counts as explicitly typed (tsc asks the property it was
  mapped from); JSDoc-typed properties never count.
- `ty_check_element_implicit_any()` (TS7053) skips callable objects (the gate predates expandos; retry it), mapped types
  over something generic, unresolved bases, JS files. `is_for_in_numeric_variable()` doesn't check that the access is
  inside the loop body.
- `ty_infer_type_predicate`: builds synthetic condition flow nodes and a reference `Expr` in the arena per
  unannotated signature.
- Assertion signatures: a callee whose name has no explicit type is silently not an assertion (tsc: TS2775/TS2776).

## The AST-side walk
- Leftovers of the old type world: `owned_symbol` + `symbol_ref` on `GenericItem`/`InferredType`/mapped keys and
  `key.expr.owned` + `ref` on interface members only existed for clones (one pointer would do);
  `LAZY_TYPE_FUNCTION` memoises the already interned `ty_function_type()` (users: `value_entry_type`,
  `class_item_get_type`, the test traversal).
- `ty_binding_assigned_type`: not memoised per binding (nested pattern accesses redone per query).
  `ty_binding_default_type` checks a default with its contextual type (bypassing its LazyType), so it keeps its own
  in-flight list (`defaults_in_flight`). `binding_element_implied_type` evaluates a default under `flow_uncached` so
  sibling references (`contextual_patterns`) aren't memoised; everything else the default mentions is uncached too.
- `binding_element_context` (lower.c) takes the property through `ty_contextual_prop_type` (index signatures
  included); tsc's getTypeOfPropertyOfType doesn't.
- `binding_add_padded_implicit_any_errors` (TS7031, tsc's padTupleType) runs for unannotated parameters and defaults
  of nested patterns; tsc reports wherever checkDeclarationInitializer runs.
- `FunctionImpl.constructed_class` is set in `class_decl_lookup_names` (which also reports TS1093 and drops a written
  constructor return type).
- `FunctionImpl.private_ambient` is set by `class_decl_typecheck` right before checking the member (AST mutation in
  the typecheck walk; FunctionImpl doesn't know its access modifier).
- `ClassItem.field.ts_assigned` (`field_type_from_assignments()`, class_decl.c): tsc's getFlowTypeInConstructor queries
  a synthetic `this.x`; tttt reuses the first real `this.x = ` target of the constructor / static block as the
  reference (none: the field is any anyway). Not ported: an ambient `declare x;` taking the base class property's type.
- `expr_requires_scope_change()` (function_impl.c, TS2373) is a hand-written expression walk (no JSX): tttt has no
  generic expression visitor.
- `TY_RELATION_ASSIGNABLE_STRICT_NULLS` (ty.h HACK): what the operator checks mean by "extends number" (null/undefined
  don't, whatever strictNullChecks says).
- `generic_params_expr_typecheck` (`f<A>` outside a call): constraints checked against the first signature only.
- `ty_function_return_type`: a return type asked for while being inferred is `any` (tsc's failed
  popTypeResolution), without TS7023.
- `lower` TYPE_TYPEOF: `typeof x` is the type of the expression `x`, narrowed (lower.c TODO says it should be the
  declared type).
- `ty_merged_value_type`: the merged type copies the first named entry's `declared` struct and gets all entries'
  members; a second `Ty` next to the unmerged one (`ty_static_type_of`, what `this` in a static member sees).
- `variable_statement_typecheck` TS1254: any identifier or property access counts as a "literal enum reference".
- `contains_spread` (members.c HACK, tsc's ObjectFlags.ContainsSpread): "a property came from a spread" (`{ ...{} }`
  doesn't count). The widened literal stays an object literal type.

## Calls and inference (`ty/call.c`, `ty/infer.c`, `ty/access.c`)
- `Inference.circularity` (`infer_from`): tsc's InferencePriority.Circularity comes from invokeOnce, both for a pair
  that is deeply nested on both sides and for a pair visited before whose recorded priority was a circularity. tttt's
  `visited` set records no priorities: a pair met again counts as circular only while it is still on the object stack.
- `props_related` (`ty/relate.c`): tsc skips a property whose source and target are the same symbol; tttt's props are
  per type, so only object literal accessors of the same function are recognized as "the same" (what the recursive
  getter idiom needs). Other shared symbols are related (and typed) as usual.
- `ty_call_typecheck` / `KEY_CALL_RESOLUTION` (plan 28): the typecheck walk skips resolving a call again when the type
  pass's last settled resolution under the same contextual type would report nothing; it trusts no resolution-relevant
  state changed. "Would report" is set by hand at each error site of `resolve_call`/`resolve_with_signatures`
  (`mark_would_report`): a new diagnostic there must set it too. Calls with errors still resolve twice.
- `infer_from`: reverse mapped types are made eagerly (bounded by `Checker.reverse_mapped_depth`; tsc's are lazy); only
  the naked-type-variable, homomorphic, mapped-constraint and return-type priorities.
- `ty_union_signatures` (tsc's getUnionSignatures) isn't cached per union. `resolve_call` falls back to member-by-member
  resolution when some member has no signatures (TS2348 where tsc says TS2349).
- `super(...)` (`resolve_into`): heritage type arguments are passed as explicit type arguments; without written ones
  they're inferred where tsc instantiates with defaults.
- `resolve_super` walks scopes instead of tsc's containers. `check_before_super` (TS17009/17011) only looks at
  `this`/`super` whose nearest function scope is the constructor.
- `report_arity`: too few arguments are reported at the callee's `inner_span`; a generic rest's second arity check
  doesn't distinguish spread arguments or several candidates (tsc's candidateForArgumentArityError).
- `reorder_candidates`: literal-typed overloads first; tsc also orders merged declaration groups (later first).
- `clone_type_params` (members.c): fresh type parameters only for bounded ones (`ty_param_is_bounded`) or when the
  mapper brings type variables in; otherwise unbounded ones are shared with the target. Anything relying on two
  instantiations *not* sharing one would notice.
- Return mapper (`infer_type_arguments`, call.c HACK): the inference from the contextual type via the outer return
  mapper only fills type parameters the main inference left without candidates (using it for everything was
  tsc-exact on tests but cost valibot 9%). The first pass gives a non-context-sensitive function argument its parameter
  type fully instantiated with the return mapper, other arguments only the top level (`instantiate_instantiable`).
  Higher-order branch not tsc's (`ty_generic_function_in_context` HACK). A binding pattern's return mapper drops what
  inferred `any` (tsc's nonInferrableAnyType, which tttt lacks) and a pattern with a rest element gives none. Nested
  literals ask `ty_instantiate_contextual_type()` only in `check_for_mutable_location()` (tsc instantiates every
  contextual type lookup with the returnMapper).
- Second pass contextual types (`infer_type_arguments`, HACK): a type parameter without candidates stays itself only
  where it would be `unknown` (then `Checker.contextual_fixing_mapper` fixes it in contextual signatures); others are
  substituted by the constraint/default as before, because tttt's contextual lookups don't go through a type
  parameter's constraint (tsc's getApparentTypeOfContextualType).
- `infer_type_arguments` second pass: a type parameter is fixed before a context-sensitive argument when the contextual
  signature's parameter types mention it (`fixed_by_argument`, probed by instantiating with `unknown`; `fixed_by_expr`
  for literals); tsc fixes lazily when a contextual parameter type is asked for. Inference from the contextual return
  type only when it has no type variables.
- `TYF_NON_INFERRABLE`: set on the silent never and `ty_any_function_type()`, propagated through unions,
  intersections, references, object literals and aliased anonymous instantiations.
- Skip-sensitive first pass (`ExprCtx.skip_sensitive`): returnOnlyType (`return_only_type()`) gives generators
  anyFunctionType instead of their body's type; the first overload round's applicability
  check skips such arguments entirely (`first_unfit_argument`); isPartiallyInferableType only as a priority.
- `infer_intra_expression` (HACK): only arrow functions are sites (plus nested/spread literals); methods and function
  expressions would need `this` recomputed per check (tttt memoizes it).
- Higher-order inference (`inference_propagate_generic`, `Arg.generic_function`/`generic_call`, `waits_for_sensitive`):
  a failed call's return type is re-inferred without them, then the arguments are re-checked against the failed
  candidate only to restore their memos (the types baseline prints the last check). `add_implementation_success` does
  the same after probing the implementation signature.
- `signature_resolve`: function expressions take the contextual signature's type parameters (`KEY_CONTEXT_DONE`) only if
  the signature is resolved after the context was assigned.
- `const` type parameters (`ty_is_const_type_variable`, `in_const_context`): no mapped/conditional/variadic cases.
  Nested array literals count as candidates by walking syntax (`add_nested_array_literals`) because tuples are interned
  and can't carry ObjectFlags.ArrayLiteral; `Inference.array_literals` approximates unionObjectAndArrayLiteralCandidates.
  Rest arguments of `...args: T` get `T[i]` as contextual type only when const.
- `ty_generator_context` (iterate.c): only a union annotation/contextual type is filtered to what a generator fits.
- Generators (`infer_generator_type`): no next type inferred from how `yield`s are used (contextual/declared TNext or
  `unknown`). A non-iterable `yield*` prints its type while the return type is in progress (function.c HACK: makes
  `yield* foo` inside `foo` any, YieldExpression6_es6), a side effect of printing.
- Async (`promise_decl`, `promised_context` in function.c): `Promise<T>` from the global looked up by name (`any` if the
  lib has none). `ty_awaited()` wraps a generic result in the lib's `Awaited<T>`, but iteration/generator types use
  `ty_awaited_no_alias()` and async returns `ty_awaited_returned()` (strips `Awaited<X>`), because tttt can't relate
  `Awaited<T>` to T through the conditional's default constraint; once it can, use ty_awaited() everywhere. Unwrapping
  is by name (`Promise`/`PromiseLike`), not any thenable.
- Contextual `this` (`ty_contextual_this_type`): the literal's contextual type and enclosing literal are remembered
  when checked (`KEY_LITERAL_CONTEXT`/`KEY_LITERAL_PARENT`, last one wins; no parent pointers); an earlier `this`
  falls back to the literal's own type. `ThisType<T>` recognised by name, not instantiated with the inference in
  progress.
- `ty_access`: `null`/`undefined` members of a union object are skipped silently (TS18047/18048 come from
  `check_access`).
- `member_refs()` (check.c) duplicates the member walk of `access()` (access.c) to get `Prop`s; special-cases numeric
  names on tuples so as not to create types (new types change type ids and so display order). Unions: a member without
  the property but with an index signature is looked up with index type `string`.
- `PropertyExpr` carries four resolve-walk pointers (`enclosing_class`, `in_constructor_of`, `this_parameter`,
  `this_function`), 32 bytes per access; a `Scope const*` would do.
- `jsx_signatures_of`: an unknown intrinsic string tag's fake signature takes the error type (tsc: TS2339).
  `Checker.jsx_element` lets `report_relation_error` apply the JSX head rule (`ty_jsx_skips_head`); relations about
  attributes outside `ty_check_jsx_element` don't see it.
- `literal_lacks_prop_of` (infer.c): tsc's propertiesRelatedTo rejects a source property that an *unwidened* object
  literal target (ObjectFlags.ObjectLiteral) lacks, in every relation; tttt's regular object literal types are also the
  widened ones declarations hold, so the rule can't live in relate.c and is only applied in `common_supertype` (where
  the candidates are unwidened). Relations to inference results or nested literals don't see it.

## Expressions (`ty/expr.c`), contextual typing
- `context_map` (`ty/expr.c`): tsc maps contextual types without union reduction (mapType's noReductions), so an
  `unknown` from one member stays a member; tttt's unions absorb into unknown, so context_map drops an `unknown` part
  when other parts give something. Equivalent while unknown contributes nothing to a contextual type; a real
  no-reduction union would be the general form.
- `ty_return_expressions_context` / `KEY_CONTEXT_IIFE_RETURN` (`ty/expr.c`, set in `check_call`): an IIFE's call's
  contextual type is recorded on the function the first time the call is checked with one; if the function's return
  type was computed before that (a context-free evaluation of the call first), its return expressions never see it.
- Expression memo (`check_with`, `LazyMemo.context`, `lazy_type_set`): an `Expr`'s memo is whatever the last check
  with a contextual type produced; `kind_uses_context` decides what gets rechecked. Correctness rests on "whoever knows
  the contextual type checks first". Flow analysis asks bottom-up: `initial_or_assigned_type()` re-derives the context
  for `x = e` and annotated declarations; compound/logical assignments and destructuring defaults still type function
  expressions without it. The `.types` traversal of an unchecked program types without context. Union order in tttt
  depends on who asks first (type ids).
- `check_with` doesn't strip freshness from a literal of the contextual type (harmless while relations normalise).
- `assign_contextual_signature`: once per function expression, from the first contextual type with a usable signature;
  generic contextual signatures aren't instantiated (their type parameters leak), no contextual `this`.
  `discriminate_by_object_members` skips optional properties a literal leaves out.
- Excess property checks (`has_excess_properties`, relate.c): keys of a generic mapped type count as known; no
  intersection-target re-check of nested properties. Union discrimination is `discriminated_union_target` without the
  key-property map. Contextual union members are reduced (`string | "a"` is `string`; tsc noReductions), also in
  `contextual_array_element_type`/`ty_tuple_slice_element_type`.
- Destructuring assignments (`assignment_target_typecheck`): object rest elements aren't checked (null source). The for-of destructuring check (statement.c HACK)
  relates the regular element type (tsc: property by property). Array literal targets type as arrays, not tuples (tsc's
  inDestructuringPattern), and a binding pattern isn't its initializer's contextual type.
- Expando assignments to functions (`foo.constructor = 1`) are checked against the existing member;
  `is_container_declaration()` (expr.c) approximates tsc's isAssignmentDeclaration for `ns.a = {}` (JS: unchecked when
  `ns.a` has exports; a CommonJS export is always taken as declaring members).
- `apparent_contextual_type` maps type variables/intersection members to apparent types only for `this` in object
  literals; `ty_apparent_type` leaves intersections alone (tsc's getApparentTypeOfIntersectionType).
- `missing_property_type` (HACK): a failed property access is errorType only under conditions approximating when
  check.c reports it (object type, not `typeof globalThis`, no unresolved base, not JS); tsc returns errorType exactly
  when it reports.
- `ty_unresolved`: an unresolved type name is a `TY_INTRINSIC` with `TYF_ANY | TYF_ERROR` printing as the written name,
  interned by name in the literal table.
- `check_array`: tuples only from a tuple-like contextual type, a destructuring pattern or `as const`.
- `tagged_template_call()` (call.c): a tagged template resolves through a stand-in CallExpr whose callee is a shallow
  copy of the tag Expr (spans and syntax only; typing the copy would make a second memo) and whose args are the
  substitutions; `CallInfo.tagged_template` adds the TemplateStringsArray. A real TaggedTemplate node kind, or call
  resolution over an argument list abstraction, would avoid the copy.
- `compute`: Bigint values are doubles (`Value.bigint`). `satisfies`
  doesn't narrow its result.
- `ty_apply_type_arguments` (instantiation expressions): maps every signature taking at least that many type arguments;
  ill-fitting ones aren't dropped. `instantiate_anonymous` instantiates their type arguments (`typeof C<O>` in `type
  R<O>`), but not one whose object type mentions the type parameters too (instantiate.c HACK). Their display is the
  expanded object type (`{ new (): D<string>; ... }` where tsc prints `typeof D<string>`).
- `ty_non_nullable` of a type parameter/deferred type whose constraint allows nullish returns the stripped
  *constraint* (tsc: `T & {}`), losing `T` (checker.c HACK).
- Fresh literals: literal ids follow tsc only where tttt makes literals in the same order (`1 | 0` vs `0 | 1`).
- Iteration (`ty/iterate.c`): errors reported at the top only (tsc adds related info).
- Error elaboration (`elaborate_error`, ty/check.c): reads memos (`ty_mutable_location_type`,
  `ty_array_literal_as_tuple`) instead of re-checking, so a memo typed under another contextual type can show a
  different source. `ty_is_generic_object` approximates isGenericObjectType. `ty_best_matching_type`: no key-property
  map, references matched by target, anonymous types by alias, "most overlappy" counts shared property names.
- `intersection_props_related`: which member TS2741 names under a whole-object failure was matched to one tsc example
  (probe `test/probes/plan19/valibot/overload_omit_intersection_missing_prop.ts`), not derived from tsc.
- `type_decl_span` (related-information positions): declared types, function type nodes and type literals only; mapped
  properties of a renaming `as` clause lose their declarations.

## Type core (`src/ty/`)
- Relation error reporting (`relate.c`, plan 34): `flush_incompatible()` reads a property's name back out of its
  pending "Types of property 'NAME' are incompatible." message (fixed prefix length) instead of storing it. tsc's
  `headMessage` is approximated by `RelationState.at_top` (the first `is_related_to()` call): a suppressed top-level
  head is only kept when a pending incompatible step suppressed it, because the callers of `ty_explain_unrelated()`
  rely on other overrides (TS2741...) dropping the natural head. `skipped_source/target` (tsc's lastSkippedInfo) and
  `override_next_head` aren't restored by `report_truncate()` (tsc's resetErrorInfo restores both).
- `ty_conditional_resolve`: an `infer X extends C` whose inference doesn't satisfy C falls back to C (instantiated with
  the conditional type's mapper and the other inferences, tsc's nonFixingMapper) only when C is then free of type
  variables; tsc falls back to a generic C too. Doing that leaked LazyPath's own `TValue` into valibot's `forward()`
  (deferred conditional types don't keep their inferences: `inferred_true_branch()` recomputes them). Probe
  `test/probes/plan19/zod/ark_infer_constraint.ts`.
- Substitution types (TY_SUBSTITUTION): the true-branch chain around a node is `TypeNode.flow`, set by the resolve
  walk; it flips at function parameters and index signature keys and stops only at function bodies (setter write types,
  `typeof` operands, arrow functions in type queries not special). The homomorphic mapped key rule (`number |
  ${number}`) isn't done. `mentions_type_variables` treats a substitution as "unsure" (precision sent react-hook-form
  into relation overflows, bugs/type_core.md).
- `renames` in `lower` (TYPE_IDENTIFIER): whether a generic alias instance keeps the alias name is approximated
  syntactically.
- Union origins (`Ty.composite.origin`, plan 37 B): not instantiated (instantiating a union with an origin makes a
  plain union), and `ty_map`-style helpers rebuild unions without them (tsc's mapType keeps `IteratorResult<...>`
  through `await`/narrowing); stage 2 in plan 37. `dedupe_unions` (checker.c) treats two unions of the same members
  as one in an intersection even when their origins differ, where tsc distributes (display only, saves a quadratic
  cross product on `keyof (A | B)`).
- `ty_realias`: a renamed object type is a second `Ty` with the same target + mapper (members resolved twice); renamed
  deferred types are copies interned by (type, alias, args), related to their twin structurally. tsc's "local alias"
  exception isn't applied.
- `generic_mapped_target_related` (relate.c HACK): a generic source that fails tsc's isGenericMappedType rule falls
  through to member-by-member comparison, which a memberless mapped type passes (stands in for `Readonly<T>` to `T`).
  The mapped type parameter P is lowered without constraint (`mapped_key_param_mapper` stand-in for `as` clauses).
  `write_constraint_of_indexed` (HACK) lacks AccessFlags.NoIndexSignatures.
- `structured_related`'s TY_KEYOF target over a generic mapped type (HACK) compares with the `as` type (or the
  constraint) only; tsc also adds the `as` type applied to the modifiers type's apparent keys (getApparentMappedTypeKeys).
- Homomorphism (`homomorphic_variable`, instantiate.c) still needs a syntactic `[K in keyof X]`: tsc's
  getHomomorphicTypeVariable also takes `[K in Keys<T>]` whose alias lowers to `keyof T`. The lowered constraint is now
  checked too (a `keyof T` substituted in a true branch isn't homomorphic: type-fest shared-union-fields-deep).
- `ty_simplified` only does indexed accesses (no getSimplifiedConditionalType, no generic-tuple-by-number case).
- `apparent_type` (relate.c) keeps a TY_INDEXED/TY_CONDITIONAL without base constraint as itself (tsc: `{}`).
- `class_base()` (members.c HACK): a base class *name* is found syntactically; any other `extends` expression is the
  return type of its first construct signature that takes the written type arguments (tsc's
  getInstantiatedConstructorsForTypeArguments, without its TS2510 check that they all agree). A base that constructs
  `any` is no base (tsc: anyBaseTypeIndexInfo, `[x: string]: any`); `ty_has_unresolved_base()` gates TS2339/TS7053
  for those instead. Inherited members of a type-parameter base come from its constraint when the class is declared,
  and aren't re-read from the type argument when a reference to the class is instantiated.
- `ty_non_augmenting_base()` (tsc's getSingleBaseForNonAugmentingSubtype) is computed from the syntax (any non-static
  class item or interface member counts as a member), not from a symbol table.
- `this` types: `Prop.thisless` follows tsc's syntactic isThisless rules; `this` in a static member is the static side;
  relations compare a reference with a this-argument structurally with the plain one (tsc: one more covariant argument).
  A bare `C`/`this` inside generic class C isn't the class's own instantiation (lower.c TODO).
- `ty_resolve_enum` / `evaluate` (enums.c): no property accesses of `const` objects; `const` variables in other files
  count as constant without tsc's resolvedOtherFiles; members of ambient non-const enums without initializer
  auto-increment (tsc: computed).
- `relate.c`: no optionality of parameters (no `undefined` added); Array<S> -> ReadonlyArray<T> shortcut on element types (HACK);
  `is_rest_array` (HACK) counts any non-type-parameter as a rest array (tsc normalizes `[...X]`).
- `has_conflicting_discriminants` (checker.c HACK): intersections with disjoint discriminants reduce to never when made,
  not lazily (getReducedType); properties/aliases in flight (`ty_node_reaches_alias_in_flight`, `members_judgeable`)
  are skipped and the answer isn't remembered, while the intersection is interned with it. Real fix: a lazy reduced
  view of intersections.
- `get_variances` / `get_alias_variances`: arrays, tuples and synthetic targets hardcoded covariant.
  `VARIANCE_UNRELIABLE` comes from a checker-wide flag set whenever a deferred/mapped type is compared during *any*
  variance measurement (tsc: only when a marker is reached through it), remembered as `CACHE_UNRELIABLE`. No
  `Unmeasurable` (treated as unreliable, so the fast path can accept pairs tsc needs identical). Alias marker
  instantiations remembered by type id (`KEY_ALIAS_MARKER`).
- Identity relation (`identity_structured`): tuples of different shapes never identical; two references to one target
  compare type arguments only; the two-conditional rule doesn't infer the source's `infer` parameters.
- Relation cache keys are plain (source id, target id), no postfix for in-scope type parameters/intersection state.
  `RelationState.overflow` (100 deep) reports TS2589 at the current node (relate.c HACK: tsc says TS2321; tttt's depth
  runs out where tsc's instantiations do).
- `ty_conditional_resolve`: permissive (TY_WILDCARD) and restrictive instantiations only replace what
  `mentions_type_variables` finds (a walk up to 8192 types / 48 deep, "unsure" beyond; a failed check defers when
  unsure) since TYF_COULD_CONTAIN_TYPE_VARIABLES is set on every function/object literal type. The wildcard doesn't
  collapse templates, keyof, template literals. An `infer` constraint mentioning type variables isn't checked against
  the inference. `ty_implied_infer_constraint` lacks the mapped-extends-type rule.
- `TyCondBranch` (`lower_branch`): branches are lazy only when the node reaches the alias being resolved
  (`node_reaches_alias_in_flight_deep`); tsc always resolves on first use (plan 29 stage 4, `bugs/type_core.md` "Circularity", undone: type-fest
  regressions not understood).
- `Ty.conditional.root` + `.mapper` (`instantiate_conditional`): outer parameters from `collect_free_params`; when it
  gives up (`TyConditionalOuter.unknown`) instantiation falls back to the uncached path with the full mapper.
- `collect_free_params` / `Ty.anonymous.free_params`: walked when the type is created; inner declarations removed at the
  end rather than scoped; gives up (`free_params_unknown`) on `typeof`, `this`, computed keys, parameters with
  initializers, every function and merged value type. `TyMapper` is always flat. `LowerCtx.generic` is only a hint.
- `Checker.current_node` (TS2589/TS2615/TS4110/TS1062 location): `lower()` makes a generic reference inside the current
  node the current node while lowering it, a guess at tsc's walk order. TS4110 has no node span.
- Reference resolution in the walk: `identifier_type_typecheck` resolves referenced aliases and `type_decl_typecheck`
  alias-to-reference bodies, but `Foo<X>` isn't lowered in the walk (no `in_alias` context); resolving every alias
  changed type id order.
- `expand_mapped`: expands once the key type is a union of literals/primitives; modifiers come from
  `MappedInfo.modifiers_type` only when the key was written as `keyof T`. `mentions_mapped()` (ty_display.c)
  compensates for instantiating member signatures that can't reference the mapped parameters (fix in `instantiate.c`,
  bugs/display.md).
- `instantiate_mapped_over_list`: homomorphic mapped over array/tuple recognised syntactically; bails out (mapped array
  methods) for rest/variadic tuples, unions of lists, `any`, list-constrained type parameters.
- Generic tuple constraints (base_constraint): only variadic parameters with array constraints are replaced; `[S,
  ...T][i]` below the fixed elements counts only the leading ones. `normalized_tuple`: `...readonly T[]` becomes
  `...T[]` but keeps the written alias for display. `lower_tuple`: rest elements keep the written `T[]` (tsc: `T`).
- `ty_keyof` of primitives only for intrinsics/literals; `ty_indexed` on anything but plain props, index signatures and
  tuple/array elements stays deferred.
- `lower_identifier`: the `object` keyword is a builtin declaration named "object". `lower` composites skip the `any`
  placeholder `parse_type` makes for a leading `&`/`|` (HACK).
- `signature_resolve` (instantiated): return types are lazier (`signature_return_type`).
- `builder_add_interface_member`: index signature value types lowered eagerly; computed keys that aren't a literal or
  unique symbol are dropped (tsc: late bound index signature from an entity-name key of type string/number/symbol).
- `ty_union`: no subtype reduction (`ty_union_subtype_reduced` on request) and doesn't drop `null`/`undefined` without
  strictNullChecks. `ty_union_subtype_reduced` bails above 64 members (PERF): tsc has no cap but skips pairs by key
  property; `ty_union_key_property` exists now, so port that and drop the cap.
- Cross-product caps: `intersect_unions`, `ty_template_literal` (100k) and `normalized_tuple` (10k) return the error
  type silently.
- `apply_mapping` (template.c HACK): Uppercase & co. map ASCII, Latin-1, basic Greek and Cyrillic only; a
  one-character placeholder steps one UTF-8 code point (JS: one UTF-16 unit). `${symbol}` templates are silently
  `string` (no checkTemplateLiteralType).
- Unique symbols: escaped names (`__@sym@12`) are mapped back by a checker-wide table (`ty_unique_symbol_of_name`), so a
  *string* property `__@x@3` would be taken for a symbol; `instanceof_target_ok` matches `__@hasInstance@<id>` by text.
  `interface_decl_lookup_names` makes every `symbol` property of `SymbolConstructor` a `unique symbol` (tsc does too).
  `check_subsequent_property` skips TS2717 between two unique symbols (one per declaration in tttt).
- Private names (`ty_private_name`, `PropertyExpr.private_class`): the lexical lookup scans class items by text per
  access; an access no class declares looks up the plain `"#x"` atom (a string property `"#x"` would match).
- Namespace imports of `export =` modules (`namespace_import_type`, lower.c): `ty_identifier_reference_type()` asks
  `ty_type_of_symbol()` about the alias, not its target, to tell namespace imports apart. Identifiers only
  (`import("./m")`, `typeof import` lack the interop shapes). The synthetic `default` prints as `default` in the symbols
  baseline (tsc: the module name).
- Divergent accessors: object literal accessors have no `Prop.write_node`.
- Optional parameters are `T | undefined` inside the function (`value_entry_type()`) but signatures show and relate
  `x?: T` without it.
- `binding_rest_elements`: a rest from a readonly array/tuple loses `readonly`. `binding_add_names`: literal types on
  exported bindings aren't widened like tsc (defaultNamedExportWithType1).

## Checks (`ty/check.c`, `unused.c`, `statement.c`, `class_decl.c`, `ty/helpers.c`)
- `check_access` (TS2339, HACKs): lets through generic mapped types (`Pick<T, K>`: members
  only via the constraint) and classes with bases `class_base()` can't see (`ty_has_unresolved_base()`). Bracket access
  never reports a missing property. Generic TS2536 (`ty_indexed_access_index_ok`) lacks tsc's TS4105 case (a
  private/protected property named through a type parameter).
- `ty_check_type_reference_constraints` (HACK): arguments that could contain type variables aren't checked for TS2344
  (~60 false positives otherwise, each a relation bug: indexedAccessRetainsIndexSignature,
  conditionalTypeClassMembers, homomorphicMappedTypeNesting, interfaceMergedUnconstrainedNoErrorIrrespectiveOfOrder).
- `check_assignable` (HACK): a source `any` is let through to anything, even `never`.
- `ty_check_index_constraints` on type literals only when the literal syntactically has an index signature and something
  to check (`type_literal_has_index_constraints`); alias bodies go through the alias (`type_decl_typecheck`): evaluating
  a literal early gives it an id (union order) and loses the alias name.
- Overload checks (`statements_check_overloads`, `class_check_overloads`) are syntactic per list; "same parent" is
  "the list declares more of it later".
- `initialized_in_static_blocks` (TS2729, HACK): looks for a top-level `this.x = ...` instead of flow at the end of each
  static block.
- `class_check_member_overrides()` (TS2610 & co) reads member kinds off class items (`class_instance_member()`, by
  name atom), not symbol flags; TS2612 skips everything tsc's isPropertyInitializedInConstructor would decide.
  `check_property_modifiers` (TS2687, HACK) leaves out methods and accessors.
- `check_subsequent_declaration` (TS2403) uses the identity relation, which exposes every type-core approximation
  between two spellings of a type (triage "TS2403 false positives").
- `ty_check_object_type_duplicate_members`: `MemberTables` is a fixed 128-entry array searched linearly; members past
  the 128th aren't checked (TS2300), quadratic.
- Heritage checks (`ty_check_heritage`): TS2320 for interfaces only (`inherited_props_identical`); no tsc-specific
  elaborations. Creating interface types early moved union order in 3 baselines.
- Emit helpers (`ty_check_emit_helpers`): spans computed by hand (`decorator_span()` scans back to `@`,
  `function_error_span()` cuts arrows at the first line, `yield*` cut to 5 chars, import specifier `full_span` trimmed).
  "Ambient" approximated per call site (`ClassDecl.is_ambient`, `Binding.ambient`, `BlockTypecheckCtx.ambient`; tsc has
  NodeFlags.Ambient). `for await` isn't checked (the statement doesn't know its function).
- `binary_op_typecheck` (instanceof, HACK): TS2358 skipped in JS files.
- `mark_jsdoc_links()` (unused.c HACK): `{@link X}` counts as a use by a text scan, looked up from the file scope.
  Class member reads are a checker table keyed by declaration span start (`ty_mark_member_referenced`).
- `for_in_accepts` (statement.c): type parameters and deferred types are accepted outright (tsc judges the constraint:
  TS2407 missed).
- Decorators are typechecked as ordinary expressions; placement checked (TS1206/TS1249), validity for the target isn't.
- `program_apply_comment_directives` (`@ts-expect-error`/`@ts-ignore`): suppression goes by tttt's error *phase*
  (RESOLVE_NAMES/TYPECHECK), not tsc's diagnostic kind, so parser-reported grammar errors aren't suppressed and
  checker-reported parse errors are; program diagnostics in the file never are. Line breaks `\n` only, `\s` ASCII only.
  Relies on every comment (and nothing else, e.g. JSX text) going through `hop_comment`.
- `SourceFile.unchecked` + `error_ctx_remove_unchecked`: the JS/skipLibCheck gate is a *filter* after resolve and
  check, not a skip (names still resolve; `block_typecheck` skipped only for whole files). `skipDefaultLibCheck` isn't
  read.
- `error_ctx_global_error_s`: file-less diagnostics have a null span, sorted first; `error_ctx_error_s` drops (and
  logs) errors with an invalid span. `error_cmp` orders by span only.

## JavaScript (plan 26)
- **JSDoc is consumed at parse time, not stored**: `jsdoc_before()` re-parses a host's comment into transient
  `JsDocTag`s; unused tags dropped. Only the *last* comment ending right before the host's first token is read for the
  host's own tags; JSDoc parse errors are dropped (no TS8032 & co.). Declaration tags go through
  `jsdoc_bind_comments()`, which parses every comment with `@typedef`/`@callback`/`@import` a second time.
- `jsdoc_bind_comments()`: a comment is claimed by the innermost statement list containing it, host or not (tsc binds
  only comments attached to a node: one before a block's `}` is claimed without binding, one inside an expression that
  isn't a host still binds). Claims set by a parse attempt that failed stay set. `@import` only at a list's top level.
  Dotted typedef names (`@callback NS.Nested.Inner`, tsc: namespaces) and `@typedef` without a name (named by the
  host) are skipped. Several typedefs in one comment share its templates, and only the first gets the constraints.
- `ParserShared.jsdoc_enum`: a one-slot handoff from `jsdoc_apply_statement()` to the statement list; statements
  parsed outside a list (if arms) drop theirs.
- `baseline_test_case.c` Decl printing: a declaration starting at an `@` preceded by `*` and spaces is a JSDoc tag and
  gets no leading trivia (sniffed from the text).
- `jsdoc_reference_reusable()`: an `@import` alias isn't reused by the types printer (tsc: not a visible declaration),
  sniffed through `PortItem.jsdoc`.
- `@type` on declaring assignments reaches the symbol through `SymbolDecl.jsdoc_type`, threaded by every
  assignment-declaration site in scope.c (a new site has to pass it too). Assignment chains (`a.x = b.y = v`): only the
  outermost has the JSDoc.
- `jsdoc_parameter_name()`: `argN` names are spans into static strings, not the file (`span_is_written()` tells them
  apart); position arithmetic on them reads garbage.
- `jsdoc_apply_type_signature()`: `@type {F}` on a function *moves* the signature's TypeNodes into it, only for a
  written function type or `{(...): R}` literal.
- CommonJS module indicator is syntactic (parser `saw_commonjs`, also set by failed attempts). `exports.x =` is
  declared from the file's top-level statements and the statements nested in their blocks
  (`add_nested_commonjs_exports()`: statements only, not an assignment inside an expression). `module.exports.x = v`
  after `module.exports = v0` declares a separate export next to `export=`; an `export=` of a non-object (`new EE()`)
  with other exports doesn't print tsc's merged `{ on(s): void; x: number; }` (moduleExportAssignment).
- `is_exports_alias()` looks up module-level variables only (tsc: `lookupSymbolForName(file)`), through a queue of 16.
- `require()` declarations are aliases through `PortItem`s the parser synthesizes; only flat object patterns qualify.
- Symbols baseline overrides keyed by span (`symbol_overrides`: `commonjs_symbol_overrides()`,
  `prototype_symbol_overrides()`, `this_property_symbol_overrides()`) stand in for tsc's
  getSpecialPropertyAssignmentSymbol shapes.
- `readonly_assignment()` never reports a declaring `exports.x =`.
- TS2339 in JavaScript (the gate went 2026-09-28): what's still missing from the binder shows as spurious TS2339 in
  ~15 tests: `Object.defineProperty(x, "p", {...})` declarations (jsExpandoObjectDefineProperty), expandos on class
  statics (`C.method.prop = 1`: class body members aren't binder symbols), `var X = m.X = {}` chained containers
  (typeFromPropertyAssignment18), `exports["e"] =` element access exports, `module.exports.a.cat =` expandos of a
  CommonJS export, `F.prototype = {...}` members of a class declaration's `this` in methods (jsDeclarationsFunctionLikeClasses2).
- Global expando namespaces (`global_expando_namespace()`) are declared into the shared globals while each file is
  bound, not per file and merged later like tsc's jsGlobalAugmentations: `declare_symbol()` lets any declaration merge
  with a global that only has expando declarations, and `binder_link_global_expandos()` gives variable initializers
  their members once every file is bound. Declaration order follows binding order (tsc's too, by accident).
- `jsdoc_prototype_type_param()`: a JSDoc name not in scope in a prototype method (`F.prototype.m = function`) looks at
  F's type parameters from anywhere inside the method's nearest function scope (tsc: only the method's own and its
  statement's comments).
- `set_function_class_symbol()`: a function expression assigned to `exports.F`/`ns.F` that assigns `this.x` is a class
  of the property's symbol (tsc: of the function expression's own symbol). `o.m = function () { this.x = 1 }` on a
  plain object isn't a class at all (tsc: it is).
- `F.prototype = {...}` members are the literal's type's props added to the instance type at member resolution
  (`builder_add_assignment_members()`), not merged symbols (tsc's mergeJSSymbols); only for function hosts.
- The class static side check (TS2417) doesn't see a JS class's expandos.
- `scope_add_expandos()` runs in `block_add_names()`, so nested top-level blocks write expandos into file-level symbols
  from the resolve walk, and `scope_add_this_property()` writes `Symbol.members` from it: should be a per-file list
  merged after the walks. Only expression statements and `var` initializers are scanned (tsc: every binary expression).
- Constructor-declared `this.x` is the union of the constructor's assignments (not getFlowTypeInConstructor); reads
  inside the constructor aren't auto-typed. `F.prototype.m = function` and `this.m = v` are one member (tsc: two).
- A JS constructor function is its anonymous function type plus a construct signature (`resolve_members()`), with
  `typeof F` special-cased in the printer (`ty_js_constructor_class()`), not a class static type.
- Other JS gates: `scope_add_port()` has no `export as namespace` alias in JS.
- TypeScript-only syntax errors in JS (TS8xxx) are reported by the parser at each construct (`js_syntax_error()`), and
  what tsc's walk would skip inside a reported construct is erased afterwards (`js_syntax_skip_since()`, by error
  index): every construct that tsc skips needs its call. Node array spans (type parameters, parameter modifiers) run
  first element to last, not tsc's `nodes.pos` (which can include leading trivia).
- `Statement.expr_jsdoc_type` sits next to `expr` in the union: every site that makes an expression statement by
  assigning `->expr` must clear it (parse_statement's `return`/`throw`/expression, the for loop's initializer).

## Modules and program (`module_resolution.c`, `tsconfig.c`, `program.c`, `main.c`)
- A `.js` file resolved with `allowJs` is parsed as TypeScript (plus JSDoc/CommonJS). `program_load` refuses a `.js`
  root without allowJs (TS6504) while an import is refused by `program_load_module_ref` (`MODULE_REF_IS_JS`): two paths.
  JavaScript from packages is never loaded (maxNodeModuleJsDepth taken as 0, `program.c`).
- `apply_realpath` loses the original path; no package ids (`name@version`), so no duplicate-package handling.
- `/// <reference lib>` in a program file loads the lib only in the CLI (`program_follow_lib_references()`); the
  harness's shared lib program can't be added to (10 suite tests have one). An unknown lib name is silently ignored
  (tsc: TS2726). A lib loaded that way comes after the program's files in load order (tsc puts libs first).
  `program_load_reference_path()` probes "", .ts, .tsx, .d.ts by hand; `no_resolve` skips it.
- `tsconfig.c`: config errors are one `failable` message (no TS5xxx); `include` globs always skip dot-directories and
  the package directories (tsc: only under `**`); the allowJs gate in `include` depends on option read order.
- `main.c`: strict by default only without a tsconfig; `default_libs` (ES5 + DOM + three ES2015 libs, what the bench
  needs) when neither config nor command line picks `--lib`/`--target`. `--keep-going` (typecheck after name errors,
  for the bench) isn't validated.
- `check_access` also reports TS2339 on modules: where resolution picks the wrong file it shows as spurious TS2339 next
  to TS2307.

## Baselines, harness, tools
- `symbol_chain.c` (tsc's getSymbolChain): sees only the scopes the traversal tracks (file, module, namespace:
  `BaselineTestCase.enclosing`), so a type shadowed inside a function isn't qualified and class type parameters aren't a
  table. `try_locals_with_exports()` merges a block's locals with exports declared in the same *file* by position.
  getContainersOfSymbol lacks alternative containing modules, firstVariableMatch and the export=-member-of-an-ambient-
  module case. A module's `default` export symbol is named like its first declaration (`export_name()` gives tsc's).
  `import("m")` specifiers are `module_name_for_error()`'s path. No accessibility cache.
- Types baseline node reuse (`ty_display.c` `display_node()`/`display_annotated()`):
  - `name_accessible()` resolves from `enclosing` (file/namespace scopes only); type parameters and parameters always
    accessible.
  - Instantiated signatures reuse the declaration when the mapper left the type alone and it doesn't mention the
    signature's type parameters (`mentions()` conservative); `TyDisplay.mapped_by` only for type predicates.
  - `T_1` renaming (`TyParamNames`): in-scope type parameters come from the traversal
    (`baseline_test_case_enter_generic()`, tsc's resolveName exceptions restated by hand); synthetic `T`s of tsc's node
    builder aren't made.
  - `display_node_identifier()`: inaccessible names are renamed with `out_symbol_name()` and the underline patched by
    byte arithmetic assuming an ASCII module name.
  - `display_node_computed_name()` prints the value of unnameable computed names (no late-bound names).
  - Syntactic typing (`single_return_expr()`, `TyDisplay.syntactic_function`): only returned/parameter-initializer
    function expressions and `as T` returns; `FunctionImpl.syntactically_contextual` counts calls and annotated
    initializers, not JSX. Accessor properties only for object literals. Index signatures, tuple names, type parameter
    defaults always synthetic. Enum member annotations print `E` where tsc reuses `E.x`.
  - The `any` underline rule reads whether *tsc's* `.errors.txt` exists (`baseline_test_result_run`);
    `BaselineTestCase.any_keeps_underline` is a one-shot flag that leaks to the next line if no types line follows.
  - `TyDisplay.widen_literals` (HACK) prints literals inside expression-made object types as their primitive.
  - `ty_elides_default_type_args` (iterate.c HACK) always prints `Iterable<T>` for the 3-parameter iteration interfaces.
  - `out_unique_symbol_name` (ty_display.c HACK): members of an interface reached through the global variable named
    like it without `Constructor`.
  - `append_qualified_name` always fully qualifies in error messages.
- Member lines (`member_text()`/`prop_text()`): container from the traversal (`member_container`, `class_symbol`) or the
  Prop's `parent`; a namespace object's member is looked up as the namespace export by accessed name.
- `collect_prop_symbol()`: a non-object union member (`X | undefined`) is skipped for property accesses and only makes
  binding patterns partial (`strict_union`); strict for accesses lost 40 tests by hiding narrowing gaps.
- `index_symbol_compile_test_info()`: `__index` symbols only for declared types and written literals, never for a lib's
  index signature (unexplained, useRegexpGroups).
- `symbol_declaration_spans()`: function declarations first per block (`SymbolDecl.block`), maybe redundant now that
  `block_add_names()` binds them first; lib file order isn't tsc's.
- `render_types_or_symbols_file` drops one trailing line break of every unit but the last; a last unit followed by
  `// @link` lines comes out one line off.
- `display_type` depth guard (`DISPLAY_TYPE_MAX_DEPTH`, display.c): probably dead now that nodes can't be cyclic;
  check and delete.
- `apply_setting` (baseline_test_case.c): `@baselineFile`, `@fullEmitPaths` and bad option values ignored; option
  diagnostics (`program_validate_options`) skipped when a `tsconfig.json` unit exists.
- `mount_test_lib_folder`: tsc's `tests/lib` is *copied* into each test's mock filesystem and reparsed per test; a
  read-only mount + cached parse would be the real thing.
- `baseline_test_cache_get_program_with_libs`: one cached lib `Program` per distinct lib list, never evicted; unknown
  lib names logged and skipped.
- `baseline_test_result_run`: with `@noTypesAndSymbols` the symbols/types components count as passed (~900 free
  passes).
- `TTTT_IGNORE_UNDERLINES=1` (`compare_outputs`): a measurement mode, not for saving results.
- `engine.c` (reads `test/baseline_results.ts` for the harness only): ignores newer constructs, no let/const/var
  distinction, drops property assignment on arrays, `value_loose_eq` stringifies.
- `failstats.py: line_ratio` is multiset line overlap, not a diff; `syntax_gaps` re-parses rendered errors (approximate
  for multi-file tests). `bench/make_corpus.py` replicates passing tests in namespaces (flatters tttt).
- Commits f3b836b..f899e4e don't compile (fixed by 7f2a7fa): skip them when bisecting.
- `string_to_double()` (string_helpers.c) is only used by value.c and accumulates decimals digit by digit (not
  correctly rounded for long mantissas); the parser uses strtod on the separator-free digits.
