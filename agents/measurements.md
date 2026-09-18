# Measurements
Recorded output of `./failstats.py` and `./bench.sh`. Replace (don't append forever) when re-measuring; keep the
date and commit. History is in git.

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

## 2026-09-18, type core stage 4b2 (type aliases and type operators evaluate to `Ty`)
Suite: **1137 full (1120), errors 4657 (4648), symbols 5981 (5979), types 2219 (2186)**; suite wall 4.9 s (4.5 s).
- Components: 51 tests gained components, 4 lost one (`homomorphicMappedTypeWithNonHomomorphicInstantiationSpreadable1`
  and `reverseMappedTupleContext`: errors, need reverse mapped type inference; `mappedTypeNoTypeNoCrash`,
  `noSymbolForMergeCrash`, `indexedAccessPrivateMemberOfGenericConstraint`: types, error-recovery printing). No full
  pass lost. Most of the types gain is alias naming (`type Bar = Omit<..>` is `Bar`, `type Tup<T> = [T, T]` is `Tup<T>`).
- `log_error` lines unchanged (2 078 in 502 tests): none of what's left is about types.
- Bench: typecheck 118 ms, total 0.60 s, RSS 842 MB - unchanged.
- valgrind clean on ~400 tests (keyof*, mappedType*, indexedAccess*, destructuring*, conditionalType*, inferTypes*,
  recursive*, tuple*, typeAlias*, circular*, variadicTuples*, promise*, awaited*, and the 3 tests that used to leave
  allocations in the shared lib AST: they don't any more).

## 2026-09-18, type core stage 4b1 (calls, inference and property access on `Ty`)
Suite: **1120 full (1115), errors 4648 (4590), symbols 5979 (5974), types 2186 (2183)**; suite wall 4.5 s (4.3 s).
- `log_error` lines 4 606 -> 2 169: the legacy "inferring types of generic with ... not yet implemented" family is gone
  with the code. What's left is mostly invalid-span errors and namespace merging (`failstats.py` lists them).
- Components: errors +94 / -39, symbols +7 / -2, types +14 / -11; 4 full passes lost (`classDeclaredBeforeClassFactory`,
  `classExtendingQualifiedName2`, `sourcemapValidationDuplicateNames`, `emitClassDeclarationWithPropertyAccessInHeritageClause1`:
  all print `typeof X` / `M.X` where tsc prints a local or qualified name differently: symbol accessibility, plan 03).
  The 39 lost error components are nearly all "inference is real now, contextual typing isn't": object/array literal
  arguments whose literals the legacy checker already widened, callbacks whose parameters are `any` instead of
  contextually typed, type predicates (`x is T`, not represented in the AST at all), reverse mapped types. Of the four
  4a losses named below, `typeParameterLeak` and `addMoreCallSignaturesToBaseSignature*` are back; `arrayFind` needs
  type predicates, `correctOrderOfPromiseMethod` mapped types over tuples + tuple contextual typing,
  `objectFreezeLiteralsDontWiden` const-context inference.
- Bench: typecheck phase 118 ms (116-122 before), total 0.58-0.60 s, RSS 843 MB. No call resolution cache yet: a call
  is resolved once for its type and once for its errors.
- valgrind clean on ~380 tests (genericCall*, overload*, functionCall*, typeArgumentInference*, spread*, restParam*,
  propertyAccess*, inferT*, arrayF*, promise[A-P]*, classExtend*, clodule*, conditionalType[A-C]*, infer[A-S]*,
  mappedType[A-C]*).

## 2026-09-18, type core stage 4a (classes, interfaces, enums evaluate to `Ty`)
Suite: **1115 full (1100), errors 4590 (4553), symbols 5974 (5939), types 2183 (2149)**; suite wall **4.3 s (5.6 s)**.
- `log_error` lines 7 590 -> 4 606. The legacy generic inference "not yet implemented" family is 2.9k of what's left.
- ~107 tests gained a component, 13 lost one (2 of them full passes). The losses: weaker call inference now that
  parameter types from interfaces/classes are `TYPE_NEW` (`arrayFind`, `typeParameterLeak`, `correctOrderOfPromiseMethod`,
  `objectFreezeLiteralsDontWiden`, `typeInferenceReturnTypeCallback`, `addMoreCallSignaturesToBaseSignature*`), single
  member enums printing as `E` in messages (`operatorAddNullUndefined`), and a few not looked at
  (`recursiveBaseCheck3`, `recursiveBaseConstructorCreation3`, `computedPropertyNames21_*`,
  `controlFlowElementAccessNoCrash1`).
- Bench: typecheck phase 116-122 ms vs 109-132 ms for b1be87d, measured back to back (total 0.68 s, RSS 838 MB). It was
  205 ms until `checker_atom` got a hash finalizer: FNV's low bits put `ns1.Foo`, `ns2.Foo`... in long probe runs.
  stdlib-only typecheck 73 ms -> 19 ms (lib interfaces are no longer merged structurally by the legacy checker).
- valgrind clean on ~650 tests (enum*, constEnum*, class[A-E]*, derivedClass*, interface[A-E]*, generic[C-F]*, inherit*,
  recursive[A-C]*, merged*, abstract*, accessor*, constructor*, static*, this*, super*, mappedType[A-E]*, arrayF*, promise*).
- Shadow mode still runs the whole suite without crashing (mismatch count not re-measured; it compares less and
  less as the legacy display of a named type *is* the new display now).

## 2026-09-18, type core stage 3 (relations on the new core)
Suite: **1100 full (1084), errors 4553 (4396), symbols 5939 (=), types 2149 (2146)**; suite wall **5.6 s (8.3 s)**.
- `log_error` lines 38 933 -> 7 590; "Max type recursion depth exceeded (type_extends_internal)" 31k -> 0 (one
  `index_type_evaluate_type_internal` depth hit is left). What's left is led by the legacy generic inference
  "not yet implemented" family (5.4k) and invalid-span errors.
- Recursive generics repro (`varianceProblingAndZeroOrderIndexSignatureRelationsAlign`): 0.68 s / 222 MB -> not
  measurable above harness startup (0.07 s / 50 MB, same as `2dArrays`).
- Spurious TS2322: 1812 in 581 tests -> 1024 in 371; missing TS2322 1542 in 535 -> 1323 in 500. Spurious TS2345 1240
  in 306 -> see `./failstats.py --code TS2345`. Elaboration chains (`Types of property 'x' are incompatible.`, TS2741/
  TS2739/TS2740 heads, parameter/return lines, variance-based type argument lines) are where most of the +157
  errors-component passes come from.
- 41 test components regressed (list with `./fasttest.sh --summary` against the saved results of ec5fbe5; none of
  them a full pass). Sampled causes: the old relation said yes where the legacy checker hands it garbage - weak generic
  inference (`unknown`/unsubstituted type parameters), no contextual typing / literal freshness (`Box<0>` vs
  `Box<number>`, `{ x: string }` against `"lit"`), no narrowing (`assert<0>(a)` in a `switch`), no discriminated
  union decomposition, elaboration that tsc attaches to the object literal property instead. Those are 4b's problems.
- Shadow mode (`TTTT_SHADOW_TYPES=1`) still runs the whole suite without crashing: 1072 mismatch lines. New in there:
  ~250 union *order* differences nested in other types (`(Derived2 | Derived1)[]`): relations now lower types during
  checking, so creation order follows check order rather than the order the shadow pass used to impose. Expected
  residue until evaluation order is tsc's (stage 4).
- Bench: typecheck phase 127.5 ms vs 124.7 ms for ec5fbe5 measured back to back (best of 5; +2%, within noise, total
  0.68 s, RSS 846 MB vs 838). stdlib-only typecheck 73 ms for both. The relation itself is ~1.4% of the bench run,
  lowering ~1%: the corpus is assignment-light, and the legacy evaluation around it is unchanged.
- valgrind clean on ~280 tests mid-session and 759 at the end (assignmentCompat*, recursive*,
  conditionalType*, inferTypes*, variance*, mappedType*, enum*, tuple*, unionType*, intersectionType*, circular*,
  genericClass*, typeAlias*, class[A-E]*, derivedClass*, subtyp*, indexSignature*, keyof*, comparisonOperator*).

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
