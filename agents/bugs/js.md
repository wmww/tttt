# JavaScript and JSDoc: open divergences
`scripts/jsstats.py` (after `scripts/failstats.py`) lists the failing JS tests by feature. documentation.md "JSDoc" /
"CommonJS" describe what exists. The suite/JS tail fan-out of 2026-10-12 (agent D) fixed 41 of the 63 JS tests left (with agent A's duplicate diagnostics);
what remains is below, each with what was found.

## Binding and merging
- Merged `var`s of several scripts each have their own `{}` container to tsgo's binder (`var C = {}; C.f = 1` in b.js,
  `var C = {}; C.g = 2` in c.js: C is `{ f: number }`, c.js's literal `{ g: number }`, TS2403 and TS2339 for `C.g`);
  tttt's one global symbol collects both expandos. Conflicting declarations already work (ScopeRare.script_duplicates,
  ttttScriptConflictExpandoHost); merged ones would need the expando host to be the file's own initializer's symbol.
- numericExportNameDeclaration (TYP): JS `module.exports[1] = 2` is `error` to tsgo's types baseline (any in tttt):
  only with `@declaration`/`@emitDeclarationOnly`, without them tsgo says any too (probe 2026-10-14): tsgo's harness
  emits declarations before it collects diagnostics and that emit caches errorType on the node. Plan 64's bucket.

## Types
- argumentsReferenceInObjectLiteral_Js(alwaysstrict=true) (TYP): with strict off `() => { return { x: [] } }` prints
  `() => { x: undefined[] }` in tsgo (TS too, and in a TS2322 message, so not node reuse), but `{ x: [], y: null }`
  prints widened (`{ x: any[]; y: any }`) and `const v = f()` is `{ x: any[] }`. tttt prints any[]. Instrumented tsgo
  (2026-10-14, getReturnTypeFromBody): the body's type before getWidenedType is `{ x: undefined[] }` with
  ObjectFlags 0x232090 and after it `{ x: any[] }`, and getReturnTypeOfSignature stores that (`{ x: any[] }`, nothing
  set before it), twice per function (two signatures?). So the printer's return type isn't that signature's: find
  which signature/type the message's `() => { x: undefined[] }` prints (serializeReturnTypeForSignature reads
  ctx.enclosingSymbolTypes first; a second signature made from the unwidened body type?). The earlier note that
  option order mattered was wrong: filetest fails the same way. ~half a session with the instrumented tsgo.
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
- jsEnumCrossFileExport (2026-10-15, agent C: one mechanism left, ~1 session): `var Host = {}` with a dotted
  `@typedef {string} Host.UserMetrics.Bargh` in the same file. tsgo's reparser makes the typedef `namespace Host {
  export namespace UserMetrics { export type Bargh = string } }`, merged into Host's exports, while the expandos of a
  `{}` container go into the *object literal's* symbol (getInitializerSymbol returns `initializer.Symbol()`), a table
  of its own: `Host.UserMetrics = {}` is still an expando (Decl at the assignment), and the JSDoc type
  `Host.UserMetrics.Action` finds the typedef namespace, no Action, then the value: TS2749. tttt keeps both in
  Host's exports, so add_expando_member() sees the typedef namespace `UserMetrics` and declares no expando (TS2339
  on every `Host.UserMetrics`, TS2694 instead of TS2749). Fix: a `{}` container's expandos in a table of their own
  (the literal's, e.g. SymbolRare of the host or a binder symbol per container literal): add_expando_member(),
  expando_host()'s nested lookup, ty_namespace_type()/make_namespace() of the container, expr.c's
  expando_container type, and the value-meaning entity resolution of JSDoc names (`Host.UserMetrics.Action` as a value
  for TS2749) read it; namespace-meaning lookups keep reading Host's exports. Everything else in the test (TS1003 of
  the nameless `@typedef`, the symbols' Decl positions) already matches.

## Declaration diagnostics in JS
Only TS6424 and isolatedDeclarations are ported for JS (`js_declaration_diagnostics_check()`,
`declaration_diagnostics_wanted()`): tsgo's declaration transformer runs on JS too (transform.go: CommonJS exports,
this-property assignments collected into class members, `collectThisPropertyAssignments`). Missing: TS4023
(jsDeclarationsTypeReassignmentFromDeclaration2, cjsObjectDefinePropertyPrivateModuleDeclarationEmit:
`Object.defineProperty(exports, "api", { value: require("pkg") })` whose type uses a non-exported interface of an
`export =` module, name `'"api"'`), TS5088 (commonJSImportNestedClassTypeReference: `exports.K = NS.K` of a class
expression expando whose method returns `new NS.K()`), TS9013 for `this.x = foo()` in a JS constructor under
isolatedDeclarations (isolatedDeclarationsJsThisPropertyAssignmentInference: tsgo's visitThisPropertyAssignments ->
ensureType(binary expression), reported over the whole assignment; check_class() would collect the first `this.x =`
per name in constructors, methods and static blocks of a JS class, skipping names the class declares). Plan-sized as a
whole (JS declaration emit's walk); the TS9013 part alone is ~60 lines.

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
