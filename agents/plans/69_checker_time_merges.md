# Plan 69: what is left of the cross-file merges through aliases: tsgo's clones

Stage 1 (2026-10-14, agent C) is done: binder_link_alias_merges() decides a cross-file merge into an alias like tsgo's
mergeSymbol (declare_pending_alias_merge() keeps the declaration a symbol of its own until the aliases resolve; an
alias resolving to nothing gives way, a target that doesn't exclude the declaration takes it in and the name, a
conflicting one gives the declaration the name), merge_into_umd_globals() splits a UMD global whose `export =` resolves
to nothing, a namespace `export =` has no members but its value exports (TS2305 at bind time), and an `export import`
in a namespace block has no local twin for the symbol chain. Fixed: duplicateVarsAcrossFileBoundaries,
importEqualsError45874, checkerInitializationCrash, moduleAugmentationWithNonExistentNamedImport, and the types of
globalArrayAugmentationWithAmbientModuleReexportMerge1 (tests ttttCrossFileAliasMerge, ttttUmdGlobalUnknownExportEquals).

Left, all three about the symbols tsgo *clones* rather than merges in place (cloneSymbol in mergeSymbol,
cloneTypeAsModuleType in resolveESModuleSymbol), which tttt doesn't make. About one session; start with an
instrumented tsgo (overlay a patched checker.go: wrap mergeSymbol to log target/source/result, like
scripts/variancetrace.py's build() does for relater.go; ~3 min to build the probe test binary with `go test -overlay
... -c ./internal/testrunner` and run it with TTTT_TESTDATA, see scripts/tsgoprobe.sh).
- declarationEmitAugmentationUsesCorrectSourceFile (symbols, types): `declare module "knex" { namespace Knex { function
  newFunc() } }` augmenting a module whose `export = knex` namespace re-exports a file-local `declare namespace Knex`
  (`export { Knex }`). Logged: mergeSymbol(alias Knex, augmentation Knex) returns a clone (2 declarations), and the
  module merge clones knex too, every checker. Yet tsgo's symbols baseline names the original declaration
  `Symbol(knex.Knex, Decl(index.d.ts, 13, 10))` (one declaration, parent knex) and types it `any`: find which symbol
  getSymbolAtLocation returns there (getMergedSymbol of the local is recorded to the inner clone by cloneSymbol;
  something else must win) before modelling it. tttt merges into the namespace in place (merge_into_exports()).
- globalArrayAugmentationWithAmbientModuleReexportMerge1 (symbols): `foo.items` through `import * as foo from 'foo'`
  of an `export = foo` function+namespace prints bare `items` in tsgo (probe: also without the test's ambient module
  merge; `import foo = require` and a namespace-only `export =` print `foo.items`). resolveESModuleSymbol gives the
  namespace import a clone when the type has signatures (cloneTypeAsModuleType), so the chain can't go through the
  import; tttt's symbol chain finds `[foo, items]` through the alias's resolved exports (candidate_list() in
  symbol_chain.c; export_equals_namespace_import_is_clone() already stops is_accessible(), not the exports step).
  Instrumenting getCandidateListForSymbol on the probe tells why tsgo's exports step doesn't find it either.
- moduleAugmentationDuringSyntheticDefaultCheck (symbols, types): the augmentation merges into the esModuleInterop
  clone of a namespace import (bugs/binder_modules.md "Export= clones").

## Validation
Each fixed with a cut-down tttt test, 0 lost, the corpora, sweep, `$ExpectType` and full sourcesweep match set
unchanged. Delete this plan when the three pass or are written up as tsgo artifacts.
