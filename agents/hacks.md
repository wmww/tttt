# Hacks
Suboptimal-but-working code. Prefer function names over line numbers.

- `current_checker` / `current_lazy_slots` (thread locals, `checker_make_current`): how legacy code without a context
  parameter finds its checker. Stays set after `program_typecheck` because the test traversal and debug display
  evaluate types afterwards. Shrinks as signatures get a `Checker*` (plan 02 stages 3-4).
- `lazy_slot_new`: LazyType slot numbers come from one process-wide atomic counter and are never reused, and
  everything that might need a memo takes one eagerly (every transient `scope_init`, every `type_identifier` made
  during checking). A checker's slot table is sparse so this only costs page fragmentation (bench RSS +6%), and it
  panics after 1G slots. Per-file numbering (plan 02 §2) didn't fit because Declarations, Scopes and check-time
  identifier types belong to no file. Revisit in stage 4/5 when slots hold `Ty*` and clones are gone; a long-running
  process (language server) needs reuse.
- `LAZY_SLOT_INLINE` / `LazyType.memo`: clones made during checking keep their memo inline, so `LazyType` hasn't
  shrunk yet (it grew 8 bytes). Dies with `type_clone` in stage 5.
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
- `baseline_test_case.c: apply_setting`: harness-only directives other than `@filename` and `@noTypesAndSymbols` are
  ignored (`@libFiles`, `@noImplicitReferences`, `@currentDirectory`, `@link`/`@symlink`, `@baselineFile`, a
  `tsconfig.json` among the test's files). Bad option values are ignored too (tsc's harness throws).
- `baseline_test_result_run`: with `@noTypesAndSymbols` the symbols and types components count as *passed*, so the
  per-component totals include ~900 free passes. A test without types/symbols baselines is run before we know
  whether it should have had any.
- `baseline_test_cache_get_program_with_libs`: one cached lib `Program` per distinct lib list (dozens per suite run,
  each with the DOM in it), never evicted. An unknown lib name is logged at info level and skipped, no TS6046.
- `display_type` depth guard (`DISPLAY_TYPE_MAX_DEPTH`): the legacy display has no cycle detection, a module that
  imports its own namespace recursed until the stack ran out. Prints `...` at depth 100. Dies with the legacy display.
- `main.c: default_libs`: without `--lib`/`--target` the CLI loads ES5 + DOM + three ES2015 libs (what the bench
  corpus needs) rather than tsc's default for the target.
- `program.c`: `.js` import path handling by suffix check - not real module resolution.
- ~60 `PERF`/`HACK`/`TODO`/`FIXME` comments in `src/` not yet catalogued here (triage).
- `failstats.py: line_ratio`: "line match" is multiset overlap of lines, not a diff (difflib was far too slow), so
  it overstates similarity when lines are reordered. Good enough for ranking nearly-passing tests.
- `failstats.py: syntax_gaps`: locates the offending source line by re-parsing tttt's rendered errors output;
  approximate for multi-file tests.
- `bench/make_corpus.py`: corpus is passing tests wrapped in namespaces and replicated. Flatters tttt.
- `main.c: --keep-going`: typechecks after name errors purely so the bench can time the stdlib; not validated.
- `port_item_resolve_alias` (`import x = A.B`): the alias is a *copy* of the target declaration, made during name
  resolution by overwriting a placeholder in place (other ports copy too, see `process_port`). An alias to something
  declared by a later alias, or to a namespace that gains members later, sees a stale/any copy. Wants real symbols
  with alias links (plan 03). `alias_conflict` likewise only approximates merging an alias with an existing name.
- `parse_import_equals`: `import x = require("m")` is treated exactly like `import * as x from "m"`. Wrong for
  `export =` modules (not supported at all yet).
- `namespace_decl_add_names`: ambient modules (`declare module "m" {}`) get a scope for their contents but are not
  registered anywhere, so `import ... from "m"` still reports TS2307. Needs a program-level ambient module table
  (module resolution work). Their `"m"` name line is also missing from the symbols/types baselines.
- `Interface.namespace_scope`: namespace value types print as `typeof A.B` via a borrowed `Scope*` stuffed into the
  structural interface; without it, display recursed forever on `namespace A { export var a = A }`. Relations go
  through the new core now, where a namespace is a declared type by scope. Dies with the legacy types.
- `parse_jump_target` / `Statement.jump_label`: labels are parsed and shown in the types baseline, but never checked
  (TS1116 unknown label, TS1107 crossing function boundary, TS7028 unused label, duplicate labels). `inside_label`
  in `BlockTypecheckCtx` just permits any labeled `break` inside any labeled statement.
- `run_baseline_test_case: path_is_script`: virtual files that aren't TS/JS by extension are left out of the program
  entirely. Right for package.json/tsconfig.json, wrong for `.json` modules under `resolveJsonModule` (5 tests).
- `EnumDecl.is_const`: recorded, nothing uses it.
- `parse_object_item`: `{ a = 1 }` (shorthand with default, only legal in destructuring assignment) parses and throws
  the default away; `?`/`!` after a member name is swallowed without TS1162. `ObjectExprItem` methods are plain
  `EXPR_FUNCTION` values.
- Class accessors (`ClassItem.method.accessor`, `class_item_accessor_evaluate_type`): pairing is by name+staticness in
  `class_decl_lookup_names` (raw pointers between items of the same vec). No TS2300 for property-vs-accessor clashes,
  no check that getter and setter types agree, readonly only means "no setter". Interface/type-literal accessors are
  converted to properties in `parse_interface`.
- Private names (`#x`): the `#` is just part of the member name string. No TS18028 target check, no "not accessible
  outside class" (TS18013), no `#x in obj`.
- `p!: T` / `let x!: T`: the `!` is hopped and forgotten (class fields only; `let resp!: T` still doesn't parse).
- Generator methods/functions (`FunctionImpl.is_generator`): inferred return type is `any`, `yield`/`yield*`
  expressions are `any`, returns aren't checked against the declared type. Needs `Generator<Y, R, N>` inference.
- Async functions (`FunctionImpl.is_async`, `promise_decl`): inferred return is `Promise<T>` built from the global
  `Promise` looked up by name during name resolution (`any` if the lib has none, eg target ES5 without `@lib`); returning a promise nests instead of flattening; returns aren't checked against the declared type;
  no TS1064/TS2705. `type_awaited` only unwraps types literally named `Promise`/`PromiseLike`.
- `await_is_operator`: decides keyword-vs-identifier from `ParserShared.in_async`/`function_depth` plus a peek at the
  next character. Top-level await never reports TS1375/TS1378 (needs module detection + `@module`/`@target`).
  Class field initialisers and parameter defaults count as "top level"/inherit the enclosing function's context.
- Decorators (`ClassDecl.decorators`, `ClassItem.decorators`, `FunctionArg.decorators`): parsed, name-resolved,
  typechecked and traversed as ordinary expressions. Nothing checks that they're valid decorators for their target
  (TS1238/TS1240/TS1241...), and `@experimentalDecorators` ordering rules (`export @dec class`, TS1206) aren't applied.
- `interface_member_typecheck`: TS1166/1168/1169 are skipped when the computed key's type is `any` or `symbol`
  (`unique symbol` is parsed as `symbol`), because `[Symbol.iterator]` can't be represented yet.
- `variable_statement_typecheck` TS1254: any property access is accepted as a "literal enum reference".
- `EnumCase.name`/`full_name`: string-literal member names take their name from the source text between the quotes
  (escapes not processed); enum member types still print as `E.b c` rather than `(typeof E)["b c"]`.
- `engine.c` ignores every construct added in parser rounds 1 and 2 (returns undefined). See triage: is it used?


## Relations bridge (plan 02 stage 3): legacy checker -> `ty/relate.c`
- `TY_RELATION_ASSIGNABLE_STRICT_NULLS` / `type_extends_strict_nulls`: what the legacy operator checks in `expr.c`
  mean by "extends number" (null/undefined don't, whatever strictNullChecks says). tsc checks operands differently
  (TS18050, never emitted here).
- `type_composite_internal`: union/intersection simplification of legacy types runs the new strict-subtype relation
  pairwise per added member (lowering both each time). Quadratic; `ty_union` itself still has no subtype reduction.
- `callable_type_typecheck` TS2370: skipped for `...args: infer A` by looking at the written node.

## Legacy code consuming the new core (plan 02 stage 4a): `TYPE_NEW` and `ty/legacy_view.c`
Classes, interfaces and enums (and every identifier type that names one) evaluate to `Ty`, carried through legacy code
as `TYPE_NEW`. All of this is scaffolding for stage 4b/5.
- `ty_legacy_view` / `view_build`: `type_resolve()` of a `TYPE_NEW` is a one-layer legacy copy of the `Ty` (interface with
  members, union, array...) owned by the checker, children `TYPE_NEW` again (lazy per property / return type through
  `Type.new_.prop` / `.ret_of`, because forcing every property type of a class when one member is looked at sends the
  legacy checker into cycles). Deferred types (`keyof T`, `T[K]`, unresolved conditionals) view as their base constraint
  (`ty_base_constraint`), `unknown` without one, so legacy code that resolves one loses the type (`x!` had to learn
  to stay in the new core for that reason: `type_remove_null_and_undefined`). An anonymous
  object with exactly one signature and nothing else views as `TYPE_CALLABLE`, anything else as `TYPE_INTERFACE`.
  Enum literals view as plain values (`type_is_trivially_truthy` has to ask the `Ty` to know better).
- `view_signature`: legacy inference identifies type parameters by `GenericItem.declaration_ref`, so the view's `Generic`
  is a clone of the items the signature's type parameters were declared by; a parameter without a `FunctionArg` to point
  at (signatures lowered from legacy temporaries) gets an arena stand-in without a name.
- `type_resolve_internal`: `type_resolve_until_identifier()` stops at a `TYPE_NEW` (it's "named") unless it's a type
  parameter, whose view is the `TYPE_IDENTIFIER` legacy code expects. `type_resolve_to_new()` exists for
  `generic_params_expr_evaluate_type` (`Bar<T>` as a value), which needs the `Ty` behind an identifier.
- `ty_declaration_is_evaluated_by_new_core`: the switch that decides which declarations' types come from the new core
  (class, interface, enum, enum member, type alias incl. `infer X`/mapped keys). What `identifier_type_evaluate_type`
  still answers itself: type parameters (as their constraint) and the `typeof` declarations of namespace imports.
  Namespaces, functions and variables are still legacy.
- `type_resolve_one_layer_internal` (TYPE_INDEXED / TYPE_KEYOF / TYPE_TERNARY): a type operator that legacy code resolves
  is lowered and handed back as a checker-owned `TYPE_NEW` (`ty_legacy_wrapper`). Nothing is memoised per node: parsed
  nodes are cheap (interning), clones made by legacy substitution are lowered as transients every time.
  `type_resolve_binding_indexed_type`: destructuring bindings are `TYPE_INDEXED` nodes with `is_from_binding`, resolved
  with `ty_access` on every resolve (no TS2339-style errors from there, `array_only` is a legacy guess). Dies in 4b3.
- `MergedType.declared_owner`: how `merged_type_evaluate_type` finds the Declaration of a merged class/interface.
- `class_decl_evaluate_structure`: the legacy structural class type survives only to feed
  `interface_typecheck_members_against` (duplicate member checks) from `class_decl_typecheck`.
- `canonical_declaration`: import aliases and re-exports are copies of a Declaration; identity goes by the declaration
  the AST node itself registered (found through `Declaration.nodes`), unless the copy merged with something since.
- `display_type(TYPE_NEW)`: renders with `ty_display`; in underline mode (= the `.types` traversal) with
  `TyDisplay.unqualified_names`, because always qualifying (`m1.C`) was wrong more often there. Both are guesses until
  symbols know what's accessible from where (plan 03).
- Single-member enums: `E.x` prints as `E` (right for `.types`, wrong in error messages where tsc shows the fresh
  literal type `E.x`). Needs fresh/regular literal types. `operatorAddNullUndefined` regressed for it. Since 4b3
  `ty_widened_literal` widens an enum member to its enum (inference, declarations, error messages).

## Calls, inference and property access on the new core (plan 02 stage 4b1): `ty/call.c`, `ty/infer.c`, `ty/access.c`
The algorithms are meant to stay; what's listed is where they lean on the legacy checker or stop short of tsc.
- `infer_from` (infer.c): no reverse mapped types (only the homomorphic `T[K]` template is seen through), no template
  literal inference, alias type arguments are only used for deferred types (there are no variances for aliases), no
  `InferencePriority` beyond naked-type-variable and return-type, type predicates don't exist (parser drops `x is T`).
- `resolve_call` on a union callee: resolved member by member and the results unioned; tsc builds one combined
  signature and reports TS2349 when it can't.
- `report_arity`: no "An argument for 'x' was not provided" related info; the span for too few arguments is the
  callee's `inner_span`, not tsc's name-of-the-property-access rule.
- `signature_resolve` / `structural_signature`: a `this` parameter is dropped from the parameter list and never checked
  against the receiver.
- `instantiate_outer` (call.c): an instantiated generic signature shares its type parameters with its target (the TODO
  in `signature_resolve`), so constraints and defaults have to be pushed through the signature's mapper chain by hand.
- `ty_awaited`: `Promise<T>`/`PromiseLike<T>` by name, as the legacy `type_awaited` does for legacy types.
- `ty_access`: no unique symbol keys, `null`/`undefined`
  members of a union object are skipped silently (TS18047/18048 aren't reported), optional chaining isn't known here.
  `index_type_typecheck` (TS2339 and friends) is still the legacy lookup over views, gated the way it always was.

## Expressions on the new core (plan 02 stage 4b3): `ty/expr.c`, contextual typing, the call passes
`ty_check_expr(expr, contextual)` is meant to stay; what's listed is where it stops short of tsc or leans on legacy code.
- **No fresh/regular literal type pairs.** Every literal type that comes out of an expression counts as fresh: it
  widens at a mutable location (`ty_widened_for_declaration`, `check_for_mutable_location`), so `const c: 1 = 1; let x = c`
  gives `number` where tsc says `1`. No object literal freshness either (no excess property checks, TS2353).
  `widen_nullable` (null/undefined -> any without strictNullChecks) is shallow: top level and array elements only.
- Expression memo (`check_with`, `LazyMemo.context`, `lazy_type_set`): the memo of an `Expr` is whatever the last check
  with a contextual type produced (overload candidates and inference passes overwrite each other; the chosen candidate
  is checked last). `kind_uses_context` decides which expressions get rechecked for a new contextual type. Correctness
  rests on the rule "whoever knows the contextual type checks first": `expr_typecheck` evaluates the expression's own
  type before walking into it, `initialized_binding_typecheck`/return statements/calls check with their context first.
  An expression first reached some other way (the `.types` traversal of a program that wasn't typechecked) is typed
  without context.
- `Checker.return_context`: the contextual type of `return` expressions is checker state saved/restored around
  function bodies (`function_impl_enter_return_context`), because `statement_evaluate_return_type` has no parameter
  for it. `FunctionImpl.evaluating_ret` is still a write into the AST during checking.
- Functions are still legacy underneath: `function_impl_evaluate_type` builds a legacy callable (memo of the function's
  LazyType), which the new core lowers by node; contextual parameter types get in through `KEY_CONTEXT_PARAM` links read by
  `initialized_binding_evaluate_assigned_type`. `statement_evaluate_return_type` unions return types with the legacy
  `type_composite` (pairwise strict-subtype reduction, lowering each time). Stage 5 should give functions a real `Ty`.
- `assign_contextual_signature`: assigned once per function expression (tsc's ContextChecked), from the first contextual
  type that has a usable signature. A union context takes the first member signature (tsc: only if they agree), generic
  contextual signatures aren't instantiated (their type parameters leak into the parameter types), no contextual
  `this`, and `is_arity_smaller` is the only filter.
- `infer_type_arguments` second pass: before each context-sensitive argument *every* type parameter that has candidates
  is fixed (tsc fixes the ones the contextual parameter type mentions). Inference from the contextual return type only
  happens when that type has no type variables (no outer inference context, no return mapper). Still no reverse mapped
  types: `contravariantOnlyInferenceFromAnnotatedFunction`, `reverseMappedTypeInferenceSameSource1` and friends used to
  pass because object literal arguments only had to be *comparable*; that mitigation is gone.
- `resolve_call` still runs twice per call (type, then errors) and nothing is cached per `CallExpr`; argument memos make
  the second run cheap but not free.
- `ty_non_nullable` of a type parameter or deferred type whose constraint allows null/undefined returns the stripped
  *constraint* (tsc: `T & {}`), which loses `T`.
- `ty_unresolved`: an unresolved type name lowers to an error-`any` that prints as the written name (tsc does the same);
  it's a `TY_INTRINSIC` with `TYF_ANY | TYF_ERROR`, interned by name in the literal table.
- `check_object`: computed keys that aren't string/number literals or plain string/number/any are **left out of the
  type** (unique symbols, unions of literals). A setter-only property takes its parameter type, no `this` typing for
  methods, duplicate names: last one wins silently. Spread copies the apparent type's properties (no optionality
  handling, no generic spread).
- `check_array`: an empty array literal is `any[]` (tsc: `never[]` under strictNullChecks, evolving `any[]` for an
  unannotated variable). Spread elements of non-arrays are `any`. Tuples only from a tuple-like contextual type or
  `as const`.
- `compute`: `typeof x` is `string` (tsc: the union of typeof results), untagged templates are `string` even in a const
  context, tagged templates take the return type of a lone non-generic signature (no call resolution), `yield` is `any`,
  arithmetic is always `number` (no bigint), `+` keeps the legacy rules (`TY_RELATION_ASSIGNABLE_STRICT_NULLS`).
  `satisfies` contextually types its operand but the result isn't narrowed by it.
- `ty_apply_type_arguments` (instantiation expressions): maps the type parameters of every signature that takes at
  least that many type arguments; signatures that don't fit aren't dropped, and nothing reports TS2635.
- `lazy_type_evaluate` moves the evaluated legacy type into the memo instead of clone + drop when it is its own
  resolution: the new core may already have lowered nodes inside it *by address* (class field with a conditional type
  annotation crashed under valgrind otherwise). With `LowerCtx.transient` gone, **any legacy object type
  (TYPE_INTERFACE/TYPE_CALLABLE) that is lowered and then freed is a dangling node key**; the remaining producers of
  such temporaries are `function_impl_into_type`, `class_decl_evaluate_structure` and legacy clones of views, none of
  which should reach a relation. Valgrind is the only guard.
- `check_assignable_or_error_internal`: both sides are still lowered on every call (cheap now: `TYPE_NEW` or parsed
  nodes). The head line comes from the new core (`ty_report_source_cstr`). A source that lowers to `any` is still let
  through even to `never`.
- `baseline_test_traversal.c`: the `extends Base` line shows the return type of the base's first construct signature.
  Known display regressions of this stage: a namespace import prints structurally instead of `typeof a`
  (`elidingImportNames`), `this: void` parameters don't print (`contextualTyping24`), qualified names in the type of an
  assignment expression (`recursiveGenericUnionType1`).
- `is_construct_signature_start` (parser): in a type, `new` followed by `(` or `<` is a construct signature. A method
  that is really called `new` with a type parameter list (`new<T>(): void` meaning a method) is misparsed now; tsc
  treats that as a construct signature too.

## New type core (`src/ty/`, plan 02). Everything here is about scaffolding or a known gap
- `class_base`: the base class is found syntactically (`extends Name`, `extends Name<Args>`, `extends ns.Name`); tsc
  evaluates the expression and takes the return type of its construct signatures (mixins, `extends f()`). The static
  side inherits properties from the base's static side and, without a constructor of its own, its construct signatures.
- `builder_add_class_items`: no parameter properties (`constructor(public x)`, the parser doesn't even keep the
  modifier), no `this` type parameter, no accessibility (private/protected are ordinary members), computed member names
  only if the legacy item has a `name_string`. Members of a class come from `LazyType`s the legacy checker evaluates
  (field initializers, method bodies) through arena `TYPE_LAZY` wrappers (`arena_lazy_wrapper`).
- `ty_resolve_enum` / `evaluate` (enums.c): no template literals or `const` variable references in initializers, members
  of ambient enums without initializer auto-increment (tsc: computed). Declaration-merged enums don't get this far
  (`declaration_init` refuses a second namespace). String-named members print as `E.b c`, not `(typeof E)["b c"]`.
- `relate.c` gaps: no excess property checks / freshness, no weak type check, no private/protected/nominal class
  rules, no discriminated-union decomposition of the source, no optionality of parameters (`undefined` isn't added
  under strictNullChecks), signatures/index signatures of a source intersection aren't combined (properties are),
  `this` types. `compare_signatures`: generic source signatures are unified positionally with same-arity generic
  targets, else erased to `any` (tsc instantiates the source in the context of the target; needs inference).
  Array<S> -> ReadonlyArray<T> is a shortcut on the element types (`structured_related`).
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
  `Exclude<keyof T, K>` distribute inside `Omit`). The composed mapper is not restricted to the type parameters the
  root mentions, and the intern key is still the instantiated operands (+ root), so the stored mapper is whichever
  got there first. Equivalent for every parameter the root mentions.
- `ty/shadow.c` and the `ty_shadow_record_*` hooks (`type_typecheck`, `*_decl_typecheck`, `program_typecheck`): the
  only thing that runs the new core today. Global recording state, one program at a time. Deleted in stage 5.
- `ty_internal.h: legacy_skip_wrappers`, `legacy_expr_type`, `resolve_declared_members`: const-cast the AST to call
  `lazy_type_get`. Harmless now (the memo is in the checker), the casts go when `lazy_type_get` takes a const pointer.
- `lower_legacy_resolution`: `typeof x` and binding-derived indexed types lower whatever the legacy checker resolved
  them to. Must never be used for type operators (conditional/keyof/indexed): forcing the legacy checker to resolve
  one nobody used blew the stack in `recursiveConditionalTypes`.
- `ty_keyof` of primitives and `ty_indexed` on anything but plain props / index signatures / tuple and array
  elements stay deferred.
- `registered_type_param`: `infer X` and mapped-type keys are `DECLARATION_TYPE` declarations, indistinguishable
  from aliases unless the conditional/mapped type that introduces them was lowered first (true whenever lowering
  starts from the enclosing declaration or annotation, which is the only way they're in scope).
- `lower_identifier`: `this` types and a bare generic class name inside its own body lower to the class applied to
  its own type parameters; there is no `this` type parameter yet. The `object` keyword is recognised by being a
  builtin declaration named "object".
- `lower` (composites): skips the `any` placeholder `parse_type` makes for a leading `&`.
- `lower_tuple`: rest elements keep the written type (`...T[]` holds `T[]`), tsc holds the element type. Tuple
  targets have no members yet (`length`, numeric props, array methods).
- `collect_free_params` / `Ty.anonymous.free_params`: which outer type parameters an anonymous type's node mentions,
  found by walking the node when the type is created. Parameters declared inside the node (signatures, mapped keys,
  `infer`) are taken out at the end rather than tracked by scope, and it gives up (`free_params_unknown`, = assume all) on `typeof`, `this`, computed keys and
  anything the legacy checker evaluated rather than parsed. `TyMapper` is always flat (no composed/merged forms yet,
  inference will want them). `LowerCtx.generic` is only a "don't bother walking" hint; shadow mode sets it everywhere.
- `signature_resolve` (instantiated): keeps the target's type parameters instead of making fresh ones. Return types
  are lazier than the rest (`signature_return_type`), because they're often the legacy checker's inferred return type.
- `builder_add_interface`: index signature and mapped type operand types are lowered eagerly (property and signature
  types are lazy); computed keys that aren't string/number literals are dropped (unique symbols).
- `qualified_name`: declared types always print with their full namespace path (what tsc does in error messages);
  tsc's `.types` baselines qualify only as far as needed from the enclosing declaration. Needs symbols (plan 03).
- `resolve_declared_members` (namespaces): the members of a namespace value are still the structural type the legacy
  checker made (`Scope.value_interface`).
- `ty_union`: no subtype reduction, doesn't drop `null`/`undefined` without strictNullChecks, no fresh/regular
  literal pairs. No unique symbol types. TS2456 is only a TODO.
