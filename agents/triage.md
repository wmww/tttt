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
- **Segfault on zod's root tsconfig** (`./release/tttt -q --keep-going -p thirdparty/corpora/zod`, not the pinned
  `packages/zod/tsconfig.test.json`, which is fine): crashes in `reverse_mapped_type_uncached` <- `infer_to_mapped`
  <- `infer_from_objects` <- ~10 nested `infer_from` <- `infer_type_arguments`, while checking a variable statement.
  Present at 0746971 (not a stack overflow: `ulimit -s unlimited` still crashes). Found by plan 36's planning session;
  `gdb -batch -ex run -ex bt --args ./release/tttt ...` with `TTTT_NO_REEXEC=1` reproduces in ~2 s. Worth pinning
  the root config as a second zod corpus once it runs.
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
- **Tagged templates aren't checked as calls** (`EXPR_TEMPLATE` with a tag in ty/expr.c: only a plain function's
  return type is used). ~25 failing tests: TS2349 on a non-callable tag (templateStringInPropertyName*,
  taggedTemplateWithConstructableTag01/02), TS2345 argument checks (taggedTemplateStringsWithIncompatibleTypedTags*,
  taggedTemplatesWithIncompleteTemplateExpressions3/6), overloads TS2769/TS2554 (taggedTemplateStringsWithOverload
  Resolution*), generic inference (taggedTemplateStringsTypeArgumentInference*), explicit type arguments
  (taggedTemplatesWithTypeArguments1/2). tsc resolves them with resolveCall like a call whose first argument is the
  TemplateStringsArray. Worth a plan: it's a checker feature, not a parser one. Found at plan 27 stage 3.
- `interface void {}` / `type void = I` (interfacesWithPredefinedTypesAsNames, reservedNamesInAliases): tsc gives both
  TS2304 "Cannot find name 'interface'" and TS2427 at `void`, i.e. its statement-start lookahead refuses the declaration
  but something still declares it; tttt reports only TS2427. Not understood.
- TransportStream (a binary file: `G@` then bytes): tsc's TS1128 is at 1:4 with a span over the rest of the file,
  tttt's at 1:2; binary-file recovery (TS1490 is right). corrupted.ts passes.
- `string_to_double()` (string_helpers.c) is only used by value.c now and accumulates decimals digit by digit (not
  correctly rounded for long mantissas); the parser uses strtod on the separator-free digits.
