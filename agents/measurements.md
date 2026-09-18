# Measurements
Recorded output of `./failstats.py` and `./bench.sh`. Replace (don't append forever) when re-measuring; keep the
date and commit. History is in git.

## 2026-09-18, type core stage 2 (LazyType memos in the Checker)
Pure refactor. Suite: 1084 full, errors 4396, symbols 5939, types 2146 - identical; wall 8.29 s (8.27 s before).
`log_error` lines 39187 -> 38933 with the same distribution: the harness reruns a test when it leaves net allocations
behind, and tests no longer leave memos in the shared lib AST (49 reruns left, 46 of them lib program cache fills).
Bench (machine noisier than for the run recorded below: unchanged tsc went 2.58 -> 3.04 s): tttt 0.650 s best, typecheck
phase 124 ms (113), **peak RSS 838 MB (790)** - `LazyType` grew 8 bytes and slot pages are fragmented by slots taken
for transient scopes, see `hacks.md`. The bench numbers further down are from before this stage.

## 2026-09-18, type core stage 1 (shadow mode)
`TTTT_SHADOW_TYPES=1 TTTT_SHADOW_STATS=1 ./release/baseline_tests --summary --dump /tmp/dump 2>err.txt` (the env vars
work with `./failstats.py` and `./test.sh` too; mismatches are `log_error`s, so failstats groups them). With the flag
off the suite is unchanged: 1084 full passes, same wall time.

- Whole suite, 10 076 programs: 229 569 recorded type nodes + 27 710 declarations lowered, members of every
  declaration resolved, everything printed. **No crashes, valgrind clean on 430 tests** (recursive*, mappedType*,
  intersectionType*, conditionalType*, infer*, keyof*, indexedAccess*, genericClass*, typeAlias*, enum*...).
  328 522 types created, 74.5 MB arena in total (7.4 KB and 33 types per program), **0.74 s for the whole shadow pass
  across the suite** (73 us per program, which includes rendering every node with the legacy display to compare).
  Suite wall time 8.3 s -> 9.3 s with the flag on.
- Of 149 885 comparable nodes (no aliases/type operators/`typeof` inside, where the new core prints differently on
  purpose): **~590 mismatches (0.4%)**, plus 539 that differ only in union member order and 3 509 only in namespace
  qualification (both expected, see `hacks.md: qualified_name`). What they are, from sampling:
  - the new core reducing where the legacy display echoes syntax: `string & number` -> `never`, `"a" & string` ->
    `"a"`, `A & (B | C)` distributed, duplicate union members/properties removed, `true | false` -> `boolean`,
    unions nested in other types reordered (the order-only check is top level only). ~60%, new core is right.
  - legacy display quirks: function types in unions without parentheses, `?` missing on optional methods, property
    names that need quotes printed bare. New core is right.
  - parse error recovery leftovers (`typeof (empty)`, `Promise<(empty)>`): garbage in, different garbage out.
  - real gaps (in `triage.md`/`hacks.md`): construct signatures in type literals parse as a method called `new`
    (parser), `{ 2.5: B }` prints as `"2.5"`, file-module namespace values print as `typeof ` with no name.
- Probes (`--shadow-types` with `TTTT_SHADOW_DUMP=1` prints every node's type id and rendering):
  `List<string>` lowered twice is one id; `Box<Box<number>>["v"]["v"]` resolves to `number` through two lazy
  instantiations; a 1500-literal union alias used 4 times: whole shadow pass 2.4 ms (3005 nodes, 1518 types, 289 KB);
  `varianceProblingAndZeroOrderIndexSignatureRelationsAlign` (the recursive generics repro): whole shadow pass
  0.11 ms, 142 types, 35 KB - the legacy checker still takes 0.8 s on it, that's stage 3's target.
- `deeplyNestedConditionalTypes` hits the instantiation depth limit (100): conditional types never resolve yet, so
  instantiating a 100-deep conditional chain instantiates all of it. Expected to go away with stage 3.

## 2026-09-18, after the second parser gaps session (working tree on top of 5aba5cc)

### Takeaways
- **Parser-rejected tests: 4572 -> 2890 (round 1) -> 1204 (round 2).** Full passes 987 -> 1084 (errors 3695 -> 4396,
  symbols 5172 -> 5939, types 1974 -> 2146). Round 2 added: object literal members (shorthand, methods, get/set,
  computed keys, spread), class accessors as real properties, `#private` names, `p!: T`, stray `;`, generator
  methods, string enum member names, spread in calls/`new`/array literals, `satisfies`, `async`/`await`,
  `function*`/`yield`/`yield*`, `delete`, template literals with substitutions, tagged templates, decorators,
  anonymous `class extends X {}`, ambient-context propagation into `declare namespace` bodies.
- What is left of the syntax list is a long flat tail (largest bucket is 34 tests), much of it *error recovery* tests
  where tsc also reports syntax errors but different ones. Real remaining constructs: destructuring with computed
  keys / rest / defaults in object patterns, `for await`, `export { a as default } from`, `import()`, `new.target`,
  `o?.()`, regex literals after `=`, `**=`, shebangs, JSX, `import defaultBinding, * as ns`. A third dedicated
  parser session is no longer the best use of a session; fold these into other work or a triage-sized task.
- **The old type core now blows up on real input.** `log_error` "Max type recursion depth exceeded
  (type_extends_internal)" fires 31k times in 36 tests. Once `_A!: A` parsed,
  `varianceProblingAndZeroOrderIndexSignatureRelationsAlign` (recursive generic classes `Either<L, A>`) took the
  whole suite down: 38 GB RSS then OOM-kill, because every level of structural comparison clones and substitutes
  ever larger types and the relation cache key is their string rendering. Contained by lowering the depth limit
  400 -> 100 (1.2 s and 230 MB for that one test, still absurd). This is the strongest evidence so far for the type
  core redesign (type identity, instantiation cache, "maybe" cache, tsc-like depth limit of identical instantiations).
- Checker gaps that the newly parsing tests make loud: missing TS2322/TS2339/TS2345, spurious TS2304 (1604 tests,
  mostly lib/target related: `Symbol`, `Promise`, `Generator`, `IterableIterator` don't exist with the ES5-only libs
  the harness loads, so **harness fidelity (`@target`/`@lib`) is now a bigger lever than more syntax**), generic
  inference "not yet implemented" (callable, union, interface, array, tuple).
- Namespaces still don't merge (167 tests), unchanged.
- No panics/crashes across the suite; valgrind (`./test.sh`) is clean on ~1000 tests of the newly parsing families.
- Perf: bench corpus grew (it's built from passing tests: 219 -> 300 files), so wall times aren't comparable with the
  previous run. Per line tttt is unchanged within noise (stdlib-only parse 18-27 ms run to run on this machine).
  Ratios are as before: ~4x faster than JS tsc, between single- and multi-threaded tsgo, worst memory of the three.

### ./failstats.py -n 12
```
failstats @ 5aba5cc
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
pass: all 1084  errors 4396  symbols 5939  types 2146  (of 11236)

checker log_error output: 39187 lines across 901 tests
  top messages:
   31403  in 36 tests  type.c:915: error: Max type recursion depth exceeded (type_extends_internal)
    2570  in 179 tests  generic.c:82: error: inferring types of generic with callable not yet implemented
    2010  in 97 tests  generic.c:99: error: inferring types of generic with union/intersection not yet implemented
     494  in 81 tests  generic.c:86: error: inferring types of generic with interface not yet implemented
     467  in 70 tests  error.c:180: error: Error added with invalid span: TS2300: Duplicate identifier '_'.
     464  in 84 tests  generic.c:90: error: inferring types of generic with array not yet implemented
     458  in 15 tests  error.c:180: error: Error added with invalid span: TS2451: Cannot redeclare block-scoped variable '_
     404  in 167 tests  declaration.c:239: error: Tried to add multiple namespaces to declaration _
     233  in 12 tests  error.c:180: error: Error added with invalid span: TS2304: Cannot find name '_'.
     212  in 142 tests  scope.c:164: error: Imported source file '_' is not a module
     183  in 99 tests  error.c:180: error: Error added with invalid span: TS2305: Module '_' has no exported member '_'.
     159  in 21 tests  generic.c:94: error: inferring types of generic with tuple not yet implemented
  ... 8 more rows, 39187 total

errors component: 6840 failing
  by shape:
    2175  both missing and spurious
    2087  tsc reports nothing, tttt reports errors (all spurious)
    1962  tsc reports errors, tttt reports nothing
     265  only missing errors
     165  only spurious errors
     161  same codes+positions, message text differs
      25  same codes+lines, column differs
  1170 tests have a spurious TS1xxx, ie the parser rejects syntax tsc accepts (typechecking is skipped for those, so their types/symbols fail too); 367 of them expect no errors at all
  missing diagnostics (tsc has, tttt lacks) by code:
    1542  in 535 tests  TS2322
     892  in 350 tests  TS2339
     680  in 38 tests  TS18050
     565  in 122 tests  TS2300
     436  in 168 tests  TS2345
     408  in 39 tests  TS2365
     334  in 134 tests  TS1005
     312  in 124 tests  TS6133
     244  in 58 tests  TS2411
     218  in 21 tests  TS2362
     201  in 22 tests  TS2356
     184  in 39 tests  TS2314
  ... 875 more rows, 18469 total
  spurious diagnostics (tttt has, tsc lacks) by code:
    7291  in 1604 tests  TS2304
    3720  in 728 tests  TS1005
    3424  in 460 tests  TS1128
    3228  in 442 tests  TS2451
    1812  in 581 tests  TS2322
    1484  in 22 tests  TS2694
    1273  in 277 tests  TS1109
    1240  in 306 tests  TS2345
    1209  in 439 tests  TS2693
     811  in 214 tests  TS7006
     770  in 237 tests  TS1434
     648  in 198 tests  TS2554
  ... 76 more rows, 34006 total
  tests whose errors diff involves a single code (would pass errors if fixed alone):
     402  spurious TS2304
     204  spurious TS2322
     169  missing TS2322
     139  spurious TS2307
     113  spurious TS2451
     110  missing TS2339
     102  missing TS6133
      93  spurious TS2693
      88  spurious TS2345
      69  spurious TS7006
      65  misplaced TS2322
      54  spurious TS2554
  ... 422 more rows, 3341 total

symbols component: 5297 failing, mean line match 95.2%; failing tests by line match: >=95%: 1827, 80-95%: 2846, 50-80%: 606, <50%: 18
  nearly passing (full list in report.txt): aliasAssignments allowJsCrossMonorepoPackage allowSyntheticDefaultImports10 commentOnAmbientVariable2 crashInResolveInterface declFileAliasUseBeforeDeclaration duplicatePackage_packageIdIncludesSubModule duplicatePackage_relativeImportWithinPackage duplicatePackage_relativeImportWithinPackage_scoped es6UseOfTopLevelRequire importAsBaseClass literalTypeNameAssertionNotTriggered

types component: 9090 failing, mean line match 81.8%; failing tests by line match: >=95%: 723, 80-95%: 3986, 50-80%: 3865, <50%: 516
  nearly passing (full list in report.txt): aliasAssignments allowSyntheticDefaultImports10 collisionExportsRequireAndAlias commentOnAmbientClass1 commentOnAmbientEnum commentOnAmbientVariable2 commentOnElidedModule1 commentOnInterface1 declFileForExportedImport declarationEmitHasTypesRefOnNamespaceUse doNotemitTripleSlashComments externalModuleReferenceOfImportDeclarationWithExportModifier

full data: build/failstats/report.txt, tests.tsv, near_symbols.txt, near_types.txt, stderr.txt
```

### ./failstats.py --syntax -n 20
```
1204 tests where tttt reports a syntax error tsc does not; text at the first one:
     34  '{'                    eg FunctionDeclaration10_es6: function * foo(a = yield => yield) {
     31  ''                     eg es6ImportNamedImportIdentifiersParsing: 
     17  '{ }'                  eg MemberFunctionDeclaration4_es6: *() { }
     13  '];'                   eg badArrayIndex: var results = number[];
     13  'id;'                  eg generatorTypeCheck32: var f: () => number = () => yield s;
     12  'default } from "s"'   eg constEnumNoEmitReexport: export { MyConstEnum as default } from './ConstEnum';
     11  'import id = id'       eg ambientExternalModuleWithRelativeExternalImportDeclaration: import m2 = require("./SubModule");
     11  '"s"'                  eg missingSemicolonInModuleSpecifier: import {x} from "./a"
     10  'id'                   eg importAssertion4: import * as f from "./first" assert
      8  'await (const id id'   eg arrayFromAsync: for await (const v of asyncGen(4)) {
      8  'async function id'    eg asyncFunctionDeclaration7_es2017: async function foo(a = await): Promise<void> {
      8  '= id;'                eg compoundExponentiationAssignmentLHSCanBeAssigned1: x1 **= a;
      8  'continue id;'         eg continueNotInIterationStatement4: continue TWO;
      8  '...id } = id'         eg destructuringUnspreadableIntoRest: const {                 ...rest1 } = this;
      8  'id id is id'          eg moduleResolutionWithExtensions_unexpected2: This file is not read.
      7  '=> {'                 eg ArrowFunction3: var v = (a): => {
      7  '/id/;'                eg controlFlowDoWhileStatement: x = /a/;
      7  '#!/id/id/id id'       eg emitBundleWithShebang1: #!/usr/bin/env gjs
      6  'async id() { }'       eg asyncAwaitIsolatedModules_es2017: static async m4() { }
      6  '.();'                 eg callChain.2: o1?.();
```

### ./bench.sh
```
corpus: 300 files, 482600 lines, 6355000 bytes; 5 runs each
tttt                   best   0.600s  median   0.600s  peak RSS    790 MB  exit 0
tsc 5.8.2 (node)       best   2.580s  median   2.690s  peak RSS    775 MB  exit 0
tsgo 7.0.2             best   0.390s  median   0.400s  peak RSS    482 MB  exit 1
tsgo single-threaded   best   0.780s  median   0.820s  peak RSS    484 MB  exit 1

tttt phases (one run):
  time: load+parse libs 13.2775878906 ms
  time: load+parse files 162.181396484 ms
  time: resolve names 149.547119141 ms
  time: typecheck 112.892089844 ms

stdlib only (parse + resolve + typecheck of thirdparty/lib/esnext.full.d.ts and everything it references;
tttt reports name errors there, so --keep-going. 52208 lines):
  time: load+parse libs 0.000244140625 ms
  time: load+parse files 21.7944335938 ms
  time: resolve names 9.80786132812 ms
  time: typecheck 48.2622070312 ms
```
