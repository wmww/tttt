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
  into the array literal here. Repro test/cases/npm__overture-stack_sqon_TS2345.ts (13 files, zod cut to ~40 lines; a zod-free cut didn't
  reproduce yet). [open]
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
  passes for paths mappings (src/ module resolution). The clone is deleted. [open]
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
  element. (claimed: cor10) [open]
