# Plan 40: declaration-emit diagnostics without emit
Planned 2026-09-29 (todo entry 6 of the window). Three stages, one to two sessions each; stage 3 may wait for plan 41.

## Why
tttt never writes `.d.ts` (out of scope), but the diagnostics tsc's declaration transformer reports are checking, and
every user with `declaration`/`composite` sees them: TS4xxx (an exported declaration names a private or unnameable
symbol), TS2527/TS4094/TS5088/TS7056 (inferred types that can't be serialized), TS2742 (not portable) and, with
`isolatedDeclarations`, TS9xxx. tttt has none of them.

**Suite** (failstats 2026-09-29): 38 failing tests fail *only* on these codes, 3 more also on them
(isolatedDeclarationErrorsClasses/Objects have spurious TS7032/TS7010 besides; declarationFiles a TS2526 for TS2527).
By stage: isolatedDeclarations 17 tests, accessibility 14, TS2742 10 (6 are the module=node16/18/nodenext copies of
nodeModulesExportsBlocksSpecifierResolution and nodeModulesExportsSourceTs). Not this plan, routed to
`bugs/missing_checks.md`: TS4111 (noPropertyAccessFromIndexSignature, unimplemented; drizzle sets the option),
TS4105, TS4109/TS4110.

**Real-world oracles, and a perf fact.** tsc's CLI with `--noEmit` reports declaration diagnostics when there is no
other error (watch.ts emitFilesAndReportErrors), so the corpora that enable declarations are clean for these codes
too and must stay at 0: **tsc's `src/compiler` (declaration + composite + isolatedDeclarations: 193k lines where every
exported thing must be recognised as syntactically typeable)**, valibot (declaration + isolatedDeclarations),
nestjs-boilerplate and drizzle (declaration; drizzle's inferred types are huge). Vue's corpus turns them off. So on
four of ten corpora tsc does work tttt skips today (`./bench.sh` passes `--declaration false` to tsc for the compiler
corpus only): the perf numbers there flatter tttt until this lands. The suite has ~1490 test files with
`declaration`/`composite`/`isolatedDeclarations`; the ones that pass now are the false-positive oracle.

## What tsc does (confirmed from `transformers/declarations.ts`, `declarations/diagnostics.ts`,
`expressionToTypeNode.ts`, checker.ts's isSymbolAccessible family)
- `getDeclarationDiagnostics(file)` runs transformDeclarations on one file and keeps only its diagnostics. It runs when
  `getEmitDeclarations(options)` (declaration || composite), for files getSourceFilesToEmit keeps: not `.d.ts`, not
  JSON, not from an external library (node_modules), JS only when emitted (allowJs). The harness always includes them
  (getPreEmitDiagnostics); the CLI with `noEmit` only if there's no other diagnostic, without noEmit always (emit).
- The transformer visits the statements that are visible (everything in a script; in a module the exported ones plus
  the non-exported ones something visible names: "late painted" statements), and inside them the members that are
  emitted (not private members' types, not function bodies). For every **written** type it checks entity names:
  checkEntityNameVisibility → isEntityNameVisible on each TypeReference / TypeQuery / heritage expression; for every
  **missing** type (variable, parameter, property, accessor, return, `export default expr`, a class's non-entity
  `extends` expression) it serializes the checker's type through the node builder (typeToTypeNode with
  declarationEmitNodeBuilderFlags: NoTruncation, UseStructuralFallback, WriteClassExpressionAsTypeLiteral,
  UseTypeOfFunction, GenerateNamesForShadowedTypeParams...), whose SymbolTracker reports:
  - trackSymbol(symbol) for each symbol it names (lookupSymbolChain) → isSymbolAccessible(..., shouldComputeAliases
    = true): Accessible, NotAccessible (TS4025-style "has or is using private name"), CannotBeNamed (TS4023-style
    "from external module but cannot be named"). The message depends on the declaration kind being emitted:
    `getSymbolAccessibilityDiagnostic` from createGetSymbolAccessibilityDiagnosticForNode (diagnostics.ts, ~60
    messages keyed by node kind, static/public, parent kind; the node the error is on is the declaration's name);
  - reportPrivateInBaseOfClassExpression (TS4094 + related TS9027-ish "Add a type annotation"),
    reportInaccessibleUniqueSymbolError (TS2527), reportInaccessibleThisError, reportCyclicStructureError (TS5088),
    reportTruncationError (TS7056), reportLikelyUnsafeImportRequiredError (TS2742: the `import("...")` specifier the
    builder generates contains `/node_modules/`), reportNonSerializableProperty, reportNonlocalAugmentation.
- isSymbolAccessible = isAnySymbolAccessible: an accessible chain (getAccessibleSymbolChain, which
  `symbol_chain.c` already ports for the printer) whose first symbol hasVisibleDeclarations (isDeclarationVisible, with
  the late-painting exceptions for unexported imports, variable statements and top-level statements), else recurse
  into the containers, modules always accessible through `import()`; otherwise CannotBeNamed when the symbol lives in
  another external module, else NotAccessible. Verified by probe: a namespace-private interface returned by an
  exported namespace function and used through `export const h = N.mk()` gives TS4025 on `h`; a class local to an
  exported function's body gives nothing (structural fallback); a non-exported top-level interface used in an
  exported signature gives nothing (late painted).
- isolatedDeclarations: the same walk, but the missing type first goes through the syntactic builder
  (expressionToTypeNode.ts, createSyntacticTypeNodeBuilder): literals, `as`/`satisfies`/`as const`, object and array
  literals it can type from syntax, functions with annotated returns, a single `return` of such an expression...
  Where it can't, `reportInferenceFallback(node)` gives the TS9xxx from createGetIsolatedDeclarationErrors
  (diagnostics.ts: the message by node kind plus the related "Add a type annotation to ..." TS9027-TS9036 the errors
  baseline prints as `!!! related`). The transformer adds its own: TS9014/TS9038 (computed names), TS9020 (enum
  initializers referencing other symbols: getEnumMemberValue.hasExternalReferences), TS9021 (heritage expression),
  TS9023 (expando functions), TS9026 (augmentation import), TS9037 (default export). Checker questions it asks:
  isLiteralConstDeclaration, isDefinitelyReferenceToGlobalSymbolObject, requiresAddingImplicitUndefined
  (isolatedDeclarationsAddUndefined), isExpandoFunctionDeclaration, getPropertiesOfContainerFunction. Not in JS files.

## Design
- **One new file, `src/ty/declaration_diagnostics.c`** (header in `ty/`): `check_declaration_diagnostics(checker,
  file)`, run by the checker that checks the file, after it (so the parallel checkers stay deterministic: same file,
  same checker, `./threadcheck.sh` / `./checkerdiff.py` must stay byte-identical). It walks the file's AST the way
  transformDeclarations does (visible statements, emitted members, where types are written or missing) and dispatches
  to the three checks below. Gate: getEmitDeclarations and the file is emitted (see above). The CLI: with `noEmit` and
  any earlier error, don't run it at all (tsc doesn't report them then; it also saves the time); the test harness
  always runs it.
- **Accessibility is a port, next to `symbol_chain.c`** (or in it): isDeclarationVisible (memoized per declaration),
  hasVisibleDeclarations with the late-painting rules (no need to record aliases to make visible: nothing is emitted,
  just treat them as visible), isSymbolAccessible, isEntityNameVisible (resolveName from the enclosing declaration's
  scope: tttt's scope lookup). getAccessibleSymbolChain is already there; don't write a second one.
- **Inferred types go through the existing printer.** `ty_display.c` already follows the node builder (tsc's types
  baseline is printed by it: the `^^^` underline marks reused nodes, `symbol_chain_at` is lookupSymbolChain,
  `shows_alias` is isTypeSymbolAccessible). Give `TyDisplay` a `DeclTracker*` (null everywhere else) that the printer
  calls where tsc's builder calls its tracker: trackSymbol from `out_symbol_name()` (every symbol named through a
  chain), class-expression privates, unique symbols, `this`, cycles, truncation, and later the `import()` specifier.
  Print into a scratch cstr that's dropped (or a sink that doesn't append: measure). A second walk mirroring the
  printer's alias/typeof/structure decisions would drift from it; don't. Declaration mode may need the builder flags
  the types baseline doesn't use (NoTruncation: MAX_DISPLAY_DEPTH and `...` must not hide symbols; deep types only
  lose diagnostics, not add them). If a printer decision differs from tsc's, that's a types-baseline bug too
  (`./displaydiff.py`).
- **Written types**: walk the type nodes of visible declarations and check each entity name with isEntityNameVisible;
  typeof queries and heritage clauses included. This part is syntactic plus name resolution: low risk, do it first
  in stage 2.
- **isolatedDeclarations** is a port of the syntactic builder's *decision* only (can this be typed from syntax, and
  where does it fall back), no nodes built, plus the transformer's own TS9xxx checks and diagnostics.ts's message and
  related-info selection.

## Stages
### Stage 1: the walk and isolatedDeclarations: DONE (2026-10-04)
`src/ty/declaration_diagnostics.c`: the transformer's walk (visible statements, emitted members, written types), a port
of the syntactic builder's decisions, the TS9xxx messages and related info, the transformer's TS9020/TS9023/TS9038.
Runs in `check_share()` per file into a per-checker ErrorCtx merged after comment directives; the CLI drops them with
noEmit/noEmitOnError when anything else was reported (`program_declaration_diagnostics_cli()`). Suite +12
(12354 -> 12366, also TS7032 for object literal setters, TS7008/TS7010 for computed interface members, import type
arguments unchecked), tsc's compiler and valibot at 0, callgrind +0.03% / +0.14% on them. Three probes of ~80
declarations matched tsc exactly. Left for stage 2 (all need symbol accessibility, see hacks.md "Declaration
diagnostics"): late painting (computedPropertiesNarrowed), TS9026 (isolatedDeclarationErrorsAugmentation, which also
fails types), TS9039, the general node-builder re-entry. Not this plan: jsDeclarations* TS9005/TS9006 (JS declaration
emit, symbolTableToDeclarationStatements).
### Stage 2: accessibility: DONE (2026-10-04, one session)
`symbol_chain.c` ports isSymbolAccessible / hasVisibleDeclarations / isDeclarationVisible (late painting,
collectLinkedAliases for namespaces, `SymbolDecl.hoisted` for vars hoisted out of blocks); the walk runs under
`declaration` alone (TS9xxx gated on isolatedDeclarations), gives each emitted declaration tsc's accessibility
diagnostic (`Emitting`, ~85 messages), checks written entity names, and prints fallback types through `ty_display`
with a `DeclTracker` (trackSymbol, trackComputedName, TS2527 unique symbol / `this`, TS4094, TS4118, TS7056 past 1M
characters), with node reuse on, WriteClassExpressionAsTypeLiteral and alias accessibility. Late painting walks the
non-exported declarations an emitted one names; TS9026. Suite 12366 -> 12376 (+10, 0 lost; every stage 2 test
passes its errors but two, four still fail types/symbols), every corpus unchanged, and type-fest/react/zod/xstate/vue agree with tsc under a forced `--declaration` (both
clean). Callgrind +0.6% nest, +1.0% drizzle, +0.3% tsc/valibot (after memoizing accessibility per walk and indexing
big tables' aliases: the first version cost nest +3.5%). Left (hacks.md "Declaration diagnostics"):
- TS7056 for deep recursive types (declarationEmitPrivatePromiseLikeInterface): tsc elides a symbol nested more than
  10 deep (visitAndTransformType), tttt's printer at MAX_DISPLAY_DEPTH 8 overall. Porting the per-symbol depth to the
  declaration mode is easy; the risk is time on big types (each print is bounded by the 1M characters, which at the
  printer's ~5k instructions per character is seconds) and false TS7056 where tttt's printer expands what tsc names.
- TS5088 (arrayFakeFlatNoCrashInferenceDeclarations): typeToTypeNodeOrCircularityElision's visited unions.
- A declaration whose syntactic builder falls back prints its whole checker type (not only the parts tsc would).
- getAlternativeContainingModules isn't in `containers_of()`.
- The printer's `import("...")` specifiers are tttt's module names, which stage 3 replaces with generated specifiers.
### Stage 3: TS2742: DONE (2026-10-04, todo entry 6 of the eighth window)
`src/module_specifiers.c` (documentation.md, end of "Declaration diagnostics"), `DeclTracker.track_module_file`, and in
`symbol_chain.c` getAlternativeContainingModules and sortByBestName. Suite +14, 0 lost; corpora unchanged; callgrind
nest +0.7%, drizzle +0.5%. What's left of the plan: TS7056 for deep recursive types, TS5088, and the hacks.md
"Declaration diagnostics" list. The original stage 3 text:
The builder's `import("...")` specifier from moduleSpecifiers.ts (getModuleSpecifiers: relative path, the package
name through node_modules, package.json `exports`/`typesVersions` blocking a subpath, symlinked packages and
realpaths, the node16/nodenext mode swap retry), TS2742 when it contains `/node_modules/`. tttt has module resolution
(`module_resolution.c`) but no reverse direction. The 10 tests; common in pnpm monorepos, which plan 41 is also about,
so do it when plan 41's specifier needs are known. If both need specifier generation, build it once.

## Validation (every stage)
`./fasttest.sh --summary`: the stage's tests pass, 0 lost (the declaration tests that pass now are the false-positive
oracle). `./realworld.py` for every pinned corpus: unchanged, tsc/valibot/nest/drizzle at their current counts with no
TS4xxx/TS9xxx/TS2742/TS2527 among them. `./threadcheck.sh` and `./checkerdiff.py` byte-identical. `./instrcount.sh` on
the four declaration corpora before and after, in `agents/measurements.md`; when stage 2 lands, `./bench.sh` compares
like with like on nest/drizzle/valibot for the first time (say so in measurements.md).

## Risks
- A printer approximation that names a symbol tsc wouldn't (or through a chain tsc wouldn't take) becomes a spurious
  TS4xxx. The corpora and the passing suite tests catch it; fix the printer, never special-case the diagnostic.
- `symbol_chain.c`'s chain is written for the symbols baseline (no meaning, see its header) and the types printer;
  isSymbolAccessible asks with a meaning and `useOnlyExternalAliasing` false. Check it answers correctly with a
  meaning before relying on it.
- Late painting is what makes non-exported top-level declarations fine in modules; getting it wrong would flood real
  code with TS4xxx. The probe above (and tsc's compiler corpus) pins it.
