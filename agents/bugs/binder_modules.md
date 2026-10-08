# Binder, symbols, merging, modules, CommonJS: open divergences

Leftovers of the 2026-10-07 triage (binder/modules share). Each entry: the test, tsc's mechanism, why tttt differs,
and what a fix needs. JS/JSDoc-specific items are in plans/26_js_jsdoc.md's Leftovers.

## Export= clones (tsc's mergeSymbol clone, getCommonJsExportEquals, cloneTypeAsModuleType)
Plan 50 (2026-10-08, deleted once done; its write-up is commit 319c0951) did getCommonJsExportEquals in place at
link time: `merge_commonjs_export_equals()` merges a module's other exports into what its `export =` assigns, no
clone (tttt's binder merges in place, so a clone recorded as the merged symbol is the same thing). Stage 0 found
tsc clones whenever the module's own file is checked, not only on import. The entries below are what is left
(single-test ordering quirks of tsc): an augmentation into a cloneTypeAsModuleType clone, JS `module.exports = {...}`
as an augmentable export=, mergeSymbol's alias rules across files.
tttt merges augmentations straight into what `export =` assigns (merge_augmentations()); tsc clones it and records
the clone as the merged symbol, which everything that resolves names sees. That is equivalent except where tsc's
clone is a *different* symbol than the one a later lookup finds:
- moduleAugmentationDuringSyntheticDefaultCheck: `declare module "moment-timezone"` where moment-timezone's
  `export = moment` names an esModuleInterop `import * as moment` (tsc's resolveESModuleSymbol ->
  cloneTypeAsModuleType: a transient clone typed `{ default: ... }`). The augmentation merges into that clone (its
  type `{ default: () => Moment; }`, moment's own symbol keeps 4 declarations); tttt merges into moment. Needs the
  namespace-import clone (TyAnonymous.module_clone / synthetic_default) to be a binder symbol augmentations can
  target.
- jsExportMemberMergedWithModuleAugmentation3 (symbols): `module.exports.x = 1; module.exports = require("./y.js")`
  with y's `export type x`: getCommonJsExportEquals clones y's *module* and mergeSymbol's x.js's `x` into y's `x`
  (an Assignment merges with anything), so every reference to either `x` shows both declarations.
  Stage 0 (2026-10-08, tsc instrumented) says otherwise: tsc's export= here is a Property|Assignment symbol (not
  an alias), and the merge of the two `x` is getInitializerTypeFromAssignmentDeclaration's `mergeSymbol(s,
  exportedMember)` while typing `module.exports` (x.js's x cloned, y's x recorded into it). tttt makes the export=
  an alias to y's module and merge_commonjs_export_equals() skips module targets; tried adding the declarations to
  y's x: not enough (x.js's own exports table must point at the merged symbol too).
- mergedDeclarations7: `import { Passport } from "passport"` of an `export =` const+namespace. tsc's
  getExternalModuleMember combines the namespace's interface `Passport` with the const's property `Passport`
  (combineValueAndTypeSymbols): a fresh transient symbol with the interface's flags, so getDeclaredTypeOfSymbol makes
  a *second* `Passport` interface type with its own `this`; `passport.use()` (PassportStatic) is then not assignable
  to it (TS2322, "'this' could be instantiated with a different subtype"), printed `Passport` vs `passport.Passport`.
  A tsc quirk; emulating it means a declared type per combined alias symbol (PortItem value_target + type target).
- jsExportMemberMergedWithModuleAugmentation: `module.exports = { Abcde }` (shorthand only) binds each property as an
  alias export, no export= (tsc's bindModuleExportsAssignment); the augmentation merges into the file's module
  symbol (a clone recorded as its merged symbol), printed `Symbol("./test", Decl(test.js, 0, 0), Decl(index.ts, 0,
  31))` and `typeof module."./test"` where tttt says `module.exports`.
- es6ExportEqualsInterop (`z2.a` named `a2`) and lateBoundAssignmentDeclarationSupport7 (`x.F.F[_str]`) involve no
  clone at all (stage 0 logged none): symbol-chain naming bugs of their own.
- jsExportMemberMergedWithModuleAugmentation2: `module.exports = { a: "ok" }` is a Property|ValueModule export= to
  tsc's binder (bindModuleExportsAssignment), so `declare module "./test" { export const a: number }` merges into it
  (tttt: TS2671, merge_augmentations() wants SYMBOL_NAMESPACE), and getInitializerTypeFromAssignmentDeclaration unions
  the literal's `a` with the export's (`{ a: string | number }`) and reports TS2300 at both when they come from
  different files. Needs: the JS export= assignment symbol as an augmentable namespace, the union with the
  cross-file TS2300 in the export= type, and the module/augmentation display (`Symbol("./test", ...)`).

## Merges tsc decides after aliases resolve
tttt declares every file's globals into one table at bind time; tsc's initializeTypeChecker merges per file with
mergeSymbol, which resolves an alias target first.
- importEqualsError45874: a global `import Foo = globals.toString.Blah` (resolves to unknownSymbol) and another
  file's `const Foo`: mergeSymbol returns the source, so the const replaces the alias in globals and they stay two
  symbols (no error, `Foo : any` at the import). tttt merges them in declare_symbol(). A fix would split them in
  binder_link() once the alias resolves to nothing.
- expandoFunctionContextualTypesNoValue: `import Foo from "blah"` (unresolved) and `export function Foo() {}` share
  the module's local (Alias|ExportValue to tsc's binder); a reference `Foo` reaches the export through
  getExportSymbolOfValueSymbolIfExported, `typeof import("./x").Foo` with the expando `Foo.bar`. tttt's references
  find the unresolved alias (any).

## Symbol display of duplicates and merged members
- genericClassesRedeclaration: two `declare module TypeScript {}` blocks each declare `class StringHashTable<T>`
  (TS2300). In the second block tsc's name resolves to the block's local, whose exportSymbol is the duplicate class
  (parent TypeScript, not in the exports table), so it prints qualified `TypeScript.StringHashTable<T>` (from the
  declaration name and from `extends StringHashTable<T>`); tttt finds the first export and prints it bare.
- mergedClassWithNamespacePrototype: `declare class Foo {}` and in another file `declare namespace Foo { namespace
  prototype { function f(): void } }`. The namespace conflicts with the class's `prototype` (TS2300, already
  reported by declare_prototype()); mergeSymbolTable then stores the source (the namespace) in Foo's merged exports
  with parent Foo, and tsc prints it `typeof Foo.prototype` where a non-conflicting name prints `typeof proto`. Why
  the accessible chain needs qualification there wasn't found (instrument getAccessibleSymbolChain on the probe).
- nodeModulesJson (symbols, x4): `config3.version` through `import * as config3 from "./config.json"` in a .cts
  prints `Symbol(version, ...)` unquoted, the JSON property's own declaration line `"version"` quoted. The property
  reaches the namespace import through getTypeWithSyntheticDefaultImportType's spread; which symbol on that path
  lacks the declarations (getNameOfSymbolAsWritten would quote them) wasn't found.
- moduleExportsAliasLoop1/2 (symbols): `exports.fn2 = Math.min` is an alias whose accessible chain names lib's
  `Math.min` member `fn2` from the file (tsc's getAccessibleSymbolChain over the module's exports). tttt's interface
  members aren't Symbols, so the chain can't reach it (vision.md: member identity).

## Bigint property names
- bigintPropertyName: TypeScript 6 parses `1n` as a property name everywhere (object literals, interfaces, classes,
  destructuring). tsc's binder declares no member for it (getPropertyNameForPropertyNameNode is undefined: the
  symbol is a detached `__missing`), so `{ 1n: 123 }` is `{}` and `const h: H = { 3n: "" }` is TS2741 for "3n";
  TS1539 at the name from checkVariableLikeDeclaration (properties) and checkGrammarObjectLiteralExpression
  (property assignments); `const { 0n: f } = arr` is TS2538 'bigint' as an index type. tttt's parse_name_or_value()
  turns the bigint into its number text (`{ 1: number }`). Needs a "no name" flag through PropertyName parsing,
  the member builders (object literal, interface, class) skipping such members, and the two grammar checks. Enum
  members with a bigint name are done (`__missing`, EnumCase.canonical_name).

## Not reproduced / already passing
- importDeclWithExportModifierAndExportAssignment(InAmbientContext): pass.
- `module.exports = items` before `const items` under `/** @type {typeof import(...)} */`
  (jsDeclarationsTypeReassignmentFromDeclaration mutation): a hand-made probe of that order matches tsc.

## Multi-file/JS mutation run, seed 16 (`MUTATE_DIR=/tmp/wt-b-mutate ./mutate.py --tests multi,js -n 3000 --seed 16`)
22 of 3000 differed; 12 fixed (commits of 2026-10-07), the JS leftovers are in plan 26's Leftovers. TypeScript ones:
- mut0806 (typeGuardNarrowsIndexedAccessOfKnownProperty8, `export const key: never = "a"`): spurious TS2564 for
  `[a.key]: string` that the constructor assigns as `this[a.key]`. tsc's tryGetNameFromEntityNameExpression falls back
  to the initializer's type when the annotation gives no name, so the access is named "a" and matches; tttt goes by
  the declared type. Flow reference matching (isMatchingReference / getAccessedPropertyName).
- mut2320 (expandoFunctionSymbolPropertyJs, `const symb: never = Symbol()`): `readonly [symb]: boolean` in an interface
  with a `never` key makes index signatures in tsc (TS2322 "Index signature for type 'number' is missing" on
  returning a plain function); tttt adds nothing for it. Members from computed names (relations area).
- mut1254 (moduleResolutionWithSymlinks_notInNodeModules): tsc writes no errors baseline at all; tttt writes one with
  only the tsconfig.json section and no diagnostics. Harness, symlinked files.
- mut0360: tsc's baseline has the harness's TS-1 pre/post-emit count line: not matchable.
