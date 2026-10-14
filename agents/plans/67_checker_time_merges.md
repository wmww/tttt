# Plan 67: cross-file merges that involve aliases, decided after the aliases resolve (tsgo's mergeSymbol)

Written 2026-10-14 by plan 66's fan-out agent C (bucket "binder/modules"). Seven suite tests, one mechanism; size ~1
session (stage 1) + a probe sweep.

## The mechanism (tsgo)
tsgo's binder keeps every file's declarations in that file's own tables (a script's in `file.Locals`); the checker's
initializeTypeChecker merges them: each script's locals into `globals` (`mergeSymbolTable(globals, file.Locals)`, file
order), then the module augmentations (`declare global` ones, then `declare module "m"` ones, per file in order).
`mergeSymbol(target, source)` (checker.go):
- target and source don't exclude each other (or either is an Assignment):
  - target not transient: `resolvedTarget = resolveSymbol(target)` (an alias's target!).
    - `resolvedTarget == unknownSymbol` (an import that resolves to nothing): **return source**: the source replaces
      the target in the table; the alias stays a symbol of its own (importEqualsError45874: `import Foo =
      globals.toString.Blah` in one script and `const Foo = {}` in another: the table's `Foo` is the const, the alias
      `Foo` is `any` and lists only its own declaration).
    - resolvedTarget doesn't exclude the source: `target = cloneSymbol(resolvedTarget)` and the source merges into
      **the alias's target's clone** (duplicateVarsAcrossFileBoundaries: `import p = P` then another file's `var p`:
      the var merges into a clone of namespace P, so `P` lists `Decl(_4, 0, 0), Decl(_5, 2, 3)` and the var `p` prints
      as `Symbol(P, ...)`; getMergedSymbol(P) is the clone).
    - else: TS2300 etc. (reportMergeSymbolError) and **return source** (checkerInitializationCrash: `export import VNode
      = react.ReactNode` in a `declare global` namespace and another file's `type VNode`: the table gets the type
      alias, the import keeps one declaration, `react.ReactNode` is named by its own chain).
  - target transient (a clone already): merge into it.
- they exclude each other: TS2300 (or TS2649 for a namespace target), **return target**.
- `mergeSymbolTable` stores what mergeSymbol returns; a merged symbol whose exports merge gets the merged parent.

tttt instead declares every script's globals straight into `Binder.globals` at declare time (declare_symbol(): one
table, merging as the binder's declareSymbol would within one file) and merges augmentations into the targets
themselves in binder_link() (merge_augmentations(), merge_into_exports()). The outcomes match tsgo except where an
alias is on either side of a cross-file merge: tttt merges the declarations into the alias symbol itself.

## The tests (validation)
- importEqualsError45874 (symbols, types), duplicateVarsAcrossFileBoundaries (symbols),
  checkerInitializationCrash (symbols), globalArrayAugmentationWithAmbientModuleReexportMerge1 (symbols, types: an
  export alias `export { foo }` of an ambient module and a `declare global` const `foo`: mergeSymbol returns source),
  declarationEmitAugmentationUsesCorrectSourceFile (symbols, types: an augmentation merging into an `export { Knex }`
  alias's target merges into a clone, the original namespace stays uninstantiated, `knex.Knex` / `any`),
  moduleAugmentationWithNonExistentNamedImport (symbols: `import { Bar } from './foo'` that resolves to nothing and a
  UMD/global `Bar`), moduleAugmentationDuringSyntheticDefaultCheck (symbols, types: an augmentation of a module whose
  `export =` names an esModuleInterop namespace import, which merges into resolveESModuleSymbol's clone; may need the
  clone as a binder symbol too, see bugs/binder_modules.md "Export= clones").
  Their entries are in bugs/binder_modules.md ("Merges tsc decides after aliases resolve", "TS 7.1 window leftovers").

## Route
The link step already runs between declare and the resolve walk (documentation.md "Name resolution"), and alias
resolution only needs the tables, so the merge can be decided there before anything resolves names through them:
1. Declare: when declare_symbol() would merge into a global (or, for augmentations, into an export) that came from
   *another file*, and either side is an alias (SYMBOL_ALIAS), don't merge: keep the newcomer a symbol of its own and
   record a pending merge (table, name, target, source, in tsgo's order: scripts in file order, then global
   augmentations, then module augmentations). Same-file merges stay as they are (tsgo's binder makes those).
2. Link: after aliases resolve, replay the pending merges with mergeSymbol's rules above: replace the table entry
   (unknown target, or a resolved-target conflict: report TS2300 like reportMergeSymbolError and keep both), merge the
   source's declarations into the alias's resolved target (tttt has no clones: merging into the target in place is
   what getMergedSymbol makes observable, check declarationEmitAugmentationUsesCorrectSourceFile, where tsgo's clone
   leaves the original namespace uninstantiated and that shows), or merge as today when no alias is involved after all.
3. The resolve walk then sees the final tables; the symbols baseline's declaration lists come from the symbols, so
   they follow.
Before coding, probe each test's reduction against tsgo (scripts/tscbaseline.sh) and log the merges tsgo makes with an
instrumented checker (mergeSymbol's returns; scripts/variancetrace.py's build() shows how to overlay a patched
checker.go): the clone cases are the uncertain ones.

## Validation
The seven tests pass; nothing lost in the suite; the corpora, sweep, sourcesweep and `$ExpectType` unchanged (the
DT sweep has many UMD globals and `export =` merges: run it full). Add a tttt test per rule (unknown target, clone
target, conflict) from the reductions.
