# Measurements
Recorded output of `./failstats.py` and `./bench.sh`. Replace (don't append forever) when re-measuring; keep the
date and commit. History is in git.

## 2026-09-19, plan 04 stages 3 + 4: modules from everywhere, printing by accessibility
Suite: **1684 full of 13163 tests** (was 1576), errors 6401 (6050), symbols 8401 (8332), types 3733 (3533). Step by
step: module refs + ambient modules + `export =` + import types + `import()` +339 tests with a gained component / -13
(package.json `exports` resolution, an augmentation meeting an export alias), `globalThis` as a namespace and keyword
names in specifiers +60, `arguments`/`debugger` +46/-2, names by accessibility +94/0, removing the written-type-name
hack +33/-10 (it hid `any`s), tsc's root file rule in the harness +39/-53 (errors -28, symbols +14, types +14: the
losers passed only because every file was a root; they need `typeRoots`/`paths` resolution, see `triage.md`).
`failstats.py`: spurious TS2304 7164 in 1713 tests -> 5420 in 1241, spurious TS2307 2398/575 -> 2224/471, all
spurious diagnostics 26357 -> 23323; log_error lines 71 -> 66. 145 module tests clean under valgrind.

Real corpus, now with its real options (`--target es2020 --lib es2020 --strict`) and `@types/node` as tsc's own
tsconfig has it: **7754 diagnostics, 1 name error** (`source-map`, package.json typings; was 51 + 11 unresolved
import types once those stopped being `any`). `@types/node` + `undici-types` (120 .d.ts files of ambient modules,
`export =`, `import x = require()`, `globalThis.X` aliases, `typeof import()`) give 60 diagnostics, none of them a
name error: TS2344 x30, TS7022 x13, TS2392 x10, TS2370 x4.

`./bench.sh`: synthetic **0.36 s** (tsgo 0.50 / 1.21 single-threaded), 514 MB; real corpus **0.79 s** (tsgo 0.83 /
1.44), 312 MB. Phases on the real corpus: parse 144 ms, resolve names 72 ms, typecheck 574 ms. The CLI no longer
frees the AST at exit (release builds, ~40 ms; `TTTT_TEARDOWN=1` does). `Symbol` lost its alias/module fields to side
structs and declarations start at capacity 1 (RSS 337 -> 327 MB with node types loaded). **The plan's "resolve names
<= 40 ms" was not reached** (72-77 ms): callgrind of the phase is 206 M instructions of which ~35 M are `rep stos` of
fresh arena chunks over-counted, `scope_resolve` + table probes 33% inclusive, declare (`scope_init_declaration`) 24%;
it is memory-bound pointer chasing (two AST walks, a scope chain walk per identifier), not something a local fix
removes. Ideas are in `triage.md`.

## 2026-09-19, plan 04 stage 2: the type side goes by symbols
Suite (`./passcount.py`): **1576 full of 13163 tests** (unchanged), errors 6050 (+1), symbols 8332, types 3533.
Gained an errors component: errorConstructorSubtypes, keyofModuleObjectHasCorrectKeys (namespace/module members go by
export name now). Lost: signatureCombiningRestParameters2 errors (lib-internal `console: Console` sees the test's `Console`
augmentation now, which is right; the two `log` signatures then need tsc's `getIntersectedSignatures` for the contextual
signature, see `hacks.md` "The binder"). `failstats.py` log_error lines unchanged (71 in 22 tests).
Real corpus: 7758 diagnostics, identical by code to stage 1. 755 binder-heavy tests (`^(enum|import|export|merged|
augment).*`) clean under valgrind.

`./bench.sh` (machine noisier than for stage 1: tsgo measured 0.52 / 0.87 s in the same run): synthetic **0.42 s
(was 0.49), peak RSS 522 MB (was 760)**, phases parse 172 ms, resolve names 100 ms (128), typecheck 76 ms. Real corpus
0.86 s, **310 MB (was 392)**, resolve names 76 ms (81), typecheck 568 ms. The RSS is the `Declaration` payload
(two `MergedType`s, three vecs, a LazyType slot per symbol and per class/enum/interface) that no longer exists.

## 2026-09-19, plan 04 stage 1: symbols, persistent scopes, aliases (the new binder)
Suite: **1576 full of 13163 tests** (was 1508), errors 6049 (5738), symbols 8332 (7891), types 3533 (3459). Lost
components versus the old binder: 7 errors, ~35 symbols, 14 types (default-export corner cases, `declare global` inside
a namespace, `export { x }` merged with an exported `x`, moduleAssignmentCompat*: listed in `hacks.md`, "The binder").
Suite wall time 5.1 s -> 3.2 s (no more copying 20k lib declarations per test program). `failstats.py` log_error
lines: thousands ("Tried to add multiple namespaces" 485, "Error added with invalid span" ~1300, "Imported source
file is not a module" 163 tests) -> 71 lines in 22 tests, none of them the binder's but one. Spurious TS2451 3245 ->
off the chart (< 300), spurious TS2300 684 -> 327 (class members and pretty-printed conflict tests).

Real corpus (`./realworld.py`): 9816 -> **7758 diagnostics**; TS2440 10 -> 0, the DOM `Node` is gone (no "baseURI" in
the output), name errors 117 -> 51 (all node globals, `arguments`, `debugger`). What is left is narrowing: TS2345 5231
(`X | undefined` arguments), TS7006 891, TS2322 583, TS2769 288. The plan hoped for "well under 7k": fixing `Node`
made ~2000 calls check for real, and they now fail on missing narrowing instead.
Probes (`test/probes/plan04/`): errors match tsc on all three; scopingBasics also symbols.

`./bench.sh`: synthetic 0.49 s (tsgo 0.52), phases parse 171 ms, **resolve names 128 ms (was 166)**, typecheck 78 ms,
peak RSS 760 MB. Real corpus 0.80 s (tsgo 0.77, 1.29 single-threaded), 392 MB (was 410), phases parse 129 ms,
**resolve names 81 ms (was 116; stage 4 target <= 40)**, typecheck 505 ms.

## 2026-09-18, plan 03: the real-world corpus parses and typechecks
Suite: **1508 full of 13163 tests** (was 1434), errors 5738 (5513), symbols 7891 (7797), types 3459 (3360). No test or
component was lost at any step. Tests with a spurious TS1xxx: 1497 (was 2017).

### Real corpus: `./realworld.py` (entry `thirdparty/tsc/src/compiler/checker.ts`, 78 files, 193k lines, 9.9 MB; clean under tsc)
| | before | after |
|-|-|-|
| syntax errors (TS1xxx) | 1119 in 31 files | **0** |
| outcome | SIGABRT after 0.7 s (heap corruption) | runs to the end, exit 1 |
| name errors | never got there | 117 (104 TS2304: `require`, `process`, ... no `@types/node`; 10 TS2440; 2 TS2451) |
| type errors | never got there | 9699 |

Diagnostics by code (all of them tttt bugs): TS2345 6840, TS7006 896, TS2322 567, TS2769 565, TS2349 179, TS2367 173,
TS2495 147, TS2304 104, TS7022 68, TS2362 56, TS2558 38, TS2538 25, TS2740 20, TS2363 20, TS2364 19, TS2556 15, TS2461 12,
TS2440 10, TS2493 10, the rest < 10 each. What they are (as far as was looked): `triage.md`, "Real corpus, first look".

Perf on the same corpus (`./bench.sh`, second table; tsc/tsgo run `-p src/compiler` with `--composite false
--incremental false` and friends, otherwise the second run finds a tsbuildinfo and checks nothing):
| | wall (best of 3) | peak RSS |
|-|-|-|
| tttt (`--keep-going`, reports 9.8k errors) | **0.87 s** (2.69 s before the two fixes below) | 410 MB |
| tsc 5.8.2 (node) | 4.92 s | 644 MB |
| tsgo 7.0.2 | 0.78 s | 397 MB |
| tsgo single-threaded | 1.27 s | 376 MB |

tttt phases: load+parse libs 17 ms, load+parse files 146 ms (68 MB/s), resolve names 116 ms, typecheck 619 ms.
RSS is 41 bytes per source byte (the synthetic corpus' ~105 is dominated by 200 copies of the same declarations).
The first callgrind profile of this run (`valgrind --tool=callgrind ./release/tttt -q --keep-going .../checker.ts`,
~10 min) found two things that were plainly wrong, fixed in the same session, typecheck 2430 -> ~600 ms:
- `span_line_index` counted newlines from the start of the file for every diagnostic (21% of all instructions with
  9.8k errors in 50k-line files). `SourceFile.line_starts` + binary search now.
- `find_prop` on an intersection type built the intersection of the property types on every lookup (2.9M lookups,
  53% inclusive, most of it in `ty_intersection` distributing over unions). Memoised per (intersection, name) in
  `Checker.intersection_props`, like tsc's synthetic property symbols.
Still not a comparison: tttt fails most calls early and tsgo reports nothing. Re-profile when the error count is down;
next suspects from that profile were `intern_slot`/`intern_composite` (union/intersection construction), the
`qsort` in `ty_union`, and `span_cmp` (error sorting).

Synthetic corpus (unchanged meaning, 328 files now): tttt 0.60 s / 798 MB, tsc 3.52 s / 862 MB, tsgo 0.54 s / 566 MB
(1.01 s single-threaded). Phases: parse ~200 ms, resolve names 174 ms, typecheck 81 ms.

## 2026-09-18, type core stage 5 (the old type world is deleted)
Suite: **1434 full of 13165 tests** (was 1408), errors 5513 (5516), symbols 7797 (7701), types 3360 (3325). `./passcount.py`
prints these from the saved results file (`./fasttest.sh -s`); 4b3 had forgotten to save its baseline.
- Where the gains came from: namespaces and merged declarations built by the new core (+6 full: `typeof ns` for
  namespace imports, clodules/fundules with the right members and parameter names), the `.symbols` property lookup on
  `Ty` instead of on views (+93 symbols components), printing everything but written type names through `ty_display`
  (+21 full; never qualifying would have been 1413, measured).
- 16 error components lost, 8 symbols/types: `typeof x` is a real type now instead of `any`, which exposes missing
  narrowing/freshness (`stringLiteralTypesInUnionTypes01/03`, `typeofStripsFreshness`, `invalidUndefinedValues`,
  `stringLiteralsWithEqualityChecks03/04`: union order `Refrigerator | "foo"` needs literal types created as lazily
  as tsc does), `aliasUsageIn*` (4 tests: `typeof moduleA` of an `import = require` module vs an interface, needs the
  static side of a class exported from a module to relate), `indexedAccessAndNullableNarrowing` (passed by luck).
- `log_error` lines 2 088 -> 2 096.
- **Bench: total 0.55 s (0.64), typecheck 87 ms (132), peak RSS 774 MB (897)**, on a corpus that grew to 319 files
  (more tests pass). tsc 3.33 s / 866 MB, tsgo 0.45 s / 559 MB (1.02 s single-threaded). The typecheck phase lost the
  legacy views, per-relation lowering of temporaries and clone/drop traffic. Parse (175 ms) and name resolution
  (166 ms) are now each twice the typecheck phase. Suite wall time 6.0 s.
- `src/` is 28.7k lines (30.6k before): -3.4k / +1.5k. valgrind clean on ~2800 tests (`class*`, `enum*`, `namespace*`,
  `function*`, `generic*`, `async*`, `mapped*`, `contextual*`, `infer*`, `object*`, `array*`, `module*`, `import*`,
  `export*`, `alias*`, `decl*`).

## 2026-09-18, type core stage 4b3 (expressions evaluate to `Ty`, contextual typing, explicit type arguments)
Suite: **1408 full of 13165 tests** (was 1362), errors 5516 (5277), symbols 7701 (7528), types 3325 (3243).
- Against the committed baseline: 50 tests newly pass fully, 4 stopped (`contextualTyping24`: `this: void` doesn't
  print; `contravariantOnlyInferenceFromAnnotatedFunction`, `reverseMappedTypeInferenceSameSource1`: reverse mapped
  type inference, used to be hidden by "object literal arguments only need to be comparable";
  `badInferenceLowerPriorityThanGoodInference`: inference priorities). Per component 450 newly pass, 31 newly fail
  (18 errors, 4 symbols, 9 types); the rest of the failing ones are listed in `hacks.md` ("Expressions on the new core").
- Roughly half of the gain is one parser fix found on the way: `new (): T` / `new <T>(): T` in an interface parsed as a
  method named `new`, so no lib constructor interface (`MapConstructor`, `PromiseConstructor`...) was constructable.
  The rest is contextual typing (lambda parameters, literal-preserving object/array literals, tuples from context,
  `as const`), the two-pass inference, inference from the contextual return type, and explicit type arguments.
- `log_error` lines 2 082 -> 2 088. Bench: total 0.64 s (0.63), typecheck 132 ms (124), RSS 897 MB (874): arguments are
  checked once per candidate now. Suite wall time 6.4 s (6.2).
- valgrind clean on ~190 tests (`contextual*`, `genericCallWith*`, `objectLit*`, `arrayLiteral*`, `mapped*`,
  `inferTypes*`, `promise*`, `decoratorMetadata*`...). One use-after-free was found that way, see
  `lazy_type_evaluate` in `hacks.md`: with `LowerCtx.transient` gone, valgrind is the guard against lowering freed
  legacy nodes.

## 2026-09-18, harness fidelity (`CompilerOptions`, multi-config tests, libs by target)
Suite: **1362 full of 13165 tests** (was 1137 of 11962), errors 5277, symbols 7528, types 3243; suite wall 6.2 s (5.4 s).
**The denominators changed, don't compare these with older rows directly:**
- A test file with `@target: es5,es2015` & co is now one test per configuration (11962 files -> 13165 tests), and
  tests with `@noTypesAndSymbols` run for their errors only (913 tests; their symbols and types count as passed,
  which inflates those two columns by 913). 2 tests don't run (no baselines, tsc's harness refuses their options).
- Like for like, the 11236 tests that ran before: full 1137 -> 1137 (none lost, none gained), errors 4657 -> 4810,
  symbols 5981 -> 6211, types 2219 -> 2225. The 1929 newly running tests: 225 full, errors 467, symbols 1315 (913 free),
  types 1017 (913 free).
- 9 tests lost a component, all because an option is honoured now: real `strictNullChecks`-only tests need narrowing
  (`classStaticPropertyTypeGuard`, `destructuringTypeGuardFlow`, `controlFlowOptionalChain2`, `optionalMethods`,
  `defaultParameterAddsUndefinedWithStrictNullChecks`, `literalTypes1`), `for-of13` (for..of over an iterator: TS2495
  was hidden by the missing lib), `es2021/es2022LocalesObjectArgument` (`Intl` var + namespaces don't merge).
- `Symbol`/`Promise`/`Generator`/`IterableIterator` are gone from the spurious TS2304/TS2693 list. What leads it now:
  `import`/`export`/`using`/`const`/`module`/`exports` as names, ie **JS files and newer syntax parsed wrongly** (the
  newly running tests are heavy on `@allowJs`, JSX and ES module syntax: spurious TS1xxx tests 1170 -> 2015).
- `log_error` lines 2 078 -> 2 082.
- Bench unchanged (typecheck 124 ms, total 0.63 s, RSS 874 MB on a corpus that grew to 307 files); `tttt` loads the
  same libs as before unless `--target`/`--lib` is given.
- valgrind clean on ~60 tests including multi-config ones (`abstractProperty`, `strictBuiltinIteratorReturn*`, `a[a-c]*`).

### ./failstats.py -n 14
```
failstats @ 1084036
11962 test files, 13165 tests (configurations), 13163 ran (913 of them errors only: @noTypesAndSymbols), 2 did not (no types/symbols baselines)
pass: all 1362  errors 5277  symbols 7528  types 3243  (of 13163)

checker log_error output: 2082 lines across 651 tests
  top messages:
     485  in 144 tests  error.c:159: error: Error added with invalid span: TS2300: Duplicate identifier '_'.
     471  in 177 tests  declaration.c:250: error: Tried to add multiple namespaces to declaration _
     378  in 19 tests  error.c:159: error: Error added with invalid span: TS2304: Cannot find name '_'.
     243  in 163 tests  scope.c:165: error: Imported source file '_' is not a module
     227  in 133 tests  error.c:159: error: Error added with invalid span: TS2305: Module '_' has no exported member '_'.
     144  in 18 tests  error.c:159: error: Error added with invalid span: TS2694: Namespace '_' has no exported member '_'.
      37  in 12 tests  error.c:159: error: Error added with invalid span: TS2451: Cannot redeclare block-scoped variable '_
      27  in 14 tests  declaration.c:250: error: Tried to add multiple namespaces to declaration
      24  in 12 tests  error.c:159: error: Error added with invalid span: TS2511: Cannot create an instance of an abstract 
      19  in 10 tests  statement.c:123: error: Global scope not set
      18  in 6 tests  error.c:159: error: Error added with invalid span: TS2503: Cannot find namespace '_'.
       8  in 3 tests  error.c:159: error: Error added with invalid span: TS2567: Enum declarations can only merge with nam
       1  in 1 tests  scope.c:75: error: Declaration B acquired multiple times

errors component: 7886 failing
  by shape:
    2428  tsc reports nothing, tttt reports errors (all spurious)
    2382  both missing and spurious
    2352  tsc reports errors, tttt reports nothing
     307  only missing errors
     225  same codes+positions, message text differs
     157  only spurious errors
      35  same codes+lines, column differs
  2015 tests have a spurious TS1xxx, ie the parser rejects syntax tsc accepts (typechecking is skipped for those, so their types/symbols fail too); 901 of them expect no errors at all
  missing diagnostics (tsc has, tttt lacks) by code:
    1316  in 500 tests  TS2322
    1056  in 407 tests  TS2339
     680  in 38 tests  TS18050
     583  in 127 tests  TS2300
     502  in 162 tests  TS1005
     356  in 28 tests  TS2365
     339  in 141 tests  TS2345
     314  in 126 tests  TS6133
     244  in 58 tests  TS2411
     201  in 22 tests  TS2356
     185  in 10 tests  TS2834
     184  in 114 tests  TS2304
     184  in 39 tests  TS2314
     175  in 37 tests  TS1206
  ... 950 more rows, 21821 total
  spurious diagnostics (tttt has, tsc lacks) by code:
   11730  in 2282 tests  TS2304
    6156  in 1174 tests  TS1005
    5088  in 737 tests  TS1128
    3283  in 462 tests  TS2451
    2398  in 575 tests  TS2307
    1989  in 405 tests  TS1109
    1839  in 606 tests  TS1434
    1485  in 23 tests  TS2694
    1132  in 385 tests  TS2322
     881  in 234 tests  TS7006
     873  in 80 tests  TS1127
     743  in 177 tests  TS2300
     732  in 220 tests  TS2693
     589  in 129 tests  TS2364
  ... 81 more rows, 44577 total
  tests whose errors diff involves a single code (would pass errors if fixed alone):
     392  spurious TS2304
     181  spurious TS2307
     161  missing TS2322
     145  spurious TS2322
     132  missing TS2339
     131  spurious TS2451
     102  missing TS6133
      90  spurious TS7006
      72  spurious TS2345
      57  spurious TS2364
      54  spurious TS2351
      39  missing TS2304
      34  missing TS2345
      33  missing TS2300
  ... 457 more rows, 3623 total

symbols component: 5635 failing, mean line match 95.7%; failing tests by line match: >=95%: 2009, 80-95%: 2920, 50-80%: 688, <50%: 18
  nearly passing (full list in report.txt): aliasAssignments allowJsCrossMonorepoPackage allowSyntheticDefaultImports10 commentOnAmbientVariable2 crashInResolveInterface declFileAliasUseBeforeDeclaration duplicatePackage_packageIdIncludesSubModule duplicatePackage_relativeImportWithinPackage duplicatePackage_relativeImportWithinPackage_scoped es6UseOfTopLevelRequire importAsBaseClass literalTypeNameAssertionNotTriggered moduleImportedForTypeArgumentPosition moduleLocalImportNotIncorrectlyRedirected

types component: 9920 failing, mean line match 83.4%; failing tests by line match: >=95%: 837, 80-95%: 4486, 50-80%: 4072, <50%: 525
  nearly passing (full list in report.txt): aliasAssignments allowSyntheticDefaultImports10 collisionExportsRequireAndAlias commentOnAmbientClass1 commentOnAmbientEnum commentOnAmbientVariable2 commentOnElidedModule1 commentOnInterface1 declFileForExportedImport declarationEmitHasTypesRefOnNamespaceUse doNotemitTripleSlashComments externalModuleReferenceOfImportDeclarationWithExportModifier importDeclarationUsedAsTypeQuery moduleLocalImportNotIncorrectlyRedirected

full data: build/failstats/report.txt, tests.tsv, near_symbols.txt, near_types.txt, stderr.txt
```

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

(the `./failstats.py` output of this date was replaced by the one in the harness fidelity section at the top)

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
