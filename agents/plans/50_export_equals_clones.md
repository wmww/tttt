# Plan 50: tsc's symbol clones (export= targets, CommonJS export merging, namespace-import clones)

Status: planned (triage 2026-10-07). Not started. Payoff: ~12 suite tests (mostly the symbols component, a few types,
one errors), the removal of four display/harness special cases, and correct identity for augmented `export =`
modules on real code (DefinitelyTyped augments `export =` packages a lot: the sweep is the real oracle here).

## tsc's mechanism
Three places make a *transient copy* of a symbol and either record it as the original's merged symbol or hand it out
instead of the original:
1. **mergeSymbol** (checker.ts ~2698): merging into a non-transient target first resolves it (an alias's target) and
   clones it (`cloneSymbol`: declarations, parent, valueDeclaration, members/exports *copied as new maps*), then
   mutates the clone. `cloneSymbol` and the end of mergeSymbol both call `recordMergedSymbol(clone, original)`, so
   `getMergedSymbol(original)` is the clone from then on, and every name lookup (`getSymbol` in symbol tables,
   resolveName, getSymbolOfDeclaration, resolveExternalModuleSymbol) goes through getMergedSymbol. Used by
   initializeTypeChecker's global merges and by mergeModuleAugmentation (`mergeSymbol(mainModule, augmentation)`,
   where mainModule is `resolveExternalModuleSymbol(module)`, the `export =` target). A target alias that resolves to
   unknownSymbol makes mergeSymbol return the *source* (importEqualsError45874).
2. **getCommonJsExportEquals** (~4996), called by resolveExternalModuleSymbol: a module whose `export =` target isn't
   the module, isn't an alias and the module has other exports gets a clone of the target (ValueModule added) whose
   exports get each other module export, **mergeSymbol'd** into a same-named one (`module.exports = require("./y");
   module.exports.x = 1` with y's `type x`: one merged x). Cached in `links.cjsExportMerged` of the target; through
   cloneSymbol it is also the target's merged symbol, so *every* reference to the target (its own file included,
   `import * as y` of y elsewhere) sees the clone once any importer resolved the module. Targets that are modules
   (`module.exports = require(...)`) are cloned like anything else.
3. **cloneTypeAsModuleType** (~5093), from resolveESModuleSymbol for a namespace import or `import()` of an `export =`
   module under esModuleInterop / synthetic defaults / ESM-imports-CJS: a per-import copy (`links.target` = the
   original, `originatingImport`) whose type is the synthetic `{ default: ...; ...members }`. Not recorded as merged:
   the import alias resolves to it. A module whose `export =` names such an import (`import * as m from "m"; export =
   m`) *exports that clone*, and augmenting that module merges into the clone in place (it is transient already):
   moduleAugmentationDuringSyntheticDefaultCheck.

Timing: 2 and 3 happen lazily during checking. The harness's type writer runs after checkSourceFile of every file,
so baselines show the state after every import has resolved. A static "as if every import resolved" model is enough
for the baselines (stage 0 checks this).

## tttt today
- Augmentations merge in place into what `export =` assigns (scope.c `merge_augmentations()`, `merge_into_exports()`,
  `merge_symbol()`): equivalent to 1 as long as nothing needs the original unchanged. Instantiated augmentations give
  the target SYMBOL_VALUE_MODULE (2f6d6ccd).
- `merged_export_equals()` / `module_export_equals()` (scope.c) is a partial 2: a copy of the target
  (`SymbolRare.merged_export_equals_of` points clone -> original) whose exports table has the module's other exports
  *skipping names the target has* (no mergeSymbol), not made for module targets, aliases with value_target, or a
  module with fewer than two exports. Only importers see it (module_export_equals()); the original's own file and
  other paths to the target don't. Consumers that undo or patch this: `binder_redirect()` (scope.h: clone ->
  original), `symbol_chain.c` (~247: a clone is named as its original), `ty/lower.c merged_export_equals_type()`,
  `ty/members.c` (~1908), `ty_display.c export_equals_clone_of()`, and the harness's
  `namespace_export_equals_clone()` (baseline_test_traversal.c), which emulates "once the module is checked every
  reference is the clone" for an uninstantiated namespace target.
- 3: `make_gather_target()` (scope.c) makes a symbol for `import * as ns` (gather_module, or synthetic_default for
  ESM->CJS in node16+), and the esModuleInterop synthetic type is a TyAnonymous with `module_clone` (lower.c ~1679,
  KEY_MODULE_CLONE -> the alias), printed by `display_module_clone()`. Augmentations can't target it.

## Failing tests that hinge on it (errors / symbols / types, 2026-10-07)
- jsExportMemberMergedWithModuleAugmentation (-SYM TYP), ...2 (ERR SYM TYP: also needs a JS `module.exports = {}`
  export= to be augmentable, Property|ValueModule, and getInitializerTypeFromAssignmentDeclaration's union of the
  literal's member with the augmentation's export plus its cross-file TS2300), ...3 (SYM: clone of a module target,
  mergeSymbol of x/x).
- typedefCrossModule3 (SYM: the clone mergeSymbols the typedef into `ns.Foo`), moduleExportPropertyAssignmentDefault
  (TYP: `typeof axios` in the file through recordMergedSymbol), lateBoundAssignmentDeclarationSupport7 (SYM:
  `x.F.F[_str]`, likely the clone's parent chain), es6ExportEqualsInterop (SYM: `z2.a` named `a2`, likely the
  accessible chain through the export= clone). The two "likely" ones are unverified: stage 0 confirms.
- moduleAugmentationDuringSyntheticDefaultCheck (SYM TYP: augmentation into a cloneTypeAsModuleType clone).
- importEqualsError45874, duplicateVarsAcrossFileBoundaries (mergeSymbol's alias resolution / source-wins rules
  across files; tttt merges globals at declare time) and expandoFunctionContextualTypesNoValue: related, stage 5.
- mergedDeclarations7 is a different tsc quirk (combineValueAndTypeSymbols makes a second interface type), not this.

## Design
One mechanism instead of four special cases: **a link-time merged-symbol redirect, original -> clone**, the
direction tsc uses, made only where tsc would clone.
1. `SymbolRare.merged_into` (Symbol*): set when a clone supersedes an original. `symbol_merged(s)` follows it (one
   hop; clones are never cloned again, tsc's transient check). Cheap test: a bit in Symbol's flags word or the rare
   pointer's presence, like binder_redirect()'s overlay check, so lookups that hit no clone pay one load.
2. Apply it where tsc calls getMergedSymbol: symbol table lookups that return a declared symbol
   (`symbol_table_get/lookup` callers in scope_resolve, export lookups, `module_resolved_exports()`),
   alias resolution results (`symbol_resolve_alias()` caches the merged target), `module_export_equals()`, and the
   AST's declaration -> symbol pointers when the checker asks for "the symbol of this declaration" (tsc's
   getSymbolOfDeclaration). Audit with a grep of `->symbol` reads in ty/ and the harness; most go through
   `ty_type_of_symbol()` / `symbol_for_meaning()`, which can apply it at the entry.
3. **CommonJS export merging (getCommonJsExportEquals) at link time**: in `binder_link()` after augmentations, for
   every module with `export =` and other exports whose target isn't the module or an alias: clone the target
   (`binder_new_symbol` + copy, as merged_export_equals() does now, but also for module targets), add SYMBOL_VALUE_MODULE,
   give it its own exports table (copy of the target's entries, not a parent link), and for each other export:
   insert, or merge into the same-named entry with tsc's mergeSymbol rules (clone that entry too and redirect it).
   Set `merged_into` on the target (and on merged entries). Delete `merged_export_equals_of`'s reverse uses.
4. **Augmentations of an `export =` module** merge into the clone when the target already is one (step 3 ran
   first), else into the target in place as now (in place == clone + redirect when nothing else holds the
   original). Order matters as in tsc: augmentations before CommonJS merging? tsc's mergeModuleAugmentation runs in
   initializeTypeChecker, getCommonJsExportEquals later and clones the *merged* target: so augment first, then clone
   for CommonJS (stage 0 confirms on jsExportMemberMergedWithModuleAugmentation*).
5. **Namespace-import clones as binder symbols**: make the esModuleInterop/synthetic-default case of
   `make_gather_target()` produce a clone symbol (target = original, its type the synthetic module type that
   lower.c makes today), so that a module whose `export =` names the import exports that symbol and
   `merge_augmentations()` can merge into it. TyAnonymous.module_clone / KEY_MODULE_CLONE then key on the symbol.
6. Delete: `namespace_export_equals_clone()` (harness), `export_equals_clone_of()` (display), the clone line of
   `binder_redirect()`, symbol_chain.c's merged_export_equals_of naming, the in_file special case of
   `merged_export_equals_type()` if the redirect makes it moot. These are the plan's measure of success as much as
   the tests.

Parallelism: everything above happens in binder_link() (single-threaded, before checking), so checkers only read.
Nothing is lazy, unlike tsc; stage 0 decides whether "clone iff some file resolves the module" or "clone always"
matches the baselines (the harness checks every file before writing types, so "some import resolves it" is the
likely rule; a module nobody imports may still be cloned by its own checkExternalModuleExports).

## Stages
0. **Instrument tsc** (see the memory note "Instrument a tsc copy": patch a /tmp copy of `built/local/_tsc.js`) to log
   cloneSymbol / getCommonJsExportEquals / cloneTypeAsModuleType with the symbol, the requesting node and whether it
   happened before the type writer; run it on the tests above plus a few sweep packages with `declare module "x"`
   augmentations of `export =` packages. Output: the exact clone rule (when, of what, recorded or not). ~1h.
1. Redirect infrastructure (`merged_into`, `symbol_merged()` at the entry points of step 2) with no producer yet:
   suite and corpora byte-identical, `./instrcount.sh` on valibot/zod/tsc within noise.
2. CommonJS merging (design 3) replacing merged_export_equals(): jsExportMemberMergedWithModuleAugmentation3,
   typedefCrossModule3, moduleExportPropertyAssignmentDefault, lateBoundAssignmentDeclarationSupport7,
   es6ExportEqualsInterop; then delete the reverse-direction special cases (design 6).
3. Augmentations into clones (design 4) and namespace-import clone symbols (design 5):
   moduleAugmentationDuringSyntheticDefaultCheck, jsExportMemberMergedWithModuleAugmentation, augmentExportEquals*
   (must stay passing), the mutation probes of augmentExportEquals3/4 (messages `typeof import("file1.ts")`).
4. JS `module.exports = {...}` as an augmentable Property|ValueModule export= with the literal/export union and its
   cross-file TS2300 (jsExportMemberMergedWithModuleAugmentation2).
5. Optional: mergeSymbol's alias rules across files (importEqualsError45874's source-wins on unknownSymbol,
   duplicateVarsAcrossFileBoundaries' merge into an alias's target) by moving those global merges into
   binder_link() where aliases can resolve.

## Validation
- The tests above (0 lost: `./fasttest.sh --summary`), augmentExportEquals1..7 and their `_1` variants, every
  `*ExportEquals*`, `*Augmentation*`, `typedefCrossModule*`, `moduleExport*` test still passing.
- Probes: `module.exports = require("./y"); module.exports.x = 1` with y's `export type x` (both x declarations at
  every reference); `import * as m from "m"; export = m; declare module "me" {...}` under esModuleInterop;
  `export = foo` of a namespace augmented with a function from two files (`typeof foo` everywhere).
- `./sweep.py` (DefinitelyTyped: many `export =` packages and augmentations, e.g. express/serve-static,
  jquery plugins) and `./expecttypes.py` unchanged or better; corpora (vue, xstate, webpack, zod, nest, react,
  `./realworld.py`) at 0; `./checkerdiff.py` / `./threadcheck.sh` identical across checker counts.
- `./instrcount.sh` before/after each stage (valibot, zod, tsc): the redirect must not show up.

## Risks
- Symbol identity changes everywhere a lookup now returns the clone: relations keyed by symbol (declared types,
  variance caches, `ty_declared_type_of`) must see one symbol per entity; a missed entry point gives two types for
  one declaration (the mergedDeclarations7 kind of bug, but unintended). Stage 1's "no producer, identical output"
  step and a debug assertion (a looked-up symbol with merged_into set) catch most of it.
- The harness/symbols baseline currently rebuilds tsc's merges in several places (hacks.md "Display and baselines");
  they may double-apply once the binder does it (as happened with function locals in 9e88d136): grep for
  merged_export_equals_of / namespace_export_equals_clone consumers first.
- Laziness: tsc clones only once something resolves the module; a static rule may clone where tsc didn't (a module
  only its own file references). Stage 0 bounds this.
- Cost of copying exports tables for every CommonJS module with `export =` and other exports (webpack has many):
  measure; a copy-on-write table parent (today's approach) is the fallback for the unmerged entries.
