# JavaScript and JSDoc: open divergences
`scripts/jsstats.py` (after `scripts/failstats.py`) lists the failing JS tests by feature; 79 of 1165 failed after the
JS rework (todo entry 8 of the thirteenth window, 2026-10-09), which deleted what tsgo 7 removed (constructor
functions, value-as-type, `@enum`, Closure syntax, CommonJS aliases). documentation.md "JSDoc" / "CommonJS" describe
what exists; plan 26 (the 6.0-era design and its leftovers) was retired 2026-10-09 (git has it). Pruned 2026-10-09
against the tsgo 7.1 suite.

## Binding and merging
- A JSDoc `@typedef T` and a same-named class/variable in a JS module (ES or CommonJS) is TS2300 in tsgo (the typedef
  gets a local twin, declareModuleMember), none in tttt (typedefCrossModule2's Foo, probe `/** @typedef {number} Foo */
  class Foo {}`). tsgo also binds top-level typedefs after every other statement: declaration lists put them last
  (jsTypedefMergedWithModuleExportProperty's symbols: a `@typedef T` merged with `const T` exported through
  `module.exports.T = T` lists the const first in tsgo; symbol_add_decl's reordering doesn't reach this merge).
- `Object.defineProperty(o, "name", ...)` on a non-exports object names the member as written, `"name"` with quotes
  (checkObjectDefineProperty); exports already do (add_define_property_export()). Needs a display span separate from
  the lookup name in add_expando_member().
- `obj.y = Object.values(obj)` of a JS `{}` container (expandoNoInferredIndex, expandoObjectIndexSignatures): tsgo
  reports TS7022 for the cycle through the container's type inside the call's inference (y any); tttt's
  guarded_assignment_symbol_type() is entered for y, but resolving `Object.values(obj)` never asks for y's type again
  (past the call's resolving barrier, Checker.resolving_start? overload 2 `values(o: {}): any[]` chosen? check
  whether infer.c's index-signature inference from the expando namespace type reads the props).
- typeFromContextualThisType symbols: `this` in a method of a literal typed by `@type {{...}}` on `const o1` is
  `Symbol(o1, Decl(type literal))` in tsgo (the reparsed type literal is named by the variable).
- `exports.fn2 = Math.min`: tsgo's symbols baseline names `Math.min` `Symbol(fn2, Decl(lib.es5.d.ts...))` (an alias
  declaration's entity name shows the alias's name with the target's declarations: moduleExportsAliasLoop1/2). Quirk.
- A module augmentation's module in the types baseline (`declare module "./test"` of a JS module): tsgo prints
  `typeof import("./test")` from the augmenting file, tttt `typeof import(".")`
  (jsExportMemberMergedWithModuleAugmentation2); naming it from outside the augmentation lost two TS tests.
- numericExportNameDeclaration (TYP): JS `module.exports[1] = 2` is `error` to tsgo's types baseline (any in tttt);
  `exports[1]` is any.
- Mutant one-offs (JS mutants 2026-10-09): TS2540 read-only `NS` (ensureNoCrashExportAssignmentDefineProperrtyPotentialMerge),
  moduleExportAlias5 TS2339, jsDeclarationsFunctions TS2323 (`export { j }` twice).

## Types
- Returned object literals aren't widened in tsgo: `function f() { return { x: [] } }` is `() => { x: undefined[] }`
  with strict off (tttt: any[]), but `{ x: [], y: null }` widens both and `const o = { x: [] }` is any[]
  (argumentsReferenceInObjectLiteral_Js and probes). Some object flag (ContainsObjectOrArrayLiteral /
  RequiresWidening) propagates differently in tsgo; find which and port it.
- A JS variable with both a TS annotation (TS8010) and a JSDoc `@type` on a function initializer: tsgo's reparser
  makes the `@type` the function's FullSignature, so the function prints as the tag's signature (`(this: Foo) =>
  void`); tttt keeps the contextual typing. Artificial.
- jsdocTypedefBeforeParenthesizedExpression: `@param a {NotADuplicateIdentifier}` with the typedef in a comment before
  a parenthesized expression statement prints `number` in tsgo, the alias name in tttt.

## JSDoc diagnostics
- An earlier comment's `@extends` still counts for TS8023 in tttt (checkJSDocAugmentsTagMatchesExtends reads every
  comment). No test.
- tsgo quirks, port only if cheap: typedefTagWrapping (a `@typedef` whose type fails to parse has a missing name,
  declared globally as '(Missing)': TS2300 "Duplicate identifier '(Missing)'" across three scripts); callbackTag4 (a
  `@this` tag's synthetic `this` parameter is named by the tag's whole text in the symbols baseline).

## Declaration diagnostics in JS
Only TS6424 is ported (`js_declaration_diagnostics_check()`); the declaration walk skips JS entirely
(`declaration_diagnostics_wanted()`): TS4023 (jsDeclarationsTypeReassignmentFromDeclaration2,
cjsObjectDefinePropertyPrivateModuleDeclarationEmit: `Object.defineProperty(exports, "api", { value: require("pkg") })`
whose type uses a non-exported interface of an `export =` module, name `'"api"'`), TS9013 for `this.x = foo()` in a
JS constructor under isolatedDeclarations (isolatedDeclarationsJsThisPropertyAssignmentInference).

## Older binder gaps (written against tsc 6, before the JS rework: recheck against tsgo before working on one)
Binder gaps (spurious TS2339 and friends):
- `exports.x =` next to `module.exports = {...}` (typedefCrossModule2: tsc merges them).
- **CommonJS exports inside functions**: tsc binds `exports.x = v` / `module.exports = v` from any container
  (bindExportsPropertyAssignment has no container check); `add_nested_commonjs_exports()` only walks top-level
  statement lists. Needs a declare-step walk into function bodies (the link step must see them).
- **`this.x = v` at the top level of a CommonJS module** should declare an export (bindThisPropertyAssignment,
  SourceFile container with commonJsModuleIndicator); only the script (global) case is done (`add_global_this_chain()`).
Other JS tests: jsDeclarationsImportAliasExposedWithinNamespace (symbols; a `@typedef ns.x` namespace merged with an
exported const), fixSignatureCaching (types; an expando-heavy JS-like file). Declaration emit (plan 40):
jsDeclarationsExportAssignedConstructorFunctionWithSub(target=es2015) (TS9005), jsDeclarationsTypeReassignmentFromDeclaration2
(TS9006).
- CommonJS leftovers: `exports.Date = Date; exports.Date = 1` also gets TS2322; `function f() { var z = 1; exports.z =
  z; }` gets TS2339 (exports inside functions).
- TS2417 (class static side) doesn't see a JS class's expando members; `export as namespace` in a JS file isn't bound
  (`scope_add_port()`). (From hacks.md, 2026-10-08)
- importTag13: `@import x = require("m")` is TS1005 'from' expected in tsgo, which then parses the module specifier as
  an expression and says TS1141 over `= require("types")`; tttt's JSDoc @import parser stops.
