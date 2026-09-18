# Hacks
Suboptimal-but-working code. Prefer function names over line numbers.

- `type_pair_cache.h`: relation cache keyed by a string rendering of both types (`hash_key_display`). Slow and
  fragile (two distinct types that print the same collide). Worse: it lives in `TypeCtx`, which is created per
  top-level `type_extends` call, so it only breaks cycles and never caches across queries. Goes away in plan 02 stage 3.
- `LazyType` memos live in AST nodes and the harness shares the lib AST between test programs, so checker results
  leak from one test into the next (harmless today because evaluated types are self-contained trees). Plan 02 stage 2.
- `type_pair_cache_entry_update`: uses a key whose ownership was already handed to the map ("hacky but it works").
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
  structural interface; without it, display (and the string-keyed relation cache) recursed forever on
  `namespace A { export var a = A }`. Any other structural walk over such a type (eg `type_extends`) is still only
  saved by the recursion depth limit. Type identity in the new type core is the real fix.
- `parse_jump_target` / `Statement.jump_label`: labels are parsed and shown in the types baseline, but never checked
  (TS1116 unknown label, TS1107 crossing function boundary, TS7028 unused label, duplicate labels). `inside_label`
  in `BlockTypecheckCtx` just permits any labeled `break` inside any labeled statement.
- `run_baseline_test_case: path_is_script`: virtual files that aren't TS/JS by extension are left out of the program
  entirely. Right for package.json/tsconfig.json, wrong for `.json` modules under `resolveJsonModule` (5 tests).
- `EnumDecl.is_const`: recorded, nothing uses it.
- `type_extends_internal`: recursion depth limit lowered 400 -> 100 purely to stop recursive generics (`Either<L, A>`
  style classes) from eating 38 GB: each level clones-and-substitutes bigger types and renders them to strings for the
  cache key. Returns `true` (assignable) when hit, 31k times per suite run. Not a fix; the type core redesign is.
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


## New type core (`src/ty/`, plan 02). Everything here is about stage-1 scaffolding or a known gap
- `ty/shadow.c` and the `ty_shadow_record_*` hooks (`type_typecheck`, `*_decl_typecheck`, `program_typecheck`): the
  only thing that runs the new core today. Global recording state, one program at a time. Deleted in stage 5.
- `LazyType.origin` / `lazy_type_origin`: remembers the tag a LazyType had before it became `LAZY_TYPE_EVALUATED`, so
  `legacy_entry_interface_decl` can `container_of` from a Declaration's merge list back to its `InterfaceDecl`.
  There is no proper Declaration -> AST node link. Stage 2 (slots) or plan 03 (symbols) should give one.
- `ty_internal.h: legacy_skip_wrappers`, `legacy_expr_type`, `resolve_declared_members`: const-cast the AST to call
  `lazy_type_get` (checker state still lives in the AST until stage 2).
- `resolve_declared_members`: only interfaces get their members from their own declarations (+ bases through a
  mapper). Class instance/static sides, enum objects and namespaces take the members of the structural type the
  legacy checker evaluated (`type_resolve` on the entry). Inherited class members, `this`, accessibility modifiers
  etc. are therefore whatever the legacy type says. Stage 4a.
- `lower_legacy_resolution`: `typeof x` and binding-derived indexed types lower whatever the legacy checker resolved
  them to. Must never be used for type operators (conditional/keyof/indexed): forcing the legacy checker to resolve
  one nobody used blew the stack in `recursiveConditionalTypes`.
- `ty_conditional`: never resolves (needs relations + inference, stage 3), not even for non-generic operands; no
  distribution over unions. `ty_keyof` of primitives and `ty_indexed` on anything but plain props / index signatures /
  tuple and array elements stay deferred as well.
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
- `signature_resolve` (instantiated): keeps the target's type parameters instead of making fresh ones.
- `builder_add_interface`: index signature and mapped type operand types are lowered eagerly (property and signature
  types are lazy); computed keys that aren't string/number literals are dropped (unique symbols).
- `qualified_name`: declared types always print with their full namespace path (what tsc does in error messages);
  tsc's `.types` baselines qualify only as far as needed from the enclosing declaration. Needs symbols (plan 03).
- `ty_get_prop`: linear scan. `ty_union`: no subtype reduction, doesn't drop `null`/`undefined` without
  strictNullChecks, no fresh/regular literal pairs. Deferred references inside their own alias
  (`type Json = string | Json[]`) hit the in-flight check and become the error type instead of tsc's deferred type
  reference. No unique symbol types. TS2589/TS2456 are only flags/TODOs.
