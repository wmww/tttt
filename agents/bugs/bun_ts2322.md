# bun's differential cases: what is left of the TS2322 buckets
These are the cases `scripts/buncases.py --last -k=-TS2322` and `-k=+TS2322` still list (2026-10-23, after corA's
21 fixes). They are grouped by root cause, as far as reading the case and tsgo's code showed. Missing means tsc reports
a TS2322 that tttt doesn't (`-TS2322`); spurious means the reverse (`+TS2322`). Many cases end in
`const sentinel: number = ""`, so a wrong parse or a program error shows up as a missing or spurious TS2322 at the
last line. The real cause is then elsewhere, and the entry says where. Show a case with `scripts/buncases.py --last
--show ID`.

## Parser and lexer: tttt's syntax errors hide the semantic TS2322s (missing)
- 02-C10, a comma expression in a type member's computed name (`get [f(), "c"](): number`): tsc reports TS1171 and
  goes on, tttt fails the member. TS2322 missing (sentinel). [open]
- 02-C17 and 02-C21b, `typeof f.<number>` and `import("./b").BFoo.<"a">`: tsc parses `.<T>` as type arguments
  silently, tttt says TS1003. TS2322 missing (sentinel). [open]
- 28-P4b `@dec in class C {}` (TS1274 in tsc), 80-R08 `@dec public class {}` and `(@dec export default class {})`
  in expressions (TS1184), 84-R08c `@dec import("./m")` (TS1206 and TS1141), 56-L14b `@await` decorators on
  function-type parameters (TS1206): tsc's modifier and decorator recovery reports one grammar error, tttt fails the
  parse. TS2322 missing (sentinel). [open]
- 32-C24a, `import("m" | "n")` in a type: tsc says TS1141 "String literal expected" at the argument and recovers,
  tttt expects `)`. TS2322 missing (sentinel). [open]
- 43-C11 `` `\8` `` in a template literal type (no error in tsc), 43-C2e `declare global {}` (tsc accepts the
  escaped contextual keyword), 43-X6b and 43-X6e (`yield` then a newline then `[]` or `/a/.nope`: tsc ends the yield
  at the line break): lexer differences. TS2322 missing (sentinel). [open]
- 43-C5c, a JSX attribute string after a comment, `x=/**/"\""`: tsc scans a normal string literal there, tttt a JSX
  string. 43-C5b is the same cause the other way: tsc processes `"\x61"` to `"a"`, so tttt's TS2322 is spurious. [open]
- Private names where tsc parses an identifier and reports TS2304: 43-P8e `<#a />`, 43-X5f `var x: #a`, 43-C13d
  `<a #b />`, 80-R01a and 80-R01b (`#constructor` in import attributes and as JSX attribute names). TS2322 missing
  (sentinel). [open]
- `await` at a module's top level in a computed name, 56-L11 and 93-P01b (`{ [await]: x }`, `interface I { [await]: 1
  }`): tsc reports TS1262 and binds an identifier, tttt says TS1109. TS2322 missing (sentinel). [open]
- 61-L15b, the `default` modifier on type members and class members (`default class: 1`): TS1070 and TS1029 in tsc,
  a parse failure in tttt. TS2322 missing (sentinel). [open]
- 43-P10a (`infer U[]` in an extends clause: tsc says "'?' expected", the postfix doesn't bind to `infer U`), 43-P10b
  (`[infer U?]`), 30-P8a and 30-P8b (`enum await` and `namespace await` in an async function), 28-P5e (`export
  default:` in an interface), 93-P07c (`async a!() {}`), 39-C18c (`declare let` then `= 1` on the next line): tsc's
  parse errors stop the semantic pass, tttt's parse succeeds. TS2322 spurious (sentinel). [open]

## Program, config and build: the program differs, the sentinel TS2322 shows it
- 37-P3: `"files": [#a]` in tsconfig. tsc reports TS1328 and TS5024 but still checks a.ts; tttt loads nothing.
  TS2322 missing (sentinel). [open]
- 39-C17a and 39-C17b: `jsxFactory` given as `a/**/.b` or `"a"`. tsc parses it as an entity name (comments
  and escapes allowed), tttt reports TS5067. TS2322 missing (sentinel). [open]
- 37-C14 (`include: ["src/.ts"]`: TS18003 in tsc, no inputs), 39-C6a and 39-C6b (a `tsBuildInfoFile` or output name
  overwriting an input, TS5056 under `-b`), 40-C7c (libReplacement resolving to a JS file, TS6504), 86-R11b
  (libReplacement resolving to a `.ts` file under composite, TS6307): tsc stops at the program error, tttt checks.
  TS2322 spurious (sentinel). [open]
- 40-C5 and 40-C12, `/// <reference lib="" />`, `lib="esnext.full"` and `lib="lib.dom.d.ts"` (the file-name form):
  tsc resolves the file-name form and reports TS2726 for the others. tttt doesn't load lib.dom from the file-name
  form (TS2304 Document). TS2322 missing at 40-C12, spurious at 40-C5. [open]
- 40-P5, 58-L11a and 58-L11b: a root file named `__inferred type names__.ts`. That is the name of tsc's synthetic
  file for automatic type directives. tsc drops the real file (its import doesn't resolve, its globals don't exist)
  and keeps `types: ["foo"]` from adding foo's globals where the file name collides. tttt keeps the file. TS2322
  spurious and missing. [open]
- 86-R11c: libReplacement `@typescript/lib-dom` as an ESM package under nodenext. tttt reports TS2834 inside the
  replacement lib and loses its `declare global`, so `doc.a = ""` is missing its TS2322. [open]
- Project references under `-b` where tsc reads the referenced project's emitted d.ts and tttt reads more than it
  should: 38-C5a (a JSON re-export without resolveJsonModule, TS6054 and TS7042 in the d.ts), 38-C5b (a `.js`
  re-export under allowJs, TS7016 and TS2305), 38-P1a and 40-P2 (a `.tsx` source whose output was never built,
  TS6305), 40-C13a (noResolve in the referenced project). In all of them tsc's import is any, so tttt's TS2322 is
  spurious. [open]
- More project references under `-b`, the other way: 40-C13b (a `/// <reference path preserve="true">` in the
  referenced project: tsc's d.ts keeps the reference, tttt's emitted d.ts lacks Glob) and 40-C13c (noResolve in the
  referencing project with references: tsc still resolves the d.ts's own imports). TS2322 missing. [open]
- 40-P4b (`/// <reference path>` to a node_modules JS file with maxNodeModuleJsDepth) and 86-R08 (node_modules JS
  packages through nested `require`s with maxNodeModuleJsDepth 3): tsc loads and types the JS, tttt says TS7016 or
  any. TS2322 missing. [open]
- 36-C10b and 36-C11: module specifiers with backslashes (`"./inner\\"`, `".\\x"` with paths). tsc normalizes the
  separators and resolves them, tttt reports TS2307. TS2322 missing. [open]
- 36-C9d (spurious) and 58-L07a (missing): duplicate packages. In 36-C9d, pp under dep/node_modules has a
  package.json identical to the top-level one, with peerDependencies, and tsc dedupes the two by package id, which
  folds in the peers' versions (the duplicated `peer` key included); tttt doesn't, so the private member mismatches.
  In 58-L07a, a nested package.json for a subpath (pkg/sub with its own name) gives two different ids in tsc, while
  tttt treats them as one. [open]

## Checker
- 01-C3: a class expression's type parameters are in scope in its own decorators (`@dec((x: T) => x) class<T> {}`):
  tsc resolves T there, tttt says TS2304 and misses the later TS2322. [fixed d2b062e28]
- 05-C11a: an overload whose rest is a tuple of callbacks, `...r: [number, ...((x: number) => void)[][]]`. tsc
  contextually types the arrow's parameter from the failing overload, tttt reports TS7006. TS2322 missing. [open]
- 06-C15: `yield* [[1], ["a"]]` in a `Generator<number[]>`. tsc elaborates the array literal element by element
  against the yielded type (`Type '[number]' is not assignable to type 'number'`), tttt reports the whole
  `string[] | number[]`: yield_typecheck() passed no expression to elaborate for `yield*`. TS2322 at another
  position. [fixed 4badee3d8] Left: `yield* [[1], ["a"]] as const` against `Generator<number[]>`, where tsc
  elaborates both elements (an out-of-bounds index of the union of tuples is undefined to it, and the element's type is
  its const one, `readonly [1]`); tttt reports the first element only, as `number[]`. Text and count only. [open]
- 09-C11b: an interface merged with two members of the same late-bound name of type string (`[s]: number`, `[s]:
  string`). tsc makes a string index signature of the union, tttt nothing. TS2322 missing. [open]
- 09-P4: `D<keyof T>` with `D<X> = X extends PropertyKey ? 1 : 2` stays deferred in tsc (its constraint is `1 | 2`),
  tttt resolves it to 1. TS2322 missing. [open]
- 10-C11b: a class with a field named by a string-typed `[ks]`, merged with an interface's call signature. tsc gives
  `d3[ks]` no number type, tttt reads the field. TS2322 spurious. [open]
- 10-SC1: an interface extending an intersection that reduces to never, or a variadic tuple alias. tsc reports
  TS2339 for the base's properties and doesn't relate the tuple, tttt keeps the base. TS2322 spurious. [open]
- 10-C19, 25-C13 and 28-C31b: a setter whose only parameter is `this: X` (TS2784). tsc takes it as the setter's
  value parameter, so the getter-setter type check and `new C().a = v` report TS2322; tttt skips it. [open]
- 11-C10: a named import of `a` from an `export =` of a union with an index signature (`{ a: 1 } | { [k: string]: 2
  }`). tsc finds the property through the union (a member's index signature makes it partial, not missing), tttt
  reported TS2305: export_equals_property_type_of() wanted a real property in every member. TS2322 missing.
  [fixed COMMIT-C10]
- 12-C1a: `{ [K in keyof T]: T }` against `{ [K in keyof U]: T }`. tsc compares the constraints (keyof U to keyof
  T) and fails, tttt accepts. TS2322 missing. [open]
- 12-C2b: `pipe1(<T,>(x: T) => [x, q] as [T, Q])` inside `outer<Q>`. tsc's returned signature keeps Q, so
  `outer(1)("")` is `[string, Q]`, while tttt instantiates Q. TS2322 missing. [open]
- 12-C7a and 26-C1a: recursive conditional types (`Cn<T, U> = T extends string ? [T, U] : Cn<U | "x", "y">`, and
  `F<0, string>["next"]`). tsc's tail recursion or instantiation caching gives another result than tttt's. TS2322
  spurious (12-C7a) and missing (26-C1a). 26-C1b is related: tsc's `keyof M<{a:1},{b:2}>["a"]` is `"z"` where tttt
  computes "b". [open]
- 15-C1a: `Tagged<K> extends Uppercase<infer U> ? U : never` with `Tagged<K> = Uppercase<string & { [P in K]: 1 }>`.
  tsc infers U as the template literal and reports it against the mapped type, tttt accepts. TS2322 missing. [open]
- 15-C12: a global `PromiseLike<T>` augmentation with another `then` overload. tsc's awaited type of
  `PromiseLike<number>` isn't number there, tttt's is. TS2322 spurious. [open]
- 15-P1: a generator contextually typed by a rest `[...G1[], G2]`. tsc's contextual type at position 0 is the union
  of the element types, so the yield's value is `string | boolean`; tttt takes one. TS2322 missing. [open]
- 16-C3d: an array pattern's default against `T extends { 0?: F } & Iterable<G>`. tsc types the element from the
  pattern's position differently, tttt reports F against G. TS2322 spurious. [open]
- 20-C15: `x.constructor === C` narrowing `typeof C | A`. tsc narrows to `typeof C`, tttt narrowed to never (isConstructedBy compares
  symbols when either is a class). TS2322 missing. [fixed 6f15f3f91]
- 20-P10: `switch (x) { case y: ... }` in a do-while loop with `y = x`. tsc narrows x by the non-unit case y, to "a",
  tttt doesn't. TS2322 spurious. [open]
- 21-C22: an object rest of a union whose members have a property named `rest` (`const { ...rest } = u`). tsc's
  `rest.k` is 1, mechanism unknown. TS2322 spurious. [open]
- 22-P1: `k || f("b", b => ...)` with `f<A extends B, B>(y: B, cb: (b: B) => void): A | B` and `k = "a"`. tsc infers
  B as string (widened), not "b", so the callback's TS2322 shows; tttt kept the literal: tsc's returnMapper is the
  cloned return context's fixing mapper, so the fresh "a" (k's type) widens to string and "b" isn't a literal of
  that context; return_inference_mapper() kept literals. [fixed d819d114b]
- 25-C9a and 25-C9b: a compound-like assignment through a `!` or an array destructuring target (`[x] = s + s`, `x! =
  x + 1`). tsc's isInCompoundLikeAssignment passes through NonNull and array literals (GetAssignmentTarget), so the
  target's type is its base literal type. tttt's compound_like_target stopped at them; it also widened a property
  access target, which tsc never does (getFlowTypeOfAccessExpression: only the flow type after the assignment).
  TS2322 spurious. [fixed ae34e2b13]
- 28-C33d: `for (var a = 1 of [])` (TS1190). tsc still checks the initializer against the element type never,
  tttt doesn't. TS2322 missing. [open]
- 31-P3: `interface I { get x(): this }`, `const r: null = i.x`. tsc reports nothing, tttt reports I to null.
  An accessor is a `this` container that doesn't propagate ContainsThis to its interface (tsc's binder), so I is
  thisless and the getter's `this` is the error type. A generic interface's `this` still prints as the reference
  (tttt makes no this_param for a thisless generic interface; types-only). TS2322 spurious. [fixed 43774737d]
- 32-C17: `const s = (Symbol)()`. tsc makes no unique symbol for a parenthesized callee, so `typeof s` is symbol, while
  tttt's is unique. TS2322 spurious. [open]
- 33-C17b: an object literal method `x()` and then `get x()`/`set x(v)`. A method excludes everything, so the binder
  makes the getter and the setter conflicting symbols of their own. The setter alone is any (TS7032), and that last
  member is o.x. tttt types it by the getter, string. TS2322 spurious. [open]
- 42-C9: `" 1"` against `${bigint}`. tsc's isValidBigIntString accepts the leading U+2028 as white space.
  TS2322 spurious. [open]
- 53-L04: a bigint literal property name (`{ 1n: s } = o`) in a destructuring assignment. tsc looks up the property
  "1n" and checks it, tttt reports TS2339 for '1'. TS2322 missing. [fixed 01be1e916]
- 57-L13a: a user `interface Iterable<T> {}` with lib es5. tsc's JSX children elaboration (TS2746, TS2745) still
  works, tttt's differs from the second element on. TS2322 missing and spurious. [open]
- 60-L19a and 60-L19b: a class's static `prototype` declared as an accessor, or private, protected or readonly.
  tsc's `C.prototype` is the declared member's type (number from the getter) and K7 (`keyof typeof G7`) takes null;
  tttt gives the instance type. TS2322 spurious and missing. [open]
- 62-L04a: a class with `get foo()` and `set [k]` where `const k = "foo"`. tsc merges the late-bound setter with
  the getter (the write type is the setter's), tttt reports TS2540 read-only. TS2322 missing (60-L20a too, and 60-L20b but for
  its errors). [fixed 7fb7ce634]
- 62-L07a: an interface with late-bound `[k10]` and `[k10b]` members whose key types are themselves indexed accesses
  of the interface (`I10[typeof k10b]`). tsc reports the circular name as no property (TS2339), tttt binds it.
  TS2322 missing and spurious. [open]
- 72-R01: an enum member initialized by an 8000-deep chain of const declarations. tsc evaluates it, tttt doesn't.
  TS2322 missing. [open]
- 73-R01f: an element access `o["x"]` narrowed by truthiness, then compared `=== 0`. tsc narrows `o.x` to never there
  and reports the TS2322, but keeps the element access's narrowing apart, so `o["x"]` gets no TS2322 and (line 2)
  `.toFixed` is TS2339 on never. tttt treats both the same. TS2322 missing and spurious. [open]
- 73-R12: `({ a: n } = t0())` with `t0<T>(): T`. tsc doesn't infer T from an object literal assignment target, so T
  is unknown (TS2339, and TS2322 for `({ v: n } = box())`); tttt infers from the target. TS2322 missing. [open]
- 77-R19a: a user `interface Array<T> { 0: string }`. A rest parameter's position 0 then reads string in tsc
  (getTypeAtPosition through the indexed access), so `f("s")` passes and `(...a: number[]) => void` isn't assignable
  to `(a: number) => void`. TS2322 missing. [open]
- 87-R04a and 14-P3: `unknown` against `{} | null | undefined`, where the `{}` is a generic function's returned
  object literal, spread or rest instantiated. tsc's isEmptyAnonymousObjectType sees neither resolved members nor a
  TypeLiteral symbol on the instantiation, so the union isn't unknown-like and TS2322 is reported; tttt calls it
  empty. TS2322 missing. Related to the `[tsgo]` entry for 14-C9a below, though not order-dependent here. [open]
- 90-R09: interfaces whose bases are circular (U1 and U2, TS2310) with a third base U3. tsc still gives U1 U3's
  `m(): this`, tttt drops the bases (TS2339). TS2322 missing. [open]
- 93-R01d: circular mapped types M, N and A. tsc's M has no properties (TS2339 for `M["a"]`), and `let m: M = 1`
  passes; tttt reports TS2615 and the TS2322. [open]
- 93-R11b: an instantiation expression `d.n<2>` as an arrow's expression body inside a generic function. tsc doesn't
  instantiate the outer T there (no block on the way to the type parameter), tttt does. TS2322 (and TS2345)
  missing. [open]
- 96-KYSELY2: deep recursive class comparison, `S<number>` against `K<number>` through `e().e().x()`. tsc finds the
  conditional's number against string, tttt's recursion cut accepts. TS2322 missing. [open]
- 96-H2: named imports from modules whose re-exports have invalid specifiers (`export { A } from 123`, TS1141). For
  F (d5.ts), tsc's import is any, tttt's is a module namespace (`typeof import("96-H2/other")`). TS2322 spurious.
  [open]
- 96-TSPATTERN1: computed-key members of object literals passed to generic functions. The callbacks' context
  sensitivity differs from tsc's (TS7006 and TS2339 belong to other buckets). The TS2322s at b.ts 20 to 22 are tsc's
  TS2345 on the call argument where tttt reports TS2322 on the result. [open]
- 99-E2: a type alias `Method` declared twice next to a `const Method` (`typeof Method` and `"get" | "post"`).
  tsc's merged alias gives Fn's parameter one meaning, tttt the other. TS2322 spurious. [open]
- 52-L05: `declare const {} = expr` and other ambient initializers. tsc doesn't check an ambient initializer's
  expression (beyond TS1254 or TS1039), tttt checks it (TS2304, the nested TS2322). TS2322 spurious. [open]
- 94-R04a: an import default cycle through `export =` (`import d from "./b"` where b is `export = a`). tsc reports
  TS2303 on the alias and types it any, so there is no TS2322 at a.ts; tttt types the module. Order-dependent, like
  08-C11c, which corA wrote up earlier. TS2322 spurious and missing. [open]

## JSX
- 27-C4a: a `const T` type parameter of an SFC, inferred from JSX attributes (`v={1}`). tsc infers T as 1, so the
  callback's x is 1 and `const one: 1 = x` passes; tttt widens to number. TS2322 missing. [open]
- 27-P1: two children against `("x" | "y")[]`. tsc reports both `{true}` and `{"x"}` (the second widened to
  string), tttt only the first. TS2322 missing. [open]
- 27-C15a: `JSX.ElementAttributesProperty` declared as a union type alias. tsc finds no props name and checks
  nothing, tttt uses the constructor's parameter. TS2322 spurious. [open]
- 15-P3: an SFC declared `(...args: [])` with no props parameter. tsc doesn't check the attributes against
  IntrinsicAttributes, tttt does. TS2322 spurious. [open]
- 17-P4 and 27-P2: under exactOptionalPropertyTypes, tsc reports TS2412 ("Consider adding 'undefined'") for a JSX
  child against an optional tuple element, and TS2322 for a spread property's type. tttt picks the other head in
  both. Only the code and message differ. [text]

## JavaScript
- 06-C2 and 11-C3: a JSDoc `@type {() => Generator<...>}` on a generator function. tsc takes the yield's type from
  the tag's next type and reports TS2322 for the yielded value, tttt says TS7057. TS2322 missing. [open]
- 43-C8d: a JSDoc tag name with an escape (`@type {number}`). tsc's JSDoc scanner recognizes it as `@type`.
  TS2322 missing. [open]
- 30-P1 and 30-P16: JS class members assigned through `this[k]` with `const k = "foo"`, or through `this.a` next to
  a field and a method `a`. tsc reports TS7022 (30-P1) and TS2322 against the method type (30-P16), tttt reports
  TS2322 and TS2300. TS2322 spurious and missing. [open]
- 51-L06a: object literal methods assigning `this.format` and `this.pair` in a JS file. tsc types them by the
  declared members, contextually (`[3, 4]` fits the tuple), and in tsc's output that file has none of the other
  errors (the sentinel included). tttt reports TS2322s. [open]
- 60-L04a: JS `this[1] = f()` and `this["-1"]`, `this["+1"]` next to methods `1()`, `[1]()`, `[-1]()`, `[+1]()`.
  tsc's numeric-name matching of a this-property assignment to the method differs (tsc: lines 4 to 7, tttt: line 3).
  TS2322 missing and spurious. [open]
- 77-R17a: a JS class field `a;` with `this.a = null; this.a = "s"` in the constructor. Whether the member type is
  the declared field's or the assignments' depends on the field's order in tsc; tttt reports TS2322s for both orders.
  [open]
- 92-R03c: a JSDoc `@import await from "./b"` inside an async function's parameter list. tsc doesn't bind the name
  `await` (TS2552 at the `@type`), tttt binds the import. TS2322 spurious. [open]

## Deep, needing a design
- 17-SC1: a complexity overflow on an object literal's whole relation. tsc then elaborates per property: the
  property that overflows again gets TS2859 at its name, the next one gets its TS2322. tttt's relation budget runs
  out on a different pass than tsc's (tsc's reporting pass overflows where tttt's non-reporting one does), so
  elaborating after the overflow gave a TS2322 chain and an extra TS2859 at the statement. Tried 2026-10-23 and
  reverted. Matching needs tttt's budget to follow tsc's relationCount per checkTypeRelatedTo call. TS2322 missing.
  [design]
- 77-R02a: a binding pattern with a default in a generic function's callback, `call(({ a } = { a: 1 }) => {}, { a: 2
  })`. tsc reports TS2322 "'{ a: number; }' is not assignable to type 'T'" from checkParameter during
  inferTypeArguments (traced with tsgotrace), so the parameter's type is the uninstantiated T there. tttt infers T
  from the pattern. The mechanism (which pass assigns the contextual parameter type, and why it isn't instantiated)
  isn't understood yet. TS2322 missing. [design]
- 26-P1: a conditional type source, `T extends string ? (x: T) => U : 0` with `T extends U`, instantiated as
  `g<T, unknown>()`. tsc relates it to `0 | ((x: string) => unknown)`, presumably through the true branch's
  substitution of T; tttt goes through the distributive constraint (T to U) and fails. TS2322 spurious. [design]
- Loop back edges: `last = last.next = item` in a loop, where `last` is narrowed by `last === null`. tsc reports
  "Type 'Item' is not assignable to type 'never'": the flow type of `last.next`'s target is never while the back edge
  is being evaluated. These are linked-list builders, real application code. Cases
  assignment-in-value-assigned-on-loop-back-edge, checks-of-a-value-assigned-on-a-loop-back-edge and
  loop-back-edge-error-in-a-function-that-other-files-call, all TS2322 missing. The re-entry during flow evaluation
  is agents/notes/reentrancy.md's territory. [design]

## tsgo artifacts
- 14-C9a: `unknown` against `typeof M | null | undefined`, with M a namespace with no exports. isUnknownLikeUnionType
  caches its answer on the union, and isEmptyAnonymousObjectType(typeof M) is true only once M's members were
  resolved (ObjectFlagsMembersResolved). In the case, `m1.toString` resolves them first. tsgotrace showed that tsgo's
  CLI reports the TS2322 when nothing resolved them first (f.ts), while its harness doesn't. The answer depends on
  history, so it is order-dependent and declined. TS2322 spurious. [tsgo]
- 07-C1a: `id({ a: 1, get g() { return this.a; }, inc() { this.a++; } })`. tsc's `g` is any without TS7023,
  because the context-sensitive method makes a first inference pass in which the getter's `this` (the literal being
  checked) is circular. Without `inc`, g is number. The getter's type is fixed by an inference pass's circularity,
  and tttt's lazily typed accessors don't reproduce it. TS2322 spurious. [tsgo]

## Generated stress files
- 99-I5, 99-M3, 99-V1 and 99-V4 (hundreds of generated files each: self-referential Box interfaces,
  class-with-private-members intersections against `never` and `Pick<Writable<D>>`, readonly key mappings, checker
  entry points such as checkReturnStatement and getWriteTypeOfAccessors): TS2322 missing and spurious by the dozen.
  Some share causes with the entries above (accessor write types, intersections of classes with private members
  reducing to never); none was split out. 99-V4 crashed earlier (agents/measurements.md). [open]
