# JavaScript and JSDoc: open divergences
`scripts/jsstats.py` (after `scripts/failstats.py`) lists the failing JS tests by feature. documentation.md "JSDoc" /
"CommonJS" describe what exists. The suite/JS tail fan-out of 2026-10-12 (agent D) fixed 41 of the 63 JS tests left (with agent A's duplicate diagnostics);
what remains is below, each with what was found.

## Binding and merging
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
  without re-resolving: the fix is in lazy_type_get()'s reentrancy, broad.)
- jsContainerMergeTsDeclaration3 (TYP): `declare class A {}` in a.d.ts and `const A = {}; A.d = {}` in b.js (TS2451
  between them): tsgo's binder makes d a member of the `{}` literal's symbol (b.js's own local, before the global
  merge), so the literal prints `{ d: {}; }`; tttt's host is the merged global, the literal prints `{}`. Conflict-only.
- `exports.fn2 = Math.min`: tsgo's symbols baseline names `Math.min` `Symbol(fn2, Decl(lib.es5.d.ts...))` (an alias
  declaration's entity name shows the alias's name with the target's declarations: moduleExportsAliasLoop1/2). Quirk.
- A module augmentation's module in the types baseline (`declare module "./test"` of a JS module): tsgo prints
  `typeof import("./test")` from the augmenting file, tttt `typeof import(".")`
  (jsExportMemberMergedWithModuleAugmentation2); naming it from outside the augmentation lost two TS tests.
- numericExportNameDeclaration (TYP): JS `module.exports[1] = 2` is `error` to tsgo's types baseline (any in tttt);
  `exports[1]` is any.
- jsDeclarationsImportAliasExposedWithinNamespace (SYM): the declarations of `myTypes` through `export {myTypes}`
  (a const merged with `@typedef myTypes.x` namespaces): tsgo lists the namespace declarations first and the export
  specifier last; tttt's baseline_symbol_declaration_spans() puts the specifier first.
- jsDeclarationsExportAssignedConstructorFunctionWithSub (SYM only): `this` in `module.exports.Sub = function () {
  this }` where `module.exports = function (p) {}`: tsgo names it `Symbol(exports, Decl(..., 3, 16))` (the function
  expression's symbol, named after the assignment?). Quirk.
- lateBoundAssignmentDeclarationSupport5/7: a unique-symbol key printed from another file is `[_sym]` in tsgo (the
  declaration's name, not accessible there), `[x.S]` in tttt (an accessible chain through the require alias), and
  the symbol `x.F[F[_str]]` vs `x.F[_str]`. Not reduced further.

## Types
- Returned object literals aren't widened in tsgo: `function f() { return { x: [] } }` is `() => { x: undefined[] }`
  with strict off (tttt: any[]), but `{ x: [], y: null }` widens both and `const o = { x: [] }` is any[]
  (argumentsReferenceInObjectLiteral_Js and probes). Some object flag (ContainsObjectOrArrayLiteral /
  RequiresWidening) propagates differently in tsgo; find which and port it. Odd harness detail (2026-10-14): the
  suite test fails only with `@alwaysStrict: true` set after other options; a filetest copy with the option earlier
  in the header passes, and the CLI with strict false gives any[] either way: something in option application order.
- Binder: an `export =`/`export default` in a function expression, arrow or method body (not a declaration) isn't a
  member of its value in tttt (declare_function_export() only knows a function declaration's symbol); tsgo's
  bindExportAssignment declares it in any container symbol's exports.
- A JS variable with both a TS annotation (TS8010) and a JSDoc `@type` on a function initializer: tsgo's reparser
  makes the `@type` the function's FullSignature, so the function prints as the tag's signature (`(this: Foo) =>
  void`); tttt keeps the contextual typing. Artificial.
- exportDefaultWithJSDoc1 (TYP): `/** @type {NumberLike[]} */export default ([ ]);` prints `([ ]) : NumberLike[]`
  in the test (with a b.ts importing the default) but `never[]` in probes without b.ts (tsgo, checkJs or not): the
  import's resolution changes the cached type of the parenthesized expression. Check order / tsgo artifact, left.
- importTypeInJSDoc (TYP): `constructor(x: MyClass.Bar)` in a .d.ts with `export = MyClass` (class + namespace) prints
  `import("./externs").Bar` in tttt, `MyClass.Bar` as written in tsgo.
- jsFunctionWithPrototypeNoErrorTruncationNoCrash (TYP): `Color.prototype = { negate: function () { return this }, ...}`:
  the `this` of each method is the literal's type, whose printing nests one level more in tsgo (each method's return
  shows the literal again with itself as `any`): tttt prints `negate: () => any` where tsgo expands it once.

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
