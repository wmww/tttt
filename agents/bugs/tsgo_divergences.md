# tsc 6 vs tsgo 7 divergences
Our oracle is tsc commit 4d4f005c (plan 48). Where tsgo 7.0.2 (the competitor, and the likely next oracle) disagrees
with it on our corpora, record it here: when tsgo becomes the oracle these are the places tttt must change side.
Each was seen on a corpus at the plan 48 stage 5 re-pin (2026-10-07).

- **Circularity (vscode)**: `class C { count = 0; constructor(r: () => number) {} } function id(n: number) { return
  n; } const c = new C(() => id(c.count));` is TS7022+TS7024 under tsc 6, clean under tsgo 7. The vscode overlay
  excludes agentFinderRestProvider.test.ts, codexProviderConfiguration.test.ts and extHostNotebookStatusCommands.test.ts
  for it. tttt matches tsgo on the reduced file but reported 3 of tsc's 5 pairs in vscode (agentFinderRestProvider:613,
  codexProviderConfiguration:148/156; not :138 nor extHostNotebookStatusCommands:37): its circularity detection there
  is neither oracle's.
- **vue** (4ab865a8): tsgo reports 10 tsc doesn't: TS2578+TS2769 x3 in dts-test/defineComponent.test-d.tsx (the
  overload error lands lines below the `@ts-expect-error`), one pair in setupHelpers.test-d.ts, TS2322 in
  functionalComponent.test-d.tsx:25, TS2345 in runtime-dom/src/directives/vModel.ts:446 (`DirectiveBinding<any,
  string>` vs `<any, "number">`).
- **effect** (v3, e730f59d): tsgo reports TS2345 at src/Schema.ts:3770 (`typeSchema(self)`: `SchemaClass<any, any,
  never>` not assignable to `S extends Schema.Any` inside `dual(2, <S, FD>(...) => transformOrFail(self,
  typeSchema(self), ...))`) and TS2769 x2 in packages/vitest/src/internal/internal.ts:132,153. Effect 4 (main) is the
  reverse: tsgo-clean, 2 TS2345 under tsc 6 in packages/effect/src (SchemaAST.ts:3109, SchemaCompiler/runtime.ts:73,
  an Exit-vs-Effect callback union under exactOptionalPropertyTypes); a second, Effect 4 corpus once tsgo is the
  oracle (tsc 6 took 43 s on its src vs tsgo's 8.6 s: a heavy perf corpus).
- **webpack, drizzle**: tsgo isn't clean either (measurements.md's bench section lists what it reports).
- **zod-root**: tsgo 7.0.2 runs away (18 GB RSS after 17 min single-threaded, then killed; multi-threaded OOM-killed
  too) where tsc 6 reports its 53. Not bisected: which package (packages/bench, scripts/ are new since the old pin)
  blows tsgo up might be a perf hazard for tttt too.
