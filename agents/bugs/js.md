# JavaScript and JSDoc: open divergences
`scripts/jsstats.py` (after `scripts/failstats.py`) lists the failing JS tests by feature. documentation.md "JSDoc" /
"CommonJS" describe what exists. The suite/JS tail fan-out of 2026-10-12 (agent D) fixed 41 of the 63 JS tests left (with agent A's duplicate diagnostics);
what remains is below, each with what was found.

## Binding and merging
- Merged `var`s of several scripts each have their own `{}` container to tsgo's binder (`var C = {}; C.f = 1` in b.js,
  `var C = {}; C.g = 2` in c.js: C is `{ f: number }`, c.js's literal `{ g: number }`, TS2403 and TS2339 for `C.g`);
  tttt's one global symbol collects both expandos. Conflicting declarations already work (ScopeRare.script_duplicates,
  ttttScriptConflictExpandoHost); merged ones would need the expando host to be the file's own initializer's symbol.
- `obj.y = Object.values(obj)` of a JS `{}` container (expandoNoInferredIndex, expandoObjectIndexSignatures): tsgo
  reports TS7022 for the cycle through the container's type inside the call's inference (y any); tttt's
  guarded_assignment_symbol_type() is entered for y, but resolving `Object.values(obj)` never asks for y's type again
  (past the call's resolving barrier, Checker.resolving_start? overload 2 `values(o: {}): any[]` chosen? check
  whether infer.c's index-signature inference from the expando namespace type reads the props: tsgo's
  getImplicitIndexTypeOfType reads every property's type, y's included). (2026-10-14: inference from the container
  works, `Object.values(obj)` without the cycle is `string[]`; TTTT_DEBUG_CYCLE prints nothing. tsgo's path: y's
  second evaluation, inside getResolvedSignature's reset resolutionStart, checks the call again
  (checkExpressionCached has no in-flight guard; the cached resolvingSignature keeps resolutionStart) and that
  inference meets y pushed twice. tttt's second evaluation likely gets the in-flight call expression's lazy type
  without re-resolving: the fix is in lazy_type_get()'s reentrancy, broad.) (2026-10-14, agent C: traced it. The
  check of `obj.buzz = ...` asks buzz's namespace Prop (prop_type: IN_FLIGHT), guarded_assignment_symbol_type pushes
  buzz, the call infers T from the container's implied index signature, reads every Prop, buzz's included: prop_type
  finds it IN_FLIGHT and returns circular at once, and ty_resolving_cycle(RESOLVING_PROP) below the call's barrier
  marks nothing above it: no TS7022, buzz `any[]`. Re-evaluating the Prop once from below the barrier, like
  lazy_type_evaluate_inner() does for declarations, wasn't enough (still no cycle reported: the second evaluation of
  the call's lazy type doesn't reach buzz's DECLARATION entry again). Needs tsgo's exact re-entry: which of
  getResolvedSignature's cached resolvingSignature / checkExpressionCached re-checks the call. ~1 session with an
  instrumented tsgo, scripts/variancetrace.py's build() shows how.)
- A module augmentation's module in the types baseline (`declare module "./test"` of a JS module): tsgo prints
  `typeof import("./test")` from the augmenting file, tttt `typeof import(".")`
  (jsExportMemberMergedWithModuleAugmentation2); naming it from outside the augmentation lost two TS tests.
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
- jsEnumCrossFileExport: `@enum` is gone in 7 (TS2749 where a namespace-typed expando is used as a type), and the
  enum objects' `Host.UserMetrics = {}` chain (TS2339 in tttt, an expando of an expando of a `var Host = {}`) isn't
  bound like tsgo's; also a TS1003 at a `@typedef` with a missing name before `*/` on the next line. Several causes.

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
