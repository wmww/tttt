# Plan 23: symbol merging and duplicate declarations

## Why
tsc's binder decides almost every "Duplicate identifier" / "cannot merge" diagnostic in one place: `declareSymbol`
with per-kind `includes`/`excludes` flags, then the checker's merge checks (checkClassForDuplicateDeclarations,
checkFunctionOrConstructorSymbol, checkExportsOnMergedDeclarations, TS2813/TS2814 in `mergeSymbol`'s class/function
cases). tttt has `declare_symbol()` with excludes too, but several declaration kinds bypass it or keep one symbol per
declaration, so the errors either go missing or come out differently. Measured 2026-09-26 (`./failstats.py --code X`
after a full `./failstats.py`), roughly 70 tests fail their errors on these codes:
- **TS2300** (Duplicate identifier): 178 missing / 199 spurious. Spurious: `duplicateIdentifierRelatedSpans1-7` (~70
  diagnostics: probably tsc's cap / related-info shape, TS6203/TS6204), predefined type names as class/interface names
  (`classWithPredefinedTypesAsNames*`, `interfacesWithPredefinedTypesAsNames`, `primitiveTypeAs*`: tsc says TS2414/TS2427
  instead), `enumErrors`, `literalsInComputedProperties1`, `noSymbolForMergeCrash` (`type A = {}` + `interface A`: tsc
  TS2300 on... check, and TS2649 missing), `umdGlobalConflict`. Missing: duplicate object literal properties
  (`objectLiteralErrors` 36, `duplicateObjectLiteralProperty`), class members (`staticPropertyNameConflicts`,
  `twoAccessorsWithSameName`, `cloduleWithDuplicateMember1`,
  `ClassAndModuleThatMerge*`), `duplicateExportAssignments` (11: a second `export =` is a TODO in
  `scope_default_export_table`), `letAndVarRedeclaration` (9), `duplicateIdentifierInCatchBlock`.
- **Default exports** (TS2528 / TS2323 / TS2813-2814 of defaults: duplicateDefaultExport, multipleExportDefault1-6,
  multipleDefaultExports01-03, exportDefault*AndValue, exportDefaultInterfaceAndTwoFunctions, ~15 tests):
  `scope_export_default()` inserts the local symbol itself into the exports table as `default`, and
  `scope_default_export_table()` reports TS2528 by hand ("TODO: the second one is lost"). tsc declares a separate
  `default` export symbol per module that merges like any other (an interface and a function merge, two classes
  don't), and the ordinary merge checks produce TS2528/TS2323.
- **TS2813 / TS2814** (class declaration merging with a function, 16 tests each: callOverloads1-5, augmentedTypes*,
  classOverloadForFunction(2), funClodule, nameCollisions, staticClassMemberError): missing entirely.

## Approach (the execution agent decides the details)
1. Read tsc's `declareSymbol` (binder.ts) and the checker's duplicate checks, then tttt's `declare_symbol()`,
   `scope_export_default*()`, `scope_check_duplicate_exports()` and how class/interface members and object literal
   members are collected (members are not binder symbols in tttt: `hacks.md` says so; check what the current state is
   before assuming). Write down in this file which of the buckets above share a root cause.
2. Default exports first: a `default` export symbol of its own, declared through the normal excludes path. Watch the
   symbols baseline (`export default class A` prints `Symbol(A, Decl(...))` and `Symbol(default, ...)` in tsc) and
   `symbol_chain.c`'s display of `typeof default` (triage.md has a related display item).
3. Then TS2813/TS2814 and the TS2300 buckets in whatever order the root causes suggest. Member duplicates may want the
   members-as-symbols question answered; if that turns out to be a redesign, stop, write it up here, and do the rest.

## Validation
- `./failstats.py --code TS2300` / `TS2528` / `TS2323` / `TS2813` / `TS2814`: the listed tests' missing and spurious
  counts drop; no new spurious ones elsewhere (`./fasttest.sh --summary` shows no lost tests).
- The three corpora stay where they are (`./realworld.py`, `--corpus type-fest`, `--corpus valibot`): duplicate
  checks are exactly the kind of thing that fires spuriously on real code with declaration merging (lib augmentations,
  namespaces merged with classes/functions/enums).
