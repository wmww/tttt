# Plan 26: JavaScript files and JSDoc

Written 2026-09-26 (planning session, todo entry 4).

## Status
**Stage 1 done** (2026-09-27, 8 commits "Plan 26 stage 1, part N"): JS tests passing 160 -> 247, suite 8258 -> 8345.
Built as designed except: JSDoc is consumed at parse time rather than stored (stage 3 managed without storing it
too), and `exports.x =` is only declared from a file's top-level statements. What was left out or approximated is in
hacks.md "JavaScript". `documentation.md` "JSDoc" / "CommonJS" describe what exists.
**Stage 2 done** (2026-09-27, 3 commits "Plan 26 stage 2, part N"): JS tests passing 260 -> 299, suite 8631 -> 8686.
Built: expandos in TS and JS (declare step, exports of the host, entity-name hosts, `{}` containers, class expression
hosts), `this.x` members of JS classes and constructor functions (resolve walk, `Symbol.members`), `F.prototype.x`,
constructor functions as classes (construct signature, `this`, `typeof F`), JS optional parameters, the TS callable
TS2339 gate removed. Not done: `F.prototype = {...}`, `Object.defineProperty` declarations, `arguments` as a rest
parameter (tsc only does that with a JSDoc `...T` now), JS widening of `null`/`[]` initializers, **the JS TS2339
gate** (removing it still loses ~57 tests: hacks.md "JavaScript and JSDoc" lists why). `documentation.md`
"Assignment declarations" describes what exists.
**Stage 3 done** (2026-09-27, 6 commits, todo entry 5): JS tests passing 310 -> 375, suite 9091 -> 9156, webpack
3981 -> 515. Built: `@typedef`/`@callback`/`@import`/`@enum` as synthesized declarations bound by the innermost statement
list (`jsdoc_bind_comments()`), `@template` (functions, methods, classes, typedefs, defaults), nested `@param o.a`,
`@type` on declaring assignments and object literal members, non-literal `@type {F}` as a function's signature, JSDoc
`import("./m")` as value-or-type, JS calls inferring any (AnyDefault), braceless `@type`/`@enum`, `@returns {asserts x}`.
Unresolved JSDoc names report TS2304 now (the silencing HACK is gone), which is what the "Stage 3 leftovers" below
cost: 26 tests lost a component. `documentation.md` "JSDoc" describes what exists.
**Stage 3 leftovers and stage 4 (part)** (2026-09-28, todo entry 5, 12 commits): JS tests passing 408 -> 470, suite
9667 -> 9729, webpack 118 -> 116. Done: global expando namespaces, generic constructor functions (templates, prototype
methods' JSDoc names), `F.prototype = {...}`, this aliases, `@type` property declarations, CommonJS exports through
aliases and nested blocks, **the JS TS2339 gate is gone** (JS literal leniency instead), the TS8xxx syntax walk,
`@implements`, `@private`/`@protected`/`@readonly`, `@template const`. What's left of the TS2339 gate's causes is in
hacks.md "JavaScript"; the `0other` sample went to triage.md. Not done: dotted typedef names (`@callback NS.Inner`),
the display items below, JSDoc parse errors, `@override` (no override checks exist at all), the JS-only grammar errors
(plainJS* tests: mostly parser recovery, triage.md).
**JS session** (2026-09-29, todo entry 7, four Opus sub-agents by bucket, ~190 commits): JS tests passing 579 -> 885,
suite 10830 -> 11274 (0 lost), webpack 9 -> 6. Done: CommonJS (declarations by assignment expression, the `module`
symbol, class expression aliases, `require("m").a.b` / `?.` aliases, `@type` on a `require` declaration, contextual
typing of exports, IIFE exports, `Object.defineProperty` declarations, `maxNodeModuleJsDepth`, TS7016/TS2307 chains),
expandos/JS classes (error vs any on missing properties, constructor functions' return flow node and `@class`, string
element access names, defaulted containers, `this` symbols), JSDoc (parse errors, `@satisfies`, `@overload`,
`@augments`, `@this`, `@template` on variable statements and its errors, `@type` on export default / catch variables /
function declarations as their signature, TS8030, printer node reuse), plain JS files checked for plainJSErrors, JSON
modules, many JS-only grammar checks. What is left is "Leftovers" at the end of this file.
Stage 2 notes (kept for the leftovers): `Object.defineProperty(exports, ...)` is the rest of spurious TS2580; the symbols baseline already has
an override hook for tsc's getSpecialPropertyAssignmentSymbol shapes (`commonjs_symbol_overrides()`), `this.x =` and
expandos will need the same kinds of lines; `binary_commonjs_declaration()` is where assignment declaration kinds
start (tsc's getAssignmentDeclarationKind), extend it rather than adding a second classifier.

## Why
1049 of the suite's tests are JS tests (a `.js`/`.jsx`/`.cjs`/`.mjs` unit, `@allowJs` or `@checkJs`); 160 pass,
889 fail (17% of all failing tests; 784 of the JS tests use `@checkJs`). `scripts/jsstats.py` (written for this plan, run
`scripts/failstats.py` first) classifies the failing ones by the features they need, from their sources and tsc's baselines:

| category | only this | involved | notes |
|---|---|---|---|
| 1 CommonJS (`module.exports`, `exports.x`, `require`) | 87 | 211 | spurious TS2580 in 128 tests, TS2304 49, TS2306 18 |
| 2 expandos, `this.x =`, prototype, `@class` | 72 | 255 | spurious TS7009 12, TS2683 11 |
| 3 JSDoc `@type`/`@param`/`@returns`/`@typedef`/`@template`/`@property`/casts | 113 | 421 | the multiplier: pairs with everything |
| 3b the other tags (`@import`, `@callback`, `@enum`, `@overload`, `@satisfies`, `@this`, `@extends`...) | 13 | 145 | |
| 4 JS leniency (optional params, `arguments`...) | 5 | 87 | spurious TS2554 15, TS7006 63 |
| 5 JS syntax diagnostics (TS8xxx, JS-only TS1xxx) | 42 | 162 | missing TS8010 14, TS8006 11, TS8009 9 |
| 6 emit (TS5055/5056/6054) | 16 | 39 | out of scope |
| 7 only a `.ts` unit differs (`requireOfJsonFile*`: JSON modules) | 27 | 38 | not this plan |
| unclassified (JS private names, TS7029, TS2393, `{}` is any in JS...) | 90 | | |

Cumulative upper bounds (tests whose every category is done): 1+2 = 181, +3 = 408, +4 = 464, +5 = 577, +3b = 722. A
40-test sample says expect a third less (incidental bugs): ~110 for 1+2, ~250 with basic JSDoc, ~450 for everything.
280 of the failing JS tests pass their errors and fail only symbols/types: the binder and JSDoc types, not
diagnostics, are what they need.

JS is a real part of what tsc checks (`checkJs` projects, mixed code bases, `.d.ts`-less dependencies read with
`allowJs`), and three of its pieces are foundational rather than additive: (1) declarations made by *assignments*
(CommonJS exports, expandos, `this.x =` members, prototype assignments) are binder work that changes what a file
exports and whether it is a module at all; (2) JSDoc is a second type syntax that must lower into the one `Ty`
machinery (vision.md "Risks": no parallel type world); (3) a handful of checker rules read "is this a JS file" (optional
parameters, `Object` is `any`, constructor functions). All three have to be designed once and then filled in.

## What tttt does today
- A `.js`/`.jsx`/`.mjs`/`.cjs` file loads with `allowJs` (`path_is_js()` in program.c, `SourceFile.is_js`) and is parsed
  **as TypeScript** (the parser only turns off type arguments on calls and `<T>x` assertions, parser.c).
- Without `checkJs` / `// @ts-check` its semantic diagnostics are filtered out afterwards (`SourceFile.unchecked`,
  `error_ctx_remove_unchecked()`), so most non-checkJs JS tests fail only their symbols/types components.
- Comments are skipped by `hop_comment()`; nothing keeps `/** */` text (except the `{@link}` text scan in unused.c,
  `unused_mark_jsdoc_links()`, hacks.md).
- `require("m")` calls are not module refs; `module`/`exports`/`require` are unresolved names (spurious TS2580/TS2304);
  a file whose only exports are `exports.x =` isn't a module (TS2306 in the importer, hacks.md).
- Gates that exist only because JS isn't understood: `check_access()` / `ty_check_element_implicit_any()` skip JS files
  (no TS2339/TS7053 there), `binary_op_typecheck` skips TS2358 in JS, scope.c:981 skips an alias in JS, expr.c:345 and
  several_types.c:257 special-case JS. Every one of them goes when the real rule exists (list them in the commit).

## Design

### 1. JSDoc parsing and attachment (parser)
tsc parses JSDoc eagerly for JS files (`addJSDocComment` in parser.ts, only under `ContextFlags.JavaScriptFile`) and
attaches the `/** */` comments in a node's leading trivia to the node, for a fixed set of node kinds (`canHaveJSDoc`).
Do the same, JS files only (TS files: only if a test shows a TS-file JSDoc effect we need; `@deprecated` & co. are
editor features):
- `hop_comment()` in a JS file records every `/**` comment that isn't `/**/` in `SourceFile.jsdoc_comments` (sorted
  by start; the parser backtracks, so append only past the last recorded start). Whitespace/comments are only ever
  hopped between tokens, so a comment seen by a failed attempt is still a real comment.
- A node that can host JSDoc finds its comments from its start position: back over whitespace from the node's first
  token, is the text before it `*/` of a recorded comment? then that one, and repeat (a node can have several;
  tsc's tags are the union, most lookups take the last). Hosts (tsc's `canHaveJSDoc` / `getJSDocCommentsAndTags`, which
  also walks *up* to the variable statement, the expression statement of an assignment, the property assignment...):
  statements (variable, function, class, expression, export), class members, object literal properties, parameters
  (inline `/** @type {T} */ a`), parenthesized expressions (casts), `export default`. There are no parent pointers, so
  whoever parses the host hands its JSDoc down to what it contains (a variable statement to its declarations and a
  function/class initializer; an `a.b = function () {}` statement to the function; a property assignment to its
  function value), exactly what tsc's walk *up* in `getJSDocHost()` finds.
- A parsed `JsDoc` is a list of tags (`@type`, `@param`, `@returns`/`@return`, `@typedef`, `@callback`, `@property`/
  `@prop`, `@template`, `@enum`, `@this`, `@extends`/`@augments`, `@implements`, `@class`/`@constructor`, `@overload`,
  `@satisfies`, `@import`, `@private`/`@protected`/`@public`/`@readonly`/`@override`, `@deprecated` ignored), each with
  its `{type}` as a **`TypeNode` parsed by the existing type parser** pointed into the comment: spans are source spans,
  errors land where tsc puts them. Two parser additions: (a) a `ParserShared.in_jsdoc` mode in which `hop_whitespace`
  skips a line's leading `*` (tsc's `skipJsDocLeadingAsterisks`, multi-line `{{ a: 1,\n * b: 2 }}`) and `*/` ends
  everything; (b) the JSDoc-only type forms as new `TYPE_*` tags or flags: `*` and `?` alone (any), `?T` / `T?`
  (nullable), `!T` (non-null), `T=` (optional, in `@param`), `...T` (variadic), `function(string, number): R` with
  `this:`/`new:` parameters, `Array.<T>` / `Object.<K, V>`, `module:foo`, and `import("m").T` (exists). Nested
  `@param {string} o.a` / `@property` tags build a type literal (tsc's `JSDocTypeLiteral`).
- Parsing only happens in JS files, so TS parse speed is untouched; measure JS-heavy parse time once (e.g. a
  `.js` copy of some corpus file) and note it.

### 2. Effective annotations: JSDoc lowers into the existing slots
tsc's rule is `getEffectiveTypeAnnotationNode()`: the written TS annotation, else in JS the JSDoc one. Mirror it with
accessors used *everywhere* the checker reads an annotation, so the rest of the checker can't tell:
- variable/parameter/property type: `@type` of the host, `@param {T} name` of the enclosing function's host (by name,
  or by position for destructured parameters), `@property` in a typedef. Parameter optionality from `T=`, `[name]`,
  `[name=default]`; `...T` makes an array rest.
- function return type: `@returns`; type parameters: `@template T, U` (and `@template {C} T` constraints, defaults
  `[T=X]`), which declare type parameter symbols in the function's/class's/typedef's scope like a `Generic` does.
- `@type {F}` on a function declaration/expression (or its host) where `F` is a function type is the *signature* of
  the function (tsc's `getSignatureOfTypeTag`): parameters get their types from it like a contextual signature,
  arity from it.
- casts: `/** @type {T} */ (expr)` is `expr as T` (tsc: a parenthesized expression with a type tag); `@satisfies`
  likewise for `satisfies`.
- `@typedef {T} Name`, `@callback Name` (+ `@param`/`@returns` tags = a function type) and `@typedef {Object} Name` +
  `@property` tags are **type alias declarations**: symbols with `SYMBOL_DECL_TYPE_ALIAS`-like decls whose body is a
  `TypeNode` (synthesized `TypeDecl`s owned by the file, not inserted into statement vectors). In a module file they
  are exported (tsc: typedefs in a module are exported), in a script global. `@enum {T}` on a `const` object: a type
  alias + the value.
- `@import { A } from "m"` (TS 5.5) is a type-only import declaration: a `PortItem`-like alias + a `ModuleRef`.
Whether the accessors fill the existing fields (`Binding.explicit_type` & co., keeping a `from_jsdoc` bit for TS8010
and display) or are read through one function is the executor's call; the requirement is that the checker has one
way to ask "what's the annotation", and that the types printer reuses JSDoc nodes where tsc's node builder does
(it does reuse them in declaration-like printing, check the types baselines before assuming).

JSDoc type *references* are resolved in the resolve walk in the scope of their host, with tsc's JSDoc-only name rules
(`getIntendedTypeFromJSDocTypeReference`: `Object` -> `any` (and `Object.<K,V>` an index signature), `String`/`Number`/
`Boolean`/`Void`/`Undefined`/`Null` -> the primitives, `Function`, `array`/`promise`/`Array` without arguments ->
`any[]`, `Promise<any>`); a reference to a *value* whose type is a class/constructor function means its instance type
(tsc's `getTypeFromJSDocValueReference`: `@type {C}` where `C` is `const C = class {}` or a constructor function).

### 3. Assignment declarations (binder)
tsc's `getAssignmentDeclarationKind()` classifies `a.b = x` / `a["b"] = x` / `Object.defineProperty(a, "b", ...)` into
ExportsProperty (`exports.x =`, `module.exports.x =`), ModuleExports (`module.exports =`), PrototypeProperty
(`F.prototype.m =`), Prototype (`F.prototype = {...}`), ThisProperty (`this.x =`), Property (`f.x =`, expandos) and
`ObjectDefinePropertyExports`/`Value`/`PrototypeProperty` (`binder.ts` `bindWorker` BinaryExpression case,
`bindExportsPropertyAssignment`, `bindModuleExportsAssignment`, `bindThisPropertyAssignment`,
`bindPrototypeAssignment`, `bindSpecialPropertyAssignment`, `bindStaticPropertyAssignment`, `bindPropertyAssignment`).
Port that classification as one function over `Expr` and bind by kind:
- **CommonJS** (JS files): a file with `exports.x =`, `module.exports =` or a `require()` call (tsc's
  `setCommonJsModuleIndicator`, unless the file already has ES imports/exports: then the ES side wins) **is a module**
  and must be known to be one before the declare step (its top-level `var`s are locals, not globals). The parser can
  note the indicator syntactically when it sees these shapes (validated by the binder: `module`/`exports`/`require` not
  declared in scope). `exports.x = v` declares export `x` (an alias when `v` is an aliasable expression, tsc's
  `isAliasableExpression`, else a property-ish value symbol whose type is the assigned type); `module.exports = v` is
  `export=` (tsc: an alias to `v` when it's an entity name, a class/function expression otherwise the value; an object
  literal of shorthand properties `module.exports = { a, b }` declares exports); `module.exports.x =` / later
  `exports.x =` after `module.exports = {...}` merge into it. `module` and `exports` get tsc's synthetic symbols
  (tsc declares `module` as a local `FunctionScopedVariable | ModuleExports`; `exports` resolves to the file's module
  symbol), and `require` is a JS-file builtin: `const x = require("m")`, `const { a, b } = require("m")`,
  `const a = require("m").a` are **aliases** (`isVariableDeclarationInitializedToBareOrAccessedRequire`) and the
  `require("m")` calls are `ModuleRef`s (tsc collects them in `collectRequireCalls`, anywhere in a JS file). Importing
  a CommonJS module from TS/ESM goes through the existing `export=` machinery (`module_export_equals()`); check
  `esModuleInterop`/synthetic default behaviour against the baselines rather than inventing it.
- **Expandos** (JS and TS: tsc binds `f.x =` on function declarations and `const f = function/arrow` in TS too,
  done for TS; JS adds `const o = {}` / class expressions / `var ns = ns || {}` namespaces):
  `f.x = v` declares `x` in `f`'s `exports` (the function+namespace merge path already gives `typeof f` those members:
  `ty_merged_value_type()`), and a variable-declared expando host gets exports on the variable symbol, which the
  variable's type then includes (tsc: `getTypeOfFuncClassEnumModule` / the JS branch of
  `getTypeOfVariableOrParameterOrProperty`). The value type of an assignment-declared symbol is tsc's
  `getWidenedTypeForAssignmentDeclaration`: the union of the widened types of every assignment to it, unless one
  of them has a JSDoc `@type`, then that.
- **JS classes** (tsc's `isJSConstructor` / `getJSClassType`): `this.x = v` in a class constructor or method, or in a
  function that thereby becomes a constructor function (also: `@class`/`@constructor`, a `F.prototype` assignment),
  declares an instance member; `F.prototype.m = v` and `F.prototype = { ... }` declare prototype members. tttt builds
  class members from AST nodes, not member symbols (`declared_raw_members()`), so these are *recorded on the class /
  function node* (a list of assignment `Expr*`s, like `FunctionImpl.yields`) and `members.c` makes `Prop`s from them
  (`Prop.lazy` = the assigned expression, `decl_spans` = every assignment, for the symbols baseline). A constructor
  function's type gets a construct signature returning that instance type; `new F()` stops being TS7009, `this`
  inside it stops being TS2683.
- **Where**: tsc binds all of this in its one binder walk. tttt's contract (documentation.md "Resolve names",
  hacks.md "Parallelism contract") is declare (per file) -> link (shared) -> resolve (per file, read-only on
  symbol tables). What importers can see (CommonJS exports, top-level expandos of exported functions) must be declared
  in the **declare step**: a JS-file statement-level scan of expression statements (the file's top level and the
  blocks the hoisting walk already visits) covers the tests; nested `exports.x =` (inside `if`, IIFEs) can come
  later. `this.x` and prototype members are file-local AST notes, recorded by the **resolve walk**, which already knows
  the enclosing function/class (`Scope.function`, `scope_enclosing_class()`) and resolves `F` in `F.prototype`.
  Expandos found only by the resolve walk (nested ones) go to a per-file list merged in a small sequential step after
  the resolve walks, never written to shared symbols during the walk.

### 4. JS checker rules
Small, each a known tsc rule, gated on `SourceFile.is_js` of the declaration (not of the use):
- every parameter of a JS function without a JSDoc type is optional (`isOptionalParameter`: arity, spurious TS2554);
  a JS function whose body uses `arguments` takes any number of arguments (`hasEffectiveRestParameter`).
- implicit-any reporting: only in checked JS (already filtered), and a JSDoc-typed parameter isn't implicit
  (reportImplicitAny); unannotated JS parameters without context are `any`.
- `null`/`undefined`/`[]`/`{}` initializers in JS declarations: tsc widens to `any` / evolving arrays in JS where TS
  wouldn't (`getWidenedTypeForVariableLikeDeclaration`, `isEmptyArrayLiteral` in JS); check baselines.
- object literals in JS are open for expando-like property assignment (`o.x = 1` after `var o = {}` is a declaration,
  covered by section 3).
- TS syntax in a JS file is an error, reported regardless of `checkJs` (tsc's `getJSSyntacticDiagnosticsForFile`:
  TS8002 `import =`, TS8003 `export =`, TS8004 type parameters, TS8005 `implements`, TS8006 interface/enum/type/
  namespace declarations, TS8008 type aliases, TS8009 modifiers, TS8010 type annotations, TS8011 type arguments,
  TS8012 parameter modifiers, TS8013 non-null assertions, TS8016 type assertions, TS8017 signatures). These are a
  syntactic walk over the AST of JS files, independent of everything else.

## Stages
One session each; the todo has two execution sessions for this plan before the next insights, the rest is for
the insights session to schedule. Record `scripts/jsstats.py`'s totals and the JS pass count in `measurements.md` after
each.

### Stage 1: JSDoc core + CommonJS
- JSDoc recording, attachment and tag parsing (design 1), for all tags (parse everything, act on few: unknown tags
  are ignored, so later stages only add meaning). JSDoc-only type forms.
- Effective annotations for `@type` (variables, properties, function signatures), `@param` (plain, optional,
  rest; nested `o.a` properties may wait), `@returns`, casts. JSDoc name rules (`Object`, `String`, `Array`...).
- CommonJS (design 3): the module indicator, `exports.x =`, `module.exports =` (+ `.x =`), the `module`/`exports`
  symbols, `require` aliases and `require()` module refs, importing a CommonJS file from TS and JS.
- Target: spurious TS2580 from 128 tests to near 0, TS2306 gone from the CommonJS tests, JS tests passing 160 ->
  ~230. `typeFromParamTagForFunction`, `jsdocTypeTag*`, `exportPropertyAssignmentNameResolution`,
  `requireAssertsFromTypescript`, `jsdocTypeFromChainedAssignment3`-style tests are the smoke tests.

### Stage 2: expandos and JS classes
- Assignment declarations beyond CommonJS: `f.x =` expandos (JS and TS; TS function expandos fix
  `tsxStatelessComponentDefaultProps` & co.), `this.x =` members of JS classes and constructor functions,
  `F.prototype.m =` / `F.prototype = {}`, `@class`/`@constructor`, `Object.defineProperty` exports and members.
- Constructor functions: construct signature, `this` type inside, `typeof F` printing; TS7009/TS2683 gone.
- JS leniency rules (design 4): optional parameters, `arguments` makes a rest parameter, JS widening of
  `null`/`undefined`/`[]` initializers; implicit-any reporting rules for JS.
- Remove the JS gates in `check_access()`/`ty_check_element_implicit_any()`: once expandos exist, JS files get TS2339
  like tsc (missing TS2339 in 55 JS tests).
- Target: JS tests passing ~230 -> ~320; the expando baselines' symbol shapes (the `Symbol(C.x, Decl(...),
  Decl(...))` lines above) matching.

### Stage 3: declarations in JSDoc (done, see Status)
**Stage 3 leftovers**, by what they cost (tests that lost a component when unresolved JSDoc names stopped being silent,
plus webpack's residue):
- (Done 2026-09-28: global expando namespaces, constructor function type parameters seen from prototype methods.)
- Dotted typedef names (`@callback NS.Nested.Inner`: namespaces), 6 tests; `@typedef` named by its host.
- Display: a typedef of a CommonJS module with `export=` prints through the export= name in tsc
  (`memoize.FunctionReturning<T>`, `MF.CompilationHooks`), tttt prints `import("memoize").FunctionReturning<T>`; a
  JSDoc type literal is printed by tsc with its newlines dropped (`{    a: string;}`); aliases of JSDoc function types
  (`@enum {function(number): number}` prints `Fs`).
- webpack: 529 -> 169 (2026-09-27) -> 107 -> 32 (2026-09-28 corpus session); what is left is in plan 19 "webpack".
  JSDoc items among it:
  - Names in the JSDoc of `var f = function (str) {}` resolve from the variable statement (tsc's parent chain goes
    JSDoc -> VariableStatement, not through the function): `@param {str} str` is the outer `str`'s type. tttt copies
    the tags into the function, whose scope has its own `str` (TS2502).
  - The display of `@type`-tagged functions: a tag written as a signature (`@type {<T>(m: T) => T}`) keeps the
    function's parameter names (tsc prints the tag's); `const k = /** @type {Fn} */ function (a, b = 1)` prints the
    return type inferred from the body where tsc prints the tag's (probe plan19/webpack/parameter_default_removes_undefined.ts).

### Stage 4: the rest of the tags and JS syntax diagnostics
`@this`, `@extends`/`@augments` (type arguments for a JS class's base), `@implements`, `@overload`, `@satisfies`,
`@private`/`@protected`/`@public`/`@readonly`/`@override`; the TS8xxx syntactic walk (design 4, last item) and the
JS-only grammar errors (TS1xxx that only apply to JS, e.g. TS18030 private names). Then `scripts/jsstats.py`'s `0other`
bucket gets sampled into triage.md. webpack uses `@overload` 29, `@extends` 17, `@this` 9, `@import` 22 (`@import`
is done).

## Validation
- Per stage: `scripts/fasttest.sh --summary` shows no lost tests (a lost TS test is a bug in the shared paths: JSDoc must
  not change anything in TS files except expandos), the stage's target counts below, `scripts/realworld.py` still 0 on tsc
  and no new diagnostics on the other corpora (they're TS: a change there means the JS gating leaked), `scripts/bench.sh`
  within noise on the TS corpora.
- JS-only accounting, with the classifier ``scripts/jsstats.py``: JS tests passing (158 at the start), JS tests failing on
  spurious TS2580/TS2304/TS2306/TS7009/TS2683/TS2554, and the count of tests per missing feature.
- Every JS gate listed in "What tttt does today" is removed or has a hacks.md entry saying why it stays.
- Keep a probe per construct in `test/probes/plan26/` (`scripts/filetest.sh` + `scripts/tscbaseline.sh` on it).

## Baseline shapes (what the symbols/types baselines print; the binder must produce exactly these)
- `this.x =` in a JS class or constructor function (`inferringClassMembersFromAssignments`): the property is
  qualified by the class and every assignment is a declaration: `>this.inConstructor : Symbol(C.inConstructor,
  Decl(a.js, 2, 28), Decl(a.js, 5, 14))`. In the types, the assigned-to left side prints the declared type (`any` while
  it's being inferred? check), uses print the union of assignments (`>c.inConstructor : string | number`). For a
  constructor function `>this : this`, `>Installer : typeof Installer`, `>this : Symbol(Installer, Decl(...))`.
- Prototype assignment (`typeFromPropertyAssignment17`): `>M.prototype : Symbol(M.m, Decl(minimatch.js, 11, 1))`,
  `>prototype : Symbol(Function.prototype, Decl(lib.es5.d.ts, --, --))`, `>m : Symbol(M.m, ...)`; the function symbol
  gets the assignment as a declaration: `>M : Symbol(M, Decl(minimatch.js, 13, 1), Decl(minimatch.js, 8, 1))`.
- Function expando (`typeFromPropertyAssignment18`): `>m.GLOBSTAR : Symbol(m.GLOBSTAR, Decl(a.js, 0, 14), Decl(a.js,
  4, 2))`, `>m : Symbol(m, Decl(a.js, 0, 30), Decl(a.js, 3, 14))`; types `>m : typeof m`, an expando `{}` value prints
  `typeof m.GLOBSTAR`.
- `module.exports =` (`jsFileClassPropertyInitalizationInObjectLiteral`): `>module.exports : Symbol(module.exports,
  Decl(foo.js, 0, 0))`, `>module : Symbol(export=, Decl(foo.js, 0, 0))`, `>exports : Symbol(export=, ...)`; types
  `>module : { exports: () => { c: number; }; }`; an object literal export prints as `typeof module.exports`.
- `exports.x =`: `>exports.wrapSync : Symbol(wrapSync, Decl(a.js, 0, 0))`, `>exports : Symbol(wrapSync, ...)` (the
  `exports` part prints the property's symbol), types `>exports : typeof import("a")`. For `module.exports.D`:
  `>module : Symbol(module, Decl(...))`, `>exports : Symbol(module.exports, Decl(bug24492.js, 0, 0))`.

## Open questions for the executor
- Exact tsc symbols/types baseline shapes for CommonJS and expandos (section "Baseline shapes" below) decide details like
  which declaration a merged export prints and whether `module.exports = {}` object literal members are
  `Symbol(a, Decl(...))` of the property assignment. Match them per construct; don't guess.
- `esModuleInterop` / `allowSyntheticDefaultImports` when a TS file default-imports a CommonJS JS module.

## Leftovers (JS session 2026-09-29; 164 JS tests fail: only-CommonJS 31, only-JSDoc 19, other 15)
From the four sub-agents' reports; test names are the ones to probe.
- **Symbols for function expressions**: object-literal functions assigning `this.x` are classes of their own symbol in
  tsc (`typeof fun`): objectPropertyAsClass, thisInObjectJs, constructorTagOnObjectLiteralMethod,
  typeFromContextualThisType, allowJscheckJsTypeParameterNoCrash; `this.x` in object-literal methods
  (checkObjectDefineProperty, checkExportsObjectAssignProperty).
- **Member merging**: `this.x` with a later field (ambientPropertyDeclarationInJs), `this.m = null` beside
  `F.prototype.m = function` is two symbols (multipleDeclarations), prototype literal accessors with `this.x`
  (jsDeclarationsFunctionLikeClasses2), class static method expandos (jsDeclarationsClassStaticMethodAugmentation),
  cross-file prototype + interface (prototypePropertyAssignmentMergeWithInterfaceMethod), JS `{}` with a `declare class`
  (jsContainerMergeTsDeclaration3).
- **export= merged in place**: tsc copies the exported value and merges the module's other exports into the copy
  (typedefCrossModule3, moduleExportPropertyAssignmentDefault, moduleExportAliasImported; printing `w.Cbs` for `Cbs`,
  test/probes/plan19/webpack/export_equals_merge.ts). `module.exports.x = v` after `module.exports = C`
  (jsDeclarationsExportSubAssignments, moduleExportAlias4).
- **`exports.x = a.b` aliases** (no symbol when `b` is a member of a type, e.g. `Math.min`): pushTypeGetTypeOfAlias,
  resolveNameWithNamspace, moduleExportsAliasLoop1/2, commonJSImportNestedClassTypeReference, moduleExportDuplicateAlias*.
- **`import * as ns` of CommonJS from ESM** needs a type of its own (nodeModulesAllowJs1 x3; a `symbol_chain.c` skip
  cost 16 tests).
- **Symbol chains** on circular namespace re-exports (getAccessibleSymbolChain/getContainersOfSymbol):
  nodeModulesAllowJs{Conditional,}PackageExports, PackagePatternExports(Trailers), nodeAllowJsPackageSelfName2,
  importDeclFromTypeNodeInJsSource (`n3.c` through an import alias). The printer's scopes are files and namespaces
  only, not functions (propertiesOfGenericConstructorFunctions' computed keys; a `require()` alias in a function).
- **JSDoc**: dotted typedef names (namespaces: callbackTagNamespace, misspelledJsDocTypedefTags,
  jsDeclarationsImportNamespacedType, jsDeclarationsImportAliasExposedWithinNamespace, importTypeInJSDoc,
  jsEnumCrossFileExport); `@augments` naming another class is the base (jsdocAugments_nameMismatch,
  jsdocAugments_errorInExtendsExpression, extendsTagEmit, extendsTag2/4); a variable's `@type` return type read
  syntactically even from a type literal with more members (getJSDocReturnType: propertyAssignmentUseParentType2,
  jsdocFunctionType, checkJsdocTypeTagOnObjectProperty1; needs a borrowed return annotation on FunctionImpl); JSDoc
  type parser recovery (jsdocFunctionTypeFalsePositive, jsdocParameterParsingInfiniteLoop); nested `@param` on arrays
  (jsdocParamTagTypeLiteral, jsdocParamTag2); `{...T=}` (jsdocParseStarEquals); `typeof b` in `@param` sees the
  function's own parameters (jsDeclarationsFunctions); jsdocTemplateClass, jsdocTemplateConstructorFunction2,
  returnTagTypeGuard, jsdocTypeTagCast, overloadTag2.
- **JSDoc names resolved lazily** (triage 2026-10-07): tsc resolves names inside a `@returns`/`@param` type literal
  (`{{ new <T>(v: ReadonlyArray<T>): S<T> }}`) and in `@overload` tags only when needed (unresolved ones never get
  TS2304/TS2583), and resolveEntityName's resolveEntityNameFromAssignmentDeclaration re-resolves each name from the
  host function (isUse), marking an outer `@template T` used (webpack deprecation.js:204, +TS6133 under `unused`).
  tttt resolves and reports every JSDoc name in the binder's resolve walk (`error_cannot_find_name()`). Fix: defer
  those reports to the checker's lowering and mark the host-level resolution as a use then. Medium, low value.
- **`@typedef {import("./MC")} MC`** of a `module.exports = function MC() {...}` from another file is `typeof MC` where
  tsc has `{ (): MW; new (): MC; }` (jsdocTypeReferenceToImportOfFunctionExpression): tttt puts the JS-class flag on the
  `export=` symbol (`set_function_class_symbol`), so several_types.c takes the `export = C` type branch; in tsc the
  class-ness is the function expression symbol's and the reference falls through to getTypeFromJSDocValueReference.
- **More symbol leftovers** (agent D 2026-10-06, re-confirmed 2026-10-07): alias-to-property symbol chains (`import {
  version } from "./config.json"` names `config3.version` `version`: nodeModulesJson x3; `exports.fn2 = Math.min`:
  moduleExportsAliasLoop1/2; tttt's props have no symbols); mergeSymbol into an alias merges into its target
  (duplicateVarsAcrossFileBoundaries) and an alias resolving to unknownSymbol is replaced by the merged declaration
  (importEqualsError45874; tttt declares all files' globals into one table at bind time); `var G = m.G = {}` makes one
  container (typeFromPropertyAssignment18); typedefCrossModule3 (the clone mergeSymbols the typedef into `ns.Foo`,
  local references see the clone); sourceFileMergeWithFunction (baseline prints the function's own symbol where tsc
  names the merged one); allowJscheckJsTypeParameterNoCrash, lateBoundAssignmentDeclarationSupport7,
  moduleExportPropertyAssignmentDefault (recordMergedSymbol: `typeof axios` everywhere in the file).
- An orphaned earlier `/** @enum {number} */` comment also declares type alias `b` for `module.exports.b = {}` in tsc
  (every JSDoc comment before a statement attaches; two `@enum`s give TS2300). Mutation-only.
- **Late-bound expandos** (`f[symb] =`, `this[_symbol]`, `F.prototype[k]`): expandoFunctionSymbolPropertyJs,
  lateBoundMethodNameAssigmentJS, lateBoundAssignmentDeclarationSupport4/5 (an annotated host's late-bound expando takes
  the annotation's member by key type since 2026-10-05: webpack's cleverMerge.js).
- **Checker**: TS1055/TS1065 (ES5 async return types, TS too: asyncFunctionDeclaration16_es5); isolatedDeclarations
  (TS9010, none exist); TS7022 for a self-importing JS default export (selfReferentialDefaultNoStackOverflow);
  TS7034/TS7005 for an evolving array read by `yield*` in a loop (yieldExpressionInControlFlow); binding-pattern
  contextual types making defaulted members optional (classExpressionNames: a naive version broke objectRestParameter,
  because tttt gives binding-pattern contexts through generic inference where tsc doesn't); `l = []` parameter
  initializers (typeFromJSInitializer); `module` redeclared per block (moduleExportAssignment2).
- webpack is at 0 (2026-10-05; plan 19).
- Can't pass: templateInsideCallback (pre/post-emit count line), jsDeclarationsDefault /
  jsDeclarationsJSDocRedirectedLookups / jsdocOuterTypeParameters1 (baselines made after declaration emit).
- JSDoc nodes and outer type parameters: tsc instantiates a JSDoc object/function type node for *every* outer type
  parameter (isTypeParameterPossiblyReferenced never meets the `@template` tag), so `@template T @callback B` used as
  `B<string>` prints `B<string>` (jsdocCallbackAndType passes by now, but the mechanism is still narrow). tttt's
  lowering collects the node's real free parameters, and `ty_anonymous_from_node()` can't enumerate the ones in scope;
  making JSDoc nodes free_params_unknown let lib's `then<TResult1>` count as mentioned by a JSDoc-typed T (webpack +2).
  The fixing side is handled narrowly in `assignment_mentions()` (`meets_jsdoc_node_in()`). A real fix records the
  in-scope `@template`s on the lowering context.

- Triage 2026-10-07 (binder/modules share; the TypeScript-side items are in bugs/binder_modules.md):
  - **Lazy TS7008 for `this.x = null`/`undefined`**: tsc reports reportImplicitAny inside
    getWidenedTypeForAssignmentDeclaration, i.e. only when something resolves the member's type (a read `d.y`, a
    method returning `this.w`); the declaring assignment itself types its target as autoType and doesn't. Probe:
    `function C() { this.y = null }` alone is clean under tsc, `function D() { this.y = null }; new D().y` is
    TS7008 at the assignment; the same for a class constructor's `this.w = null` with and without `m() { return
    this.w }`. tttt reports all of them from js_assignment_implicit_any_check(). Needs reporting where
    assignment_symbol_type() first computes the member's type (once, checker-count independent), excluding the
    declaring assignment's own left side. Same shape as the bodiless-renaming TS7031 laziness.
  - allowJscheckJsTypeParameterNoCrash: object literal methods bind with PropertyExcludes (not MethodExcludes), so
    `this.data2 = 1` inside method `data1` (bindThisPropertyAssignment into the literal's members) and the later
    method `data2` are one symbol (both declarations in the symbols baseline); tsc then leaves data2 out of the
    literal's type (`{ data1(val: any): void; }`, `this.data2 : any`). Why it drops it wasn't found.
  - typeFromPropertyAssignment30 (TS): `const c: Combo = () => 1; c.p = {}`: tsc prints the arrow as `{ (): number;
    p: {}; }` (the expando member typed by its initializer) while `c.p` reads Combo's `p`; tttt gives the member
    the annotation's type (getAnnotatedTypeForAssignmentDeclaration would too, so the member the arrow's type gets
    through mergeJSSymbols must be another symbol). typeFromPropertyAssignment18: `var GLOBSTAR = m.GLOBSTAR = {}`
    makes one container, m.GLOBSTAR's (getExpandoSymbol through the chained assignment), `typeof m.GLOBSTAR`.
  - jsDeclarationsClassStaticMethodAugmentation: `Clazz.method.prop = 5` (an expando on a static method) is a member
    of the method in tsc (class statics are the class symbol's exports, lookupSymbolForPropertyAccess finds them);
    tttt's spurious TS2339 because class members aren't binder symbols (lookup_for_property_access() only walks
    exports). Fixing it needs static members reachable as expando hosts.
  - pushTypeGetTypeOfAlias: `module.exports = function () {}; exports.blah = exports.someProp` is an alias whose
    resolution goes through getCommonJsExportEquals' clone back to blah: TS2303 at `exports.blah`; tttt resolves
    the entity alias to nothing, silently.
  - ensureNoCrashExportAssignmentDefineProperrtyPotentialMerge: `B.NS = require(...)` and
    `Object.defineProperty(B, "NS", ...)` on a `module.exports = B` container are TS2323 "Cannot redeclare exported
    variable 'NS'" at both in tsc.
  - typeFromParamTagForFunction mutation (`declare function require(id: number)`): tsc's checkCallExpression
    resolves the signature before isCommonJsRequire, but a require *declaration* doesn't check its initializer; the
    TS2345 tsc shows only with ES module files in the program wasn't reduced.
  - JSDoc attachment: statements and class/object literal members now take leading comments only (fixed: a comment
    on the same line as the previous token is a trailing one). Hosts that tsc lets take trailing comments too
    (parameters, variable declarations, function expressions, parenthesized expressions) still use jsdoc_before();
    the initializer hosts of jsdoc_apply_variable_initializers() (object, class, identifier) are leading-only in tsc
    and aren't distinguished yet.
  - JS declaration emit diagnostics (TS9005/TS9006 jsDeclarationsExportAssignedConstructorFunctionWithSub and
    jsDeclarationsTypeReassignmentFromDeclaration2, TS6232 jsDeclarationsCrossfileMerge, TS18042
    jsDeclarationsImportAliasExposedWithinNamespace, TS5088 arrayFakeFlatNoCrashInferenceDeclarations) all come from
    JS declaration emit (symbolTableToDeclarationStatements), which tttt doesn't do (plan 40 excludes it; emit is out
    of scope). TS5088 is plan 40's leftover for TS too. Leave them unless declaration emit comes into scope.
- Multi/js mutation run seed 16 leftovers (the fixed ones are commits of 2026-10-07; TS ones in bugs/binder_modules.md):
  - mut1602 (checkJsdocTypeTag2, `const x1: never = (a) => a + 1` under `/** @type {function (number)} */`): with a
    TS annotation in JS (TS8010) tsc still types the arrow by the `@type` signature (getSignatureOfTypeTag reads the
    host's tag), so it is `(a: number) => any` and the JSDoc function type gets TS7014; tttt drops the tag when the
    binding has a written type.
  - mut2021 (typeFromPropertyAssignment39, `const bar = {}; bar["baz"]["blah"] = 3`): tsc gives `bar` a namespace
    declaration (TS7053 says 'typeof bar', the symbols baseline a second declaration at the statement's full start);
    the dotted form works, the literal element-access chain doesn't reach add_expando_namespace_decls().
  - mut2107 (typeTagCircularReferenceOnConstructorFunction without `MyClass.prototype = {}`): `/** @type {MyClass} */
    function MyClass() {}` is TS7023 (the function's type is its own value type, circular) in tsc; tttt is silent.
  - mut2183 (jsDeclarationsExportDefinePropertyEmit, `Object.defineProperty(module.exportsX, "d", ...)`): the typo
    makes tsc's bindPotentiallyMissingNamespaces declare `module` as a namespace, so TS2551 prints 'typeof module';
    tttt's commonjs_module_namespace() covers assignments, not defineProperty targets.
  - mut2973 (fillInMissingTypeArgsOnJSConstructCalls, `super(SubA, 1)`): missing TS2554 against a base written with
    Flow syntax (`class B<T: BaseA>`, TS1005 recovery); plain JS classes give it. Parser recovery of the type
    parameter list.
  - mut0091 (enumTag, `@enum {function(number): string}`): tsc reports one TS2322 at a member name
    ('(n: any) => number' not assignable to 'Fs'), tttt elaborates to the arrow's return of each member.
  - mut0417 (enumTagOnExports): the orphaned `@enum` leftover above.
