# Plan 22: JSX

## Why
JSX is part of the TypeScript language (every React code base), and until 2026-09-25 the suite didn't even collect
`.tsx` tests. Now it does: **377 `.tsx` test configurations (328 files), 2 pass** (errors 2, symbols 18, types 7).
Another **80 configurations of `.ts` test files** contain JSX through `@filename: x.tsx` or `@jsx` (15 pass).
`./failstats.py --jsx` measures that subset: **457 tests, 17 pass, 419 with a spurious TS1xxx**, ~3.4% of the suite,
all blocked on the same missing front end.

What tttt does today (`FAST=1 ./filetest.sh` on `<div className="a">hi</div>`): parses `<div` as a type assertion,
then a cascade: TS2304 on every tag and attribute name, TS1005/TS1109/TS1128/TS1110 from the parser, TS2362/TS2365
from `<`/`>` read as operators. In the `.tsx` tests: **371 of 377 have a spurious TS1xxx** (so nothing is typechecked
and types/symbols fail too); spurious TS2304 in 333, TS1109 in 340, TS1005 in 310, TS2362 in 191, TS2749 in 141.

What tsc expects in the `.tsx` tests' errors baselines (occurrences): TS2322 478, TS2769 309 (no overload matches:
components resolve like calls), TS1005 130, TS2786 123 (not a valid JSX component), TS1109 100, TS1003 82, TS2741 74,
TS2304 72, TS2874/TS2875 50 each (factory / `jsx-runtime` not in scope), TS2339 50, TS17008 38, TS2558 28, TS2698 24,
TS1381/TS1382 24 each, TS2607 22, TS17021 22, TS7026 20, TS2792 20, TS2609 20, TS2604 16, TS2783/TS2746 14, TS2657 14.

What the test files use (of the 328 `.tsx` files): `@jsx` 326 (preserve 190, react 136, react-jsx/jsxImportSource 24),
their own `declare namespace JSX` 127, `@libFiles` (tests/lib `react.d.ts`, `lib.d.ts`) 92, `/// <reference
path="/.lib/react16.d.ts">` (or react18) 61, fragments `<>` 28, `ElementChildrenAttribute` 15, `@jsx`/`@jsxFactory`
pragmas or options 16, `LibraryManagedAttributes` 8.

## Architecture decisions
- **Parse JSX in tttt's hand-written parser, no tokenizer mode needed.** tttt parses straight from `char*`, so JSX text
  (where `//` is not a comment and `'` is not a string) is just a different hop loop. The language variant is a flag
  on `SourceFile` (tsc's `LanguageVariant.JSX`: `.tsx`, `.jsx`, and `.js` files); the parser carries it in
  `ParserShared`. In a JSX file `<T>expr` is never a type assertion, and a generic arrow needs `<T,>` or
  `<T extends X>` (tsc's parseJsxElementOrSelfClosingElementOrFragment vs tryParseParenthesizedArrowFunction).
- **AST**: one `EXPR_JSX_ELEMENT` (tag name expression: identifier, `this`, property access, or a namespaced
  `a:b` name; optional type arguments; attributes; children; closing tag span; self-closing flag) and
  `EXPR_JSX_FRAGMENT` (children only), plus child/attribute structs: `JsxAttribute` (name: identifier incl. hyphens,
  or namespaced; initializer: none / string literal / `{expr}` / element), `JsxSpreadAttribute`, children: text (with
  a "whitespace only" flag: tsc drops those), `{expr}` (possibly empty or `{...expr}`), element, fragment. Give the
  attributes list its own `Expr` (say `EXPR_JSX_ATTRIBUTES`) so the call machinery can point an `Arg` at it (below).
- **The JSX namespace and the element type are symbol lookups**, not special types: tsc's getJsxNamespaceAt (the
  factory's namespace from `@jsx`/`jsxFactory`/`@jsxImportSource`: `React.JSX` first, then the global `JSX`), then
  `JSX.Element`, `JSX.IntrinsicElements`, `JSX.ElementClass`, `JSX.ElementType`, `JSX.ElementAttributesProperty`,
  `JSX.ElementChildrenAttribute`, `JSX.IntrinsicAttributes`, `JSX.IntrinsicClassAttributes`,
  `JSX.LibraryManagedAttributes`. Cache the namespace per file (per factory entity), not per element.
- **Components resolve through `ty/call.c`, not a parallel implementation.** tsc's resolveJsxOpeningLikeElement is
  resolveCall with one argument (the attributes) and the component's call *or* construct signatures (SFC vs class
  component, getUninstantiatedJsxSignaturesOfType). Generalize `CallInfo` from `CallExpr const*` to a call-like (a
  small tagged struct or a callback for "collect args" + "callee span"), and let `collect_args` produce the single
  `Arg{ .expr = <the EXPR_JSX_ATTRIBUTES> }`. Then two-pass inference for context-sensitive attributes
  (`onClick={e => ...}`), overload resolution (TS2769), generic components, and explicit type arguments
  (`<Comp<string> />`) come for free. Resist writing a JSX-only overload loop; `hacks.md` would be the right place
  for it if you truly must.
- **The attributes type is an object literal.** tsc's createJsxAttributesTypeFromAttributesProperty: attributes in
  order, spreads through the same spread machinery as object literal spreads, a `children` property (named by
  `ElementChildrenAttribute`) synthesized from the children (one child: its type; several: an array/tuple type), fresh
  for the excess-property check, except that hyphenated names (`data-x`) never count as excess (isIgnoredJsxProperty
  in relate). Reuse `ty_object_literal`/the object literal builder in `ty/expr.c`. Contextual typing is push-down in
  tttt (`ty/expr.c` header comment), which is exactly how the props type reaches attribute initializers.
- **Intrinsic vs value tags**: tsc's isJsxIntrinsicTagName (identifier starting lowercase, or containing `-`, or a
  namespaced name). Intrinsic tags look up `JSX.IntrinsicElements[name]` (TS2339 on `JSX.IntrinsicElements`, or
  TS7026 when there's no `IntrinsicElements` and `noImplicitAny`); value tags are name references (flow, symbols,
  TS2304, unused-import marking).
- **The factory is a reference**: `React` in `import * as React from "react"` is used by every JSX element under
  `jsx: react` (tsc's markJsxAliasReferenced). Without that, TS6133 fires on every React import: do it in stage 2,
  not as polish. Under `react-jsx`, the implicit import of `<jsxImportSource>/jsx-runtime` is a module reference (TS2875
  when it doesn't resolve, TS2792 hints).

## Progress
- **Stages 0 and 1 done (2026-09-25).** `/.lib` + `@libFiles` in the harness, the parser (see documentation.md
  "JSX"), TS17004. JSX subset: errors 17 -> 243, symbols 51 -> 125, types 37 -> 39 of 457; spurious TS1xxx down to
  the error-recovery long tail listed in triage.md ("JSX parse error long tail"). Everything JSX is `any` so far; the
  types/symbols traversal only visits value tag names, attribute values and child expressions (TODO in
  `expr_compile_test_info_inner`). Real React typings (react16.d.ts) load cleanly, after a program fix: a script's own
  `declare module "react"` is what `"react"` means inside it (tsc's resolveToOwnAmbientModule).
- Stage 2 notes: tag type arguments are looked up and `type_typecheck`ed already (so TS2749/TS2304 on them work),
  TS2558 on them is stage 3. The top missing codes in the subset are the checker's: TS2322 (133 in 57 tests), TS2769,
  TS2874 (factory not in scope), TS2741, TS2558, TS2786, TS2875; spurious TS7006 (50 in 17 tests: callbacks in
  attributes without a contextual type) and TS6133 (unused React import: markJsxAliasReferenced).

## Stages
Each stage ends with `./fasttest.sh --summary` clean of losses, `./fasttest.sh -s`, and a commit. Measure the JSX
subset with `./failstats.py --jsx` (numbers in `measurements.md`).

### Stage 0: harness
- `@libFiles: a.d.ts,b.d.ts`: tsc's harness (harnessIO.ts compileFiles) adds `/.lib/<name>` as *root* program files;
  `/.lib` is `tests/lib` (vfsUtil.ts `testLibFolder`). So do `/// <reference path="/.lib/react16.d.ts" />`. Mount
  `thirdparty/tsc/tests/lib` at `/.lib` in the harness's mock fs. They are not input files: they don't get baseline
  sections, but their declarations show in symbols (`Decl(react16.d.ts, 216, 49)`, real line numbers, unlike the
  default lib's `--`). Note `lib.d.ts` here is tests/lib's own 17k-line ES5 lib, combined with `@noLib` mostly;
  check how the harness's default lib interacts when both are present.
- (Done 2026-09-25: the harness collects `.tsx` files, `./failstats.py --jsx`.)
- `SourceFile` language variant flag; TS17004 (`Cannot use JSX unless the '--jsx' flag is provided`) belongs to
  stage 1 but the option plumbing (`jsx`, `jsxFactory`, `jsxFragmentFactory`, `jsxImportSource`, already parsed in
  `compiler_options.c`) should be checked here.

### Stage 1: parse
Elements, self-closing elements, fragments, attributes (string, expression, element-valued, spread, hyphenated,
namespaced), children (text, expressions, empty `{}` and comment-only `{/* */}`, nested), entities are text (only
checked for display), type arguments on tags, the `<T,>` arrow rule, JSX in every expression position (including
`return (<div/>)`, conditional operands, arrays, arguments). Errors tsc gives here: TS17002 (expected corresponding
closing tag), TS17008 (no corresponding closing tag), TS17014/TS17015 (fragment closing), TS1381/TS1382 (`}` / `>` in
text), TS2657 (JSX expressions must have one parent element), TS17021 (unicode escape in a JSX name), TS1003. Match tsc's recovery on the
`jsxParsingError*`, `jsxInvalidEsprimaTestSuite`, `jsxEsprimaFbTestSuite`, `tsxErrorRecovery*` tests (they are the
parser's oracle; `./failstats.py --syntax` restricted to the JSX subset should end empty). Type everything as `any`
for now if that helps get the syntax in first, but the resolve walk (`scope.c`) must already visit tag names and
attribute expressions as references so the binder is right from the start.
**Done when**: no JSX test has a spurious TS1xxx; the parser-only tests pass errors.

### Stage 2: the JSX namespace, intrinsic elements, element type
getJsxNamespaceAt (global `JSX`, `React.JSX` through the factory, pragmas), `JSX.Element` as the type of every element
and fragment (TS2304-like errors when missing: check tsc's messages), intrinsic element lookup and attribute checking
against `IntrinsicElements[tag]` & `IntrinsicAttributes` (TS2322/TS2741 with tsc's elaboration into the attribute:
elaborateJsxComponents), children checked as expressions, `ElementChildrenAttribute`, factory references
(TS2874, markJsxAliasReferenced, TS6133), TS17004. Baseline traversal: types (`><test1 x={0} /> : JSX.Element`,
`>test1 : any` for intrinsic tags, `>x : number` for attribute names: that's the attribute's type) and symbols
(`>test1 : Symbol(JSX.IntrinsicElements.test1, ...)`, attributes as `Symbol(x, Decl(file.tsx, 14, 6))`: the
attribute declares a symbol of the attributes object literal). The `tsxAttributeResolution*`, `tsxElementResolution*`,
`tsxIntrinsicAttributeErrors`, `jsxChildrenGenericContextualTypes` style tests (own `declare namespace JSX`, no
components) are the target.

### Stage 3: components
Value tags: SFCs (call signatures) and class components (construct signatures; props from the first construct
parameter, or `ElementAttributesProperty` on the instance type: getJsxPropsTypeFromClassType), `IntrinsicClassAttributes`,
`LibraryManagedAttributes`, resolution through `ty/call.c` (see Architecture), TS2769/TS2786/TS2604/TS2607/TS2608/
TS2609 (checkJsxReturnAssignableToAppropriateBound, `ElementClass`, `ElementType`), generic components and inference
(inferJsxTypeArguments), explicit type arguments (TS2558), spread attributes (TS2698, TS2783 "specified more than once"),
children count/type errors (TS2745/TS2746/TS2747), contextual types for callbacks in attributes.
**Validation**: the react16/react18 tests (`/.lib/react16.d.ts` is 2.7k lines of real React typings: `callsOnComplexSignatures`,
`checkJsxChildrenProperty*`, `tsxSfcReturn*`, `tsxStatelessFunctionComponent*`, `tsxGenericAttributesType*`,
`tsxReactComponentWithDefaultTypeParameter*`, `tsxUnionElementType*`).

### Stage 4: modes and the rest
`react-jsx`/`react-jsxdev` (implicit `jsx-runtime` import resolved through module resolution, TS2875/TS2792),
`jsxImportSource` pragma/option, `jsxFactory`/`jsxFragmentFactory` options and `@jsx`/`@jsxFrag` pragmas (TS17016,
TS17017, TS18...: check), `preserve` vs `react` differences (the factory reference), `.jsx` files under `allowJs` (only
as far as JS support goes elsewhere: don't build JS semantics here). Then rank what's left with failstats on the JSX
subset and either finish or write the leftovers into triage.md.

### After the plan
Insights should decide whether a **React corpus** (a pinned, small, real `.tsx` project that's clean under tsc, wired
into `realworld.py`/`bench.sh` like type-fest/valibot) is the next oracle. The suite's JSX tests are narrow (mostly
hand-written `JSX` namespaces); real React typings exercise overloads, `LibraryManagedAttributes` with `defaultProps`,
and huge intrinsic element tables, which is also where JSX perf lives (tsgo is not fast on big React projects).

## Estimate
Stage 0: small (part of a session). Stage 1: one session. Stage 2: one session. Stage 3: one or two sessions (it's
where the call-like generalization lands). Stage 4: one session. Expected yield: most of ~450 tests' errors; types and
symbols follow if the baseline traversal is done in stage 2 rather than bolted on.

## Risks
- The call-like generalization of `ty/call.c` touches the hottest, most tuned code in the checker. Keep the plain
  call path's cost unchanged (bench after stage 3).
- tsc's JSX error recovery is peculiar (it re-scans tokens in JSX context); expect the parser stage to take longer on
  error tests than on valid syntax. Don't let a handful of esprima error tests eat the session: stage 1 is done when
  valid JSX parses and the common errors match.
- The two harness libs (`/.lib/lib.d.ts` + `@noLib`) may interact with the harness's lib caching
  (`baseline_test_cache_get_program_with_libs`): the lib program is shared between tests, `@libFiles` must not be.
