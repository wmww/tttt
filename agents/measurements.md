# Measurements
Recorded output of `./failstats.py` and `./bench.sh`. Replace (don't append forever) when re-measuring; keep the
date and commit. History is in git.

## 2026-09-18, on top of 4af1220 (first measurement)

### Takeaways
- **The parser is the single biggest cause of test failures.** 4.5k of 11.2k tests get a syntax error from tttt
  that tsc doesn't report, and since a parse error disables typechecking those tests fail all three components.
  The top shapes are few and cheap: `module X {}` (the legacy spelling of `namespace`, ~900 tests incl.
  `declare`/`export` forms), `declare module "x"` (~160), `import x = require("x")` (~170), computed/symbol method
  names `[Symbol.iterator]() {}` (~110), decorators (~80), labels, `do {} while`, generators `function*`,
  `.json`/non-TS virtual files being parsed as TS (~150), spread in call/new args, `#private` names.
  TS2304 (cannot find name) being the top spurious code is mostly a knock-on of this and of missing libs/options.
- No panics/crashes across the suite (the harness would die; `failstats.py` reports that if it ever happens).
- `log_error` "not yet implemented" output is dominated by generic inference (callable, union/intersection,
  interface, array, tuple): ~4.5k lines across a few hundred tests. Matches the vision's view that the type core is next.
- Only 148 failing errors-components are "right codes, wrong text/column", so type *printing* in messages is not
  yet a big lever; missing TS2322/TS2339/TS2345 (assignability, property lookup) are the big checker gaps.
- The 726 tests that never run: 412 only have multi-config baselines (`name(target=es5).types`...; `target` and
  `module` are ~80% of them), 314 have no `.types`/`.symbols` baseline at all (JS-emit/sourcemap-only tests etc).
- Perf: on code tttt can check, tttt is ~5x faster than JS tsc, ~1.5x faster than single-threaded tsgo and ~20%
  *slower* than default (multi-threaded) tsgo. Caveats, all of which flatter tttt: the corpus is the easy tests it
  already passes, replicated; tttt does no flow analysis and far less checking; lib set is small. Memory is bad:
  ~500 MB for 4 MB of source (tsgo: 365 MB). tttt's phases sum to ~270 ms of a 360 ms run; the rest is presumably
  teardown (freeing every node individually) - see triage.

### ./failstats.py -n 12
```
failstats @ 4af1220
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
pass: all 829  errors 3080  symbols 4740  types 1679  (of 11236)

checker log_error output: 7220 lines across 672 tests
  top messages:
    2253  in 175 tests  generic.c:82: error: inferring types of generic with callable not yet implemented
    1450  in 88 tests  generic.c:99: error: inferring types of generic with union/intersection not yet implemented
     921  in 24 tests  type.c:915: error: Max type recursion depth exceeded (type_extends_internal)
     463  in 19 tests  error.c:180: error: Error added with invalid span: TS2451: Cannot redeclare block-scoped variable '_
     445  in 61 tests  error.c:180: error: Error added with invalid span: TS2300: Duplicate identifier '_'.
     406  in 82 tests  generic.c:86: error: inferring types of generic with interface not yet implemented
     303  in 75 tests  generic.c:90: error: inferring types of generic with array not yet implemented
     233  in 12 tests  error.c:180: error: Error added with invalid span: TS2304: Cannot find name '_'.
     183  in 99 tests  error.c:180: error: Error added with invalid span: TS2305: Module '_' has no exported member '_'.
     141  in 36 tests  declaration.c:239: error: Tried to add multiple namespaces to declaration _
     129  in 21 tests  generic.c:94: error: inferring types of generic with tuple not yet implemented
     105  in 63 tests  scope.c:146: error: Imported source file '_' is not a module
  ... 9 more rows, 7220 total

errors component: 8156 failing
  by shape:
    3296  tsc reports nothing, tttt reports errors (all spurious)
    2933  both missing and spurious
    1391  tsc reports errors, tttt reports nothing
     201  only spurious errors
     189  only missing errors
     124  same codes+positions, message text differs
      22  same codes+lines, column differs
  4543 tests have a spurious TS1xxx, ie the parser rejects syntax tsc accepts (typechecking is skipped for those, so their types/symbols fail too); 2344 of them expect no errors at all
  missing diagnostics (tsc has, tttt lacks) by code:
    1730  in 560 tests  TS2322
     898  in 350 tests  TS2339
     680  in 38 tests  TS18050
     564  in 123 tests  TS2300
     494  in 192 tests  TS2345
     454  in 47 tests  TS2365
     336  in 136 tests  TS1005
     312  in 124 tests  TS6133
     299  in 27 tests  TS2362
     244  in 58 tests  TS2411
     201  in 22 tests  TS2356
     184  in 39 tests  TS2314
  ... 878 more rows, 19328 total
  spurious diagnostics (tttt has, tsc lacks) by code:
   22250  in 3676 tests  TS2304
   14235  in 3007 tests  TS1005
   10618  in 2346 tests  TS1434
    9750  in 1653 tests  TS1128
    4792  in 202 tests  TS2503
    3594  in 516 tests  TS2451
    3508  in 927 tests  TS1184
    2122  in 563 tests  TS1109
    1703  in 637 tests  TS2693
    1134  in 402 tests  TS2322
     702  in 225 tests  TS2345
     674  in 139 tests  TS2300
  ... 73 more rows, 82987 total
  tests whose errors diff involves a single code (would pass errors if fixed alone):
     212  spurious TS2304
     156  spurious TS2322
     139  missing TS2322
     112  spurious TS1005
      97  missing TS6133
      76  spurious TS2345
      72  missing TS2339
      71  spurious TS2451
      52  spurious TS7006
      44  spurious TS1128
      40  spurious TS2307
      38  spurious TS2554
  ... 324 more rows, 2389 total

symbols component: 6496 failing, mean line match 92.9%; failing tests by line match: >=95%: 1751, 80-95%: 3579, 50-80%: 1116, <50%: 50
  nearly passing (full list in report.txt): commentOnAmbientVariable2 crashInResolveInterface es6UseOfTopLevelRequire moduleResolutionWithRequireAndImport noBundledEmitFromNodeModules noImplicitAnyAndPrivateMembersWithoutTypeAnnotations protoAsIndexInIndexExpression sourceMapWithMultipleFilesWithCopyright typeReferenceDirectiveWithTypeAsFile typeReferenceDirectives10 typeReferenceDirectives11 typeReferenceDirectives13

types component: 9557 failing, mean line match 77.1%; failing tests by line match: >=95%: 492, 80-95%: 3320, 50-80%: 4813, <50%: 932
  nearly passing (full list in report.txt): commentOnAmbientClass1 commentOnAmbientEnum commentOnAmbientVariable2 commentOnInterface1 doNotemitTripleSlashComments noBundledEmitFromNodeModules sourceMapWithMultipleFilesWithCopyright tripleSlashReferenceAbsoluteWindowsPath typeReferenceDirectiveWithTypeAsFile library-reference-scoped-packages conditionalTypeDiscriminatingLargeUnionRegularTypeFetchingSpeedReasonable arithmeticOperatorWithTypeParameter

full data: build/failstats/report.txt, tests.tsv, near_symbols.txt, near_types.txt, stderr.txt
```

### ./failstats.py --syntax -n 20
```
4572 tests where tttt reports a syntax error tsc does not; text at the first one:
    568  'module id {'          eg AmbientModuleAndNonAmbientClassWithSameNameAndCommonRoot: module A {
    158  'declare module "s"'   eg aliasDoesNotDuplicateSignatures: declare module 'demoModule' {
    155  'module id'            eg ClassAndModuleThatMergeWithModuleMemberThatUsesClassTypeParameter: module clodule1 {
    135  '= require("s"'        eg aliasAssignments: import moduleA = require("./aliasAssignments_moduleA");
    112  '[id.id'               eg ES5For-ofTypeCheck10: [Symbol.iterator]() {
    109  'declare module id'    eg AmbientModuleAndAmbientFunctionWithTheSameNameAndCommonRoot: declare module Point {
    103  '{'                    eg FunctionDeclaration10_es6: function * foo(a = yield => yield) {
     98  'export module id'     eg TwoInternalModulesThatMergeEachWithExportedLocalVarsOfTheSameName: export module A {
     80  '@id'                  eg classStaticBlock19: @decorator
     78  ''                     eg TemplateExpression1: var v = `foo ${ a
     73  'id {'                 eg ambientEnumDeclaration2: declare const enum E1 {
     63  ': {'                  eg commonJsIsolatedModules: "compilerOptions": {
     56  'id:'                  eg blockScopedBindingsReassignedInLoop2: loop:
     36  '= require("s")'       eg aliasesInSystemModule1: import alias = require('foo');
     34  'do {'                 eg breakInIterationOrSwitchStatement2: do {
     30  'id() {'               eg builtinIterator: next() {
     30  ': id,'                eg maxNodeModuleJsDepthDefaultsToZero: "compileOnSave": true,
     29  '}'                    eg assignmentCompatWithDiscriminatedUnion: enum MyEnum { A = 1, B = 2 }
     28  'id;'                  eg awaitBinaryExpression5_es2017: o.a = await p;
     28  'id'                   eg checkJsdocTypeTag6: methodWithMoreParameters(more) {}, // error
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
