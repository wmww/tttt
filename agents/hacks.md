# Hacks
Suboptimal-but-working code, by subsystem. Each entry: where (function names), what is approximated, the correct rule,
and the oracle that would show it wrong. How things work belongs in documentation.md; missing diagnostics and features in
`bugs/*.md`, triage.md or a plan. `grep -rn HACK src` finds the code-side markers.

## Global state, memory, parallelism
- **The shared graph's words (notes/shared_graph.md) keep a state byte each for the default path** (`FunctionReturn.state`,
  `SymbolLinks.declared_state`, `TyObjectState.base_state`, Prop.type_flight, Signature.res_flight, ret_flight, predicate_flight,
  TyObjectState.members_flight, MappedInfo.template_flight, TyParamState's three...): without --shared-graph in flight is that byte, as S0
  designed (overlay.h); with it the byte is never touched. Correct: the Overlay for both (the claims are a stack with a
  filter since the flag's K1 overhead cuts (measurements.md "Shared graph K1 overhead"), a push and a load, cheaper than the map probe that was the measured reason). And function_return_type() counts an argument's return inferred under an outer inference
  as tainted wholesale (keep_outer), where only the reads of the outer inference need to be (the design log's "P2 progress", which notes/shared_graph.md points to). Measured at D: the bytes sit in padding holes; two byte stores per computation, under
  0.1% of instructions.
- **A graph with one checker publishes nothing until a second one adopts** (InternTable.others null while alone;
  graph.c graph_adopt()'s publish_identities() catches up): correct only because a lone checker's second one comes when
  the first isn't running (start, its own file boundary, the recheck after the join). A path adopting a checker while
  the first runs alone would race the catch-up (the design log's "PERF" note, which notes/shared_graph.md points to).
- `ast_free()` / `ast_delete()` (ast_alloc.c): the AST arenas trust the size they're given, and a too-big one hands
  the next allocation of that size someone else's bytes. `ast_delete()` takes `sizeof(*p)`, so a box allocated short
  (`property_dot_new()`, `require_ports` arrays) needs its real size at every free; nothing checks it. Correct: a
  size-checked debug build (a header word per block). Oracle: the suite under a build that poisons and verifies.
  Also: dropped arenas' addresses are never reused (virtual space grows by every AST a process ever parsed: the test
  harness, `-b`), `ast_owns()` assumes 47-bit user addresses, and free blocks left when a file ends are lost (3%).
- `source_text_map()` (source_file.c): source text is a MAP_PRIVATE mapping of the file wrapped in a hand-built long
  `cstr` (STC's private layout: `cstr_l_set_cap`), and `source_texts_release()` drops its pages at every recycle. A
  file changed by another process mid-run is read changed, a truncated one reads as ending there (`text_sigbus()` maps
  zeros over the faulting page and warns once); only tttt's own declaration writes are guarded (`source_text_detach()`, a linear scan of every mapping per
  written file). Anything that drops or mutates `SourceFile.contents` must check `text_mapping` first. Correct: a
  `csview` text field. Oracle: the corpora and sweeps (the harness's mock fs never maps).
- `check_share()` / `checker_memory()` (program.c, ty/checker.c): checker recycling assumes no diagnostic depends on a
  checker's history; false in a few places (bugs/sourcesweep_checker.md "History dependences recycling exposes"; the
  undefinedProperties member order was one until 2026-10-21). `checker_memory()` leaves out LazyMemo pages (real memory the budget doesn't
  see), and the 256 MB budget (96 MB per-checker floor, `CHECKER_MEMORY_FLOOR`) is a wall-time compromise. Correct: no history dependence. Oracle:
  `scripts/kdiff.py -k 1 -m 0`.
- `current_checker` / `current_lazy_slots` / `compiler_options()` (thread locals, `checker_make_current`): how the AST
  side (`*_typecheck`, `*_evaluate_type`, `ty_of_node`) finds its checker; left set after `program_typecheck` for the
  test traversal. Correct: a passed check context (checker + errs + options). Many parsed options are inert (`grep -o
  'compiler_options()->[a-z_]*' src`).
- `Checker.return_context` (`function_impl_enter_return_context`): the contextual type of `return`s is saved checker
  state where tsc walks up to the function (getContextualReturnType). Wrong for a `return` evaluated outside its body's
  check (shows as types-baseline/TS2322 differences on returns).
- **Lazily made instance props** (`members_prop()`, `members_prop_make()`, `MembersLazy`, ty.h/members.c;
  type_graph_design.md step 1): an instantiation's props array is preceded by a header (source, mapper) found by
  pointer arithmetic, and an unmade slot is its source prop's pointer with the low bit set. members_prop_make() takes
  its arena from `current_checker`, not the caller's `me` (keeping `me` alive cost every ty_get_prop() a frame).
  Every read of a slot that can still be tagged is atomic (relaxed where only the name is read, acquire otherwise).
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

- Deferred bodies (how they work and what a drop puts back: documentation.md "Deferred bodies"). The
  approximations:
  - Body walk outputs are read through materializing accessors (`function_impl_walked()`, trailing `_` on the raw
    fields); left raw: an untyped uninitialized field's `ts_assigned_` (`class_untyped_field_ts_assigned()`, the parser
    keeps that constructor eager), `outer_this_function`, `uses_arguments` (JS, eager). A new raw reader of a body's
    output is a silent order dependence. Oracle: the suite against `--no-defer-bodies`, kdiff/checkerdiff.
  - What isn't deferred is decided by syntax (`skip_body()`, `undefer_body()`): each case is a names-phase reader of the
    body (parameters with expressions, a `(` after the parameter list, IIFEs, constructors of classes with instance
    fields without define semantics or untyped uninitialized ones, `import`/`require` in a body, regex-or-division
    ambiguity, non-element `<` in .tsx, a directive prologue, `typeof` in a written return type, `with`/ambient). Correct: those readers lazy, or
    answered from the parse. Cost: vscode undefers 5.3k of 96k bodies (`TTTT_DEFER_STATS=1`).
  - A new write outside the bodies from their walk must be undone at a drop (`deferred_bodies_undo()`) or a new way of
    keeping a materialization alive must allocate it lasting (`AstArena.lasting`; owner materializations carried cost
    vscode K1 +155 MB). Oracle: `TTTT_AST_POISON=1 --checker-memory 0` on the corpora, `kdiff.py -k 1 -m 0`.
  - A body materialized alone (`DeferredBodies.singly`, documentation.md) assumes its walk writes outside its body only
    its function's walk fields, its scope's locals, its four outer flow nodes' marks (set to the whole walk's,
    `walked_flow_flags`) and unused registrations (put in order once all are made); `singly` is decided by the undo
    log alone. A new cross-body write that isn't in the undo log (or isn't per body) silently makes a single body's
    walk differ from the whole file's. Oracle: `--checker-memory 0` (every file, since 2026-10-18) with
    `TTTT_AST_POISON=1` on the corpora, `scripts/kdiff.py -k 1 -m 0`, the suite at `--checker-memory 0` (136 single
    bodies).
  - AST strings are in the arena only where the parser seals them (`ast_cstr_seal()`: literals, template texts, object
    keys, class member names incl. computed keys' text, binding keys); a long string another AST site makes (an enum
    member's `canonical_name`, JSDoc names) is still malloc'd, and leaks when a materialization holding it is dropped (cleat's cstrs aren't counted by
    the harness's leak check; valgrind on vscode K1 found none left, 2026-10-18). An AST string taken out of the AST
    must go through `ast_cstr_take()`, dropped through `ast_cstr_drop()`: a plain `cstr_drop()` of a sealed one aborts.
  - Signature/body name collisions and body syntax errors rerun the whole program without deferral
    (`program_defer_failed()`). Correct for collisions: walk the signature again after the body's names. Oracle:
    `defer: restarts` on the sweeps (0 on the corpora, 0 on the source sweep since 2026-10-18; `defer: collided` names
    the body).

- **The CLI's address-space teardown runs in a detached reaper** (main.c exit_detached(): a CLONE_VM child that drops
  its descriptors, waits on a pidfd for the process to be gone and exits holding the last mm reference). The caller
  gets the status and the pipes' end before the kernel frees the pages (7-66 ms: nest to vscode), but that sys time
  is nobody's: `/usr/bin/time` and wait4's rusage undercount sys by it. A caller that is a child subreaper
  (PR_SET_CHILD_SUBREAPER: some init shims and harnesses) gets the reaper as an extra child to reap, its exit
  status 0. `TTTT_SYNC_EXIT=1` (or TTTT_TEARDOWN, valgrind, sanitizers, debug builds) exits in the process: set it
  when comparing sys or kernel cycles across builds. bench.sh (a second run per sample for CPU), perfshape.sh and
  perfsweep.py (synced runs, wall to the last output byte of `--time`) already do. The tail was measured on a box
  with THP `madvise` (4 KB pages: 7-66 ms); the laptop, THP `always`, should measure it (fewer pages to free: it may
  be smaller there).
- `graph_fresh_worker()` (program.c), `--shared-graph`'s shadowed-worker hand-over: the retired checker stays allocated
  until the program ends or a graph-wide recycle (its arena holds published types); 0-12 a 20-run table.
- `program_set_checker_memory_shared()` (program.c, program.h): unused by the CLI since the memory default changed
  (2026-10-22: no growing start, `default_checker_memory()` in main.c). Delete it and its `checker_memory_shared`
  field after the laptop bench confirms the defaults.

## Parser
- **A program with a syntax error is loaded twice when it has deferred bodies** (`program_deferred_bodies_parse()`,
  `check_files()`, plan 78): a gated program (syntactic diagnostics only) must report the syntax errors of every function
  body, but the bodies are only listed (`SourceFile.deferred_bodies`) by the resolve walk, which a gated program parses
  before or never runs; with skipped bodies and no list it returns false and `check_files()` runs the whole program again
  without deferral (`--listFilesOnly` doesn't defer at all). tsc parses each file once and reports from that parse.
  Correct: the parser records each file's skipped bodies itself (a list it owns, in source order), and a gated program
  parses just those for their diagnostics, no second load. Rare: only programs with syntax errors (clidiff over every
  project: none of the 3831 changed). Oracle: the CLI on a.ts `function f() { let x = ; }` plus b.ts `const y = ;`
  (both TS1109, like tsc), timed under `TTTT_DEFER_STATS=1` (the restarts count).
- `unique symbol` placement (TS1330-TS1335): checked while parsing through `ParserShared.unique_allowed_at`
  (`parse_type_inner`, `expect_annotation_type`, `parse_jsdoc_type_expression`), not on the tree
  (checkGrammarTypeOperatorNode). A JSDoc `@type`'s host is checked where the tag is applied (variables:
  `unique_symbol_variable_check()`, casts in `jsdoc_apply_parenthesized()`, getters in `jsdoc_apply_function()`,
  class fields as written ones); where tsgo's reparser makes no annotation of an `@type` (an expando, an object literal
  property, `this.x =`, `export default`) neither checks it. A new host that takes an `@type` as its annotation needs
  the check added there. Oracle: ttttJsdocUniqueSymbolPlaces/Hosts, ttttHackJsdocUniqueSymbolLet, mutate.py on JS tests.
- Strings are UTF-8: a lone surrogate escape is kept as WTF-8 bytes (`append_code_point()`), which strict decoders
  (`cutf8_encode`) drop; `regex_check_literal()` ports tsc's UTF-16 scanner on a UTF-16 copy of the literal. A NUL byte is a
  character for tokens and lists (`at_eof()`) but some lookaheads still stop at it.
- Escaped identifiers (`name_cooked()`, span.c `name_cooked_slow`): a global mutex-guarded registry, each lookup a linear
  scan of the file's cooked names (quadratic for thousands). Comparisons against literals (`span_equals(name,
  "constructor")`) and `EnumCase.name` of string members stay raw (bugs/binder_modules.md "Escaped identifiers");
  `hop_escaped_keyword()` reports TS1260 even speculatively. Correct: cook once at scan time into the node.
- Two notions of "reserved": `parse_identifier()` refuses only `is_reserved_identifier()` (it also parses identifier
  names and keyword types), `parse_binding_identifier()` and enum names all of `is_reserved_word()`. Correct: per caller,
  tsc's parseIdentifier / parseIdentifierName / parseBindingIdentifier. Oracle: syntaxstats.py. Probed 2026-10-23, matches tsgo (`let x: yield;` in a module).
- Conflict markers (TS1185): `Decl()` positions don't skip back over one; none recognised after U+2028/U+2029.
- No missing-identifier node: `written_name()` prints `(Missing)` for empty zero-width names, bindings use `BINDING_NOOP`
  where tsc declares `(Missing)`; `parse_throw()`'s missing expression is a zero-width implicit any (TS1142 finds the
  keyword by `strlen("throw")`); `return;` is an `undefined` literal (`statement_is_bare_return()`, wants a `has_value`). Probed 2026-10-23, matches tsgo (`throw` with its `;` on the next line).
- Missing nodes and trivia (`token_full_start()`, `unhop_trivia()`): `ParserShared.last_trivia_start/end` remember only
  the last whitespace hop; `expect_expr()` moves back so the parent ends at the previous token and continues a missing
  operand with binary/assignment operators and `.` only (not `[`/`(`, nor `<` outside JSX); the symbols baseline skips ASCII whitespace forward for
  `Symbol(unknown)` (a comment breaks it). Probed 2026-10-23, matches tsgo (`a + /*c*/ [` at the end of the file).
- Spans rebuilt by scanning text: full starts by skipping spaces backwards (TS1433, TS1385-TS1388, comments not
  skipped), `question_dot_after()`, an object literal missing `}` (`parse_object`), a bodyless accessor's TS1005,
  `json_check_value()`'s TS1328 (tsc: after a comment). A `span_from` on a parent's anchor inside a child parser before
  `parse_success` truncates. Correct: real token full starts. Oracle: errtextdiff.py positions.
- Error recovery (documentation.md "Error recovery"): `list_abort_or_skip()` skips a token that starts an
  element of the list itself; the fail cache ignores `parsing_context`; `parse_binding_content()` stops at the first bad
  element; a committed arrow whose parameters fail falls back to a parenthesized expression; `tok_scan_end()` is a second
  token notion next to `hop_token()`; `parse_jsx_attributes` approximates isInSomeParsingContext. Oracle:
  syntaxstats.py. Probed 2026-10-23, matches tsgo (`function f(a, , b) {}`).
- Statements: `statement_takes_semicolon()` lists by tag who calls parseSemicolon(); statement spans include the `;`,
  port items' and class members' don't (`span_with_semicolon()`). `IfStatement` flattens `else if` (no Statement for
  the nested if: `if_else_resolve_names()` reports its TS7027 itself, its span found by skipping `else` past the
  previous branch; tested by ttttHackElseIfUnreachable); `statement_start_needs_check()` gates isStartOfStatement by first
  word (a new rejecting case must be added).
- Private names outside classes have no flag: `object_item_is_private_name`, `interface_member_is_private_name`,
  `enum_decl_typecheck` check the first character (such an enum member is declared, prints `__missing`). Probed 2026-10-23, matches tsgo (`enum E { #a }`).
- Class accessors pair by name+staticness in `class_decl_lookup_names_in_owner()` (`class_namesakes()`; raw
  `accessor_first`/`accessor_getter` pointers into the items vec); readonly means "no setter". `{ a = 1 }` keeps `ObjectExprItem.shorthand_default`.
- Parsed and dropped (no bug entry covers them): computed non-literal enum keys
  (`EnumCase.computed_name`, member prints `(typeof E)["[e]"]`, tsc `E`); non-string import specifiers; a `ModuleRef`
  from an abandoned attempt stays listed (`add_module_ref`); `global x` is `NamespaceDecl.is_shorthand` typed any;
  `ModifiedNode.name` stands in for getErrorSpanForNode (TS1248). Probed 2026-10-23, matches tsgo (`enum E { [e] = 1 }` with `e: string`: errors and types).
- Heritage clauses (`parse_heritage_clauses()`, `parse_heritage_element()`): elements parsed as expressions, then again
  as types if entity names; TS1172-TS1176/TS1097 from the parser; unchecked parts in `unchecked_*`/`invalid_*` fields
  resolved silently for baselines; `BaselineTestCase.hidden_type_spans` hides what isPartOfTypeNode drops. Correct: one
  ExpressionWithTypeArguments node. Probed 2026-10-23, matches tsgo (`class C extends A, B {}`).
- `jsdoc_type_outside_jsdoc()`: `?T`/`T?`/`!T`/`T!` are a `TYPE_JSDOC` wrapper; `*` reports TS8020 without a node.
- Top-level await (`parse_file()`): the whole file is reparsed with the statements that saw an `await` identifier as
  await contexts (`ParserShared.await_statements`).
- `undefined` is a literal; `expr_lookup_names` rewrites it into an identifier when a non-global declaration is in
  scope (`Expr.undefined_assigned` for targets). Correct: an identifier resolved to undefinedSymbol.

## Binder and name resolution (`scope.c`)
- `binder_redirect` / `Binder.overlays`: a program adding to a shared-lib symbol gets a clone (`clone_parent_symbol`),
  the checker maps through `canonical()` (ty/lower.c): anything new keyed by `Symbol*` must too. Fallout: an augmented
  `Console` shows both `log` signatures (wants getIntersectedSignatures); `maps_merged_type_param()` (ty_display.c)
  misses an interface merged with a lib one (promisePermutations x3). Probed 2026-10-23, matches tsgo (an augmented global `Console.log` overload: `console.log`'s type and a TS2322 header, in a module and a script).
- `make_gather_target`: namespace imports, `import x = require()` and `export as namespace` alias a namespace symbol of
  their own (`Symbol.gather_module`) so the type prints `typeof ns`. Node16 `import * as ns` of CommonJS copies the
  module's exports at link time (right only because nothing merges into module exports after `binder_link()`), plus
  `Symbol.synthetic_default`, whose `prop->decl->symbol` is the alias (symbols baseline prints `d.default`).
  `namespace_import_is_clone()` (symbol_chain.c): `ns`'s type is the module's, not tsc's esModuleInterop clone, so
  symbol chains guess when tsc's clone wouldn't name the module. Probed 2026-10-23, matches tsgo (`import * as ns` of an `export =` .d.ts under esModuleInterop, returned from exported functions: types and .d.ts).
- `resolve_uncached()` (module_resolution.c) HACK: a package name with export maps resolves TypeScript extensions in a
  first pass, reproducing tsc's selfNameModuleAugmentation behaviour (microsoft/TypeScript#50762) by other means.
- `merge_symbol()` returns early for alias sources and other binders' targets: module augmentations
  (`merge_augmentations`, `retarget_decl`) never merge into shared-lib symbols, an alias in an augmentation meeting an
  existing name isn't merged, `add_pattern_declarations` copies only flags and declarations of an export both declare
  (not a nested namespace's exports); names added through an `export *` module reach only that module. Correct: a
  `merge_symbol` that can clone its target. Tested by ttttHackAugmentationAliasConflict (known failing: `export { x as y }` in an augmentation misses TS2323/TS2484).
  `aug_merge_symbol()` decides tsc's resolveAlias-time getMergedSymbol (resolveIndirectionAlias, for an alias whose
  target is an alias) at merge time, from PortItem.source_symbol: tsc caches an alias's target when first resolved, so
  an alias resolved before an earlier augmentation merged would see the declared symbol there; tttt doesn't model that
  timing (appflare, ttttAugmentationTwoPathsIndirect).
- `export =`: named imports see only the namespace side; `namespace_import_type` asks `ty_type_of_symbol()` about the
  alias (identifiers only), so `import("./a")` of an `export =` module prints `typeof ns` where tsc spells out
  `{ ...; default: ... }`, and the synthetic `default` prints as `default` in symbols (tsc: the module name). Probed 2026-10-23, matches tsgo (`import("./a")` and `typeof import("./a")` of an `export = namespace` module: types, TS2322 headers).
- `export = a.b` / `export default a.b` is an alias (`resolve_entity_alias()`): a class static merged with a namespace
  interface is an alias whose `value_target` is the static, and `module_export_equals()` may return the alias itself.
  tsc makes one symbol with both declarations (exportEqualsProperty2 symbols).
- `arguments` has no symbol (`IdentifierExpr.is_arguments`, `IArguments` by name).
- Exported and local declarations are two unlinked tables (tsc: `exportSymbol`; `SymbolRare.export_symbol` only for
  `export default` locals, `scope_declare_default_local()`): TS2440 special-cased in `binder_link`, TS2395 a separate
  pass; TS2441, TS1203, TS2308, TS2661, TS2669 come from binder/link (tsc: checker); TS2308 re-walks `export *` apart
  from `module_resolved_exports()`.
- `block_is_instantiated` (statement.c): a namespace reached as an `export { a }` target (`statement_instance_state_of`)
  is judged with `outer` and no `BlockChain` of its own enclosing namespaces.
- Class/interface members aren't symbols (`Prop` is the identity): `bind_member_symbols` + `class_decl_lookup_names`
  apply binder rules at lookup (written names only); `ty_class_member_symbols()` (check.c) is the checker's declareSymbol
  + lateBindMember for a class (computed names by the key's type), `object_bind_members` replays the binder for object
  literals, where computed keys still pair by source text; `ty_member_name()` evaluates every computed key. See
  "member symbols" under Expressions.
- Merged interface/class type parameters share one symbol per name (`scope_new_generic_merged`, serial walk) except
  against the shared lib; `type_parameter_is_unused` compensates.
- TS2301/TS2844: fields are walked before the constructor's scope exists, so `constructor_locals()` (class_decl.c)
  lists the names the constructor will declare (`function_collect_local_value_names()`: its parameters and top-level
  declarations by hand, nested `var`s through statement_hoist_names()'s own walk, `hoist_walk()`). Correct: tsc's
  locals of the constructor, which needs its scope before the fields' walk.
- TS1361/TS1362 valid sites (`FlowBuilder.type_only_alias_ok`, `in_type_query`) set by enclosing constructs, not
  derived; `export =`/`export default` of a type-only name gets `symbol` without `target`; TS1376/TS1377's related span
  (`span_from_type_keyword()`) scans back for `type` (PortItem has no span of the whole specifier).
- Null-ErrorCtx resolution of the body of `with` because the resolve walk reports checker errors
  (`statement_resolve_names_reachable`, binder errors there lost; `BaselineTestCase.in_with`). Correct: the resolve walk stops reporting checker errors.
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
  tsc follows resolved bases).

- **Import attribute errors and uses** (`attribute_value_error` and the `clause_unchecked` setup in `port_statement.c`):
  with a non-string attribute value (TS2858) tsc checks neither the declaration's module nor its bindings, and reports
  what resolving them finds only when something uses a binding. tttt resolves aliases eagerly and has no use tracking
  outside noUnusedLocals, so a binding counts as used when its name appears as a word anywhere else in the file
  (`import_binding_mentioned`: a comment or string mentioning it too). A re-export or side-effect import has none.
  The correct mechanism is tsc's: lazy alias resolution on use. A reference that resolves to the binding
  (resolveName -> resolveAlias -> getTargetOfImportSpecifier/Clause) reports TS2307/TS2305 at the import the first
  time. Closing this needs either use tracking in all modes (the resolve walk marking each alias it hits, like
  `referenced` under noUnusedLocals) or deferring these aliases' error reporting to their first use in the checker.
- **interface_heritage_typecheck's `aug_one_level`** (interface_decl.c, scope.c aug_merge_fixup): when module
  augmentations clone one symbol along a chain longer than one (tsc's mergeSymbol of a re-export of an already augmented
  interface: @stencil/vitest's `interface Assertion extends CustomMatchers` through vitest over @vitest/expect's), tttt
  makes the symbol the end of the chain for everything, but checks its declarations' heritage (TS2320) as the first
  clone, which tsc's getSymbolOfDeclaration (getMergedSymbol once) gives them. tsc's lookups apply getMergedSymbol a
  path-dependent number of times (a declaration's symbol once, a type reference twice, a table entry once more), so
  names and the symbols baseline still differ there (npm_My-first-stencil-project-001_TS2320's SYM/TYP). The faithful
  model (a symbol the binder made is its first clone; type references and lookups go one level further) was tried on
  2026-10-24 and regressed ttttAugmentationCloneChain's symbols and types (which symbol the harness names, local twins
  of module exports): it needs the harness's twin logic and the display's container search reworked with it.

- `contextual_signature()` (ty/expr.c) of an intersection with type variables builds the intersection of its members'
  apparent types and takes that's signatures, where tsc's getSignaturesOfType of the intersection concatenates each
  member's. Only a conditional member whose apparent type is a union without common call signatures is dropped
  first (convex's withOptimisticUpdate, ttttContextualIntersectionWithConditional); an indexed access's apparent union
  still distributes (intersectionOfTypeVariableHasApparentSignatures passes that way). Correct: per-member signatures.

## Control flow and narrowing (`flow.c`, `ty/flow.c`)
- `without_unit()` (ty/flow.c): the `!==`-a-unit filter of a union is memoized per (union, unit), KEY_FLOW_NOT_UNIT.
  This assumes is_not_unit_comparable_to's answers never change, but they come from comparability relations and base
  constraints, which tsc recomputes each time (a relation answered "maybe" inside a cycle could differ later). It is
  a perf memo from the single-threaded memory-system work. Oracle: `scripts/oracles.py` byte-identical; drop the memo if a divergence traces
  back to it.
- Evolving arrays: switch clauses narrow the final array, not the `TY_EVOLVING_ARRAY`. Oracle: suite evolving-array
  tests, flagflip noImplicitAny cells. Probed 2026-10-23, matches tsgo (`let a = []` pushed to in switch clauses, with a fallthrough, noImplicitAny).
- `expr_is_narrowable_reference()`: `a = b` counts whatever its left side; tsc wants a LeftHandSideExpression (differs
  only after parser recovery).
- A rest pattern's initializer is checked once
  more for the root pattern only (`ty_rest_binding_initializer_type`, tsc's CheckMode.RestBindingElement does every
  level); call.c's return mapper ignores a pattern with a rest element. Oracle: suite destructuring tests' types. Probed 2026-10-23, matches tsgo (rest patterns nested in array and object patterns, from literals and from a generic call).

- **Unmatched `@param` types and `arguments`** (`function_text_mentions_arguments`, function_impl.c): tsgo resolves the
  type of the last `@param` that names no parameter only when the body references `arguments`, for TS8029. The
  parameters are looked up before the body walk sets `uses_arguments`, so the candidate's lookup errors are kept when
  the function's text has the word `arguments` anywhere (a string, a comment, a nested function's own `arguments` too).
  The correct mechanism is tsc's containsArgumentsReference, which is the body walk's answer (`uses_arguments`, set in
  expr.c: an `arguments` identifier not inside a nested non-arrow function). Closing this needs the candidate's
  lookup errors to be kept until the body walk has run, then reported only if `uses_arguments`. That means a
  buffered ErrorCtx that survives a parallel (sharded) walk, which today sends any non-shard context to `discarded`.

## Declarations and bindings (AST-side walk)
- `ty_binding_assigned_type` (not memoised): tsc unions the undefined-stripped element with the default (subtype
  reduction), then widens; tttt decides with the fresh default, but when that drops the default it reduces again
  against the widened default (`ty_union_subtype_reduced(me, types, 2)`). Oracle: suite `.types` of pattern defaults. Probed 2026-10-23, matches tsgo (object and array pattern defaults against literal-union declared types).
- Array pattern rest (`binding_element_of`): the parent type is widened, so "any parent is any" peeks at the root
  initializer for a nullable. Correct: keep the unwidened parent (tsc's getTypeForBindingElementParent).
- `class_decl_lookup_names_in_owner()` drops a constructor's return annotation after TS1093; tsc keeps and ignores it. Probed 2026-10-23, matches tsgo (`constructor(): number {}`).
- `field_type_from_assignments()`: the first real `this.x =` stands in for tsc's synthetic flow reference of the
  property (getFlowTypeInConstructor). Oracle: JS suite tests. Probed 2026-10-23, matches tsgo (JS constructor `this.x = null; if (c) this.x = 1`, undefined then a string, a number then a string).
- `ty_merged_value_type`: a second `Ty` beside the unmerged one (`ty_static_type_of`, what static `this` sees). Correct:
  one type per merged symbol (tsc's getTypeOfFuncClassEnumModule).
- `ty_sig_param_declared_type()` / `ty_sig_param_symbol_type()` recover a parameter's declared type by taking undefined
  back out of `SigParam.type` when `undefined_added` (tsc keeps the symbol's type and adds at getTypeOfParameter): for
  an instantiation whose declared type became undefined (`p: T = x` with T = undefined) that prints `never`. Probed 2026-10-23, matches tsgo (`p: undefined = undefined`, `p: T = undefined as T` with `T extends undefined`).
- `ty_report_pattern_iterable()`: TS2318 'Iterable' gated on an array/object literal initializer because tttt computes
  the pattern's contextual type eagerly (tsc's createIterableType only when asked).

## Calls and inference (`ty/call.c`, `ty/infer.c`)
- `Checker.outer_inference` is dynamic state where tsc finds a node's inference context among its ancestors
  (getInferenceContext): every evaluation that isn't part of the argument must clear it (`function_return_type()`
  unless the function is the argument, variable initializers, `ty_infer_type_predicate()`), as must any new cached
  evaluation reachable from inference. Symptom: order-dependent `any` (trpc), found by `scripts/kdiff.py`.
- `visited_add()` keys pairs by mode like tsgo's InferenceKey, but the alias-to-alias branch of `infer_from()` recurses
  unguarded (tsgo's invokeOnce guards it); guarding aliases changes karakeep's alias variances (perf_leads.md).
- TS7023/TS7024 own-context circularity (`ty_function_context_is_self`) and TS7011/TS7018 reportability
  (`ty_function_decide_widening_reportable()`) are decided where the return type is inferred, through the anchor or
  else the first contextual signature (`KEY_CONTEXT_RETURN`), not the check of the moment's.
- Skip-sensitive rounds (`resolve_with_signatures`): tsc's SkipContextSensitive round checks every argument, sensitive
  functions as anyFunctionType; tttt leaves them out of `check_args_until_unfit` (`skipped_function_unfit()`) and
  relates literal ones afterwards (`first_unfit_skipped_literal()`), so a literal is related after the arguments behind
  it (observable in which argument TS2345 lands on). Generators: `return_only_type()`, `first_unfit_argument`. Probed 2026-10-23, matches tsgo (`f(1, x => {}, "s")` with the callback first, middle or last, and a non-sensitive `() => true`).
- Higher-order (`inference_propagate_generic`, `Arg.generic_function`/`generic_call`, `waits_for_sensitive`,
  `ty_generic_function_in_context`): a failed call is re-inferred without them, then arguments are re-checked against
  the failed candidate only to restore memos (`add_implementation_success` likewise). tsc resolves once.
- Return mapper (`infer_type_arguments`, `outer_return_mapper()`): tsgo's returnMapper, except a call whose return
  source is the main inference's reuses the main inference. A pattern's return mapper drops inferred `any` (tsc's
  nonInferrableAnyType), rest patterns give none; `ty_pattern_unclone` copies the pattern type per call; nested literals
  instantiate context only in `check_for_mutable_location()`. Probed 2026-10-23, matches tsgo (`box(id("a"))` against `Box<"a">`, inside arrays, with object and tuple literals).
- TS7056 from a display depth limit (`ty_display`, 8 * 16 levels with a tracker) where tsc only has the 1M-character
  limit (recursive reverse mapped types); a finite type nested deeper gets a spurious one. Oracle: dtsdiff.
- Reverse mapped arrays/tuples are eager but cut at depth 4 (`Checker.reverse_mapped_depth`; tsc's
  reverseMappedSourceStack/isDeeplyNestedType). Generic reverse mapped types instantiate via `{target, mapper}`, not
  instantiateReverseMappedType. Inference priorities lack SpeculativeTuple (AlwaysStrict is `Inference.always_strict`,
  which also stands in for NoConstraints).
- `context_is_tuple_like()` misses call spreads (tsc's intra-expression sites through spreads). Probed 2026-10-23, matches tsgo (`f(...[1, "a"] as const)` against a tuple rest, a spread tuple holding a callback, an array spread).
- Array literal candidates (`Inference.array_literals`, filled by `add_nested_array_literals` in call.c): tsc marks
  each array literal's type ObjectFlags.ArrayLiteral, so literal-ness belongs to the candidate an inference site
  pushes (`unionObjectAndArrayLiteralCandidates`, `const` type parameters). tttt's array and tuple types are interned
  and can't carry the flag, so the inference records the argument's nested array literal types and treats any
  candidate equal to one as a literal. Since 13-P4a only the literals under parts of the parameter that mention a
  type variable are recorded (`c: [1]` against `c: unknown` no longer makes `xs: number[]` a literal), but two sites
  reaching type variables with identical types (a literal `[1]` and a declared `number[]`) still can't be told apart.
  Correct: a literal bit per candidate, set where the walk infers from a literal's type.
- `ty_union_signatures` (relate.c) isn't cached. `resolve_call` falls back per member for `super`, `new` of a callable
  union and JSX when a member has no signatures; tsc uses the union signatures (`super`) or its own JSX rule.
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
  context (`KEY_LITERAL_CONTEXT`, last wins; tsc's pushed contextual type, a push/pop stack is the contextual-check state work);
  `ThisType<T>` found by name, instantiated with `ty_instantiate_fixing()`. Probed 2026-10-23, matches tsgo (two overloads with different `ThisType<>`s on one object literal method).

## Expressions and contextual typing (`ty/expr.c`, `ty/access.c`)
- **ty_settle_function_context() checks a function expression's context root, not the path to it** (ty/expr.c, the
  root recorded by the names walk, FunctionImpl.context_root): tsc's getContextualSignature walks up and resolves only
  what the path needs; tttt checks the whole root expression (one statement's expression, one initializer, or the
  enclosing function's return inference plus one return expression) as the walk would, because a call is resolved
  once and an inner call resolved before its outer one would differ. No root is recorded for class field
  initializers, for-in/of iterables, decorators, computed keys, enum initializers or `export default` with a JSDoc
  @type: those keep the push-down-only behaviour (an implicit any if asked first). Roots in flight on this thread
  (memo flags, the per-thread stack ty_context_root_enter() fills from initialized_binding_typecheck() and return
  statements) aren't settled; a stack overflow (256) counts as in flight. A RETURN root finds its return expression by
  span containment (return_contains_target()). The settle enters a node at the root (an expression statement's
  expression span, not the statement's: a TS2589 there would be at another start than the walk's). Oracle: the
  suite's ttttContextRootSettled, the shared-graph seeds and fingerprint. Probed 2026-10-23, matches tsgo (callbacks in field and static field initializers read from an earlier method, a for-of iterable, an enum initializer, a decorator, a computed key, a JS `export default` with @type).
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
- Instantiation expressions (`apply_type_arguments_part`): a signature whose constraints the type arguments violate is
  kept uninstantiated (tsc's getInstantiatedSignatures) by mapping its first type parameter to TY_UNREACHABLE_NEVER, a
  mark no type argument or mapper makes (`ty_instantiation_keeps_signature()`, `instantiate_members()` pushes it as
  is); an instantiation of the result by an outer mapper leaves it as is too, where tsc's would instantiate its outer
  type parameters. Correct: a kept-signature list on the instantiation. Tested by ttttHackInstantiationKeepsViolating.
- Context re-derivation (`initial_or_assigned_type()`) covers `x = e`, logical assignments and annotated declarations,
  not destructuring defaults. Anchors (`ty_record_call_anchors()`) don't go through calls (first call wins),
  skip non-generic one-signature calls (`CallResolution.exact`) and generators' returns; tsc's getContextualType walks
  every parent up to links.resolvedSignature. Probed 2026-10-23, matches tsgo (callback defaults in object and array patterns and parameters, nested generic calls in an annotated array).
- `KEY_CONTEXT_CHECKED`/`assign_contextual_signature_as`: the first non-skipping check fixes a function's contextual
  signature; generic contextual signatures aren't instantiated; a function without one still takes `this` from the
  last contextual signature (tsc asks lazily).
- Apparent contextual types: `intersection_prop_context` intersects per-member answers, distributing over a type
  variable member's union/intersection constraint within fixed bounds (tsc: the intersection's apparent type once); `context_map` drops `unknown` parts when others contribute
  (tsc: noReductions). Probed 2026-10-23, matches tsgo (`{ f: (x: number) => void } & { f: unknown }` (and `& { f: {} }`) on a method and an arrow).
- Excess properties (`has_excess_properties`): `discriminated_union_target` and `ty_best_matching_type` have no
  key-property map. Probed 2026-10-23, matches tsgo (a 12-member discriminated union: another member's property, extra properties, no discriminant, a `3 | 4` discriminant).
- Destructuring assignments (`assignment_target_typecheck`): for-of destructuring relates the widened element type
  (tsc: per property, so no excess property checks). Probed 2026-10-23, matches tsgo (`for ({ a } of [{ a: 1, b: 2 }]);`).
- Expandos on functions are checked against existing members; `is_container_declaration()` approximates
  isAssignmentDeclaration for `ns.a = {}`. Probed 2026-10-23, matches tsgo (TS function and arrow expandos `f.a = {}; f.a.b = 1`, with declaration emit).
- `member_refs()` (check.c) duplicates `access()`'s walk to get `Prop`s without creating types (two walks to sync).
- JSX: the head rule (`ty_jsx_skips_head` via `Checker.jsx_element`) applies only inside `ty_check_jsx_element`;
  attribute types skip the regular object literal state. Probed 2026-10-23, matches tsgo (a JSX spread before and after an attribute whose object has an excess property, an unknown attribute).
- `assignment_value_context`: `TYF_BINDING_PATTERN` is both tsc's object literal `pattern` and isFromBindingPattern, so
  an object assignment pattern only marks its context when the value isn't a call. Correct: a separate flag.
- `x satisfies T`: on failure prints T and discards the text, for the printing's side effects (tsc builds TS1360 there:
  `const f = () => 1 satisfies typeof f` is TS7023). Correct: report where checked (checkSatisfiesExpression).
- `definitely_unrelated()`'s `mentions_type_variables()` walk resolves signatures' return types when
  `ty_function_free_params()` can't tell syntactically (unannotated non-literal parameter defaults, unannotated returns
  with bodies); tsc's getPermissiveInstantiation is lazy, so tttt can leave a conditional deferred for good when that
  return type needs what's being resolved (mikro-orm's `toJSON(strict = true): Omit<EntityDTO<this>, I>`).
- `ty_literal_rechecked()` (elaborate_element's tsc checkExpressionForMutableLocationWithContextualType): re-reads a
  literal's cached element/property types against the new context (literals kept, arrays tuples where the context is
  one) instead of re-checking the expression in CheckModeContextual; calls and functions keep what they inferred. Probed 2026-10-23, matches tsgo (`[{ a: g() }]`, `[{ a: h("y") }]`, a method in an annotated literal, a tuple from a generic call).

## Relations (`ty/relate.c`)
- Reporting runs: tsc's error-node checkTypeAssignableTo callers run once, explaining (check.c relate_explaining(),
  ty_check_assignable()); the checkTypeRelatedToAndOptionallyElaborate ones (check_assignable()) and the overload
  error reports run plainly first, like tsc's. One one-run caller still runs plainly first: the static side
  (`ty_check_static_heritage`, TS2417: it relates to each intersection member plainly to name the one that fails, which
  tsc's single run does inside typeRelatedToEachType).
- `is_reducible_intersection()` (checker.c) looks for a literal property against a type parameter one in the shape
  tsc's never-filled instantiation would reduce; an optional literal's undefined is stripped first (as before the optional-properties-carrying-undefined change),
  where tsc's getReducedType would reduce through `"a" | undefined` too. Correct: getUniqueLiteralFilledInstantiation. Probed 2026-10-23, matches tsgo (`T & { k?: "a" }` against `{ k: "b" }`: assignment, inference, a constrained T).
- `conditional_source_related()` relates the distributive constraint without tsc's hasNonCircularBaseConstraint(source)
  gate: computing it inside an alias's resolution gives spurious TS2456 (type-fest's `_ConditionalPickDeep`, crawlee).
  Correct: lazier alias references, or the gate after the alias resolves.
- `resolved_base_constraint()` simplifies indexed accesses, and conditionals only when `may_simplify_conditional()`
  sees a `T extends U ? T : never` shape as written (all conditionals: mikro-orm/seeder 2.3x types); misses a branch
  that becomes the check type through the mapper. Types without type variables are their own base constraint except
  string mappings, templates, substitutions; tsc also computes unions/intersections containing those
  (`Uppercase<string> | number`).
- Error reporting: `TyReportLine` has no message arguments, so `report_property_incompatible()` parses paths out of
  messages (`incompatible_line_path()`) and `relation_line_said()` compares texts (tsgo's chainArgsMatch compares
  arguments); headMessage is `RelationState.at_top` + `head_code`. Correct: lines with arguments. Probed 2026-10-23, matches tsgo (property mismatch paths with one name at three depths).
- Elaboration (`elaborate_error`) reads memos (`ty_mutable_location_type`, `ty_array_literal_as_tuple`) instead of
  re-checking; `type_decl_span` covers declared types, function type nodes, type literals and functions only (renaming `as`
  properties lose declarations). Probed 2026-10-23, matches tsgo (a mismatch in a key-remapped mapped type (`as `x${K}``) and in a plain one).
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
- `structured_related_members`: the whole-source fallback for an intersection target covers type parameters, keyof,
  indexed access and conditional sources, not templates, string mappings or substitutions (tsc: any instantiable
  source goes on to its constraints).
- `props_related` skips the same property by `Prop` identity plus a rule for object literal accessors of one function
  (tsc: same symbol).
- `is_rest_array` takes anything but a type parameter or a generic mapped type as a rest array. Correct: normalize `[...X]` when the tuple is made
  (tsc's createNormalizedTupleType).
- Identity (`identity_structured`): differently shaped tuples never identical (tsc compares their properties), no
  `infer` in the two-conditional rule. TS2403 (`check_subsequent_declaration`) exposes every gap. Probed 2026-10-23, matches tsgo (`var x: [a: number]; var x: [number];`).
- The depth-100 backstop (`MAX_RELATION_DEPTH`, assumed related) counts every `recursive_related()` level (`st->depth`);
  tsc counts sourceStack and targetStack separately (a side-only push doesn't count): tttt gives up earlier.
- Alias variance (`in out`): the reporting pass falls back to the interface's own variances on an invariant failure
  only for references to the same target. The null-base TS2417 check takes the first `extends` of the first merged
  interface (`class_decl_merged_interface_base`), not getBaseTypes.
- `TY_INDEXED`'s include_undefined (noUncheckedIndexedAccess) is dropped where an indexed access is rebuilt from parts
  (`simplified_parts`, `ty_actual_type_variable`). Oracle: flagflip noUncheckedIndexedAccess. Probed 2026-10-23, matches tsgo (noUncheckedIndexedAccess with `T extends Record<string, number>`: `t[k]`, `T[string]`, `keyof T`, destructuring, arrays).
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
  rule in `relation_line_said()` (vitest TS2344; since 2026-10-20 only for a mapped source whose apparent type
  differs, atproto's string-index Simplify gets tsc's TS2741 head), `intersection_props_related`'s TS2741 member
  choice (a valibot probe).
- **A generic mapped target falls back to member-by-member comparison for a generic source** (relate.c, after
  `generic_mapped_target_related()`): tsc returns False there unless both are mapped types (mappedTypeRelatedTo), and
  the memberless mapped types the fallback passes stand in for its rules between two generic mapped types (`Readonly<T>`
  to `T`). Carve-outs keep tsc's False for an object source without properties, arrays and non-variadic tuples, and
  (2026-10-20) any object source with non-generic keys (atproto's `$Typed`, mikro-orm's `OperatorMap<T>` against
  `EntityProps<T>`), and (2026-10-22, cor6) every intersection source: its members were each tried before (mapped
  ones by the rules above), and the memberless pass made better-drizzle's `{...} & { [K in X<sub>]?: ... }` to
  `{ [K in X<super>]?: ... }` True, its variances independent and its TS2502 cut a 2M-type runaway
  (ttttIntersectionToGenericMappedTarget). What remains: a generic non-intersection source with generic keys (a type
  parameter, an indexed access, a conditional, a generic mapped type that mapped_related() rejected). Returning outright
  loses circularConstrainedMappedTypeNoCrash, restTypeRetainsMappyness, mappedTypeRelationships and zod's
  SortItem<output<T>> (generic keys) against `{ [K in keyof A | keyof B]: (A & B)[K] }`.

## Type core (`src/ty/`)
- **A signature re-entered while its parameters are being typed** (members.c sig_resolving_record(),
  function_signature_params()): the re-entry gets every parameter, the one in flight as any and the later ones typed
  inside the cycle. Correct: tsc's lazy parameter types (getTypeOfSymbol per parameter when asked), where an argument
  for the in-flight one reports the circularity (TS2502/TS7022). Design: bugs/sourcesweep_checker.md's kebo entry.
- **Two circularity diagnostics come from printing a type for its side effects** (the string is dropped):
  lower_node()'s TYPE_INDEXED case prints an anonymous object whose indexed access failed (`ty_to_cstr(object)`), so
  that resolving its member types makes `type A = { a: A }["x"]` circular like `{ a: A }["a"]` (TS2456; the TS2339 itself
  is still type_typecheck()'s); the satisfies case of
  ty/expr.c's expression computation prints the target of a failed `satisfies`, so that the return type in flight of
  `const f = () => 1 satisfies typeof f` is circular (TS7023, `() => any`). tsc gets both from its own resolution order:
  getIndexedAccessType with its accessNode and checkSatisfiesExpression build their messages (typeToString) right
  there, and the printer's resolution of members is what runs into the circularity. Correct: resolve what the message
  would resolve (the object's member types, the target's types) explicitly at those two points, so that display has
  no observable effect. That is what would make lazy or skipped message formatting safe (notes/checker_speed.md: the
  display of errors a `@ts-expect-error` suppresses is 2.6% of drizzle). Oracle: the suite's circularity tests
  (TS2502/TS7022/TS7023) with display stubbed out at those sites.
- The per-read shadow test (`!my graph_shadowed` on every word's fast path: sw_get(), ty_members(), sig_res() & co):
  0.2-0.6% of K1 instructions (zod -0.6% without it) for 0-1 shadowed words a K12 run. The alternative, a shadowed
  worker giving its file up at once to a fresh worker, needs a mid-file abort (the redo's longjmp, its leaks, a
  per-file error context), deleted with the shared graph (notes/shared_graph.md); kept as a future lever. sw_look_fast() & co (ty.h) inline the default path of overlay.c's functions.
- TyParamState.circular (members.c ty_param_constraint_is_circular()) is a word with no in-flight state: a re-entry
  computes it again and the first answer stays. Without a graph that is a first-wins CAS, as before.
- signature_copy() (members.c): a copy of a signature whose resolution is in flight here gets a snapshot record
  published as resolved; before the words the copy's state stayed in flight forever with the snapshot, so a done-ness
  peek of the copy now says resolved. No oracle shows it.
- `--shared-graph` (ty/graph.c, notes/shared_graph.md) is deterministic by construction except for the open items in notes/shared_graph.md's
  "Stage 4f: done" (kdiff groups C and W, seeds' drizzle TS2322, effect's Schema.tst.ts one-off). Identities go out at creation (intern_set()), memo kinds as their computation ends (graph_memo_flush()). A
  memo kind's slot must never hold an in-flight placeholder (a flush would publish it: KEY_IN_FLIGHT is the marker,
  notes/shared_graph.md), nor a flag be set later on a hash-consed type (OBJECT_MODULE_CLONE). A side table entry that describes a shared object (a
  property's write type, its synthetic parts) must be an identity kind, published before the
  object is reachable: graph_kind_class() lists the ones found so far (KEY_PROP_WRITE_TYPE, KEY_SYNTHETIC_PROP_PARTS,
  KEY_PROP_MAPPED_TYPE), more may hide behind a flag on a shared struct (KEY_ALIAS_MARKER is scratch, each checker's own
  marker instances: published at creation it changed excalidraw at K12 threads). ty_interface_in_base_cycle()'s own walk is a word
  (SymbolLinks.interface_cycle). TSan with the flag is far from clean: `scripts/sharedgraph/tsan.sh` found ~55 distinct report groups
  over the 14 `-p` corpora at T4/T12 (2026-10-21; the list is the design log's "Stage 4f gates", which notes/shared_graph.md points to), most of them
  lazy fields on shared types read plainly beside another worker's write. graph_kind_class() puts KEY_FLOW_NARROWED /
  KEY_FLOW_DISCRIMINANT (census: SHARE) with the scratch kinds, and treats identity records (SymbolLinks,
  FunctionReturn) as published whatever their contents' taint. Since the shared graph work the intern memo kinds' taint is
  runtime (graph.c's MemoTaint, every checker on a graph): a computation's start is its key's miss, found again at the
  put through a per-checker direct-mapped miss table (4096 slots; a pushed-out miss leaves its start in a floor bucket
  of 1024 that any put of a key not in the table takes, so it is never taken for clean: at worst a key sharing the
  bucket is taken for tainted, ~20-180 per corpus run; forgotten at each file's start). Puts that miss no recorded
  miss (intern_slot() straight away) start at the put. Identity-valued memo kinds (KEY_MAPPED_KEY_PARAM,
  KEY_EVOLVING_ARRAY, KEY_REVERSE_MAPPED, KEY_UNION_NODE: their value is a fresh object no key hash-conses; the
  conditional instances, whose inference can make fresh nested objects, by a kdiff bisection rather than a proof)
  publish whatever their taint, like identities: a tainted value of theirs reaches the others. A clean variance group rewinds graph_taint_now to its start
  (claim_and_measure()): its internal events (its own claims read) don't taint its callers; sound because nothing
  started inside the group is still in flight, but it is the one place the counter goes backwards.
- Re-entry below resolution barriers (notes/reentrancy.md): tsc re-enters anything in flight below `resolutionStart` (a call's
  first resolution, the outermost variance measurement) and stops only when a request is above a barrier; tttt
  approximates per site: declarations and inferred return types re-enter below the call barrier every time, as tsc
  does (barrier termination, notes/reentrancy.md, 2026-10-21), aliases below the innermost call or variance barrier
  like tsc (alias_reenters(), since 2026-10-23), symbol properties to depth 16 (prop_type_slow()'s
  `prop_reentry_depth`, a stack backstop: at 4 it cut opencode's chains, history-dependently), base types find their
  cycle across barriers (ty_resolving_cycle_across_calls()), a call re-resolved while in flight drops the nested result
  (call_return_type()), and members never re-enter (members_reentered_unguarded() synthesizes the TS5115). The
  barriers are one stack now (`Checker.barriers`) and every per-kind rule is in ty_resolving_reenters(). Correct:
  barrier termination everywhere (properties' depth 4 included), nested results winning. Oracle: `scripts/reentrytrace.py` (tsgo's events
  against tttt's), mikro-orm/tests and classVarianceResolveCircularity2 (what a partial generalization broke).
- **An alias body's homomorphic mapped instance is re-instantiated only where met directly** (`ty_alias_body_mapped_instance()`,
  `alias_body_twin` in `ty_instantiate()`): `type N = Readonly<{...}>` is a twin of the ordinary instance (tsc caches it
  under N); tsc instantiates it with any mapper (couldContainTypeVariables is true for any mapped instance or type
  literal), getting the ordinary instance, so an instantiated signature returning N prints `Readonly<{...}>`. tttt turns
  the twin back when ty_instantiate() meets it with a mapper, but not inside a type literal without free type parameters
  (`{ a: N }` in `Box<P>`'s members), which tttt doesn't instantiate and tsc does. TYF_COULD_CONTAIN_TYPE_VARIABLES
  can't say it: it doubles as "generic" in member resolution (setting it on the twin broke excalidraw's `Mutable<T>`).
  Only a twin whose arguments tsc's couldContainTypeVariables can't rule out turns back (`tsc_may_contain_type_variables()`,
  tsc's getObjectTypeInstantiation carries its arguments' answer to the instance): `type Slots = Readonly<InternalSlots>`
  over a non-generic alias, an interface or an instantiated alias stays Slots (vue's `$slots`). An approximation of
  tsc's flag kept beside tttt's exact one. Probed 2026-10-23, matches tsgo (`type N = Readonly<{ a: 1 }>` in an interface member's type literal `{ a: N }`).
- Eager-base runaway model (`probe_eager_bases()`, documentation.md "Eager base runaways"; a deliberate artifact
  model: webpack and zod need it).
  Approximations: probes run only when the reference's arguments mention a type whose members are in flight or whose
  bases mention itself at depth >= 2 (`mentions_declared()`, perf: valibot's depth-1 `BaseIssue<T>` is skipped; a
  deferred base's arguments are resolved for it, nested deferred references aren't looked into); the
  guard is "the resolving stack is unchanged since the re-entered members started", with `RESOLVING_MAPPED_CONSTRAINT`
  standing for tsc's key-parameter constraint entry; tsgo's poisoned caches after the runaway aren't modelled (vue's
  two TS7006, missing_checks.md). Right rule: none (tsgo artifact); the corpora (zod, vue, valibot) show drift.
- `structured_related_impl()` TY_TYPE_PARAM: a type parameter source against a union takes its hoisted constraint
  (`constraint & T`) only on a definite True, because tttt relates a generic intersection member by member (meeting the
  outer T relation as Maybe). Right rule: tsc's whole-intersection comparison; flagflip/sweep would show misses. Probed 2026-10-23, matches tsgo (a T constrained to literals against unions with a generic U, U[], a template literal, an intersection).
- Instantiation keys of object literals and JavaScript function expressions by mentions, not by outer type parameters
  (tsc's getObjectTypeInstantiation): `object_literal_type_mentions()` / `ty_function_type_mentions()` (contextual
  parameters could carry a call's type parameters; the contextual-check state work); unknown mentions fall back to the whole mapper (perf:
  docmost 2.2M types), and `declared_inside()` (`instantiate_anonymous()`) drops only parameters declared inside the
  node. An object literal with a getter has instances of both kinds (zod's recursive-types.test.ts depends on which
  meet). The identity depends on history: "unknown" is a peek (a property type not computed yet), and so is
  `literal_instance_is_itself()` (members and property types peeked); under `--shared-graph` the first instantiator
  fixes it for everyone (the shared graph's design log, the IDENT note: notes/shared_graph.md). TypeScript function expressions are keyed by
  `ty_function_outer_type_params()` now (tsc's way; on drizzle, effect, excalidraw, zod and nest no function's computed
  mentions were outside that set). Right rule: the same for object literals (needs the literal's scope) and JavaScript
  (JSDoc `@template`s aren't in the scopes' OuterGenerics); perfsweep types ratio and zod show it.
- `ty_function_outer_type_params()` takes the type parameters of an enclosing context-sensitive function's generic
  contextual signature from KEY_CONTEXT_CHECKED (a peek of this checker's contextual-check state, like
  `ty_function_closed()`): tsc resolves that function's signature (getOuterTypeParameters). Right rule: the settled
  context (CTX's `ty_settle_function_context()`), if instantiating an unsettled one ever shows.
- Stable type ordering (`ty/order.c`) ties still broken by tttt ids: roots without `outer`, deferred references with no
  cheap node position (`type_node_start()`), two nodes at one token, position-less unique symbols, intrinsic variants,
  same-symbol anonymous types with equal mappers. Shows as union order (displaydiff, expecttypes "union order"); `TTTT_ORDER_STATS=1` counts
  them by class and `TTTT_ORDER_SEED=N` permutes them (the union-order gate, notes/shared_graph.md). Spread alternatives are keyed structurally.
- Pseudo types (`ty/pseudo.c`): `pseudo_of_variable()` always gets a non-contextual place (tsgo: a variable inside a
  callback is contextual); `Pseudo.in_const` is syntactic (tsgo: isConstContext); `pseudo_enter_signature()` treats an
  instantiated signature's type parameters as fresh by mention; `reused_node_length()` ignores comments in the range;
  `pseudo_member_prop()` matches by name, then declaration span; a fresh object literal's `pseudo_type()` is null by flag, not
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
  (`decl_map_type_end()`: nested parens counted to 4 pairs, `TypeNode.paren_extra`; `source_modifiers()`;
  `decl_map_back_trivia()`). Reused nodes tsgo prints anew (trpc's `infer T extends object`, mixin constructors) map
  where tsgo doesn't (12 dtsdiff projects by maps alone); a node reused from another file is mapped with its own file's
  offsets (tsgo drops such ranges; tttt can't tell a TypeNode's file). dtsdiff `--names` lists them.
- isolatedDeclarations reporting (`declaration_emit.c`): no parents, so `locate()` re-walks the serialized declaration;
  a node reported from another declaration gets no related info; `print_part()` prints Inferred parts as
  `ty_pseudo_type()`, NoResult parts print nothing (missed TS4xxx); printing stops at TS7056 truncation (tsgo's
  later properties still report: monkeytype); `interface_declares_method()` matches by name only (tsgo:
  getSignaturesOfSymbol); `declared_item()` loses deep chains (zod shape spreads, knip). Source sweep/clidiff show it.
- Member order (`builder_sort_stable()`, tsc's getNamedMembers): sorted for object literals, mapped/reverse-mapped
  types, namespaces, merged values and declared class/interface members (`builder_prototype_last()` for `prototype`);
  declaration-less props sort by name bytes, unique symbols last (tsc: escapedName then symbol id). Shows in types/symbols baselines.
- `UnaryOpExpr.no_type_params_in_scope` (TS7057 `yield`): only a bare type parameter counts as a call's; a contextual
  `T[]` is taken as is. Right rule: getResolvedSignature's instantiation of the contextual type.
- `ty_realias`: a renamed object type is a second `Ty` (members resolved twice); renamed deferred types are copies; no
  "local alias" exception; whether an alias instance keeps its name (`renames` in `lower`) is syntactic.
- `lower_identifier()` `through_import`: a generic alias instance takes the import's alias only for union/intersection,
  deferred conditional, indexed access or deferred reference bodies, and anonymous ones without a name of their own
  (an indexed access body resolved to a method's type, qwik's FunctionComponent), not instantiated anonymous/mapped
  ones that have one (tsgo keys those on the alias too); doing it made arktype's `finalizeTree` run away into 3
  spurious TS5114 (uninvestigated).
- Conditional types (`conditional.c`): deferred conditionals don't keep inferences (`inferred_true_branch()`
  recomputes). Permissive/restrictive instantiations
  replace only what `mentions_type_variables` finds (8192 types / 48 deep, else "unsure": defer), resolving signatures
  tsc never would (`callable_definitely_related()` short-cuts two idioms, bugs/type_core.md); a getter whose return
  type is in flight counts as unsure (zod recursive getters otherwise TS7023).
- `instantiate_conditional`: outer parameters from `collect_free_params`, which gives up on `typeof` of a name possibly
  declared under type parameters, non-class `this`, non-literal parameter initializers, bodies without return annotation, JSDoc callbacks (then the uncached full-mapper path,
  `TyConditionalOuter.unknown`). `TyMapper` is flat.
- Mapped types: homomorphism (`homomorphic_variable`) needs a syntactic `[K in keyof X]`; `expand_mapped` expands once
  keys are literal unions, modifiers only for a written `keyof T`; `mentions_mapped()` (ty_display.c) compensates for
  instantiating member signatures that can't mention the parameters (bugs/display.md).
- `expand_mapped()` HACKs (perf): homomorphic over `T extends A | B` whose keys are only index signatures adds nothing
  (tsc: an index signature), and a union of arrays/tuples isn't expanded (valibot +3.6% because `T[string]`'s
  constraint comes out unknown). Fix: `T[string]` of a union of
  index-signature objects is the union of their values.
- `class_base()` HACK: class names found syntactically; other expressions take the first fitting construct signature
  (TS2510 by `ty_class_base_constructors_differ()`); a base constructing `any` is no base (its sides get tsc's
  anyBaseTypeIndexInfo, `class_base_is_any()`); type-parameter bases take the constraint's
  members at declaration; `ty_non_augmenting_base()` is syntactic. A bare `C`/`this` inside generic `C` isn't its
  own instantiation (lower.c TODO). Probed 2026-10-23, matches tsgo (overloaded mixin constructors with differing or compatible instances, type-parameter bases in a class declaration and expression).
- `never_reduced_prop` (permanent, perf: zod +4.1%, drizzle +0.9%): `ty_reduced()` skips members flagged
  `TYF_COULD_CONTAIN_TYPE_VARIABLES` unless `is_known_object_for_reduction`; tsc looks at every member.
- `primitive_constraint_of` HACKs (permanent, perf): a deferred type in a declared constraint is no primitive (valibot
  +5% otherwise), nor an indexed access into a conditional with generic branches (react +15%).
- Generic tuples: `base_constraint` replaces only variadics with array constraints; `lower_tuple` keeps the written
  `T[]` for rests (tsc: `T`). `apply_mapping` steps code points (JS: UTF-16 units); `${symbol}` is silently `string`.
- `builder_add_interface_member`: index signature types lowered eagerly; computed keys neither literal nor
  string/number/symbol typed are dropped (declaration emit has its own `late_index_members()`). Probed 2026-10-23, matches tsgo (`[k]: number` with `k: any`).
- Unique symbols: escaped names (`__@sym@12`) mapped back by `ty_unique_symbol_of_name`, so a string property `__@x@3`
  passes as a symbol; `instanceof_target_ok` matches `__@hasInstance@<id>` by text; one unique symbol per declaration
  (tsc: per merged property), so `check_subsequent_property` skips TS2717 between two.
- Private names (`ty_private_name`): lexical lookup scans class items by text (`class_decl_find_private_name`). Object literal accessors have no `write_node` (`prop_decl_rare()`).
- `Checker.current_node` (TS2589/TS2615/TS4110/TS1062 location): `lower()` makes a generic reference current while
  lowering it (a guess at tsc's order); `Foo<X>` isn't lowered in the walk (resolving every alias changed type ids).
- Instantiation limit: an exhausted budget's results are memoized like tsc's (`test/probes/instantiation_limit/`), but
  tttt instantiates more eagerly (variance measurement, eager members), so a tttt-only TS2589 poisons what tsc would
  still compute; a cut-short conditional instance is re-reported only within its epoch.
- `item_is_literal()` (ty_display.c): an object literal property valued by an array/object literal isn't printed from
  syntax in the unwidened type, told syntactically because tttt's tuples/arrays have no fresh twin (tsgo:
  ContainsObjectOrArrayLiteral). A real rule, not an artifact (without it only
  declarationEmitUsingAlternativeContainingModules1/2 lose their types). Correct fix: fresh array/tuple literal types.
- `reference_from_type_node()` / `recursion_identity()`: ObjectFlags.FromTypeNode, for tuple, class and interface
  references made from a type node first. Order-dependent like tsc's (whoever makes the reference first decides).
- `ty_distributed_type_param()`: tsgo distributes the conditional's own check type node too; tttt keeps the original
  (its `conditional.check ==` identity tests broke 12 tests) and emulates the rest: `conditional_source_related()`
  maps twins back, `instantiate_conditional()` keeps identity mappings of params with a twin (unless every outer
  parameter maps to itself: tsgo's seeded root.instantiations), the identity relation
  treats a twin as its original, and `identity_structured()` tells a written distributive conditional (the root,
  whose check type is tsgo's twin) from one instantiated onto the same parameter (tsgo's original): an instance whose
  outer mapping is the identity is a root in tsgo but not here. That identity isn't symmetric, so its result is whichever direction is compared first: `recursive_related()` keys identity by the pair in id order like tsgo, and matching tsgo relies on tttt asking the pairs in tsgo's order (held on es-toolkit, the probes and the sweeps; not guaranteed). Correct: distribute the node and replace the identity tests.
- `ty_tuple_target()`: all tuple targets share `Checker.tuple_params` (tsgo: fresh per target; type-fest's IntRange
  made 4.5M). Unsound if a type mentions two targets' raw parameters at once. Element props share the read-only
  `ty_prop_decl_value_name` (`prop_decl_shared()` guards writes).
- `expand_generic_intersection_homomorphic` (members.c): index signatures come only from a part that is itself a
  generic homomorphic mapped type; tsc's resolveMappedTypeMembers takes every part's, but that added an inference
  candidate in localTypeParameterInferencePriority. Shown by: that test when generalized. Such an expansion is marked
  `index_signatures_incomplete` (MembersRare) and check_access/TS7053 don't report what it seems to lack (react-
  navigation's `Partial<{ [E in keyof (EventMap & Core)]: X }>[string]`, better-auth); its accesses are still the
  error type where tsc has `X[string] | undefined`. Fixing the inference removes both.
- `never_index_read` (checker.c): an instantiated anonymous object indexed by never is never, except a mapped type, whose keys its
  declaration tells (a type literal as written, no instantiation, reads its index signatures since 2026-10-24: amcharts5) (`Record<string, T>[never]` is T, the instantiated declaration's keys); tsc reads index signatures everywhere, but resolving xstate's
  keyless mapped types tipped TS5115 (flagflip nosnc/sloppy +7). Correct: instantiation accounting that doesn't
  depend on member resolution, or a lazy template for keyless mapped types. Probed 2026-10-23, matches tsgo (`r[undefined as never]` of a `Record<string, number>`).
- `rest_literal()` (ty/lower.c) / `object_symbol()` (ty/order.c): a destructuring rest's binding symbol, which orders
  it in unions (tsc's getRestType symbol), lives in the anonymous type's `literal_expr` union slot (`rest_binding`,
  flagged `rest_of_binding`), free for synthetic objects; it is also kept in KEY_REST_SYMBOL links. Readers of
  `literal_expr` must keep checking `object_literal` first. Correct: a symbol on anonymous types like tsc's. Oracle: the
  suite's union order (ttttRestTypeUnionOrder).
- `ty_keyof()` (ty/checker.c): a mapped type with an `as` clause over `keyof X` gets its keys from
  ty_mapped_type_name_keys() (tsc's getIndexTypeForMappedType), other renamed mapped types still from their expanded
  members (an `any` name there would be a string index signature). Oracle: ttttKeyofMappedAsAnyName.
- `KEY_REDUCTION_KEY`'s memo in subtype reduction (each member's discriminating property): could keep a transient
  answer if a property type is read mid-cycle (circular resolution); no test or oracle shows it.

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
  symbol, printing `typeof Base`); `Checker.display_of`/`display_as` patch the messages. Fix: a synthetic anonymous type
  remembering the symbol it prints as.
- `check_access` (TS2339): bracket access never reports there (`ty_check_element_implicit_any`, TS7053, mirrors
  getPropertyTypeForIndexType's errors separately); said to fire on modules too (TS2339 next to TS2307: not
  reproduced with namespace/default imports of a missing module, 2026-10-17).
- `ty_check_index_constraints` on type literals only when syntactically needed (`type_literal_has_index_constraints()`):
  the others aren't lowered at their check, and an alias's literal body must be made as the alias's type first.
- Syntactic stand-ins: overload grouping (`statements_check_overloads`, `class_check_overloads`); TS2729
  (`initialized_in_static_blocks`: top-level `this.x =`/`C.x =`, not tsc's control flow at each block's end; mutate.py shows it); member kinds in `class_check_member_overrides()`
  (`class_instance_member()`, not symbol flags; TS2612 skips isPropertyInitializedInConstructor);
  `check_property_modifiers` (TS2687) skips class methods (accessors are compared with, as a first declaration).
- `ty_check_class_duplicate_members()` HACK: a late-bound name (`[k]`, `[Symbol.x]`) conflicts with the member table
  (static `prototype`) without joining it; tsc also reports an early `a` against a later `get [k]` (k = "a") at both. Probed 2026-10-23, matches tsgo (`a = 1; get [k]()` with `k = "a"`).
- Emit helpers (`ty_check_emit_helpers`): spans by hand (`decorator_span()`, `function_error_span()`); "ambient" per
  site (`ClassDecl.is_ambient`, `Binding.ambient`, `BlockTypecheckCtx.ambient`; want NodeFlags.Ambient).
  `EMIT_HELPER_EXTENDS/ASSIGN/GENERATOR/VALUES/READ/SPREAD_ARRAY/MAKE_TEMPLATE_OBJECT` are dead (ES5/downlevelIteration
  only) but stay in the enum: `helpers.c`'s table is indexed by bit.
- `unused_mark_jsdoc_links()` HACK: `{@link X}` is a use by text scan, looked up from the file's scope (fix: resolve
  parsed tags). A JSDoc right before a statement in a block is also resolved from that block's scope
  (`unused_mark_leading_jsdoc_links()`, block_lookup_names_as), so a local only a link names is used; the file-wide
  scan still runs over every link (class members', parameters' JSDoc too), so a shadowed name keeps the outer one used. Member reads are keyed by declaration span start (`ty_mark_member_referenced`). Tested by ttttHackJsdocLinkShadowed (known failing: a link to a shadowed name keeps the import used). The fix needs the link's scope: the walk would resolve it, but deferred bodies aren't walked when the file's links are marked.
- `program_apply_comment_directives`: suppression by tttt's error phase, not tsc's diagnostic kind; `\n` and ASCII
  `\s` only; relies on every comment going through `hop_comment`.
- `SourceFile.unchecked` + `error_ctx_remove_unchecked`: skipLibCheck/noCheck/JS filtering happens after checking, so
  skipLibCheck'd `.d.ts` are checked in full for nothing (tsc's skipTypeChecking doesn't): a perf lead, if checking
  them first doesn't change what checked files see. Plain JS is checked in full and filtered (as tsc).
- `ty_check_element_implicit_any` (TS7053...): unaliases the apparent intersection itself
  (`ty_intersection_apparent_unaliased`) because `ty_apparent_type()` keeps an intersection's alias where tsc's
  getApparentTypeOfIntersectionType makes it anew when a member changes. Other messages printing an apparent
  intersection may show the alias tsc doesn't. Correct: apparent type without the alias (mind relation caches).

## JavaScript and JSDoc (bugs/js.md)
- `IdentifierType.jsdoc_unchecked` (`identifier_type_check_arity()`): tsc only *resolves* `@returns`/`@throws`/`@augments`
  types (never checkSourceElement); tttt runs `type_typecheck` and skips just the constraint check. Correct: a
  resolution-only walk that keeps arity errors (jsdocArrayObjectPromiseNoImplicitAny). Shown by: mutate.py on JS tests.
- TS8023 (`jsdoc_apply_class()`): only checked when the tag's rightmost name differs from the clause's; tsgo's
  checkJSDocAugmentsTagMatchesExtends compares every tag's type with the base (`@augments A<string>` on
  `extends A<number>` is TS8023 too). Shown by: a probe / mutate.py.
- `jsdoc_bind_comments()`: claims made by failed parse attempts stay; `@import` only at a list's top level; dotted
  `@typedef` names skipped; typedefs sharing a comment share its `@template`s. Correct: tsgo's per-tag reparse.
  Shown by: jsstats.py, the sweep's JS packages. Probed 2026-10-23, matches tsgo (two typedefs in one comment with one @template, a dotted @typedef, an @import mid-comment).
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
- `symbol_chain_enter_class()`: a thread-local the baseline traversal sets names a CommonJS-exported named class
  expression by its own name inside its body; error messages still name it from the file.
- `ty_report_enclosing()` / `statement_scope_at()` (ty_display.c): tsc prints a relation message's type from its value
  declaration when that is a non-context-sensitive expression (getTypeNamesForErrorDisplay: annotation reuse, names
  qualified from there). tttt types don't know their expression: a function expression prints from its first
  parameter's scope (else the file), an object literal from the namespace/function body/block around a property written
  in it, a spread-only literal from nowhere; context sensitivity is guessed from member types. Correct: the literal's
  type knows its node (or the resolve walk stores a scope on the Expr). Shown by: errtextdiff.py header buckets. Probed 2026-10-23, matches tsgo (object literals, function expressions and a spread-only literal declared in a namespace, mismatching inside and outside it).

### Symbols baseline reconstruction (harness: tttt has no tsc-shaped member symbols)
- `collect_prop_symbol()`: a non-object union member lacks the property only for binding patterns, because a property
  access's object is sometimes narrowed less than tsc's (applying it everywhere loses 70 symbols components). Correct:
  narrow like tsc, then drop the gate.
- Span overrides stand in for tsc's symbols: `this_property_symbol_overrides()` (getSpecialPropertyAssignmentSymbol;
  `builder_add_assignment_members()` and enum members append spans to the checker's `Prop` for it), `module.exports`'s
  `exports` member (span override in the traversal's `EXPR_PROPERTY` case), `append_decl()` sniffing a JSDoc typedef's
  dotted name / tag Decl trivia from the text, `symbol_declaration_spans()` dropping a JSDoc namespace declaration at
  another's start.
- Member-table merges rebuilt in the harness only (the checker doesn't merge): `class_prop_declarations()` (field +
  parameter property/interface member), `register_type_parameter_member_merges()` (`BaselineTestCase.merged_declarations`,
  registered when the class is walked: an earlier use misses it), `baseline_symbol_declaration_spans()` (namespace local
  + its exported declarations; a function's type parameter + a parameter/local, `Scope.type_params_function`). A union property's symbol dedupes members by (first declaration, mapper), treating
  an instantiation whose type equals the declared one as uninstantiated (tsc's lazy instantiateSymbol may differ).
- `unknown_symbol_text()`: tsc's unknownSymbol is named by the first alias of an unresolved import in the innermost table.
- `namespace_decl_compile_test_info`: an ambient module's name is `symbol_text()` unless that comes back quoted (a text
  test, not a chain query).
- `BaselineTestCase.any_keeps_rendering` leaks to the next types line if the node it was set for adds none.

- `identifier_flow_type()` walks an import's flow (an alias that isn't a variable) only when a narrowing flow node names
  it as a whole (`Symbol.flow_narrowed`, set by the flow builder: `flow_names_of()`), or as the object of a called
  method when the target may have a `this` predicate (`flow_narrowed_this`; a module or namespace only if one of its
  exported function declarations declares one: `may_have_this_predicate()`). tsc walks every alias; the gate is exact
  but for a `this` predicate typed on a namespace's exported variable (`export const isX: (this: ...) => this is ...`),
  which isn't seen. A narrowing node naming an unresolved identifier opens the gate for the whole file
  (`SourceFile.flow_unresolved_names`).

## Harness and tools
- **The taint prototype's hooks** (`TAINT_*` in lazy_type.c, checker.c's intern hooks, members.c, relate.c, lower.c,
  function.c, conditional.c, instantiate.c, flow.c, expr.c, ty.h; the taint prototype, notes/shared_graph.md): compiled out unless
  `-DTTTT_TAINT`, but the explicit kinds' early returns are wrapped in `TAINT_IF()`, which makes them harder to read.
  Kept so cleandiff can be rerun after each stage 4a-4c refactor. Delete them once the overlay protocol (4d) records
  the same events for real. The intern hooks find a value's computation by matching a missed lookup's hash to the put that follows (a
  1024-entry ring); a put with no matching miss gets an empty window, so it counts as clean (`unmatched` in the raw
  output: mostly structural kinds whose value is its key). The one-word `node_links` table has no hooks: only its
  explicit kinds are covered (FUNCTION_RETURN, BASE_CONSTRAINT, FUNCTION_FREE_PARAMS)
- Deliberate simplifications (permanent, see documentation.md "baseline_tests"): `apply_setting`, the lib `Program`
  cache, `@noTypesAndSymbols`, `engine.c`, `failstats.py`'s `line_ratio`.
- `-b`'s visibility window (`build()` in main.c): a program sees an unreferenced project's emitted declarations only
  when that project is at least 4 places (`--builders`) earlier in schedule order (tsgo 7.1's build order sorted by
  dependency depth), and waits for it: a deterministic
  stand-in for tsgo's builder race (it waits only on references, so whether `dist` exists yet depends on timing;
  atproto's whole solution gets it right but for one import, oauth-client alone at its default builders doesn't). The
  `EmitView` doesn't list in-memory files in directory listings (an `include` glob into a reference's `dist` misses
  them).
- The CLI checks a config that would emit JavaScript as `noEmit` (`Options.implicit_no_emit`), marked
  `CompilerOptions.no_emit_implied`: the option checks of an emitting program (TS5011, TS5055/5056, TS5096) and the
  exit status (noEmitOnError or a blocked output: 1) go by `compiler_options_no_emit_given()`, the config's own noEmit;
  anything else that reads `no_emit` sees the forced one. Lifts with JS emit. Shown by: scenarios.py (no longer drops
  TS5011/5055/5056 from emitting commands).
- JavaScript declaration diagnostics (`declaration_diagnostics_check()`): `check_commonjs_exports()` repeats the
  CommonJS exports' serialization (for painting) into a scratch context, `check_js_commonjs_exports()` having reported
  them. `export_specifier_local()` (check_entity_name()): a module export merging `export { x }` with a dotted
  `@typedef`'s namespaces is checked as x's local, where tsgo's binder merges the namespaces. Shown by:
  jsDeclarationsImportAliasExposedWithinNamespace.
- JavaScript declaration emit guesses (one or two suite tests each): a default-exported function's `@overload`s keep
  the statement's comments, other functions' don't (`emit_function_declaration()`); a constructor's `@overload`s have no
  comments; `@property` description margins follow parseTagComments by observation (the first line's text column, or
  the tag's column when the description starts on the next line; `jsdoc_tag_description()`, `{@link}` copied
  as written); `ty_jsdoc_name_is_type()` copies tsgo eliding the import of a JSDoc name its fallback resolves as a
  value (tsgo's own `.d.ts` then doesn't resolve).
- JavaScript declaration emit (`declaration_emit.c`): CommonJS exports are found by a syntactic
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
- **boltbuild.sh rebuilds llvm-bolt's instrumentation runtime when the installed one calls through the GOT** (Arch's
  llvm-bolt 23.1.3-4: GCC with -fno-plt, so `memset` goes through `GOTPCREL`, which BOLT's JITLink rejects): it fetches
  `bolt/runtime/{instr.cpp,common.h,sys_x86_64.h}` at llvmorg-VERSION from GitHub and compiles them with
  LLVM's CMakeLists flags plus `-fplt` into `bolt/rt-VERSION`. Network once per llvm-bolt version; x86-64 only. Correct: a
  distro runtime without GOT relocations (an Arch bug report), then this block goes.

## Content mappers (`content_mapper.c`, documentation.md "Content mappers")
- `alias_original_names()`: tsgo substitutes an alias segment's original text in the *arguments* of a diagnostic
  covering it (displayMessageArgs); tttt replaces the quoted virtual name in the formatted text. Wrong if that name
  is quoted for another reason.
- `append_compiler_options()` sends only the options tttt has, not tsgo's whole CompilerOptions JSON (paths, lib,
  strings): a mapper reading those gets nothing.
- Not done: the `locale` initialize parameter, mapper protocol/stderr logging, declaration output names for mapped
  files (unchecked since tttt emits declarations), `-b`'s tsbuildinfo `contentMapperIdentities`.
- `node_in_mapped_file()` (ty_display.c, declaration maps): a reused TypeNode doesn't know its file; found from one of
  its spans, a keyword by comparing the text at its offset. Correct: TypeNodes knowing their file. Shown by: dtsdiff.py.
- `type_member_setter_name()` (ty/members.c): a type member accessor pair's setter name, for prop_write_type()'s TS2502,
  is read back off the source text before the setter's parameter name, because PropDeclRare's size is asserted (foot's
  872b2e626) and has no room for a Span. A computed setter name falls back to the property's name span.
- `made_before_step()` (ty/relate.c, the flag's walk order for deep nesting): a function type's return type is
  peeked (what this worker knows) to decide "made before" (fact 2 of sharing_design.md §6); inferred lazily, so under
  --shared-graph whether it is known can be history. Correct: a structural fact for it (the literal's/function's
  inferred return type made part of its key, or decided without the peek). Shown by: a `-DTTTT_GRAPH_CHECKS` build's
  "read the return type of" report (openscience/backend/cli only, output unaffected).
