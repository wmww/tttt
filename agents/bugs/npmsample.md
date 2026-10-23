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
  isTypeAssignableTo(index, keyof object) path). Repro test/cases/npm_firelab_TS2538.ts (7 lines). [flip] [open]
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
