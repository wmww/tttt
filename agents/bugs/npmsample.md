# Random npm projects (scripts/npmsample.py) divergences
Found by the npm sample (agents/notes/npm_sampling.md); per seed the table is in agents/measurements.md "Random npm
projects". Repros are test/cases/npm_*.ts (failing until fixed; registry test/npmsample/repros.tsv); text-only
differences (same file, line, code) aren't divergences for the npm workflow and get no repro (agents/notes/npm_sampling.md).

- Seed 20261023, JohnDeved/zooqle-api-ts: `let [, a]: string[] | number[] = s.match(r)` (a union with null) read the
  element through ty_access_reading instead of leaving the declared type (missing TS2345/TS2339/TS2322). Repro
  test/cases/ttttDestructuredNullableUnionInitial.ts. [fixed 68bd2051d]
- Seed 20261023, vijay8856/react-native-find-background-location-vd: an unknown compiler option set to null was
  TS5023 (tsgo's onPropertySet skips null values). Repro test/cases/ttttConfigUnknownOptionNull.ts. [fixed 8ec4e47a4]
- Seed 20261023, AkilSaji/firelab: spurious TS2538 "Type '{}' cannot be used as an index type" indexing with a
  truthiness-narrowed generic indexed access: `const cap = CAP_FOR[method]` (CAP_FOR: `Partial<Record<"x" | "y",
  keyof Caps>>`, method: M extends "x" | "y") then `if (cap) caps[cap]`. tsgo types the narrowed cap
  `NonNullable<Partial<Record<"x" | "y", "a">>[M]>` and `caps[cap]` `Caps[NonNullable<...>]`, no error; tttt's index
  check sees '{}' (the `& {}` of NonNullable without the indexed access's constraint, probably in the TS2538
  isTypeAssignableTo(index, keyof object) path). Repro test/cases/npm_firelab_TS2538.ts (7 lines). [flip] [fixed 1f9c5ae2d]
- Seed 20261023, overture-stack/arranger (modules/sqon): `SqonBuilder.or([study, site])` with `as const` objects
  against `SqonNode | SqonNode[]` (SqonNode a union with zod v4's `z.infer` of a looseObject): tsgo reports TS2345 on
  the whole argument, tttt elaborates into the array literal and reports TS2322 on the element `site` (the same line;
  2 missing, 3 extra in the project). A union target containing the array type: tsgo's elaborateError doesn't descend
  into the array literal here. Repro test/cases/npm__overture-stack_sqon_TS2345.ts (13 files, zod cut to ~40 lines).
  Root cause: findMostOverlappyType's key overlap. tttt counted a tuple's "0" for the array member through its numeric
  index signature; keyof's intersection doesn't (numeric index key: number), so the object member with a string index
  signature is the best match, its [0] is unknown and nothing is elaborated (ttttBestMatchNumericIndexKeys). Fixed:
  index_key_includes_name in relate.c. The repro's errors component passes; its types component still differs on
  an import() specifier inside zod (`../core/core.cjs` where tsgo prints `../core/core.d.cts`), a separate [text]
  bug. [fixed 1096fcc71]
- Seed 20261023, alex-korneyko/ngx-summernote-mod: the TS2769 elaboration of `combineLatest(requests).subscribe(
  (remotePaths: string[]) => ...)` (rxjs 6.5.3, `const requests = []` an evolving array pushed
  `Observable<string | false>` in a for-of) prints `(value: unknown[]) => void` where tsgo prints
  `(value: (string | false)[]) => void`: tttt infers unknown for the last combineLatest overload's T (the evolving
  array's type or the overload's inference). Same (file, line, code). Line 350 of
  projects/ngx-summernote/src/lib/ngx-summernote.directive.ts at commit of the setup record; a cut-down
  evolving-array probe didn't reproduce; the clone is deleted. [text]
- Seed 20261023, axiona/uri: TS2416 for a class `implements CallbackType<ValueType, Type, ...>` (an alias of an
  intersection, node_modules/@axiona/validator/validatable/callback.d.ts(42)): tsgo names the base type expanded
  ('Readonly<Message<MessageType> & Value<Type | ValueType> & ...> & Validation<...> & Argument<...>', truncated),
  tttt by the alias ('CallbackType<ValueType, Type, MessageType, Arguments, Boolean>'). The clone is deleted. [text]
- **npm__uzm_katex-woff2_TS2577** (seed 20261024, @uzm/katex-woff2@0.1.0, KaTeX/KaTeX src/strict.ts:10): `type
  StrictFunction = (...) => Exclude<Strict, StrictFunction>` with StrictFunction a member of Strict. The distributed
  Exclude compares StrictFunction with itself, and tttt's callable_definitely_related shortcut asked for its return type
  (the Exclude being resolved): TS2577. tsc's isTypeAssignableTo answers identity first. Fixed by taking the true
  branch for identical check and extends types (ty_conditional_resolve). [flip] [fixed 389887243]
- **npm_react-smooth-scroll-hook_TS7016** (seed 20261027, react-smooth-scroll-hook@1.3.4, ron0115/react-smooth-scroll-hook
  tsconfig.bench.json): `paths: { "*": ["./src/*", "./node_modules/*"] }` under bundler resolution. tsgo resolves
  `react` through the paths candidate ./node_modules/react to the untyped index.js (TypeScript and JavaScript extensions
  tried together at each location for bundler/node16/nodenext) and reports TS7016, then TS2503 for `React.` namespaces;
  tttt goes on to @types/react (as if TypeScript extensions were tried everywhere before JavaScript, node10's two
  passes). Missing 3 TS7016 + 1 TS2503 there; not a flip (both have errors). Guess: the module resolver's extension
  passes for paths mappings (src/ module resolution). The clone is deleted. Fixed in resolve_uncached
  (src/module_resolution.c): a `paths` mapping is tried with every extension at once before the two-pass package
  lookup, and a result found through paths has a package id only when found as a file (tsgo's paths loader is
  nodeLoadModuleByRelativeName), so TS7016 has no `npm i --save-dev @types/...` chain there. [fixed 2026-10-23]
- **npm__veto-party_baum__package_manager__npm_TS2339** (seed 20261024, @veto-party/baum__package_manager__npm@0.2.0-pre-54,
  veto-party/baum; tsgo 5, tttt 106). `type IConfigMapStructure = typeof VariableFeature.makeInstance extends () =>
  IFeature<any, any, infer Structure> ? (Structure extends Record<infer Key, infer Value> ? Map<Key, Value> : never) :
  never`, with VariableFeature a class over json-schema-to-ts's FromSchema of a `patternProperties` schema. tsgo
  infers Structure as the whole FromSchema result `{ [x: string]: { [x: string]: {} } }`; tttt as `{ [x: string]: {}
  }` (IFeature's From appears in `verifyObject(): element is ToDefinitionStructure<Path, From>` and in
  `ingestObject(): IngestResult<From>[]`: likely a candidate from the predicate through ToDefinitionStructure<any,
  ...> chosen or combined differently). FromSchema itself agrees on both sides; a self-contained mimic of the two
  methods infers `{ [x: string]: {} }` in both, so the difference needs the real chain (597-line repro, reduce.py plus
  hand fixes for the names the cut left). Every one of the 101 extra diagnostics looks downstream of this (property
  reads on `{} & { source: string }`, `unknown` entries): one root cause until a fix shows otherwise. [open]
- **npm__markw65_prettier-plugin-monkeyc_TS7034** (seed 20261031, @markw65/prettier-plugin-monkeyc@1.0.69,
  markw65/prettier-plugin-monkeyc; tsgo 13, tttt 15). src/printer.ts:74 `let rest, ...; ({ print: x, ..., ...rest } =
  estree)`: an auto-typed `let` (no initializer, no annotation) whose only assignment is an object rest element of a
  destructuring assignment. tsgo's control flow takes the assigned type from the rest element (getAssignedType of a
  rest property), tttt reports TS7034 at the declaration and TS7005 at the use. The array rest form `[first,
  ...others] = [1, 2, 3]` works. Guess: tttt's flow assignment for object-pattern targets skips the spread/rest
  element. tsc's getAssignedType has no case for a spread assignment: the rest gets the error type, so the auto-typed
  `let` is any from there on; tttt's pattern_path_type returned nothing for an object rest, which left the auto type
  (unassigned: TS7034). Fixed in pattern_path_type (src/ty/flow.c). [fixed 2026-10-23]
- Seed 20261029, vobsjs/vobs (@vobs/theme): `x?. (args)` (whitespace between `?.` and `(`) was TS1003 Identifier
  expected, and the syntax error hid the program's other diagnostics. Repro test/cases/npm__vobs_theme_TS1003.ts.
  [fixed daf488efd]
- Seed 20261029, vobsjs/vobs (@vobs/theme), behind the parser bug: `JSX.IntrinsicElements` an intersection of mapped
  types over HTMLElementTagNameMap and SVGElementTagNameMap had no tags (569 spurious TS2339 'div' does not exist).
  Repro test/cases/npm__vobs_theme_TS2339.tsx. [fixed ef9b44945]
- Seed 20261029, ionic-team/stencil-component-starter: spurious TS2320 on @vitest/expect's `interface Assertion<T>`
  (13 of them; tsgo is clean). @vitest/expect declares Assertion and `export type { Assertion }`, vitest augments it
  (`declare module "@vitest/expect"`) and re-exports it (`export { Assertion } from '@vitest/expect'`), and
  @stencil/vitest augments it through the re-export (`declare module 'vitest' { interface Assertion extends
  CustomMatchers }`). tsgo's mergeSymbol merges each augmentation into a clone (cloneSymbol of the resolved target, a
  clone of a clone the second time), and getMergedSymbol of the original declaration is only the first clone, so the
  interface @vitest/expect declares never has CustomMatchers as a base: no conflict with JestAssertion's
  jest.Matchers members. tttt's aug_merge_symbol/aug_merge_fixup (scope.c) put the second augmentation's base into
  what the declaration means. Symbols differ too (tsgo's import from 'outer' has 2 declarations). Repro
  test/cases/npm_My-first-stencil-project-001_TS2320.ts (inner/outer/aug, 30 lines). Also seed 20261028's
  0115-ionic-team-stencil-component-starter (tsgo 0, tttt 13 TS2320, the same augmentation). (claimed: corB) [flip] [open]
  test/cases/npm_My-first-stencil-project-001_TS2320.ts (inner/outer/aug, 30 lines). Errors fixed: the
  declaration's heritage is checked as the first clone (aug_one_level, hacks.md); the repro's SYM/TYP still differ (which
  clone a name means: tsc's getMergedSymbol applied once, twice or more depending on the path). (claimed: corB) [flip]
  [fixed cf4ba416a]
- Seed 20261029, nirholas/free-crypto-news: spurious TS2345 on unified's `.use(rehypeHighlight.default, { detect:
  true, ignoreMissing: true })` (`use<P extends unknown[]>(plugin: Plugin<P, Input, Output>, ...parameters: P |
  [boolean])`): tsgo's inference takes P from the arguments (no excess property check against Options), tttt the
  plugin's parameters `[options?: Readonly<Options> | null | undefined]`. Cut-down probes without unified's Plugin
  conditional return type and CallableInstance base didn't reproduce. Repro test/cases/npm__nirholas_crypto-news_TS2345.ts
  (11 files from reduce.py). [open]
- Seed 20261028, tamagui/tamagui (@tamagui/codemod-flat-values@3.1.3, code/core/codemod-flat-values): spurious
  TS2339 "Property 'replaceWithText' does not exist on type 'never'" and TS2349 at src/transition.ts:302 (tsgo 0
  errors, tttt 2). `let node: A | B | null = null`, assigned in a loop after a type-predicate `continue`
  (`if (!isJsxAttribute(attribute)) continue; node = attribute`), then read through an optional chain (`node?.m()`),
  then `if (!node) return`, then `() => node!.replaceWithText(..)` in a closure past the last assignment. Without the
  optional-chain read it matches. Guess: the closure's narrowing past the last assignment (tsc 5.4's
  isPastLastAssignment) goes through the optional chain's flow condition and narrows to never. Repro
  test/cases/npm__tamagui_codemod-flat-values_TS2339.ts (13 lines). (claimed: cor10) Root cause: `node!` there is an
  incomplete never in tsgo too (the K2 query, initial type = declared because of the `!`, reaches its loop label while
  that loop is in progress through the optional chain's branches): the silent never. tttt's flow made it silent as well,
  but the walk's property access check (src/expr.c, EXPR_PROPERTY) and resolve_call_inner (src/ty/call.c) reported on
  it; tsc's checkPropertyAccessExpressionOrQualifiedName and resolveCallExpression return early for silentNeverType.
  Errors fixed (the flip: tsgo 0, tttt 0 now) [flip] [fixed 2026-10-23]; left: the repro's types baseline, where tsgo's
  type writer asks again after the loop caches are complete and prints `node!` as JsxAttribute while tttt prints the
  memoized never (a harness-only difference: tttt memoizes the first, incomplete, evaluation). [text]

## Real-world flips from other sources
- **rw_foldkit_TS6133** (source sweep, foldkit packages/foldkit/tsconfig.build.json and tsconfig.docs.json; tsgo 0,
  tttt 1): `import { Record } from 'effect'` (a namespace, `export * as Record`) used only as `Readonly<Record<K, V>>`
  in types. tsgo's getTypeFromTypeAliasReference, for a generic alias reference that isn't another alias's body, also
  resolves the name with SymbolFlags.Alias (the import alias it would print the instantiation by), which marks the
  import referenced; tttt's name resolution found only the global Record and reported TS6133. Fixed: the reference
  remembers such an import alias (IdentifierTypeRare.args_alias) and lowering marks it under tsc's condition
  (src/ty/lower.c). (claimed: cor10) [flip] [fixed 2026-10-23]
