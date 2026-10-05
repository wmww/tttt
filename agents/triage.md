# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-05 (triage session, todo entry 10 of the eighth window; suite 12731 -> 12753, 0 lost, every
corpus unchanged bar one union order inside webpack's existing cleverMerge.js false positive; scenarios 208 -> 215,
only out-of-scope TS6377 left). Five Opus sub-agents: (A) the 1-vs-4-checker nondeterminism was import attribute
values skipping name resolution (all shared expression cache slot 0; slot 0 now refused) plus a stack over-read in
quoted member names: suite dumps are byte-identical at 1 and 4 checkers; Pick/Partial excess properties; bare-return
return types. (B) CLI gating as tsc's emitFilesAndReportErrors, absolute module paths in CLI messages, tsconfig syntax
errors and missing `extends`. (C) getTargetofModuleDefault/TS2613, TS1479 for the JSX runtime import, the
isolatedModules/verbatimModuleSyntax alias checks (+13). (D) the checkReturnStatement port landed after fixing its
two order bugs: flow typed assigned values without their contextual type, and function expression bodies are now
deferred to the end of the file like tsc's checkNodeDeferred. (E) plan 43 (AST memory). Leftovers are in hacks.md,
missing_checks.md "Known missing checks", type_core.md "Circularity" (getter_cycle.ts).

- (triage 2026-10-05, D) tttt now defers function expression bodies to the end of the file (`Checker.defer_functions`,
  tsc's checkNodeDeferred), but tsc also defers class expressions, JSX elements and type assertion checks; those
  still run in place. Order-dependent answers there would look like the zod TS2344s did: check before porting more.
- `has_inferable_index()` (relate.c) grants every anonymous object type an implicit index signature; tsc only types with a symbol of the right kind (isObjectTypeWithInferableIndex). Found by the sweep (plan 44, rrdtool: D/literalKeyof.ts); check how much wider tttt's rule is and whether it hides errors elsewhere.
- (effect corpus) `infer_rest_variadic`/`check_rest_arg` keep an argument's type from whichever candidate typed it
  first (`if (arg->type) continue`), though its contextual type is now the candidate's rest element: with overloads
  whose rest types differ, a later candidate sees types checked against an earlier one. tsc re-checks per candidate
  (checkExpressionWithContextualType caches nothing across candidates for non-sensitive arguments). Tried 2026-10-05
  (clearing the non-spread rest arguments' types at the top of infer_rest_variadic): suite, corpora (effect
  byte-identical) and instructions unchanged, and no probe found that observes it (an earlier candidate regularizing a
  literal, `q<T extends ["a"]>(n: number, ...a: T)` before `q<T extends string[]>(s: string, ...a: T)`, already
  passes), so not committed; it was suspected of effect's 1-vs-4-checker differences, which are gone.

- **A project reached through a symlink is checked differently and ~45x slower.** `tttt -p thirdparty/corpora/webpack` run from a worktree (where `thirdparty/corpora` is a symlink to the main checkout) takes 45 s instead of 1 s, makes 525k type mappers (93% repeats) and reports different diagnostics (TS2859 "Excessive complexity" in webpack's examples) than the same project by its real path. Probably files reached by both the symlinked and the real path are loaded twice (tsc resolves symlinks by default, preserveSymlinks false), so every type exists twice and nothing is identical. Repro: `cd /tmp/<worktree> && ./release/tttt -q --keep-going -p thirdparty/corpora/webpack | md5sum` vs the same with `$(realpath thirdparty/corpora/webpack)`. Measurement scripts that pass relative paths from a worktree (arenasites.sh copies, `--time` probes) measure this pathology instead of the real project; instrcount.sh and threadcheck.sh use realpath.

- `has_inferable_index()` (relate.c) grants every anonymous object type an implicit index signature; tsc only types with a symbol of the right kind (isObjectTypeWithInferableIndex). Found by the sweep (plan 44, rrdtool: D/literalKeyof.ts); check how much wider tttt's rule is and whether it hides errors elsewhere.

- **`this` in a method of an object literal argument whose contextual type is a union with an intersection member is
  the literal, not the contextual signature's `this: any`** (fan-out fo6-a, from the DefinitelyTyped sweep: ember__object
  test/proxy.ts, spurious TS2339 + TS7006 + TS7023). Repro: `interface G<T> { get(this: any, key: string): T }
  interface S<T> { set(this: any, key: string, value: T): T } type Obj<A, B> = G<A> | S<B> | (G<A> & S<B>);
  declare function computed<A, B = A>(...args: [...keys: string[], cb: ((this: any, key: string) => A) | Obj<A, B>]): A;
  class P { f = "x"; full = computed("f", { get() { return `${this.f}`; }, set(key, value: string) { return value; } }); }`
  (tsc: no error).
- (fan-out errors tail, 2026-10-05) Agent a's `cf5f800d` "tsc's mapped-type unwrapping in the deep-nesting check" was
  not merged: it fixes 2 of 3 errors of deeplyNestedMappedTypes but no whole test, and costs +0.17% instructions on
  drizzle (+0.05% type-fest) because comparisons that stopped early now go on, as tsc's do. Recreate it from that
  description (isDeeplyNestedType's mapped-type identity) when the `Id2<Id2<...>>` case is understood too.
- (same session) tripleSlashReferenceAbsoluteWindowsPath: d's fix was superseded by c's triple-slash change, which turns
  `C:\a\b\c.ts` into `C:/a/b/c.ts`; tttt's path joining doesn't treat that as absolute (and the test's file keeps the
  backslash name). Not lost against the session start, but it passed mid-session.
- (same session) valgrind shows a "possibly lost" block from the thread-local `program_modules_exporting` /
  `program_containers` caches in `symbol_chain.c` (never freed), eg importElisionConstEnumMerge1. Pre-existing.
- (2026-10-05 fan-out) The symbols harness's expression walk is near the stack limit on binderBinaryExpressionStress:
  inlining a bigger `declaration_get_spans()` segfaulted the release suite (now `noinline`). Make the walk iterative
  over binary-expression left spines, as the checker presumably does.
- `./scenarios.py`: 3 tsbuild/projectReferenceWithRootDirInParent/reports-error-for-same-tsbuildinfo-file* fail (missing
  TS6377 at src/main/tsconfig.json:6); already failing at dfde5fbc.
- (2026-10-05 fan-out) react +0.8% instructions over the session from four small commits (a29ce0b2, 84b12f2b,
  8619aacb, 5b06210a; measurements.md). Check whether the first two are real work on a hot path.
- deeplyNestedMappedTypes passes errors and symbols but not types: tsc prints the interface property `static:
  PropertiesReduce<T, this['params']>` as its expansion through the inner aliases (`Evaluate<Readonly<Partial<Pick<{ [K
  in keyof T]: Static<...> }, ...>>> & ...>`), tttt keeps the outer alias name. An alias display question (when tsc
  drops an alias whose body is another alias applied to a generic mapped type), plan 35 territory.
