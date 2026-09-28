# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-27 (triage session, todo entry 10 of the second window). Everything that was here was fixed
(TS1338/TS1312, the ambient jump error, TS1156 spans, TS2374 after parse errors, `F.prototype = {}` in TS, the
`undefined` suggestion, TS2318 IterableIterator, TS18041) or went to `bugs/type_core.md` (variance annotations,
non-widening undefined, `infer X extends C` scope), `bugs/display.md` (object literal numeric-string keys),
`bugs/missing_checks.md` (TS2815), plan 19 (exactOptional's missing type and `in` narrowing, padded symbols), plan 26
"Leftovers collected by triage" (JSDoc attachment, printing through export=), plan 27 leftovers (infer lookahead).

- `shows_alias()` (ty_display.c) hides type aliases declared in a block/function body when printing for a place. tsc's
  isDeclarationVisible also makes non-exported aliases inside namespaces invisible in some cases; unverified.
- `export default;` gives TS1434 + "Cannot find name 'export'" + TS1005 "'export' expected" where tsc says only TS1109
  Expression expected at the `;` (the modifier loop takes `default` now for any following char; something later
  still rejects the statement). Found at plan 35 stage 1.
- `baseline_test_traversal.c` `port_item_add_alias`: the `import type`/`export type` declared-type rule checks the
  resolved symbol for class/interface/enum/alias only; tsc's getDeclaredTypeOfSymbol also takes type parameters and
  enum members. Unverified whether any test reaches that.
- **const type parameter inference with several candidates** (plain TS too): `function f4<const T>(x: [T, T]): T;
  f4([[1, "x"], [2, "y"]])` gives TS2322s where tsc infers `readonly [1, "x"] | readonly [2, "y"]`
  (jsdocTemplateTag6). Found at the plan 26 session.
- Rest element grammar errors are unimplemented: TS2566 (`{ ...rest: x }`, which tttt doesn't even parse: tsc parses
  a property name after `...`), TS2501, TS2462, TS1186; also `a?.\`x\`` (tsc TS1358, tttt TS1003) and a comma
  expression as a computed key (`[a, b]: 1`, tsc parses it and reports TS1171; tttt TS1136). All in
  plainJSGrammarErrors, whose parse errors hide its ~50 grammar errors.
- Override checks (TS4113-TS4123, `override` keyword and noImplicitOverride, JSDoc `@override`) don't exist at all
  (override_js2/4, jsdocOverrideTag1 and the TS tests of the feature: `./unimplemented_codes.sh`).
- JS `0other` bucket (plan 26 stage 4 sample, 78 tests): node16/nodenext package `exports`/`imports` and self-name
  resolution in allowJs tests (TS2209/TS2210/TS2303, 15 tests: nodeModulesAllowJs*, nodeAllowJsPackageSelfName);
  importNonExportedMember8-11 (TS2595-TS2598 + TS2497: importing an `export =` module with named/default imports);
  TS18042/TS18043 (types in JS import/export: jsDeclarationsInterfaces, elidedJSImport1,
  jsxCheckJsxNoTypeArgumentsAllowed); TS7029 (jsFileCompilationBindReachabilityErrors), TS2393 duplicate function
  implementation in JS; ~25 are types-only (`./displaydiff.py -f` on them).
- Tagged templates resolve as calls now (plan 35 session 2). Left: `new tag<T> \`...\`<U>(...)` parses wrong
  (taggedTemplatesWithTypeArguments1/2: tttt reports TS7009/TS2347/TS2693 where tsc has TS2345s), `super<T> \`\``,
  incomplete templates' types (taggedTemplatesWithIncompleteTemplateExpressions4/5), taggedTemplateChain, and the
  non-strict `[]` argument printing as `any[]` where tsc says `undefined[]` in TS2345 (taggedTemplateStringsWith
  OverloadResolution1: same for plain calls?).
- `interface void {}` / `type void = I` (interfacesWithPredefinedTypesAsNames, reservedNamesInAliases): tsc gives both
  TS2304 "Cannot find name 'interface'" and TS2427 at `void`, i.e. its statement-start lookahead refuses the declaration
  but something still declares it; tttt reports only TS2427. Not understood.
- TransportStream (a binary file: `G@` then bytes): tsc's TS1128 is at 1:4 with a span over the rest of the file,
  tttt's at 1:2; binary-file recovery (TS1490 is right). corrupted.ts passes.
- `string_to_double()` (string_helpers.c) is only used by value.c now and accumulates decimals digit by digit (not
  correctly rounded for long mantissas); the parser uses strtod on the separator-free digits.
- Plan 32 leftovers (the plan is done and deleted): TS2507 "Type 'X' is not a constructor function type" for an
  `extends` expression is missing everywhere (only `new` reports it), and so is its related info for a type parameter
  (TS2735 "Did you mean for 'T' to be constrained to type 'new (...args: any[]) => ...'?"); a class extending a base
  that constructs `any` should get tsc's anyBaseTypeIndexInfo (`[x: string]: any`) instead of the
  `ty_has_unresolved_base()` gate; tsc's display of `new Outer<string>().m` clones generic method type parameters
  (`Outer<string>.Outer.m<U>.In`). mixinAccessModifiers/mixinPrivateAndProtected are in `bugs/type_core.md`.
- Corpus session (todo entry 8): generic homomorphic mapped types now get index signatures from their constraint,
  eagerly instantiated: valibot +2.5% instructions. tsc resolves them lazily; a lazy IndexInfo value (or computing the
  mapped type's members only when asked for index infos) would take that back.
- Variance annotations (`in T`/`out T`) now set the variance (measure_variances()), but their checks aren't implemented: TS2636/TS2637 (tsc's checkVarianceAnnotations: the annotation must agree with the measured variance, only on interfaces/classes/object-ish aliases), TS1274 (modifier on a non-type-declaration parameter). Also tsc unions the modifiers of all declarations of a merged interface's type parameter (getTypeParameterModifiers); tttt reads the one GenericItem the type parameter points at.
- `ty_check_object_literal_pattern()` treats every computed name in a binding pattern as ObjectLiteralPatternWithComputedProperties
  only when its type isn't a literal/unique symbol (like tsc), but `lower.c`'s rest-type computation (the `named`
  check near `binding_pattern_type`) still skips all computed keys: `const { ['a']: x, ...rest } = o` may keep `a` in
  rest. Unverified.
