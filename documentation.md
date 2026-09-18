# tttt Documentation

## Programs
The repository contains three programs.

### tttt
The actual typechecker, drop-in replacement for tsc. See `build/tttt --help` for usage.

Can be compiled and run with `./run.sh`. This makes a debug build and runs it under Valgrind, so won't be fast.

Completely written in C. Code lives in [src](src/). The only dependency is [cleat](https://codeberg.org/sphi/cleat), an MIT-licensed standard library for C which comes with its own vendored copy of the STC container library (no relation to the swc-related project of the same name). It's included in this repository under [thirdparty/cleat](thirdparty/cleat/), and automatically compiled by the build system. `thirdparty/refresh.sh` re-downloads it at the pinned commit.

### baseline_tests
Runs tttt against TypeScript's official test suite. See `build/baseline_tests --help` for usage.

Can be compiled and run with `./test.sh` (debug build, under Valgrind, takes minutes) or `./fasttest.sh` (release build, not under Valgrind, takes seconds). Features like regex filtering and displaying diffs are supported, see `./test.sh --help` for details.

The tests target includes most `tttt` code, as well as additional C code in [src/test](src/test/).

### diffapp
A graphical app that allows you to edit TypeScript code and see tsc's and tttt's test output for that code in real time. It watches for changes to tttt's source code, and recompiles and re-runs tttt on save.

Generally run via `./diffapp.sh`. A test name or source file can be specified. If there's already a running instance the script will send the given test to that instance.

It's a nodejs web app with an html/css/js frontend in [test/diffapp](test/diffapp/). It's intended to run locally (basically uses the browser as a GUI toolkit only). The tttt integration lives in [src/test/diffapp_runner.c](src/test/diffapp_runner.c).

## Architecture
To keep development fast, LoC down, Tess sane and (in specific cases) tttt fast, we do things the most straightforward way wherever possible.

### AST
At the core of tttt is a TypeScript AST (Abstract Syntax Tree). This is a data structure that maps to parsed source code, and on which the typechecker runs. AST nodes are:
- `Block` - sequence of statements, often surrounded by `{` `}`s
- `Statement` - chunk of code that does not result in a value, often each on their own line or separated by `;`
- `Binding` - assignments and function parameter lists use bindings. They are often simply identifier names, but can be more complicated in the case of destructuring
- `Expr` - nestable expression, such as a function call or value literal
- `Type` - specified type. Note during typechecking many types are created, passed around and dropped. These are represented by the same `Type` struct, but are not considered part of the AST (relevant for memory management, see below)

This list is not exhaustive as there are other structs owned by these. Each of these structs has it's own `.h` and `.c` files. Look through them for more details.

There is no AST node struct, interface or visitor. The AST is simply a *concept* that includes these structs, and is relevant for reasoning about the architecture.

### Span
A span is a reference to a section of source code, often used for error messages. Spans don't own anything, and don't need to be dropped. Identifier names are generally stores as spans.

### Display
tttt has its own rendering pass for AST nodes in [src/display.c](src/display.c), driven by a `Display` struct (`display_type()`, `display_expr()`, ...). It stays separate from [cleat's display system](thirdparty/cleat/docs/display.md) because it's on the typechecker's hot path (every failed type comparison renders both types into a message) and because it has rendering modes cleat knows nothing about: `debug` (the developer-facing tree), `brief` (the same, truncated), `parsed` and `underline` (used by the test runner to underline a type within its rendering).

The two systems are bridged in [src/display.h](src/display.h). `declare_ast_display()`/`impl_ast_display()` generate a cleat display function per AST node, so `Span`, `Type const*`, `Expr const*`, `Error const*` and friends are all in `DISPLAYABLES` and can be passed straight to `println()`, `cstr_d()`, `panic()`, `log_error()`, `error_ctx_error_d()` and so on. A plain sequence gets the TypeScript-diagnostic form, `debug_d(x)` gets the developer form and `brief_d(type)` gets the truncated one.

Everything that isn't an AST node goes through cleat's display system directly; `src/` has no `printf`-style format strings left apart from JavaScript's number-to-string precision in `value.c`.

### Parser
The parser (including a minimal custom parsing framework) lives in `parser.c`. We do not compile grammars, combinate or tokenize. We simply take a const char* and use it to build an AST. Like real men.

See dedicated section below for details.

### Program
The `Program` struct owns the source code buffers and ASTs of all loaded files, manages imports, etc.

### CompilerOptions
`CompilerOptions` ([src/compiler_options.h](src/compiler_options.h)) is what tsc calls the same: every boolean option (one X-macro list, with which ones belong to the `strict` family), `target`, `module`, `moduleResolution`, `jsx`, `moduleDetection` and `lib`. `compiler_options_set(options, "strictNullChecks", "false")` takes names and values as they're written in a tsconfig, on the command line (`tttt --target es2020 --strictNullChecks false`) or in a test's `// @target: es2020`; `compiler_options_resolve()` then applies tsc's dependent defaults (`strict` to its family, `module` from `target`, `moduleResolution` from `module`, `useDefineForClassFields` from `target`...), so every field holds the effective value. `compiler_options_lib_files()` gives the standard library files to load: the `lib` list, else the default library of the target (`lib.d.ts`, `lib.es6.d.ts`, `lib.es2016.full.d.ts`...).

Each `Program` owns one (`program_options()`, change it before loading files). Code that parses, resolves names or checks reads the current program's through the global `compiler_options()`, which `program.c` sets around each phase. Most options are parsed but not acted on yet.

### Test configurations
A tsc test whose directives give several values for an option (`// @target: es5,es2015`, `// @strict: *`, `// @module: *,-amd`) is several tests, each with baselines named `name(option=value,other=value)`. `baseline_test_configurations()` (`src/test/baseline_test_case.c`) follows tsc's `getFileBasedTestConfigurations()`: only boolean and enum options vary (a comma in `@lib` or `@types` is a list), `*` means every value of the option, `-x` excludes one, spellings of the same value (`es6`/`es2015`) collapse, option names are sorted. The suite holds one `BaselineTestResult` per configuration; `-f someTest` matches all configurations of a test, `-f 'someTest\(target=es5\)'` one. Tests with `// @noTypesAndSymbols: true` have no `.types`/`.symbols` baselines and are compared on errors alone.

### Scope
A lexical scope, maps identifier names to `Declaration`s.

### ScopeStack
Used for looking up identifier names, scopes can be pushed and popped.

### LazyType
`LazyType` is a struct owned by the AST (and by `Declaration`s and `Scope`s). They're `Type` wrappers that allow a referencing type to be created before the `LazyType` is evaluated. In practice that means it's possible to create a type while in the resolve names phase, but not evaluate what that type actually is until the typecheck phase when all names are resolved.

The process goes something like this:
1. An AST struct, for example `Expr`, contains a `LazyType`
2. In the resolve names phase, it calls `lazy_type_bind(&lazy, LAZY_TYPE_EXPR)`, which sets the tag and gives the LazyType a *slot* number
3. That function or some other function then creates a `Type` with a pointer to its LazyType using `type_lazy()` (the expression's type hasn't and *can't* be evaluated yet since names haven't been fully resolved, so this is the only way to pass around its type)
4. Later, in the typecheck phase, `type_resolve()` is called on the `Type`, which calls `lazy_type_get()`
5. The tag of the LazyType is `LAZY_TYPE_EXPR`, so `lazy_type_evaluate()` uses the pointer to the LazyType to calculate a pointer to its containing `Expr` and calls `expr_evaluate_type()`
6. It then stores the returned type in the LazyType's memo (`LazyMemo`), marks it done and returns a pointer to the evaluated type
7. Future calls to `lazy_type_get()` for the same Expr quickly return a pointer to the evaluated type, without needing to call `lazy_type_evaluate()`

LazyTypes play a critical role in detecting self-referential types. While a LazyType is being evaluated its memo is in an in-flight state. If something tries to evaluate it again in the same call stack it's evaluated type is set to `type_self_referential_error`.

**The memo is not in the LazyType.** The LazyType itself never changes after the resolve names phase: it's a tag and a slot number, and the memo lives in the slot storage (`LazySlots`, a sparse three level table allocated on first touch) of the current `Checker` (`current_checker` / `current_lazy_slots`, thread local). Each `Program` has one checker, created by `program_make_current()`, which `program_typecheck()` calls; call it yourself to look at the types of a program that wasn't typechecked. This keeps the AST read-only while checking, which is what lets many programs share one lib AST (the test harness does) and later lets several checkers work on one AST. Two exceptions, neither of which writes to the AST during checking: `LAZY_TYPE_EVALUATED` LazyTypes carry a type that was known when they were created (`lazy_type_preeval()`), and LazyTypes inside types that get cloned during checking (`identifier_type_clone`) use `LAZY_SLOT_INLINE`, a memo inside the LazyType, because they live and die within one checker.

### The new type core (`src/ty/`)
The `Type` struct above is being split in two ([agents/plans/02-type-core.md](agents/plans/02-type-core.md)): it stays as the *syntax* of a type, and semantic types become `Ty` (`src/ty/ty.h`). The migration is more than half way: **every written type (classes, interfaces, enums, type aliases, and the type operators `keyof`, `T[K]`, conditional and mapped types) is evaluated by the new core**, and all type relations run on it (`type_extends()`, `type_overlaps()` and the assignability errors lower both sides and ask `ty/relate.c`). **Calls, generic inference and property access are resolved by the new core too** (the callee, arguments and indexed type still come from the legacy checker and are lowered; the result goes back as `TYPE_NEW`). Expressions, functions and namespaces are still evaluated by the legacy checker (`type.c` & co); when it resolves a type operator node it gets a checker-owned `TYPE_NEW` back (`ty_legacy_wrapper()`), and a deferred type (`T[K]` with `T` a type parameter) views as its base constraint (`ty_base_constraint()`). The `Checker` also owns the legacy checker's LazyType memos (see above).

- **`TYPE_NEW` and the legacy view** (`legacy_view.c`): a `Ty` travels through legacy code as a `Type` with tag `TYPE_NEW` (owns nothing, cloning copies a pointer, `display_type()` prints it with `ty_display()`). Legacy code that wants to switch on a structural tag calls `type_resolve()` as always, which turns a `TYPE_NEW` into its *view*: one layer of the `Ty` as a legacy type (a `TYPE_INTERFACE` with its members, a `TYPE_UNION`, a `TYPE_ARRAY`...) whose children are `TYPE_NEW` again, built once per `Ty` and owned by the checker. Property and return types inside a view are lazy (`Type.new_.prop` / `.ret_of`). `type_resolve_until_identifier()` stops at a `TYPE_NEW`, except for type parameters, which view as the `TYPE_IDENTIFIER` legacy inference looks for. Legacy substitution (`replace_named_types_with()`) instantiates a `TYPE_NEW` with a mapper. `ty_declaration_is_evaluated_by_new_core()` decides which declarations go this way; `class_decl_evaluate_type()`, `interface_decl_evaluate_type()`, `enum_decl_evaluate_*()`, `merged_type_evaluate_type()` and `identifier_type_evaluate_type()` are where the `TYPE_NEW`s come from.
- **Declarations know their AST nodes**: `Declaration.nodes` (`DeclNode`: class, interface, enum, enum member), filled by `*_add_names()`. The new core builds the members of a declared type from them: every merged interface body, a class's fields/methods/accessors (overloads hide the implementation signature), inherited instance members, and for the static side `prototype`, static members, inherited static members and the construct signatures (the class's own, else the base class's with this class as the return type). Import aliases are copies of a Declaration, so identity goes through `canonical_declaration()`.
- **Enums** (`enums.c`): an enum's type is the union of its members' types with the enum as the union's alias (that's how it prints as `E`, and `formatUnionTypes`-style collapsing prints a full run of members inside a bigger union as `E` too). A member's type is a literal type keyed by (value, enum), so members with equal values share a type, like in tsc; a member whose initializer isn't a compile time constant gets an opaque "computed" type (`TYF_ENUM`). Values come from a small constant evaluator over `Expr` (literals, unary/binary operators, references to other members). The enum object (`typeof E`) has a readonly property per member and the numeric reverse mapping.

- A `Checker` owns an arena, a type id counter and the intern tables. Every `Ty` is immutable once published and interned, so **pointer equality is type identity**: `List<string>` is one pointer no matter how often or where it's written. Ids are handed out in creation order and intrinsics are created in tsc's order, because unions print in id order.
- Intern keys are small arrays of `u64` words (a `KEY_*` tag followed by ids/pointers, `ty_internal.h`) in one open addressing table type (`InternTable`). `Checker.types` holds literals, unions/intersections, references, tuple targets, deferred types and instantiated anonymous types; `Checker.links` holds everything that is *about* an AST node or `Declaration` (declared types, type parameters, anonymous types by node, alias instantiations), so the AST is never written to. `intern_slot()` pointers die on the next insert into the same table; use `intern_get()` and look the slot up again when the value takes interning to compute.
- Kinds: intrinsic, literal (incl. enum members), type parameter, declared (interface, class instance/static side, enum object, namespace value, tuple target), reference (generic declared type + arguments; arrays are references to the global `Array`, tuples to a tuple target), anonymous (type literal / function type by AST node, or `{target, mapper}` instantiation), union/intersection (sorted by id, cheap reductions only, alias is part of the identity like in tsc), and the deferred `keyof`/indexed access/conditional types, whose constructors resolve eagerly when they can. A mapped type becomes ordinary properties and index signatures once its key type is known (`expand_mapped()`).
- **Members are lazy** (`members.c`): `ty_members()` resolves props, call/construct signatures and index signatures on first use; `prop_type()` and `signature_resolve()` lower or instantiate the types inside on first use. That is what makes recursive types legal and cheap.
- **Instantiation never copies** (`instantiate.c`): `ty_instantiate(ty, mapper)` returns `ty` itself unless it has `TYF_COULD_CONTAIN_TYPE_VARIABLES`, rebuilds interned kinds through their constructors, and turns an anonymous type into an interned `{target, mapper}` pair restricted to the type parameters the node actually mentions (`free_params`).
- **Relations** (`relate.c`): `ty_is_related(source, target, relation)` for assignable, comparable, strict subtype (union reduction) and an assignable variant with strict nulls. It follows tsc's structure because the observable results depend on it: simple relations by flags first, then a per-relation cache on the checker keyed by `(source id, target id)`, a "maybe" stack for pairs that are being compared further up (assumed related, made definitive when the outer comparison finishes), a deep-nesting check on the generic target of the types on the stack (what terminates infinitely expanding generics), and for two references to the same generic type a comparison of the type arguments by *measured variance* (`get_variances()`, marker type parameters, cached in `Checker.links`). `ty_explain_unrelated()` reruns a failed check in reporting mode and returns tsc's elaboration chain, outermost line first; the first line is either the generic TS2322 head or something that replaces it (TS2741 missing property...). `TTTT_DEBUG_RELATE=1` logs every top-level query, `TTTT_DUMP_VARIANCES=1` every measured variance.
- **Calls** (`call.c`): `type_call_evaluate_type()`/`type_typecheck_call()` lower the callee and hand over. `resolve_call()` collects the call or construct signatures of the callee's apparent type (a union is called member by member, an intersection through all its members' signatures), and `resolve_with_signatures()` takes the first candidate that fits, like tsc's `resolveCall`: arity (`ParamShape` spreads a tuple rest parameter out, `Args` spreads tuple spread arguments out), type arguments inferred from the arguments, every argument related to its parameter, with a subtype round before the assignable round when there are overloads. When nothing fits the errors are tsc's: TS2554/2555/2575 for arity, the argument error of a lone candidate, TS2769 with one "Overload n of m" branch per candidate otherwise.
- **Inference** (`infer.c`): one walker (`infer_types()`) for generic calls and for `infer X` in conditional types. It keeps covariant and contravariant candidates apart (parameters flip the direction under `strictFunctionTypes`, reference type arguments by measured variance), knows priorities (a naked type parameter in a union gets what the other members didn't match), and records whether the type parameter was only seen at the top level of a parameter type. `inference_call_result()` is tsc's `getInferredType` for calls (literal widening unless the constraint is primitive or the type parameter is at the top level of the return type, common supertype, covariant preferred over contravariant when it fits, `unknown`/default/constraint fallbacks are in `infer_type_arguments()`); `inference_conditional_result()` is union / intersection. `InferParam.fixed` is the seam for the context-sensitive second pass that comes with contextual typing.
- **Property access** (`access.c`): `ty_access(object, index)` for a known index, through apparent types (`String` for strings, the constraint for a type parameter, `Function`/`Object` members), tuple elements, then index signatures. `index_type_evaluate_type()` is a wrapper around it, and so is the indexed access *type* `T[K]` once neither operand is generic (`try_resolve_indexed()`).
- **Conditional types** (`conditional.c`): `ty_conditional()` resolves as soon as the check type isn't generic at the top level: `infer` type parameters are inferred by walking check and extends type in parallel, the extends type is instantiated with the results and related. A deferred conditional type remembers the conditional type it was written as and the mapper that leads from there to it (`Ty.conditional.root`/`.mapper`, tsc's ConditionalRoot), and `ty_instantiate()` always restarts from the root with the composed mapper: that's where distribution happens, when the *written* check type parameter turns out to be a union, however many instantiations later (`Exclude<keyof T, K>` inside `Omit<T, K>`). Branches that mention the alias being resolved are lowered on first use (`TyCondBranch`), like array and tuple nodes in that position (`ty_deferred_reference()`, tsc's deferred type references: `type Json = string | Json[]`).
- **The bridge from the legacy checker** (`lower_for_relation()` in `type.c`, `LowerCtx.transient`): legacy types passed to a relation can be temporaries that are freed right after, so nothing in the new core may keep a pointer into them. Parsed nodes and the memos of slotted LazyTypes are stable (`legacy_stable_view()`) and get lowered lazily by node as usual; any other object type is lowered eagerly and interned by its shape (`ty_structural_from_legacy()`). This goes away when expressions evaluate to `Ty`.
- `lower.c`: `ty_from_node()` is the only producer of `Ty` from syntax; `ty_declared_type_of(Declaration)` gives the type a declaration declares (in-flight detection for circular aliases). `ty_from_legacy()` additionally follows the legacy `TYPE_LAZY`/`TYPE_REF` and is scaffolding.
- **Alias names**: unions, intersections, object types, arrays/tuples (as deferred references), `T[K]` and conditional types written as the body of an alias carry `Ty.alias` + `alias_args`, instantiation instantiates the arguments, and `ty_realias()` renames the instantiation of a generic alias that is itself the body of another alias (`type Bar = Omit<Foo, "c">` prints as `Bar`). A homomorphic mapped type instantiated with an array or tuple becomes an array or tuple (`instantiate_mapped_over_list()`).
- `ty_display.c` prints like tsc's `typeToString()`: alias name, else reference name + arguments (`T[]`, tuple syntax), else declared name, else structure.
- `shadow.c`: `tttt --shadow-types` / `TTTT_SHADOW_TYPES=1` records every type annotation and type declaration the legacy checker typechecks, lowers and prints them all with a fresh `Checker` afterwards, and `log_error`s where the rendering differs from the legacy display of the same node (only where they're meant to agree). `TTTT_SHADOW_STATS=1` adds a summary line per program, `TTTT_SHADOW_DUMP=1` prints every node's type id and rendering, which is the easy way to poke at the new core with a small `.ts` file.

### Engine
tttt includes a JavaScript interpreter that evaluates an AST. It's not fast, standards complaint, tested, complete, or a priority.

## Phases
The process of loading and typechecking TypeScript code is done in several phases

### Load from the filesystem
Cleat's `Fs` abstraction allows tests to mock the filesystem, so the `Program` sees multiple files without the test runner having to write them to disk. The mock lives in [src/test/mock_fs.c](src/test/mock_fs.c); everything else uses the `fs` global.

### Parse
The parser creates an AST from the source code string, see below.

### Resolve names
Identifier names (of both values and types) are added to a `Scope`, the `Scope` is pushed to the `ScopeStack`, and then names are looked up wherever they are used. Scopes are not preserved after this phase is complete (with the exception of those owned by namespaces, and other special cases).

During this process additional imported source files may be added to the program. When resolving names is done for the current file(s) they are parsed and their names are resolved. This is repeated until no new files are added (see `program_load()`).

### Typecheck
The AST is now in it's finished state, so we can traverse it looking for type errors. This is the phase when the `*_evaluate_type()` functions are called (via `type_resolve()` -> `type_resolve_*()` -> `lazy_type_get()` -> `lazy_type_evaluate()` -> `*_evaluate_type()`)

## Memory Management
The source code and ASTs of all parsed files is kept valid until after all above phases have finished. The AST holds references to the code (via spans) and any types created from the AST may hold references into the AST, so it would not be safe to free either the code or the AST while anything created from it remains in use. Furthermore the AST should not be structurally modified after parsing to keep pointers valid (ex pushing to a vec that's part of the AST is generally unsafe, since it may trigger a reallocation and invalidate other elements).

### Types
Sometimes types are owned by the AST and sometimes they are not. Types will not be used after the AST is dropped, but if a type is not part of the AST it may be dropped before final cleanup.

One easy way to tell if a struct may not always be owned by the AST is if it's clonable. Types are clonable, but the AST is not. If something has a `*_clone()` function it's likely not always owned by the AST.

Since types are *sometimes* part of the AST (generally when they are explicitly specified in the source code) they sometimes need to own other AST things (such as how `infer`ed types own an `ArcDeclaration`). In these cases they will generally have both an owned value and a non-owning reference. Clones will only pick up the non-owning reference, with the assumption the copy of the type with the owned value will outlive them (because it should be part of the AST). We don't trust arcs (shared references) because retain cycles.

### Declarations
In TypeScript multiple declarations in the source code can contribute to a single logical declaration (this is known as declaration merging). For this reason declarations are generally stored as arcs. When initializing a declaration from a Scope you don't know if you're getting a fresh one or an existing one. Sometimes there's an existing one, but it's invalid to merge with it so you get a fresh one and an error is produced. The Scope figures this all out.

## Parsing
As mentioned above the parsing framework is custom, but it should be fairly straightforward.

### Parser struct
Each parse function (generally) creates it's own instance of `Parser` named `p`. This stores a current position in the source code, a pointer to the parser one level up (it's "parent") and some other things.

Each perse function receives a pointer to it's parent parser as an argument, as well as a pointer to an instance of the data structure it will attempt to populate, called `ret`. It returns a bool:
- `parse_success(&p)` - the function successfully parsed it's thing, `ret` is populated, the parent's current position is updated to match `p`'s position and any errors added to this parser or children **are** added to the parent
- `parse_fail(&p)` - the function did not parse it's thing, `ret` is undefined (could be unchanged, could be 0s, could be junk data, does **not** need to be dropped), the parent's position is not changed and any errors added to this parser or children are dropped (**not** added to parent)

`parse_success()` **always** returns true and `parse_fail()` **always** returns false. This makes it easy to return the correct value along with calling the corresponding parser cleanup function. It's **almost always** incorrect for these not to match up 1:1. The exceptions are simple low-level parse functions that avoid creating their own parser instances for performance, and a couple places where child parsers are created in the same function as their parents.

Note that a parser can succeed without errors, succeed with one or more errors or fail without errors. It **cannot** fail with errors (any errors added are silently dropped on fail). The idea is a parser either parsed something and advanced the current position (success) or did not (failure). Errors are only relevant when a parser parsed something, and understood a way that thing is wrong.

### Tokens
Unlike other typescript/javascript parsers, tttt doesn't tokenize before parsing. Sometimes tokens are needed though (such as when an error message for an unexpected token is to be added), which is what `hop_token()` is for. It's not called for most code.

### Anchors
Anchors are helpful for making spans. They are created with `anchor(&p)`. `span_from(a)` creates a span from the parser's position when the anchor was created to the parser's current position.

### Whitespace
`hop_whitespace(&p)` needs to be called anywhere whitespace may appear.

Newlines are generally ignored but change how js/ts are parsed in some cases. For this reason parsers keep track of if the last whitespace they hopped contained a newline. This gets propagated to the parent on success.

### Function context
Whether `await` and `yield` are operators or plain identifiers depends on the function being parsed. `ParserShared` keeps `in_async`, `in_generator` and `function_depth`; `function_ctx_enter()`/`function_ctx_leave()` wrap the parsing of every function's parameters and body (see `parse_non_arrow_function()` and `parse_arrow_function()`), and `FunctionFlags` carries `async`/`*` from whoever saw the keyword to the function parser. Since a source position is always inside the same function, this doesn't conflict with the fail cache.

### Fail cache
The fail cache is used to cache specific locations where specific parse functions have failed to parse before. There are some cases (such as arrow functions vs parenthesized expressions) where the correct way of parsing something is not obvious until after the parser has parsed some sub-expressions. If these cases are nested the time complexity of doing this naively is Not Good. However if you keep track of where you've tried and failed to parse specific things it's fine, so that's what we do. For details on precisely how it works you'll have to decipher the code.
