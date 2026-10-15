# JavaScript and JSDoc: open divergences
`scripts/jsstats.py` (after `scripts/failstats.py`) lists the failing JS tests by feature. documentation.md "JSDoc" /
"CommonJS" describe what exists. The suite/JS tail fan-out of 2026-10-12 (agent D) fixed 41 of the 63 JS tests left (with agent A's duplicate diagnostics);
what remains is below, each with what was found.

## Binding and merging
- Merged `var`s of several scripts each have their own `{}` container to tsgo's binder (`var C = {}; C.f = 1` in b.js,
  `var C = {}; C.g = 2` in c.js: C is `{ f: number }`, c.js's literal `{ g: number }`, TS2403 and TS2339 for `C.g`);
  tttt's one global symbol collects both expandos. Conflicting declarations already work (ScopeRare.script_duplicates,
  ttttScriptConflictExpandoHost); merged ones would need the expando host to be the file's own initializer's symbol.

## Types
- Binder: an `export =`/`export default` in a function expression, arrow or method body (not a declaration) isn't a
  member of its value in tttt (declare_function_export() only knows a function declaration's symbol); tsgo's
  bindExportAssignment declares it in any container symbol's exports.
- A JS variable with both a TS annotation (TS8010) and a JSDoc `@type` on a function initializer: tsgo's reparser
  makes the `@type` the function's FullSignature, so the function prints as the tag's signature (`(this: Foo) =>
  void`); tttt keeps the contextual typing. Artificial.
- importTypeInJSDoc (TYP): `constructor(x: MyClass.Bar)` in a .d.ts with `export = MyClass` (class + namespace) prints
  `import("./externs").Bar` in tttt, `MyClass.Bar` as written in tsgo.
- jsFunctionWithPrototypeNoErrorTruncationNoCrash (TYP): see bugs/display.md "Fresh object literal types".

## JSDoc diagnostics
- An earlier comment's `@extends` still counts for TS8023 in tttt (checkJSDocAugmentsTagMatchesExtends reads every
  comment). No test.

## Declaration diagnostics in JS
Ported (2026-10-15): TS6424, isolatedDeclarations' walk (with a class's `this.x =` members as TS9013:
check_js_this_properties()), and outside isolatedDeclarations a CommonJS file's `module.exports = x` of a local
variable, `Object.defineProperty(exports, "n", { value: e })` and `exports.x = e` (not a function or class)
serialized like variables (check_js_commonjs_exports(): TS4023, TS5088...). Not walked: JS functions and classes,
expandos, JSDoc typedefs/namespaces, `module.exports = { ... }` literals, ES module exports of JS files, a base class's
inference fallback; the full JS declaration emit is plan 64 stage 6. No suite test left.

## Older binder gaps (written against tsc 6, before the JS rework: recheck against tsgo before working on one)
- `exports.x =` next to `module.exports = {...}`.
- **CommonJS exports inside functions**: tsc binds `exports.x = v` / `module.exports = v` from any container
  (bindExportsPropertyAssignment has no container check); `add_nested_commonjs_exports()` only walks top-level
  statement lists. Needs a declare-step walk into function bodies (the link step must see them).
- **`this.x = v` at the top level of a CommonJS module** should declare an export (bindThisPropertyAssignment,
  SourceFile container with commonJsModuleIndicator); only the script (global) case is done (`add_global_this_chain()`).
- CommonJS leftovers: `exports.Date = Date; exports.Date = 1` also gets TS2322; `function f() { var z = 1; exports.z =
  z; }` gets TS2339 (exports inside functions).
- TS2417 (class static side) doesn't see a JS class's expando members; `export as namespace` in a JS file isn't bound
  (`scope_add_port()`). (From hacks.md, 2026-10-08)
- Mutant one-offs (JS mutants 2026-10-09): TS2540 read-only `NS` (ensureNoCrashExportAssignmentDefineProperrtyPotentialMerge),
  moduleExportAlias5 TS2339, jsDeclarationsFunctions TS2323 (`export { j }` twice).

## Unfiled suite failure (triage 2026-10-14, plan 66)
- Assigning a function to a class's static method (`class L { static m() {} }` then `L.m = function (q) {}`; a class
  expression's too, `var K = class {...}`, `ns.sub = class {...}`) misses tsgo's TS2322 "Type '(q: any) => void' is
  not assignable to type '() => void'" (found next to ttttExpandoClassExpressionStatics, triage 2026-10-14).
