# Hacks
Suboptimal-but-working code. Prefer function names over line numbers.

- `IndexedType`/`KeyofType`/`TernaryType.has_cache/cached`: the last checker state that is written into AST-owned
  `Type`s during typechecking, including the lib AST the harness shares between test programs (3 tests still leave
  net allocations behind in it: `excessPropertyCheckWithEmptyObject`, `intersectionsOfLargeUnions`,
  `modularizeLibrary_ErrorFromUsingES6ArrayWithOnlyES6ArrayLib`). Goes away when type operators resolve in the new
  core (plan 02 stages 3-5), not worth giving slots.
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
- `baseline_test_case.c: load_test_source`: compiler option directives handled as ad-hoc string compares; almost
  all options ignored.
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
- `object_expr_evaluate_type`: computed keys become a named member if the key type is a string/number literal, a
  `[x: string]`/`[x: number]` index signature if it's string/number/any, and are **silently left out** otherwise
  (unique symbols, unions of literals). Accessor pairs are merged by name; a setter-only property is writable `any`-ish
  (first parameter's type). No `this` typing for object literal methods, no contextual typing of members.
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
  `Promise` looked up by name during name resolution (`any` if the lib has none - which is the default ES5 lib set in
  the harness); returning a promise nests instead of flattening; returns aren't checked against the declared type;
  no TS1064/TS2705. `type_awaited` only unwraps types literally named `Promise`/`PromiseLike`.
- `await_is_operator`: decides keyword-vs-identifier from `ParserShared.in_async`/`function_depth` plus a peek at the
  next character. Top-level await never reports TS1375/TS1378 (needs module detection + `@module`/`@target`).
  Class field initialisers and parameter defaults count as "top level"/inherit the enclosing function's context.
- `EXPR_SPREAD`: typed as the operand's element type if it's an array, else `any` (tuples, iterables, strings TODO).
  `type_typecheck_call` stops checking argument count and types at the first spread argument (no TS2556).
- `EXPR_TEMPLATE`: substitutions are checked as expressions and the template is `string`; a tagged template's type is
  the tag's return type only if the tag is a plain non-generic function (no overloads, no generic inference, no
  argument checking, no `TemplateStringsArray`), else `any`. Template literal *types* are untouched.
- `satisfies` (`AsTypeExpr.is_satisfies`): assignability check + TS1360 only, the type doesn't contextually type the
  expression, and the source is shown widened.
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
- `type_extends` / `type_overlaps` / `check_assignable_or_error_internal` (`lower_for_relation`): both sides are
  lowered on every call, since expressions still evaluate to legacy types. Whatever is passed may be a temporary that
  gets freed, hence `LowerCtx.transient`: `ty_structural_from_legacy` lowers such object types eagerly and interns them
  by shape instead of by node, keeping pointers only to what `legacy_stable_view` vouches for (parsed nodes, memos of
  slotted non-identifier LazyTypes, via arena copies of the `TYPE_LAZY` wrapper). The rule "parsed or slotted = outlives
  the checker" is implicit and unverified beyond valgrind runs. Builds members in the arena before knowing whether
  the shape already exists (garbage per call). Overloads inside a temporary keep only the first signature. All of
  this dies when expressions evaluate to `Ty` (stage 4b).
- `check_assignable_or_error_internal`: the head line of TS2322/TS2345 is still the legacy rendering of the legacy
  types (with `type_abstract_literals`), the elaboration lines under it come from `ty_display`. `TyDisplay.widen_literals`
  imitates the abstraction for temporaries. A source that lowers to `any` is let through even to `never`, because the
  legacy checker says `any` for whatever it can't type (missing initializer...).
- `legacy_infer_extends` (+`_more`, `type_contains_inferred`): the old structural relation survives privately in
  `type.c` for one job, resolving legacy conditional types that contain `infer` (the results are needed as legacy
  types). No cache; cycles are cut by a coarse (declaration, declaration) in-progress list that assumes "extends",
  plus the old depth limit. Conditional types without `infer` pick their branch with the new relation. Dies in 4b.
- `TY_RELATION_ASSIGNABLE_STRICT_NULLS` / `type_extends_strict_nulls`: what the legacy operator checks in `expr.c`
  mean by "extends number" (null/undefined don't, whatever strictNullChecks says). tsc checks operands differently
  (TS18050, never emitted here).
- `type_composite_internal`: union/intersection simplification of legacy types runs the new strict-subtype relation
  pairwise per added member (lowering both each time). Quadratic; `ty_union` itself still has no subtype reduction.
- `generic_infer_callable_types` / `inference_gave_up`: a type parameter whose inference ran into a "not yet
  implemented" shape (or a named generic like `Box<T>`) becomes `any` instead of `unknown`, so that the (now
  correct) relation doesn't turn every weak inference into a TS2345/TS2322.
- `callable_type_typecheck` TS2370: skipped for `...args: infer A` by looking at the written node.

## New type core (`src/ty/`, plan 02). Everything here is about scaffolding or a known gap
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
- Enums: still the nominal placeholder. `is_simple_related` lets members of one enum assign to each other (no
  declaration widening yet) and treats a member without initializer (NaN placeholder value) as equal to any number
  literal. `enum_has_kind` asks the legacy structural type whether an enum is numeric.
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
  primitives. No `as` clauses, no array/tuple homomorphism, `-?` doesn't strip `undefined`, modifiers come from
  `MappedInfo.modifiers_type` only when the key type was written as `keyof T`.
- `class_base_type`: inherited class members, reached by `container_of` from the instance LazyType to the `ClassDecl`
  and only for `extends Name` / `extends Name<Args>`. The static side inherits nothing yet.
- `ty/shadow.c` and the `ty_shadow_record_*` hooks (`type_typecheck`, `*_decl_typecheck`, `program_typecheck`): the
  only thing that runs the new core today. Global recording state, one program at a time. Deleted in stage 5.
- `legacy_entry_interface_decl`: `container_of` from a Declaration's merge list entry (a `TYPE_LAZY` whose LazyType
  has tag `LAZY_TYPE_INTERFACE_DECL`) back to its `InterfaceDecl`. There is still no proper Declaration -> AST node
  link; stage 2 didn't add one (nothing needed it yet). Stage 4a needs it for classes and enums, or plan 03 does it.
- `ty_internal.h: legacy_skip_wrappers`, `legacy_expr_type`, `resolve_declared_members`: const-cast the AST to call
  `lazy_type_get`. Harmless now (the memo is in the checker), the casts go when `lazy_type_get` takes a const pointer.
- `resolve_declared_members`: only interfaces get their members from their own declarations (+ bases through a
  mapper). Class instance/static sides, enum objects and namespaces take the members of the structural type the
  legacy checker evaluated (`type_resolve` on the entry). Inherited class members, `this`, accessibility modifiers
  etc. are therefore whatever the legacy type says. Stage 4a.
- `lower_legacy_resolution`: `typeof x` and binding-derived indexed types lower whatever the legacy checker resolved
  them to. Must never be used for type operators (conditional/keyof/indexed): forcing the legacy checker to resolve
  one nobody used blew the stack in `recursiveConditionalTypes`.
- `ty_keyof` of primitives and `ty_indexed` on anything but plain props / index signatures / tuple and array
  elements stay deferred.
- `enum_member_type`: enum members without an initializer have no value in the legacy checker, so they're literal
  types with a NaN placeholder value (identity by member declaration still holds). `TY_DECLARED_ENUM` is a nominal
  stand-in; tsc's enum type is the union of its member literals (matters for `E | E.A` reduction, printing runs of
  members as the enum). `ty_declared_type_of` reaches the member from `EnumCase.typeof_declaration` by `container_of`.
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
  found by walking the node when the type is created. Doesn't track scopes (a signature's own type parameters count
  as mentioned, harmless), and gives up (`free_params_unknown`, = assume all) on `typeof`, `this`, computed keys and
  anything the legacy checker evaluated rather than parsed. `TyMapper` is always flat (no composed/merged forms yet,
  inference will want them). `LowerCtx.generic` is only a "don't bother walking" hint; shadow mode sets it everywhere.
- `signature_resolve` (instantiated): keeps the target's type parameters instead of making fresh ones. Return types
  are lazier than the rest (`signature_return_type`), because they're often the legacy checker's inferred return type.
- `builder_add_interface`: index signature and mapped type operand types are lowered eagerly (property and signature
  types are lazy); computed keys that aren't string/number literals are dropped (unique symbols).
- `qualified_name`: declared types always print with their full namespace path (what tsc does in error messages);
  tsc's `.types` baselines qualify only as far as needed from the enclosing declaration. Needs symbols (plan 03).
- `ty_union`: no subtype reduction, doesn't drop `null`/`undefined` without strictNullChecks, no fresh/regular
  literal pairs. No unique symbol types. TS2456 is only a TODO.
