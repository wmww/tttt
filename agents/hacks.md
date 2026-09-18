# Hacks
Suboptimal-but-working code. Prefer function names over line numbers.

- `current_checker` / `current_lazy_slots` (thread locals, `checker_make_current`): how the AST-side walk
  (`*_typecheck`, `*_evaluate_type`, `ty_of_node`, the `Ty const*` displayable) finds its checker. Stays set after
  `program_typecheck` because the test traversal and debug display evaluate types afterwards. The type core itself
  takes a `Checker*`; the walk should get one too (a check context with checker + errs + options).
- `lazy_slot_new`: LazyType slot numbers come from one process-wide atomic counter and are never reused, and
  everything that might need a memo takes one eagerly (every transient `scope_init`, every `type_identifier` made
  during checking). A checker's slot table is sparse so this only costs page fragmentation (bench RSS +6%), and it
  panics after 1G slots. Per-file numbering (plan 02 §2) didn't fit because Declarations, Scopes and check-time
  identifier types belong to no file. Slots hold a `Ty*` now and check-time clones are gone, so this can be
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
- `baseline_test_case.c: apply_setting`: harness-only directives other than `@filename` and `@noTypesAndSymbols` are
  ignored (`@libFiles`, `@noImplicitReferences`, `@currentDirectory`, `@link`/`@symlink`, `@baselineFile`, a
  `tsconfig.json` among the test's files). Bad option values are ignored too (tsc's harness throws).
- `baseline_test_result_run`: with `@noTypesAndSymbols` the symbols and types components count as *passed*, so the
  per-component totals include ~900 free passes. A test without types/symbols baselines is run before we know
  whether it should have had any.
- `baseline_test_cache_get_program_with_libs`: one cached lib `Program` per distinct lib list (dozens per suite run,
  each with the DOM in it), never evicted. An unknown lib name is logged at info level and skipped, no TS6046.
- `display_type` depth guard (`DISPLAY_TYPE_MAX_DEPTH`): the syntax display has no cycle detection. Nodes can't be
  cyclic any more (the structural namespace types that were are gone), so this is probably dead; check and delete.
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


## The AST-side walk and what's left of the old type world
- **`TYPE_LAZY` / `TYPE_REF` / binding `TYPE_INDEXED` are still `TypeNode` tags** (plan 02 wanted them gone). They're
  the links name resolution makes between a `Declaration` and what gives it its type (`Declaration.value_type`,
  `MergedType.merge_list`, `Binding.assigned_type`), and lowering follows them. They're never cloned semantically or
  resolved any more, so they're harmless, but they belong in a symbol (plan 03: a symbol's declarations + a
  `ty_type_of_symbol`), not in the syntax tree's type.
- `MergedType` / `LAZY_TYPE_MERGED` / `LAZY_TYPE_EVALUATED`: a declaration's type and value sides are each a list of
  nodes plus a LazyType (`declared_owner` / `value_owner` say which side and whose). With one entry the LazyType is
  `LAZY_TYPE_EVALUATED` and lowers the entry on every get (no memo); `node_skip_to_syntax()` and
  `baseline_test_case_add_type_node()` look through it to find the written type. Same fate as above.
- `CallableType` (`TYPE_CALLABLE`) still has its own `generic`/`args`/`ret` next to the `FunctionImpl` it owns, filled
  in by `callable_type_resolve_names`. `Signature.node` reads those, `Signature.function` reads a `FunctionImpl`
  directly: two ways to resolve a signature where `CallableType { FunctionImpl* }` would leave one.
- `type_clone` (26 callers) survives for name resolution copying declarations (`declaration_clone_in_place`,
  `decl_props_clone`, ports). A cloned `IdentifierType`/`CallableType` is lowered like an original, keyed by its own
  address, so the copy of an interface-typed declaration is a second anonymous `Ty` for the same written type.
- `ty/check.c: check_access` (TS2339 and friends): a missing property is only reported under
  `noUncheckedIndexedAccess` and for dot access, the gate the legacy checker had. Everything is in place to report it
  honestly (`ty_access` returning null), but it floods until narrowing and apparent types of everything are right.
  A source that is `any` is let through to anything, even `never` (`check_assignable`).
- `TY_RELATION_ASSIGNABLE_STRICT_NULLS`: what the operator checks in `expr.c`/`ty/expr.c` mean by "extends number"
  (null/undefined don't, whatever strictNullChecks says). tsc checks operands differently (TS18050, never emitted here).
- `function_impl_typecheck_signature` TS2370: skipped for `...args: infer A` by looking at the written node.
- `generic_params_expr_typecheck` (`f<A>` outside a call): constraints are checked against the first signature only.
- `for_each_loop_statement_evaluate_var_type`: `for..in` is `string`, `for..of` only knows arrays and strings (TS2495
  for everything else, iterables are `any`).
- `ty_function_return_type`: `void` when there's no `return` (tsc: `never` if the end isn't reachable), generators are
  `any`, async wraps in `Promise<T>` without awaiting `T`. A function whose return type is being inferred further up
  the stack is `any` without TS7023.
- `lower` TYPE_TYPEOF: `typeof x` is the type of the expression `x` (it used to be `any`). tsc takes the *declared*
  type of an identifier, not the narrowed one; no difference until narrowing exists.
- `baseline_test_case_add_type_node`: a declaration whose type is a written type *name* (`var x: X.Y.Z`, an enum
  member) shows as written (the legacy syntax printer), everything else through `ty_display` with unqualified names.
  Measured: +21 full tests over never qualifying. The real rule needs symbol accessibility (plan 03).
- `ty_merged_value_type`: the merged type copies the first named entry's `declared` struct (a class static side or an
  enum object) and gives it all entries' members; it's a second `Ty` next to the unmerged one. Non-exported namespace
  members are properties too (`Scope.declarations`, as the legacy structural type had it).

- `canonical_declaration`: import aliases and re-exports are copies of a Declaration; identity goes by the declaration
  the AST node itself registered (found through `Declaration.nodes`), unless the copy merged with something since.
- Single-member enums: `E.x` prints as `E` (right for `.types`, wrong in error messages where tsc shows the fresh
  literal type `E.x`). Needs fresh/regular literal types. `ty_widened_literal` widens an enum member to its enum.

## Calls, inference and property access (`ty/call.c`, `ty/infer.c`, `ty/access.c`): `ty/call.c`, `ty/infer.c`, `ty/access.c`
The algorithms are meant to stay; what's listed is where they stop short of tsc.
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
- `ty_awaited`: `Promise<T>`/`PromiseLike<T>` by name.
- `ty_access`: no unique symbol keys, `null`/`undefined`
  members of a union object are skipped silently (TS18047/18048 aren't reported), optional chaining isn't known here.

## Expressions (`ty/expr.c`), contextual typing, the call passes
`ty_check_expr(expr, contextual)` is meant to stay; what's listed is where it stops short of tsc.
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
  function bodies (`function_impl_enter_return_context`), because `statement_collect_return_types` and
  `statement_typecheck` have no parameter for it.
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
  arithmetic is always `number` (no bigint), `+` goes by `TY_RELATION_ASSIGNABLE_STRICT_NULLS`.
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
- `class_base`: the base class is found syntactically (`extends Name`, `extends Name<Args>`, `extends ns.Name`); tsc
  evaluates the expression and takes the return type of its construct signatures (mixins, `extends f()`). The static
  side inherits properties from the base's static side and, without a constructor of its own, its construct signatures.
- `builder_add_class_items`: no parameter properties (`constructor(public x)`, the parser doesn't even keep the
  modifier), no `this` type parameter, no accessibility (private/protected are ordinary members), computed member names
  only if the item has a `name_string`. Field and accessor types come from their LazyTypes through arena `TYPE_LAZY`
  wrapper nodes (`arena_lazy_wrapper`), because `Prop.node` is the only lazy source a property has.
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
- `lazy_type_get((LazyType*)...)` const casts (`ty/expr.c`, `ty/lower.c`, `ty/members.c`): harmless, the memo is in the
  checker; they go when `lazy_type_get` takes a const pointer.
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
  `infer`) are taken out at the end rather than tracked by scope, and it gives up (`free_params_unknown`, = assume all) on `typeof`, `this`, computed keys,
  `TYPE_LAZY` links, and for every function and merged value type (a body can mention any type parameter in scope). `TyMapper` is always flat (no composed/merged forms yet,
  inference will want them). `LowerCtx.generic` is only a "don't bother walking" hint.
- `signature_resolve` (instantiated): keeps the target's type parameters instead of making fresh ones. Return types
  are lazier than the rest (`signature_return_type`), because they're often inferred from a body that is being checked.
- `builder_add_interface`: index signature and mapped type operand types are lowered eagerly (property and signature
  types are lazy); computed keys that aren't string/number literals are dropped (unique symbols).
- `qualified_name`: declared types always print with their full namespace path (what tsc does in error messages);
  tsc's `.types` baselines qualify only as far as needed from the enclosing declaration. Needs symbols (plan 03).
- `ty_union`: no subtype reduction, doesn't drop `null`/`undefined` without strictNullChecks, no fresh/regular
  literal pairs. No unique symbol types. TS2456 is only a TODO.
