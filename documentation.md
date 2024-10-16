# tttt Documentation

## Programs
The repository contains three programs.

### tttt
The actual typechecker, drop-in replacement for tsc. See `build/tttt --help` for usage.

Can be compiled and run with `./run.sh`. This makes a debug build and runs it under Valgrind, so won't be fast.

Completely written in C. Code lives in [src](src/). The only dependency is an MIT-licensed C container library called STC (no relation to the swc-related project of the same name). It's included in this repository under [thirdparty](thirdparty/), and automatically compiled by the build system.

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

### Parser
The parser (including a minimal custom parsing framework) lives in `parser.c`. We do not compile grammars, combinate or tokenize. We simply take a const char* and use it to build an AST. Like real men.

See dedicated section below for details.

### Program
The `Program` struct owns the source code buffers and ASTs of all loaded files, manages imports, etc.

### Scope
A lexical scope, maps identifier names to `Declaration`s.

### NameStack
Used for looking up identifier names, scopes can be pushed and popped.

### LazyType
`LazyType` is a struct owned by the AST. They're `Type` wrappers that allow a referencing type to be created before the `LazyType` is evaluated. In practice that means it's possible to create a type while in the resolve names phase, but not evaluate what that type actually is until the typecheck phase when all names are resolved.

The process goes something like this:
1. An AST struct, for example `Expr`, contains a `LazyType`
2. In the resolve names phase, it sets its LazyType's tag to `LAZY_TYPE_EXPR`
3. That function or some other function then creates a `Type` with a pointer to its LazyType using `type_lazy()` (the expression's type hasn't and *can't* be evaluated yet since names haven't been fully resolved, so this is the only way to pass around its type)
4. Later, in the typecheck phase, `type_resolve()` is called on the `Type`, which calls `lazy_type_get()`
5. The tag of the LazyType is `LAZY_TYPE_EXPR`, so `lazy_type_evaluate()` uses the pointer to the LazyType to calculate a pointer to its containing `Expr` and calls `expr_evaluate_type()`
6. It then sets `.evaluated` to the returned type, the tag to `LAZY_TYPE_EVALUATED` and returns a pointer to the evaluated type
7. Future calls to `lazy_type_get()` for the same Expr quickly return a pointer to the evaluated type, without needing to call `lazy_type_evaluate()`

LazyTypes play a critical role in detecting self-referential types. While a LazyType is being evaluated it's put in an in-flight state. If something tries to evaluate it again in the same call stack it's evaluated type is set to `type_self_referential_error`.

### Engine
tttt includes a JavaScript interpreter that evaluates an AST. It's not fast, standards complaint, tested, complete, or a priority.

## Phases
The process of loading and typechecking TypeScript code is done in several phases

### Load from the filesystem
The `Filesystem` abstraction allows tests to mock the filesystem, so the `Program` sees multiple files without the test runner having to write them to disk.

### Parse
The parser creates an AST from the source code string, see below.

### Resolve names
Identifier names (of both values and types) are added to a `Scope`, the `Scope` is pushed to the `NameStack`, and then names are looked up wherever they are used. Scopes are not preserved after this phase is complete (with the exception of those owned by namespaces, and other special cases).

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

### Fail cache
The fail cache is used to cache specific locations where specific parse functions have failed to parse before. There are some cases (such as arrow functions vs parenthesized expressions) where the correct way of parsing something is not obvious until after the parser has parsed some sub-expressions. If these cases are nested the time complexity of doing this naively is Not Good. However if you keep track of where you've tried and failed to parse specific things it's fine, so that's what we do. For details on precisely how it works you'll have to decipher the code.
