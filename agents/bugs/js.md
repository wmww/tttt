# JavaScript and JSDoc: open divergences
`scripts/jsstats.py` (after `scripts/failstats.py`) lists the failing JS tests by feature. documentation.md "JSDoc" /
"CommonJS" describe what exists. The suite/JS tail pass of 2026-10-12 fixed 41 of the 63 JS tests left (with a duplicate-diagnostics fix);
what remains is below, each with what was found.

## Binding and merging
- Merged `var`s of several scripts each have their own `{}` container to tsgo's binder (`var C = {}; C.f = 1` in b.js,
  `var C = {}; C.g = 2` in c.js: C is `{ f: number }`, c.js's literal `{ g: number }`, TS2403 and TS2339 for `C.g`);
  tttt's one global symbol collects both expandos. Conflicting declarations already work (ScopeRare.script_duplicates,
  ttttScriptConflictExpandoHost); merged ones would need the expando host to be the file's own initializer's symbol. [open]

## Types
- Binder: an `export =`/`export default` in a function expression, arrow or method body (not a declaration) isn't a
  member of its value in tttt (declare_function_export() only knows a function declaration's symbol); tsgo's
  bindExportAssignment declares it in any container symbol's exports. [open]
- A JS variable with both a TS annotation (TS8010) and a JSDoc `@type` on a function initializer: tsgo's reparser
  makes the `@type` the function's FullSignature, so the function prints as the tag's signature (`(this: Foo) =>
  void`); tttt keeps the contextual typing. Artificial. [open]
- jsFunctionWithPrototypeNoErrorTruncationNoCrash (TYP): see bugs/display.md "Fresh object literal types". [dup]

## JSDoc diagnostics
- An earlier comment's `@extends` still counts for TS8023 in tttt (checkJSDocAugmentsTagMatchesExtends reads every
  comment). No test. [open]

## Declaration diagnostics in JS
Ported (2026-10-15): TS6424, isolatedDeclarations' walk (with a class's `this.x =` members as TS9013:
check_js_this_properties()), and outside isolatedDeclarations a CommonJS file's `module.exports = x` of a local
variable, `Object.defineProperty(exports, "n", { value: e })` and `exports.x = e` (not a function or class)
serialized like variables (check_js_commonjs_exports(): TS4023, TS5088...). Not walked: JS functions and classes,
expandos, JSDoc typedefs/namespaces, `module.exports = { ... }` literals, ES module exports of JS files, a base class's
inference fallback; the full JS declaration emit is the JavaScript declaration emit work. No suite test left. [design]

## Older binder gaps (written against tsc 6, before the JS rework: recheck against tsgo before working on one)
- `exports.x =` next to `module.exports = {...}`. [fixed probe 2026-10-23]
- **CommonJS exports inside functions**: tsc binds `exports.x = v` / `module.exports = v` from any container
  (bindExportsPropertyAssignment has no container check); `add_nested_commonjs_exports()` only walks top-level
  statement lists. Needs a declare-step walk into function bodies (the link step must see them). [fixed probe 2026-10-23]
- **`this.x = v` at the top level of a CommonJS module** should declare an export (bindThisPropertyAssignment,
  SourceFile container with commonJsModuleIndicator); only the script (global) case is done (`add_global_this_chain()`). [fixed probe 2026-10-23]
- CommonJS leftovers: `exports.Date = Date; exports.Date = 1` also gets TS2322; `function f() { var z = 1; exports.z =
  z; }` gets TS2339 (exports inside functions). [open]
- TS2417 (class static side) doesn't see a JS class's expando members; `export as namespace` in a JS file isn't bound
  (`scope_add_port()`). (From hacks.md, 2026-10-08) [open]
- Mutant one-offs (JS mutants 2026-10-09): TS2540 read-only `NS` (ensureNoCrashExportAssignmentDefineProperrtyPotentialMerge),
  moduleExportAlias5 TS2339, jsDeclarationsFunctions TS2323 (`export { j }` twice). [open]

## Unfiled suite failure (triage 2026-10-14)
- Assigning a function to a class's static method (`class L { static m() {} }` then `L.m = function (q) {}`; a class
  expression's too, `var K = class {...}`, `ns.sub = class {...}`) misses tsgo's TS2322 "Type '(q: any) => void' is
  not assignable to type '() => void'" (found next to ttttExpandoClassExpressionStatics, triage 2026-10-14). [open]

## `@extends` on a mixin over `any` (extendsTag10, the 7.1.0-beta, 2026-10-23)
`/** @extends {A<string>} */ class B extends mixin(A) {}` with `mixin(Base: any)` returning `class extends Base`: the
beta gives the clause `<string>` (any non-entity clause takes the tag's type arguments) and is silent, with a B that
has neither `value` nor `extra` (TS2339 on both); tttt reports TS2315 "Type '(Anonymous class)' is not generic" and
`new B()` "not constructable" (TS2351). What tsgo's resolveBaseTypesOfClass does with type arguments on a
non-generic constructor type isn't emulated. [open]

- **`const [x] = require("m")` and `const { ...rest } = require("m")` are named imports in tsgo** (bun 08-C9a,
  25-C20a, 33-C6c): tsgo's binder declares an alias for every binding element of a require-initialized declaration
  (ast.IsVariableDeclarationInitializedToRequire walks a BindingElement up to its declaration), array patterns and
  object rest elements included, so `const [arr2] = require("./esm2")` reports TS2305 "has no exported member 'arr2'"
  and `const [x] = require("./m")` with `exports.x` is silent. tttt's note_require_declaration() (parser.c) takes only
  identifiers and object patterns of plain properties, so it destructures the module object: a spurious TS2488 (the
  namespace isn't iterable) and a missing TS2305. Not confirmed whether Strada does the same (likely a tsgo
  simplification, then [tsgo]). [open]
