# Binder, symbols, merging, modules, CommonJS: open divergences

Leftovers of the 2026-10-07 triage (binder/modules share), pruned 2026-10-09 against the tsgo 7.1 suite. Each entry: the test, tsc's mechanism, why tttt differs,
and what a fix needs. JS/JSDoc-specific items are in bugs/js.md.

## Export= clones (tsc's mergeSymbol clone, getCommonJsExportEquals, cloneTypeAsModuleType)
Plan 50 (2026-10-08) did tsc 6's getCommonJsExportEquals in place at link time; tsgo 7.1 has no getCommonJsExportEquals
at all (bindCommonJSTypeExports for every module: the module's types move onto the `export=` symbol), and since
2026-10-09 tttt follows it (`bind_type_exports()`, merge_commonjs_export_equals() is gone). The entries below that
blame getCommonJsExportEquals were written against tsc 6 and need rechecking against tsgo before any work. Stage 0 found
tsc clones whenever the module's own file is checked, not only on import. The entries below are what is left
(single-test ordering quirks of tsc): an augmentation into a cloneTypeAsModuleType clone, JS `module.exports = {...}`
as an augmentable export=.
tttt merges augmentations straight into what `export =` assigns (merge_augmentations()); tsc clones it and records
the clone as the merged symbol, which everything that resolves names sees. That is equivalent except where tsc's
clone is a *different* symbol than the one a later lookup finds:
- moduleAugmentationDuringSyntheticDefaultCheck: `declare module "moment-timezone"` where moment-timezone's
  `export = moment` names an esModuleInterop `import * as moment` (tsc's resolveESModuleSymbol ->
  cloneTypeAsModuleType: a transient clone typed `{ default: ... }`). The augmentation merges into that clone (its
  type `{ default: () => Moment; }`, moment's own symbol keeps 4 declarations); tttt merges into moment. Needs the
  namespace-import clone (TyAnonymous.module_clone / synthetic_default) to be a binder symbol augmentations can
  target.
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
- es6ExportEqualsInterop (`z2.a` named `a2`) involves no clone at all (stage 0 logged none): a symbol-chain naming
  bug of its own.
- jsExportMemberMergedWithModuleAugmentation2: `module.exports = { a: "ok" }` is a Property|ValueModule export= to
  tsc's binder (bindModuleExportsAssignment), so `declare module "./test" { export const a: number }` merges into it
  (tttt: TS2671, merge_augmentations() wants SYMBOL_NAMESPACE), and getInitializerTypeFromAssignmentDeclaration unions
  the literal's `a` with the export's (`{ a: string | number }`) and reports TS2300 at both when they come from
  different files. Needs: the JS export= assignment symbol as an augmentable namespace, the union with the
  cross-file TS2300 in the export= type, and the module/augmentation display (`Symbol("./test", ...)`).

## Merges tsc decides after aliases resolve
Done for cross-file merges into aliases (binder_link_alias_merges(), merge_into_umd_globals()); what is left is the
clones tsgo makes on the way (plan 69): declarationEmitAugmentationUsesCorrectSourceFile,
globalArrayAugmentationWithAmbientModuleReexportMerge1 (symbols), moduleAugmentationDuringSyntheticDefaultCheck.

## Symbol display of duplicates and merged members
- nodeModulesJson (symbols, x4): `config3.version` through `import * as config3 from "./config.json"` in a .cts
  prints `Symbol(version, ...)` unquoted, the JSON property's own declaration line `"version"` quoted. The property
  reaches the namespace import through getTypeWithSyntheticDefaultImportType's spread; which symbol on that path
  lacks the declarations (getNameOfSymbolAsWritten would quote them) wasn't found.

## Bigint property names
- Done 2026-10-14 by syntax: span_is_bigint_name() (span.h) is what the member builders, duplicate checks and symbol
  merges skip; the members keep their number text as name_string (a JS name lookup of `"1"` won't find them either,
  since nothing declares them). A computed `[1n]` is TS2464, unchanged.

## Multi-file/JS mutation run, seed 16 (`MUTATE_DIR=/tmp/wt-b-mutate scripts/mutate.py --tests multi,js -n 3000 --seed 16`)
22 of 3000 differed; 12 fixed (commits of 2026-10-07), the JS leftovers went to plan 26 (since retired; bugs/js.md). TypeScript ones (mut0806 and mut2320 rechecked as probes 2026-10-09: still differ):
- mut0806 (typeGuardNarrowsIndexedAccessOfKnownProperty8, `export const key: never = "a"`): spurious TS2564 for
  `[a.key]: string` that the constructor assigns as `this[a.key]`. tsc's tryGetNameFromEntityNameExpression falls back
  to the initializer's type when the annotation gives no name, so the access is named "a" and matches; tttt goes by
  the declared type. Flow reference matching (isMatchingReference / getAccessedPropertyName).
- mut2320 (expandoFunctionSymbolPropertyJs, `const symb: never = Symbol()`): `readonly [symb]: boolean` in an interface
  with a `never` key makes index signatures in tsc (TS2322 "Index signature for type 'number' is missing" on
  returning a plain function); tttt adds nothing for it. Members from computed names (relations area).
- mut1254 (moduleResolutionWithSymlinks_notInNodeModules): tsc writes no errors baseline at all; tttt writes one with
  only the tsconfig.json section and no diagnostics. Harness, symlinked files.
## UMD globals merged from scripts (`merge_into_umd_globals()`)
Declarations merged into a UMD global (`export as namespace X` plus a script declaring `X`) move into its module
whatever the file order; tsc drops the UMD global when a script declared the name first. A conflicting target is
skipped silently, where tsc reports it against the resolved target. (Moved from hacks.md, 2026-10-08.)

## TS 7.1 window leftovers (triage 2026-10-09)
- TS expandos follow tsgo's binder, but the binder still adds 6.0's SYMBOL_DECL_EXPANDO to the host (filtered in
  symbol_declaration_spans(), ignored by is_nested_expando_function()): the checker's expando rules want that namespace
  meaning. A real port of bindDeferredExpandoAssignment would drop it. Class statics aren't in host->exports at bind
  time, so `Point.origin = v` over a `declare class Point { static origin }` still declares (targetTypeTest1 symbols).

## Escaped identifiers: what's left
Names with unicode escapes (`\u0061`, `a\u{62}`) resolve by their cooked value (2026-09-29): `name_cooked()`
(`span.h`) decodes a view of a registered file's text (files with a `\u`, registered by `parse_file()`, cooked
strings cached per file until it's dropped), and it's applied where names become keys: symbol tables (hash, lookup,
insert), `checker_atom()`, `ty_string_literal()`, and the identifier comparisons that decide meaning (`name_equals()`:
labels, private names, type parameter lookups, overload runs...). The parser decodes escapes into the values it makes
from names (`identifier_name_value()`: dot property names, `name_string` of members, object binding keys).
`Symbol.name` stays raw, which is what tsc prints (`Symbol(a, ...)`, `classType2` in types); messages quote
the source as written.

Left:
- Comparisons of names against literals (`span_equals(name, "constructor")`, `"prototype"`, `"__proto__"`...) are by
  raw text: `constructor` isn't a constructor. tsc compares escapedText there. No test needs it.
- Computed keys compare by source text, JSX attribute names stay raw (TS17021 there anyway).

## Global ambient modules are merged after the global types: an augmented global interface's base can't see them
DT sweep's last mismatch, types/ember/v2 (-TS2306 at test/function-ext.ts:1, -TS2339 x4). function-ext.ts is a module
with `import Ember from "ember"` and `declare global { interface Function extends Ember.FunctionPrototypeExtensions {} }`;
"ember" is a `declare module "ember"` in the script index.d.ts (which the package's own name also resolves to). tsgo's
initializeChecker merges global augmentations, then calls getGlobalType for IArguments/Array/Object/Function/
CallableFunction/NewableFunction/String/Number/Boolean/RegExp/ReadonlyArray/ThisType, and only *then* merges the
scripts' ambient module declarations ("deferred since they may require other global symbols"). getGlobalType("Function")
asks isThislessInterface, which resolves every declaration's `extends` entity names (resolveEntityName, ignoreErrors)
and so the import alias `Ember`: resolveExternalModule finds no ambient "ember" yet, takes the resolved file, reports
TS2306 "is not a module" at the import (TS2307 if nothing resolves), and caches the alias as unknown for good: the
Function augmentation gets no base and `function() {}.property()` is TS2339. tttt resolves imports with every ambient
module known. Mirroring it: for interfaces of those global names declared in a global augmentation, resolve the first
identifier of each dotted `extends` name; if it's an import alias of a bare specifier that only a script's ambient
module declares (not a module file, not a pattern), resolve it as a file (TS2306/TS2307 at the import) and make the
alias unknown program-wide. Not done (one DT package).
