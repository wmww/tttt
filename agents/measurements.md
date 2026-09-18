# Measurements
Recorded output of `./failstats.py` and `./bench.sh`. Replace (don't append forever) when re-measuring; keep the
date and commit. History is in git.

## 2026-09-18, failstats after the first parser gaps session (bench numbers are still from 4af1220)

### Takeaways
- **The parser is still the single biggest cause of test failures, but shrinking**: 4572 -> 2890 tests get a syntax
  error from tttt that tsc doesn't report (a parse error disables typechecking, so those fail all three components).
  Done in the first parser session: `module X {}`, dotted namespace names, `declare module "x"`, `import x =
  require()` / `import x = A.B`, `import "x"`, labels, `do..while`, `const enum`, non-script virtual files. Full pass
  count 829 -> 987 (errors 3080 -> 3695, symbols 4740 -> 5172, types 1679 -> 1974).
- What is left is a long tail led by members: computed/symbol method names `[Symbol.iterator]() {}`, object literal
  members (shorthand `{ x }`, methods, get/set, computed keys, spread: **none** of these parse), class members
  (`p!: T`, `#private`, stray `;`), then generators/`yield`, `await`, decorators, spread in calls/arrays/`new`,
  string enum member names, `satisfies`, `export { a as "s" }`, `import()`.
- Newly visible checker gap: **namespaces don't merge** (`module A {} module A {}`): `declaration.c` "Tried to add
  multiple namespaces" now fires in 167 tests. That is symbol-table work (plan 03 territory).
- No panics/crashes across the suite (the harness would die; `failstats.py` reports that if it ever happens).
- `log_error` "not yet implemented" output is dominated by generic inference (callable, union/intersection,
  interface, array, tuple). Matches the vision's view that the type core is next.
- Only ~170 failing errors-components are "right codes, wrong text/column", so type *printing* in messages is not
  yet a big lever; missing TS2322/TS2339/TS2345 (assignability, property lookup) are the big checker gaps.
- The 726 tests that never run: 412 only have multi-config baselines (`name(target=es5).types`...; `target` and
  `module` are ~80% of them), 314 have no `.types`/`.symbols` baseline at all (JS-emit/sourcemap-only tests etc).
- Perf (not re-measured this session): on code tttt can check, tttt is ~5x faster than JS tsc, ~1.5x faster than
  single-threaded tsgo and ~20% *slower* than default (multi-threaded) tsgo. Caveats, all of which flatter tttt: the
  corpus is the easy tests it already passes, replicated; tttt does no flow analysis and far less checking; lib set
  is small. Memory is bad: ~500 MB for 4 MB of source (tsgo: 365 MB). tttt's phases sum to ~270 ms of a 360 ms run;
  the rest is presumably teardown (freeing every node individually) - see triage.

### ./failstats.py -n 12
```
failstats @ 7634383
11962 test files, 11236 ran, 726 did not: 412 only have multi-config baselines (name(option=value).types), 314 have no types/symbols baseline at all
  options varied by the multi-config tests we skip (tests):
     192  target
     150  module
      12  strict
      12  moduleresolution
      12  exactoptionalpropertytypes
       8  isolatedmodules
       7  esmoduleinterop
       5  usedefineforclassfields
  ... 18 more rows, 436 total
pass: all 987  errors 3695  symbols 5172  types 1974  (of 11236)

checker log_error output: 7753 lines across 886 tests
  top messages:
    2330  in 178 tests  generic.c:82: error: inferring types of generic with callable not yet implemented
    1454  in 88 tests  generic.c:99: error: inferring types of generic with union/intersection not yet implemented
     921  in 24 tests  type.c:915: error: Max type recursion depth exceeded (type_extends_internal)
     467  in 70 tests  error.c:180: error: Error added with invalid span: TS2300: Duplicate identifier '_'.
     463  in 19 tests  error.c:180: error: Error added with invalid span: TS2451: Cannot redeclare block-scoped variable '_
     448  in 82 tests  generic.c:86: error: inferring types of generic with interface not yet implemented
     403  in 167 tests  declaration.c:239: error: Tried to add multiple namespaces to declaration _
     382  in 78 tests  generic.c:90: error: inferring types of generic with array not yet implemented
     233  in 12 tests  error.c:180: error: Error added with invalid span: TS2304: Cannot find name '_'.
     212  in 142 tests  scope.c:164: error: Imported source file '_' is not a module
     183  in 99 tests  error.c:180: error: Error added with invalid span: TS2305: Module '_' has no exported member '_'.
     129  in 21 tests  generic.c:94: error: inferring types of generic with tuple not yet implemented
  ... 8 more rows, 7753 total

errors component: 7541 failing
  by shape:
    2716  tsc reports nothing, tttt reports errors (all spurious)
    2553  both missing and spurious
    1674  tsc reports errors, tttt reports nothing
     225  only missing errors
     205  only spurious errors
     146  same codes+positions, message text differs
      22  same codes+lines, column differs
  2860 tests have a spurious TS1xxx, ie the parser rejects syntax tsc accepts (typechecking is skipped for those, so their types/symbols fail too); 1365 of them expect no errors at all
  missing diagnostics (tsc has, tttt lacks) by code:
    1626  in 553 tests  TS2322
     898  in 350 tests  TS2339
     680  in 38 tests  TS18050
     567  in 123 tests  TS2300
     479  in 183 tests  TS2345
     411  in 40 tests  TS2365
     338  in 137 tests  TS1005
     312  in 124 tests  TS6133
     296  in 24 tests  TS2362
     244  in 58 tests  TS2411
     201  in 22 tests  TS2356
     184  in 39 tests  TS2314
  ... 878 more rows, 18806 total
  spurious diagnostics (tttt has, tsc lacks) by code:
   12802  in 2402 tests  TS2304
   10671  in 1982 tests  TS1005
    8940  in 1528 tests  TS1128
    3688  in 523 tests  TS2451
    2974  in 845 tests  TS1434
    2086  in 548 tests  TS1109
    1670  in 615 tests  TS2693
    1515  in 495 tests  TS2322
    1484  in 22 tests  TS2694
     787  in 249 tests  TS2345
     679  in 138 tests  TS2300
     655  in 320 tests  TS1108
  ... 74 more rows, 55778 total
  tests whose errors diff involves a single code (would pass errors if fixed alone):
     317  spurious TS2304
     179  spurious TS2322
     151  missing TS2322
     136  spurious TS2307
     108  spurious TS2451
     101  missing TS6133
      99  missing TS2339
      84  spurious TS2345
      60  misplaced TS2322
      52  spurious TS7006
      50  spurious TS1128
      41  spurious TS1005
  ... 375 more rows, 2943 total

symbols component: 6064 failing, mean line match 94.2%; failing tests by line match: >=95%: 1831, 80-95%: 3451, 50-80%: 752, <50%: 30
  nearly passing (full list in report.txt): aliasAssignments allowJsCrossMonorepoPackage allowSyntheticDefaultImports10 commentOnAmbientVariable2 crashInResolveInterface declFileAliasUseBeforeDeclaration duplicatePackage_packageIdIncludesSubModule duplicatePackage_relativeImportWithinPackage duplicatePackage_relativeImportWithinPackage_scoped es6UseOfTopLevelRequire importAsBaseClass literalTypeNameAssertionNotTriggered

types component: 9262 failing, mean line match 79.4%; failing tests by line match: >=95%: 609, 80-95%: 3541, 50-80%: 4384, <50%: 728
  nearly passing (full list in report.txt): aliasAssignments allowSyntheticDefaultImports10 collisionExportsRequireAndAlias commentOnAmbientClass1 commentOnAmbientEnum commentOnAmbientVariable2 commentOnElidedModule1 commentOnInterface1 declFileForExportedImport declarationEmitHasTypesRefOnNamespaceUse doNotemitTripleSlashComments externalModuleReferenceOfImportDeclarationWithExportModifier

full data: build/failstats/report.txt, tests.tsv, near_symbols.txt, near_types.txt, stderr.txt
```

### ./failstats.py --syntax -n 20
```
2890 tests where tttt reports a syntax error tsc does not; text at the first one:
    118  '[id.id'               eg ES5For-ofTypeCheck10: [Symbol.iterator]() {
    109  '{'                    eg FunctionDeclaration10_es6: function * foo(a = yield => yield) {
     83  '@id'                  eg classStaticBlock19: @decorator
     32  'id() {'               eg builtinIterator: next() {
     31  'id'                   eg checkJsdocTypeTag6: methodWithMoreParameters(more) {}, // error
     30  'id;'                  eg asyncFunctionsAndStrictNullChecks: var number = await promise;
     28  ''                     eg TemplateExpression1: var v = `foo ${ a
     26  'id {'                 eg amdDeclarationEmitNoExtraDeclare: return class extends base {
     22  '...new id'            eg iterableArrayPattern15: fun(...new FooIterator);
     20  '...id);'              eg accessorsOverrideProperty9: super(...args);
     20  'id };'                eg bigintArbirtraryIdentifier: export { foo as 0n };
     19  'id,'                  eg argumentsReferenceInObjectLiteral_Js: arguments,
     19  ': id<void>'           eg asyncArrowFunction10_es2017: var foo = async (): Promise<void> => {
     18  '=> {'                 eg ArrowFunction3: var v = (a): => {
     16  ': id'                 eg asyncGeneratorPromiseNextType: async function *saverGen(): AsyncGenerator<void, void, Promi
     15  'id(id) {'             eg allowJscheckJsTypeParameterNoCrash: data1(val) {
     15  '@id()'                eg decoratedBlockScopedClass1: @decorator()
     14  'get id() {'           eg accessorDeclarationEmitJs: get getter() {
     14  '#id: id;'             eg accessorDeclarationOrder: #name: string;
     14  'satisfies id'         eg commentEmitOnParenthesizedAssertionInReturnStatement2: .getThing() satisfies Promise<string>);
```

### ./bench.sh
```
corpus: 219 files, 314600 lines, 4152910 bytes; 5 runs each
tttt                   best   0.370s  median   0.380s  peak RSS    498 MB  exit 0
tsc 5.8.2 (node)       best   1.980s  median   2.160s  peak RSS    610 MB  exit 0
tsgo 7.0.2             best   0.290s  median   0.300s  peak RSS    368 MB  exit 0
tsgo single-threaded   best   0.590s  median   0.660s  peak RSS    371 MB  exit 0

tttt phases (one run):
  time: load+parse libs 16.2917480469 ms
  time: load+parse files 87.080078125 ms
  time: resolve names 92.8933105469 ms
  time: typecheck 74.3571777344 ms

stdlib only (parse + resolve + typecheck of thirdparty/lib/esnext.full.d.ts and everything it references;
tttt reports name errors there, so --keep-going. 52208 lines):
  time: load+parse libs 0.000244140625 ms
  time: load+parse files 18.2048339844 ms
  time: resolve names 8.52392578125 ms
  time: typecheck 41.716796875 ms
```
