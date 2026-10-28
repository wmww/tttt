# bun's differential cases: what is left of the TS2339 buckets and the others bundiff owned
The cases `scripts/buncases.py --last` still lists (2026-10-23, after bundiff's fixes: the oracle went from 1279 to
1507 of 2411 cases matching at (file, line, code), with others' work) for every code but corA's (TS2322, TS7022,
TS2303, `agents/bugs/bun_ts2322.md`), corF's (TS1xxx parser recovery) and corH's (spurious TS2345, TS2353, TS2344,
TS2719, TS2488). Missing (`-`) means tsc reports it and tttt doesn't; spurious (`+`) the reverse. A case's last line
is usually `const sentinel: number = ""`. Show one with `scripts/buncases.py --last --show ID`. The first part is
what was looked at, by root cause; the second lists the rest by code, uninvestigated, with a first instance each.

## Looked at, by root cause
- Control flow of loops with `finally` and back edges after a never-returning call or a `break` in a narrowing:
  73-R01e and 73-R01f (TS2339 both ways on `x.foo` in `do { try {} finally { x.foo } } while (true)` after `fail()`),
  73-R03a (an evolving `let x = []` narrowed by `Array.isArray` in a for loop's incrementor: tttt says TS2339 on never
  for `x.push`, tsc's silent never from an incomplete loop type doesn't report; also its TS7006), 99-V4 and
  checks-of-a-value-assigned-on-a-loop-back-edge (TS2339, TS2345, TS7006). The flow engine's incomplete-type and
  silentNeverType handling in loops. [open]
- Object literal getters' `this` inside a generic call's argument, 07-C1b (TS2339 missing, ThisType) and 07-C1c
  (spurious): type_core.md "getter's lazy typing". [dup]
- Inference to an instantiation expression type: 22-C1, `f<T>(a: typeof K<T>, b: T)` with a construct signature
  argument infers T = string | number in tsc (covariantly through the instantiation expression type), `""` in tttt:
  TS2339 missing on `.length`. [open]
- Two `export as namespace` in one file merged with global namespaces of their names: 01-C9. tsc's mergeSymbol
  clones the module per global (UX sees namespace UX's members, UC only UC's); tttt merges both into the module
  (merge_into_umd_globals), so UC.extra resolves: TS2339 missing. A fix needs the module's exports as they were
  before the first merge for later globals. [open]
- A non-ambient `module "./b" { ... }` augmentation (TS1035): tsc reports it and doesn't merge, tttt merges the
  interface: 01-C6b TS2339 missing. [open]
- Expandos through a container's exports: tsc's binder looks an assignment's name up in the container symbol's
  exports too (lookupName), so `g.x = 1` in `function f` with a `namespace f { export function g }` is an expando of g,
  and `export * as ns` in a function body declares `f.ns`: 30-C17a, 30-C17b, 30-P21 (`a.b = 1` beside
  `exports.a`), 30-C16b, 51-L06b. tttt reports TS2339 on the read (spurious). [open]
- An expando assignment inside a decorator expression, 30-C3 (`@(f.a = 1 as any)`): tsc's binder doesn't bind it,
  the read is TS2339 (missing in tttt). [open]
- `this` in a JS class's computed method name inside an object literal, 30-C19b (TS2339 missing on `this.g`). [open]
- `this` in a decorator's arrow function of a nested class (`(z: this) => z.x`), 33-C4d: tsc's `this` there is the
  outer class's; tttt's is the inner class (spurious TS2339). [open]
- Bigint literal property names in destructuring assignments, 53-L04 and 57-L03: tsc reports TS1539 and TS2538 and
  reads nothing, tttt reads the property `1` (spurious TS2339, missing TS2538; TS1539 is corF's). [open]
- Accessors with a `this` parameter, 58-L01 (`get x(this)`, `set x(this, v)`): tsc's `this` in such an accessor body
  is any (no TS2339, no TS7006 for an implicit `this`); tttt types it as the class. [open]
- Private names: `o.#a` on a type literal `{ #a: number }` (30-C9b, TS18016 then nothing in tsc) and `x.#p` on a
  union with an index signature member (89-R15): tttt reports TS2339 where tsc finds the class's private name or
  stays quiet. [open]
- `Pick` and `Writable` of a protected key (`Pick<D, "id">` with `protected readonly id`, TS2344), 99-V1's
  protected_* files: tsc keeps `id` a property of the mapped type and reports TS2445 at accesses, tttt drops it
  (TS2339 spurious, TS2445 missing). Found 2026-10-29 (bun): the smallest form is `declare const u: Pick<D, "id"> | D; u.id = 'x'` (tsc TS2540,
  tttt TS2339): tsc's createUnionOrIntersectionProperty keeps a union property whose non-public declarations are
  common to all members (hasCommonDeclaration: Pick's mapped id is declared at D.id), tttt's
  ty_union_non_public_conflict() compares prop_root()s, and a mapped prop's root is its template (`.target`), not the
  property it maps. Now compared by declaration (ty_props_share_declaration(): the name span mapped_prop_decl() copies),
  and a mapped property reads as public (no value declaration of its own). [fixed 8d0716ee1]
- 99-V1's protected_* files, other shapes than the union above: `Writable<Pick<D, "id">> | D` with D's `id` of a union
  type (D's property is an instantiation's, instance_prop(), with no declaration of its own: ty_props_share_declaration()
  walks to the declared one), and `D & (J | W)` = `(D & J) | (D & W)` (tsc's hasCommonDeclaration counts every
  declaration of a member's synthetic property, so D's `id` is common, and the union's property is public through `D &
  J`: synthetic_common_declaration(); TS2536 rather than TS4105 for `T["id"]`, getConstituentProperty's intersection
  property being public: intersection_prop_non_public()). [fixed]
- 99-V1, still open: a mapped type over a non-generic union's keys, `{ [K in keyof typeof u]: (typeof u)[K] }` with `u:
  W | D`, has the union's properties to tsc (forEachMappedTypePropertyKeyTypeAndIndexSignatureKeyType reads
  getPropertiesOfType of the modifiers type, a union property with a common non-public declaration and public through
  one member included), tttt the keys of `keyof (W | D)` (`keyof W & keyof D`, never): +TS2339 at
  protected_class__writable_of_pick.ts:148 and protected_writable_class__plus_readonly.ts:212/276 (expand_mapped(),
  members.c). The private_class_* files: `Pick<Writable<D>, "id"> & D` with D's `id` private (+TS2322 `W & D` to `{ id
  }`, -TS2339 `t.id` on `T extends W & D`). [open]
- Circular mapped types and interfaces whose members are being resolved: 10-C8c (`interface I9 extends
  Array<typeof a9>`), 11-C15 (`{ [K in "a"]: A1["a"] }`: tsc's property is missing, TS2339, where tttt says TS2615),
  93-R01a, 93-R01c, 93-R01d (mapped types with `as` clauses through each other), 12-C1d (`Alt<B, A>` alternating),
  76-R05a, 76-R05b (interfaces whose index signature names their own instantiation's property), 90-R09 (a base
  through a cycle: TS2430 and TS2322 missing), 08-C13b and 08-C13d (a declaration's computed names reading its own
  type), 89-R13: which member resolution sees an unfinished type and what it reports (TS2339, TS2615, TS2502, TS2310,
  TS7053) differ. [open]
- A parenthesized destructuring target, 55-L08 (`(x.kind) = v` in a condition): tsc doesn't narrow by it (TS2339
  missing three times). [open]
- 12-C1c: inside class C<T, U>, `c.m()` with `c: C<U, { a: 1 }>` and `m(): { [K in keyof T]: U }`: tsc's result is
  `{ a: { a: 1 } }` (the homomorphic mapped type over the class's own U, mapped through the instantiation again),
  tttt's `{ [K in keyof U]: { a: 1 } }`: TS2339 spurious. [open]
- 19-P2: `keyof typeof m4` of a namespace import of a module that only has `export type * from "./m1"`: tsc's
  mapped type has `a` (the type-only re-export's value is in the namespace's keys), tttt's is `{}`: TS2339
  spurious. [open]
- 06-C16, `NI.Inner<string>` as an expression with `Inner` an interface: tsc says TS2689 ("Cannot extend an
  interface", its expression-with-type-arguments check), tttt TS2339 on `typeof NI`. [open]
- Modules: 39-C4a and 39-C4b (`typeof import("./b/b")` default of an .mts under a `type: module` package: TS2339
  missing and spurious), 75-R02 (`default` of `typeof N` namespaces re-exported, spurious TS2339), 87-R08, 94-R04a,
  27-P4b (JSX intrinsic `div` with a module-scoped JSX namespace: TS2339 spurious, TS7026 missing). [open]
- A synthetic default decided by the first import, across files: tsc's cache is per checker (synthetic_default_first
  in lower.c keys by importing file, to stay K-independent), so a dynamic import in one file after a static one in
  another can differ from tsc; no bun case. [note]
- TS6133 spurious, an alias marked used by tsc's error printing: 24-SC2 (`import { o, sym }` where only `o`'s
  printed type mentions `[sym]`: tsc's type-to-string marks the alias referenced). [open]
- TS6133 spurious on parameters of type predicate functions that are mis-assigned (74-R15a: `export const e1: 0 =
  (a: unknown): a is string => true` reports nothing in tsc, the deferred unused check never runs for a function
  expression checked only against a failing target). [open]
- TS6196/TS6133 spurious on type parameters and parameters of merged and non-ordinary declarations: 04-C10a, 04-C10b
  (an interface method merged across files), 04-C13 (`type Foo<T> = intrinsic`), 30-C14a and 30-C14b, 31-SC6
  (parameters without types named like imports, `(Foo) => void`), 80-R18a, 80-R18b, 83-R10a (functions and classes in
  `implements` clauses, which tsc never checks), 10-SC8, 31-P6. tsc's checkUnusedIdentifiers doesn't reach them. [open]
- TS7023 missing where tsc's return type inference meets itself through a call tttt doesn't resolve: 30-P3 and 30-P4
  (`check(k())` with a generic `check<T>(x: T): asserts x` in the function: tsc resolves the assertion call for the
  reachability of the return, inferring T from `k()`, which needs the function's own return type), 31-P1, 31-P2,
  31-P7, 31-P8 (`o[k] = ...` where `k` depends on `g()`: the element assignment's flow asks for k's type), 10-C1 (a
  default parameter `b = g4()` of a function g4 calls). tttt's call effects and element keys don't evaluate what tsc
  evaluates, so the cycle isn't met. [open]
- 54-L02a, a misplaced `export { x as y }` in a function body: resolved by a checker under deferred bodies,
  triage.md [open] (TS2304 both ways). [dup]
- A JSDoc link to a shadowed name keeps the outer import used (ttttHackJsdocLinkShadowed, hacks.md
  `unused_mark_jsdoc_links()`). [open]

- **corH's codes (spurious TS2345, TS2353, TS2344, TS2719, TS2488), what is left after 2026-10-23's fixes**:
  - Circularity tsc finds by re-entering below a call barrier, keeping the nested call's result: 72-R15b (+TS2345 at
    `g15`'s `f15("wrong", null!, "also wrong")`, with -TS7022/-TS7023/-TS2502 on the defaults `b = g3()`, `b:
    ReturnType<typeof g7>`, a method's `b = this.n()`, `o14`'s self-reference), the 10-C1 pattern: `function f3(a:
    number, b = g3())` with g3 returning `f3(1, 1)`. tsc reaches the cycle only when the call gets as far as b's type
    (`f1("wrong", 1)` fails at its first argument and has none). Stages C/D of agents/notes/reentrancy.md (its bun test
    list has 10-C1 and 72-R15b). [design]
  - 77-R02a +TS2353 (`'z' does not exist in type '{ a: number; y: number; }'` for a callback's pattern default against
    the instantiated T): the same cause as bun_ts2322.md's 77-R02a entry (tsc checks the default against the
    uninstantiated T and says TS2322). [dup]
  - 77-R19a +TS2345: a rest parameter's element at a position is `R[index]` (an augmented `interface Array<T> { 0:
    string }`): type_core.md. [dup]
  - 08-C9a, 25-C20a, 33-C6c +TS2488: `const [x] = require("m")` binds named imports in tsgo: js.md. [dup]
  - 88-R04a +TS2488 in a libReplacement file that skipDefaultLibCheck should skip: binder_modules.md. [dup]
  - 83-R01c +TS2488 (`for (var i = [...x in b];;)`): tsgo's for-initializer DisallowIn: missing_checks.md. [dup]

### 2026-10-29 (worker bun)
- TS2509 for every base constructor return type that isn't a valid base (a union, primitive, unknown, a type parameter
  without an object constraint, a generic mapped type), not only an intersection reduced to never: 99-V1's 184
  missing TS2509. [fixed 471f9879f]
- TS2842 spurious where the renaming's name is also a type parameter of the signature that the signature mentions
  (tsc merges the two into one symbol: 99-U1, 99-U2, 126 diagnostics); missing for a rest element with a property
  name, `{ ...a: b }` (28-P10b, 28-P11, 52-L01). [fixed 564bf1e64]
- TS2310 missing for an interface whose base is an intersection leading back to it (`interface K extends N`,
  `type N = K & {...}`): 10-C21. [fixed d6f1b8564]
- TS2310 missing where tsc's base resolution itself asks for the interface's members: 13-C1a/b/c
  (`interface D extends Partial<D>`: instantiating the homomorphic mapped type computes getIndexType(D) eagerly,
  which resolves D's members, which needs D's bases, in flight), 14-P2 (`interface A extends Cond<A>`: the
  conditional type is resolved eagerly against A). tttt keeps `keyof D` and the conditional deferred and sees no
  cycle. The resolution-order family (members.c's RESOLVING_BASE_CONSTRAINT handles one shape). 62-L13a and 89-R14
  are TS2506/TS7022 circularities (cor3's family). [open]
- In a cycle of interfaces tsc keeps the base of the one resolved second (J0 extends J1 extends J0: J1's base J0
  stays, `j1.x0` is fine); tttt drops both (TS2339 spurious). test/cases/ttttInterfaceBaseCycleKeepsSecondBase.ts,
  known failing. [open]
- 99-V3's 56 missing TS2339 (`take({ first = 1 })` contextually typed by `(value: unknown) => void`): tsc's binding
  element defaults referencing the pattern's own parameter type are TS7022 circular and any; the lazy parameter typing
  of agents/notes/reentrancy.md (cor3's). [dup]
- TS6133 missing for a parameter property outside a constructor (04-C6, 32-C11: still a parameter to tsc's unused
  check) and spurious use from a parameter decorator tsc never checks (04-C4, no experimentalDecorators).
  [fixed ccffc8cee]
- TS6133 spurious for an import only an untyped signature parameter names (`(Foo) => void`, 31-SC6): tsc's
  reportImplicitAny resolves the parameter's name as a type use (TS7051), the signature's type parameters first.
  [fixed 9cafcc7cf]
- JSDoc `{@link}` uses (agents/hacks.md `unused_mark_jsdoc_links()`, a text scan of the whole file): tsgo resolves a
  link only in the comment text of a JSDoc (and of its tags) attached to a node its checkSourceElement visits
  (EagerJSDoc). Not a use in tsgo: `@throws {@link Foo}` (a type expression, 43-P4a), an escaped name
  `{@link Foo\u0046}` (43-C8c), JSDoc on a named tuple member (54-L08) or on an expression statement (80-R17a,
  80-R17b), `@typedef`/`@callback` tags' property/param descriptions in a TS file (54-L06), member names after `#`
  that are keywords (54-L07a: `a#delete` and `c#default` use, `b#foo`, `d#of`, `e.delete` don't). Needs the links
  resolved from parsed, attached JSDoc. [open]

- 83-R10a: functions inside heritage elements tsc never checks (TS2500/TS2499, clauses after TS1172/TS1174/TS1175)
  reported unused locals and TS1235; deferred bodies kept reporting what an immediate walk wouldn't. [fixed cf537256c]
- 99-V1's TS2790: an intersection's property is optional only when all its members' are. [fixed 903cbccb2]
- 99-V1's TS2415: a base constructor's return type is the base reduced (`D & (J | W)`). [fixed f57bb563c]
- 99-V1's TS2698/TS2700/TS2339 on intersections that reduce to never (spread, rest, keyof-mapped). [fixed ef5be1fd5]
- `t.other` with `<T extends D & { id: string }>` (D's id private): tsc's reduced apparent type of T is never
  (TS2339), tttt's type literal inside the generic signature has TYF_COULD_CONTAIN_TYPE_VARIABLES and
  is_known_object_for_reduction() declines to reduce (99-V1's `*_generic` lines). [open]
- checks-of-a-value-assigned-on-a-loop-back-edge (87 TS2322, TS2345...): tsc reports the assigned expression's errors
  twice, once while the loop's flow types are incomplete (getInitialOrAssignedType checks `x = more ? "a" :
  takeNum(x)` with x still `string`) and once in the ordinary check (`string | number`); tttt reports the second
  only. Diagnostics produced during flow analysis are the mechanism: a design question. [design]
- -TS2448: two mechanisms, the JSX factory (`React`, `Frag`) declared after the element that uses it (tsc resolves
  the factory at the element as a use: 01-C2b, 15-C3b, 27-C3a, 27-P7, 34-C2b) and `const arguments = 1` in a
  function body (tsc's reference to the implicit `arguments`: 25-C17c, 33-P4g, 51-L01, 51-L05, 77-R12b). [open]
- 83-R01c (+TS2304 33): the for-initializer DisallowIn leak, missing_checks.md (ttttForInitializerDisallowInLeak). [dup]

- 56-L15, 28-C11b: TS1358 for every tagged template whose tag is in an optional chain, not only right after `?.`.
  (28-C11b's spurious TS1099 for `o?.a<>\`x\`` went with 2a93a79fe.) [fixed 4352de83b]
- 58-L01 / 63-L03b (accessor `this` parameters), the rule found in tsc: getAccessorThisParameter counts a `this`
  first parameter only when the accessor has 1 (getter) or 2 (setter) parameters; an accessor without its own takes
  the other accessor's (getSignatureFromDeclaration, getAnnotatedAccessorThisParameter), so `this` in both bodies is
  that parameter's type (any); an unannotated setter `this` in a pair takes the getter's this type
  (getTypeForVariableLikeDeclaration), circular and silently any when the getter has none, hence no TS7006 there.
  Not implemented. [open]
- 14-P5, 16-C10 (+TS7006): contextual parameter types from a union of signatures/objects where tsc discards a member
  (a rest tuple intersected with conflicting discriminants, a template-literal discriminant under `as const`), not
  analysed further. [open]

### 2026-10-29 (worker bun2): the parser keys
The cases whose TS1xxx diagnostics differ, by mechanism (tsgo's parser is `internal/parser/parser.go`; grammar errors
tttt's parser reports are tsgo's checker's, see `grammar_error()`).
- Grammar errors tttt's parser reports where tsc's checker stops at an earlier grammar error of the same node (its
  `||` chains: checkGrammarModifiers before a class's or interface's heritage clauses and a variable statement's list,
  the heritage clauses' TS1172/TS1174/TS1175 before the class's type parameters, TS1358 before a tag's type
  arguments) or never looks (import type arguments, the heritage elements past the base): 28-C10a 28-P6a 28-P6b
  28-P6c 28-P7b 02-C21a 28-C10b 28-C11b 28-P8a 28-P8b 28-C23 56-L20 62-L12. [fixed 2a93a79fe]
- Grammar errors in what tsc never checks, still open: the initializers of a function type's parameters (84-R02c:
  +TS1174/TS1172/TS1173/TS1098 in a class expression there; tsc says TS2371 only), a `for` statement's declaration
  list after TS1036 in an ambient block (28-C26d: +TS1009), 56-L09 (+TS1326 for `extends import<string>, v`: the
  checker reports it for the base although TS1174 ends tsc's chain first, class_decl.c). 51-L12: `typeof
  import("./m").g<>` is TS2635 for a non-generic `g` (missing), and the empty list instantiates a generic `f` to `() =>
  void` in tsc (tttt keeps `<T>() => T`). [open]
- Decorators followed by something other than a class: tsc's parseDeclarationWorker dispatches on the keyword after
  the decorators and modifiers without the lookahead a statement takes, and says TS1146 "Declaration expected" only
  when none matches; a class member that is only decorators is a property missing its name (TS1146): 28-P4b 32-C25
  32-C25b 32-C25c 34-P5 57-L02b 57-L02f 61-L14. [fixed e51ced678]
- Decorators before `export;` (57-L02c: '{' expected, an export declaration) and before `import(...)`/`import.meta`
  (84-R08c, 84-R08d: an import declaration whose module specifier is an expression, TS1141/TS1109; tttt's import
  parser has no such path), decorators in an object binding pattern (53-L16b: TS1180). tttt: TS1146 then +TS1128.
  [open]
- Modifiers on an object literal's methods: tsc's checkGrammarMethod runs the whole checkGrammarModifiers first
  (TS1244 abstract, TS1024 readonly, TS1028/TS1029/TS1040...) and says TS1184 only when that passes; either ends the
  member's grammar checks (no '{' expected for a missing body); `readonly` stays a modifier of the property (TS2540,
  TS2704): 02-C13b 28-C18a 32-C10a 32-C10c 32-C10d 53-L18. [fixed a6685537f]
- Object literal accessors with `abstract` on one of a pair: TS2676 on both (02-C13c, missing); a class expression with
  modifiers in an expression (`@dec public class {}`, 80-R08: tsc parses them, TS1184). [open]
- Decorators on the parameters of signatures (function and constructor types, interface members, `@await` in an
  async body): tsc's parseParameter takes them (TS1206 from the checker, TS1109 for `@await`); tttt fails the type
  and cascades (+TS1005 +TS1109 +TS1128 +TS1131 +TS1146 +TS1434): 56-L14b 83-R20a. JavaScript `function h(export
  @dec x)`: modifiers before decorators, TS8012 (tttt TS1359): 84-R01. [open]
- `yield` takes an operand only on the same line and is an AssignmentExpression (no `?:` or binary operator after
  it): -TS1109/-TS1005 43-X6a 43-X6b 43-X6c 43-X6e. [fixed 69c3e0a83]
- A private name where an identifier name goes: tsc's tokenIsIdentifierOrKeyword says yes to PrivateIdentifier (it
  follows Identifier among the token kinds), so `#a` is an Identifier node in a type reference (TS2304), a JSX tag or
  attribute name, an import attribute's key, and after `asserts` parseIdentifier's TS18016; not where isStartOfType
  is asked (an indexed access, type arguments): 43-P8e 43-X5f 56-L19 80-R01a 80-R01b 43-C13d. [fixed cf9b96c9c]
- `await` at the top level of a module in a computed property name: tsc's parsePropertyName keeps an `await`
  identifier there from marking its statement for the top-level-await reparse, so it stays an identifier (TS1262 from
  the binder, a class member's computed name being outside the class, a type's too; TS2304), and `await.x` is no
  await expression: 56-L11 93-P01b. [fixed 2ef4eb206]
- `<await>(x: await) => x` and `<T, await>(x: T) => x` at a module's top level: any `await` Identifier (a type
  parameter's, a type reference's, inside a function too) marks the statement for the await-context reparse, where
  they start no arrow function; a failed speculative arrow function marks nothing (74-R15d). [fixed 6e6b12bcc]
- JSX lexing and recovery: attribute names and values (`b=>x`, `<a << />`, `<a /= />`, a comment before a value,
  backslashes in attribute strings: 43-C13e 43-C13f 43-C13g 43-C5c), `<<`/`<=` after a tag name (tsc scans one token:
  43-C4b 93-P04a 93-P04c 93-P04d 93-P04e), a merge conflict marker among the children (tsc ends the element there:
  43-P7b 43-X5h 74-R16a 74-R16b 74-R16c: the marker's ConflictMarkerTrivia token starts where the JSX text did and no
  rule consumes it, so every expectation after the element ('</', ')', '{', a list's) is deduplicated at that one
  position until a list's abortParsingListOrMoveToNextToken skips it and parsing goes on after the marker's line;
  tttt continues after the marker at once: it needs a pending unconsumable token in the parser), TSX generic arrows `<T,>x => x` and `</T>` in type parameter lists
  (56-L05b 56-L06). ±TS1003 ±TS1005 ±TS1109 ±TS1128 +TS1139 +TS1145. [open]
- Escaped keywords: `\u0065lse;` is the keyword to tsc's statement parser (TS1128, TS1135 as an argument: 82-R17c
  82-R17e); an escaped modifier before a line break or a conflict marker isn't one (tttt +TS1260: 83-R03a 93-P05);
  `keyof n\u0065w () => Z` (83-R03c); `d\u0065fault class: 1` (61-L16). [open]
- Arrow and type-argument speculation: `async <T extends object>(x: T)` at EOF, `async <T extends U>(x: any);`
  (57-L18 56-L10), `<T>(x?: number);` (74-R15e), `f((a, ...) => 1)` (83-R13a d16), `f<new>(x)`, `f<import(>(x)`,
  `f<abstract new>(x)` (83-R01b), `@null! as typeof mk<number>|` (93-P02b), `a?..b` (92-R02b), `a?.<T>\`x\``
  (92-R02h: type arguments after `?.`, then TS1358). [open]
- JSDoc parsing: `@import a-b from` (43-C8e), `{@link Map#delete}` reserved words TS1359 and `{@link Map.}` (54-L07b),
  `@augments {A<=}`/`{A<<=}` and `{<>}`/`Array<<>>` (56-L01 56-L20b), `function () {}` with `@overload` in JS
  (61-L17, TS1003 twice), `typeof a..b` (84-R04), `@import yield`/`{ A as await }` in generators and async functions
  (92-R03b 92-R03d), type literal members `static {}` and `[` (92-R07a 92-R07b 92-R07f). [open]
- Declaration and statement recovery: `default` as a member modifier (61-L15b: TS1070, TS1029), contextual-keyword
  declarations with a missing name (`type )`, `interface ]`, a class member `interface? )`, `throw type { A }`:
  96-SOLID1), `export *` and `export type *` at EOF (TS1109 then 'from' expected:
  export-from-without-a-module-specifier-where-declarations-are-emitted), `export * as`/`super`/`new.` at EOF
  (43-P9d 43-P9h 43-P9l), `(this?: any) => void` (34-P6a: one ',' expected), `import("m" | "n")` (32-C24a: TS1141),
  `typeof Array<<T>() => T>` (43-C4d), `(x = €) => x` (43-C14a: TS1127 only), an unterminated template at EOF
  (93-P03a 93-P03b: no ')' expected), a JS object literal method without a body (80-R09b: TS8017),
  `@dec get x(): number;` in a class (34-C16: no '{' expected). [open]
- Ambient contexts: TS1183 for each function body in a `declare` member's initializer and for an accessor body in an
  ambient type literal (53-L15 61-L09), the initializers of `declare const {} = ...` not checked (52-L05: +TS2304
  +TS1183 +TS1039), `declare export default (...)` (80-R04: TS1120, tttt TS1029). [open]

## Not looked at, by code
- Each paragraph below is a code and direction with the cases it mismatches in, uninvestigated: at least one open
  root cause each, counted here once. [open]

-TS2339 (missing), 35 cases: 01-C6b, 01-C9, 07-C1b, 08-C13d, 10-C8c, 10-SC1, 11-C15, 12-C1d, 16-C6, 22-C1, 30-C19b,
30-C3, 39-C4a, 43-X6d, 43-X6e, 54-L02d, 55-L08, 61-L15b, 62-L07a, 71-R12a, 73-R01e, 73-R01f, 73-R04, 76-R05a, 76-R05b,
88-R04b, 89-R13, 93-R01a, 93-R01c, 93-R01d, 96-TSPATTERN1, 99-V1, 99-V3, 99-V4,
checks-of-a-value-assigned-on-a-loop-back-edge. First: 01-C6b a.ts:4 "Property '…' does not exist on type '…'.".

+TS2339 (spurious), 35 cases: 06-C16, 07-C1c, 08-C13b, 08-C9b, 12-C1c, 19-P2, 21-C14c, 21-P5, 27-P4b, 30-C16b,
30-C17a, 30-C17b, 30-C9b, 30-P2, 30-P21, 33-C4d, 39-C4b, 51-L06b, 53-L04, 57-L03, 58-L01, 59-L20, 73-R01e, 73-R03a,
75-R02, 76-R05a, 87-R08, 89-R15, 90-R09, 94-R04a, 99-I5, 99-V1, 99-V4, 99-V6,
checks-of-a-value-assigned-on-a-loop-back-edge. First: 06-C16 a.ts:2 "Property '…' does not exist on type '…'.".

-TS2304 (missing), 24 cases: 01-C13, 01-C6c, 15-C6, 25-C6, 28-P9, 31-C14, 33-C12a, 33-P6b, 40-C5, 40-P5, 42-C5b,
43-P8e, 43-X5f, 51-L05, 52-L03, 54-L02a, 56-L07, 56-L11, 58-L11b, 78-R07, 80-R12a, 80-R13b, 91-R12d, 93-P01b. First:
01-C13 a.ts:4 "Cannot find name '…'.".

+TS2304 (spurious), 23 cases: 01-C11, 01-C3, 01-C8, 03-C11b, 30-C17b, 31-SC11, 31-SC8, 33-C11a, 34-C14b, 39-C13,
40-C10a, 40-C12, 40-C13b, 52-L05, 54-L02a, 61-L03, 79-R03a, 79-R03b, 82-R17c, 82-R17e, 83-R01c, 90-R10b, 99-V4. First:
01-C11 a.ts:2 "Cannot find name '…'.".

-TS7023 (missing), 18 cases: 10-C1, 11-P6c, 15-P6b, 21-C1b, 27-C1, 30-P3, 30-P4, 31-P1, 31-P2, 31-P7, 31-P8, 55-L12b,
72-R15b, 75-R18c, 76-R20, 78-R01, 81-R01b, 99-V4. First: 10-C1 a.ts:1 "'…' implicitly has return type '…' because it
does not have a return type annota".

+TS7006 (spurious), 17 cases: 05-C11a, 13-P2a, 14-P5, 16-C10, 27-C5, 51-L06a, 58-L01, 58-L05, 59-L18, 60-L20b,
62-L04a, 63-L03b, 73-R03a, 81-R01b, 83-R01c, 96-TSPATTERN1, 99-V4. First: 05-C11a a.ts:3 "Parameter '…' implicitly has
an '…' type.".

-TS2502 (missing), 16 cases: 10-C2b, 11-P2, 11-P4, 11-P6c, 17-C4, 19-P4, 59-L20, 72-R15a, 72-R15b, 76-R05c, 79-R03b,
81-R01, 81-R01b, 81-R04, 85-R16b, 99-V4. First: 10-C2b a.ts:2 "'…' is referenced directly or indirectly in its own
type annotation.".

-TS2345 (missing), 15 cases: 16-C5a, 16-C5b, 20-C3b, 22-C9, 25-P2, 26-P3a, 35-C1c, 35-C1e, 93-R11b, 95-J3, 96-ARKTYPE2,
96-TSPATTERN1, 99-A1, 99-V4, checks-of-a-value-assigned-on-a-loop-back-edge. First: 16-C5a a.ts:4 "Argument of type
'…' is not assignable to parameter of type '…'.".

+TS6133 (spurious), 14 cases: 24-SC2, 30-C14a, 31-P6, 31-SC6, 43-C8b, 43-P4b, 43-P4c, 51-L01, 51-L20b, 74-R15a,
80-R18a, 80-R18b, 83-R10a, 91-R12b. First: 24-SC2 a.ts:1 "'…' is declared but its value is never read.".

-TS6133 (missing), 13 cases: 03-SC8, 04-C4, 04-C6, 32-C11, 43-C8c, 43-P4a, 54-L06, 54-L07a, 54-L08, 71-R09a, 80-R17a,
80-R17b, 97-M1. First: 03-SC8 a.ts:2 "'…' is declared but its value is never read.".

+TS2307 (spurious), 11 cases: 01-C10, 28-C21a, 36-C10b, 36-C11, 40-C13c, 40-P1, 41-P3a, 41-P3b, 42-C11, 42-P2, 59-L09.
First: 01-C10 a.ts:1 "Cannot find module '…' or its corresponding type declarations.".

-TS2448 (missing), 11 cases: 01-C2b, 15-C3b, 25-C17c, 27-C3a, 27-P7, 33-P4g, 34-C2b, 51-L01, 51-L05, 77-R12b, 90-R10b.
First: 01-C2b a.tsx:3 "Block-scoped variable '…' used before its declaration.".

-TS2540 (missing), 11 cases: 06-C11b, 08-C25, 08-P1, 14-C5, 14-C6, 32-C1, 53-L18, 60-L19b, 80-R20b, 99-V1, 99-V4.
First: 06-C11b b.js:1 "Cannot assign to '…' because it is a read-only property.".

-TS7006 (missing), 11 cases: 06-C7, 07-C1d, 09-C8, 15-C2, 15-C4a, 15-P4, 16-C3a, 16-C3b, 16-C3c, 33-C17b, 63-L03b.
First: 06-C7 a.js:3 "Parameter '…' implicitly has an '…' type.".

-TS2310 (missing), 10 cases: 10-C21, 13-C1a, 13-C1b, 13-C1c, 14-P2, 62-L13a, 72-R10c, 72-R10d, 89-R14, 99-V4. First:
10-C21 a.ts:1 "Type '…' recursively references itself as a base type.".

+TS7053 (spurious), 10 cases: 08-C13b, 30-P2, 31-C5, 34-P2a, 51-L08, 59-L20, 60-L04a, 60-L05, 99-V1, 99-V4. First:
08-C13b a.ts:3 "Element implicitly has an '…' type because expression of type '…' can'…'{ (): vo".

-TS2307 (missing), 9 cases: 03-C15b, 30-P13a, 30-P13c, 42-P7, 58-L11a, 61-L12, 91-R12c, 92-R03d,
import-type-options-in-options. First: 03-C15b a.ts:1 "Cannot find module '…' or its corresponding type
declarations.".

+TS6196 (spurious), 9 cases: 04-C10a, 04-C10b, 04-C13, 10-SC8, 30-C14b, 31-SC6, 80-R18a, 80-R18b, 83-R10a. First:
04-C10a g1.ts:1 "'…' is declared but never used.".

+TS2300 (spurious), 7 cases: 02-C14b, 08-P1, 30-C20, 30-P16, 33-P2, 58-L08b, 76-R05c. First: 02-C14b a.ts:1 "Duplicate
identifier '…'.".

+TS2305 (spurious), 7 cases: 03-C8b, 08-C9b, 11-C10, 28-C21d, 30-C16a, 86-R16, 99-C1. First: 03-C8b a.ts:1 "Module '…'
has no exported member '…'.".

-TS2313 (missing), 7 cases: 10-C15, 11-P4, 13-C1b, 13-C1c, 79-R03a, 79-R03b, 99-V4. First: 10-C15 a.ts:1 "Type
parameter '…' has a circular constraint.".

+TS2502 (spurious), 7 cases: 06-C14, 08-C13c, 76-R05b, 81-R09, 81-R09b, 89-R13, 99-V4. First: 06-C14 a.ts:2 "'…' is
referenced directly or indirectly in its own type annotation.".

-TS6305 (missing), 7 cases: 38-P1a, 39-C3d, 40-C13a, 40-P1, 40-P2, 42-P9a, 42-P9b. First: 38-P1a a.ts:1 "Output file
'…' has not been built from source file '…'.".

+TS7023 (spurious), 7 cases: 10-C12, 10-C7b, 11-P3, 39-P5b, 75-R18, 81-R01, 99-V4. First: 10-C12 a.ts:1 "'…'
implicitly has return type '…' because it does not have a return type annota".

-TS7031 (missing), 7 cases: 09-C18, 09-C8, 51-L19c, 54-L20b, 77-R01a, 77-R01b, 99-V4. First: 09-C18 a.ts:1 "Binding
element '…' implicitly has an '…' type.".

-TS2454 (missing), 6 cases: 09-P3, 31-C6, 31-SC1, 85-R03c, 85-R03g, 99-V4. First: 09-P3 a.js:1 "Variable '…' is used
before being assigned.".

-TS2488 (missing), 6 cases: 57-L13c, 88-R04a, 88-R04b, 89-R13, 99-V3, 99-V4. First: 57-L13c a.ts:2 "Type '…' must have
a '…' method that returns an iterator.".

+TS2552 (spurious), 6 cases: 01-C13, 31-C14, 31-SC10, 44-C3, 80-R13b, 92-R03d. First: 01-C13 a.ts:4 "Cannot find name
'…'. Did you mean '…'?".

-TS2693 (missing), 6 cases: 08-C2, 33-C3, 33-P1, 88-R05a, 90-R10b, 99-C1. First: 08-C2 a.ts:2 "'…' only refers to a
type, but is being used as a value here.".

-TS2698 (missing), 6 cases: 63-L14b, 99-N2, 99-N3, 99-N4, 99-T1, 99-V1. First: 63-L14b a.ts:26 "Spread types may only
be created from object types.".

+TS2774 (spurious), 6 cases: 16-C9c, 16-C9d, 16-C9f, 71-R04a, 71-R04c, 71-R04d. First: 16-C9c a.ts:2 "This condition
will always return true since this function is always defined. Di".

-TS6054 (missing), 6 cases: 38-C5a, 40-C11a, 40-C11b, 40-C11c, 42-C5b, 42-C5c. First: 38-C5a ref/out/index.d.ts:1 "File
'…' has an unsupported extension. The only supported extensions are '…', '…".

-TS7042 (missing), 6 cases: 35-C3a, 35-C3b, 36-C3, 36-C3b, 36-C3c, 38-C5a. First: 35-C3a a.ts:1 "Module '…' was
resolved to '…', but '…' is not used.".

-TS7053 (missing), 6 cases: 13-C2b, 31-C5, 34-P2a, 60-L06, 89-R13, 96-ARKTYPE6. First: 13-C2b a.ts:1 "Element
implicitly has an '…' type because expression of type '…' can'…'typeof E".

+TS7057 (spurious), 6 cases: 06-C2, 11-C3, 33-P6b, 43-X6a, 43-X6c, 43-X6d. First: 06-C2 b.js:3 "'…' expression
implicitly results in an '…' type because its containing generato".

-TS8024 (missing), 6 cases: 16-C15a, 25-C17a, 33-P4b, 33-P4c, 33-P4d, 33-P4h. First: 16-C15a b.js:2 "JSDoc '…' tag has
name '…', but there is no parameter with that name.".

+TS18046 (spurious), 6 cases: 06-C10, 21-C14a, 27-C16, 31-C13, 58-L05, 60-L16a. First: 06-C10 a.ts:2 "'…' is of type
'…'.".

-TS2300 (missing), 5 cases: 10-C17b, 34-P2b, 61-L15b, 62-L13a, 99-V4. First: 10-C17b a.ts:2 "Duplicate identifier
'…'.".

-TS2305 (missing), 5 cases: 08-C9a, 25-C20b, 33-C6b, 33-C6c, 38-C5b. First: 08-C9a a.js:1 "Module '…' has no exported
member '…'.".

+TS2313 (spurious), 5 cases: 02-C4d, 11-C2c, 79-R03c, 92-R17a, 99-I5. First: 02-C4d a.ts:1 "Type parameter '…' has a
circular constraint.".

-TS2344 (missing), 5 cases: 09-C5c, 13-C4, 26-P4, 92-R07e, checks-of-a-value-assigned-on-a-loop-back-edge. First:
09-C5c a.ts:1 "Type '…' does not satisfy the constraint '…'.".

-TS2373 (missing), 5 cases: 01-C2c, 15-C3c, 27-C3c, 34-C2c, 99-V4. First: 01-C2c a.tsx:3 "Parameter '…' cannot
reference identifier '…' declared after it.".

-TS2425 (missing), 5 cases: 03-C2b, 03-C2d, 03-C5, 13-C1c, 99-V2. First: 03-C2b a.ts:2 "Class '…' defines instance
member property '…', but extended class '…' defines i".

+TS2454 (spurious), 5 cases: 25-C12b, 28-C24b, 31-SC7, 32-C21b, 99-V4. First: 25-C12b a.ts:1 "Variable '…' is used
before being assigned.".

+TS2540 (spurious), 5 cases: 14-P9, 23-C2b, 30-P5, 60-L20a, 62-L04a. First: 14-P9 a.ts:2 "Cannot assign to '…' because
it is a read-only property.".

+TS2578 (spurious), 5 cases: 39-C9a, 39-C9b, 99-V3, 99-W2, 99-W9. First: 39-C9a a.ts:2 "Unused '…' directive.".

+TS2657 (spurious), 5 cases: 43-P7a, 43-P7b, 74-R16a, 74-R16b, 74-R16c. First: 43-P7a a.tsx:1 "JSX expressions must
have one parent element.".

-TS2741 (missing), 5 cases: 15-C4b, 23-C2, 27-P4a, 27-P4c, 43-C12a. First: 15-C4b a.ts:1 "Property '…' is missing in
type '…' but required in type '…'.".

+TS2741 (spurious), 5 cases: 27-C11b, 27-C12, 27-C8, 76-R05a, 99-V4. First: 27-C11b a.tsx:4 "Property '…' is missing
in type '…' but required in type '…'.".

-TS7008 (missing), 5 cases: 09-C4, 79-R11, 92-R07a, 92-R07b, 92-R07f. First: 09-C4 a.ts:2 "Member '…' implicitly has an
'…' type.".

+TS7032 (spurious), 5 cases: 59-L18, 60-L20b, 62-L04a, 63-L03b, 81-R01b. First: 59-L18 a.ts:6 "Property '…' implicitly
has type '…', because its set accessor lacks a parameter".

-TS2315 (missing), 4 cases: 14-P6, 79-R03a, 79-R03b, 99-V4. First: 14-P6 a.ts:1 "Type '…' is not generic.".

-TS2318 (missing), 4 cases: 01-C5b, 05-C8a, 11-C6b, 11-P1. First: 01-C5b :0 "Cannot find global type '…'.".

-TS2451 (missing), 4 cases: 10-C17, 30-C20, 30-C25, 31-SC8. First: 10-C17 a.ts:2 "Cannot redeclare block-scoped
variable '…'.".

-TS2456 (missing), 4 cases: 14-P6, 79-R03a, 79-R03b, 99-V4. First: 14-P6 a.ts:1 "Type alias '…' circularly references
itself.".

-TS2475 (missing), 4 cases: 06-P7, 91-R02, 99-B2, 99-B4. First: 06-P7 a.ts:4 "'…' enums can only be used in property or
index access expressions or the right ".

-TS2500 (missing), 4 cases: 03-C12, 30-P7a, 33-C5a, 34-P1a. First: 03-C12 a.ts:2 "A class can only implement an
identifier/qualified-name with optional type argum".

-TS2589 (missing), 4 cases: 99-V4, 99-W10, 99-W2, 99-W9. First: 99-V4
resolveObjectTypeMembers__01_base_index_signature_from_keys_of_another_instantiation.ts:6 "Type instantiation is
excessively deep and possibly infinite.".

-TS2716 (missing), 4 cases: 02-C4d, 11-C2c, 11-C2d, 99-V4. First: 02-C4d a.ts:1 "Type parameter '…' has a circular
default.".

-TS2774 (missing), 4 cases: 02-C6b, 71-R04b, 71-R04e, checks-of-a-value-assigned-on-a-loop-back-edge. First: 02-C6b
a.ts:2 "This condition will always return true since this function is always defined. Di".

+TS2799 (spurious), 4 cases: 33-P7a, 33-P7b, 62-L01, 92-R11d. First: 33-P7a a.ts:2 "Type produces a tuple type that is
too large to represent.".

-TS2800 (missing), 4 cases: 33-P7a, 33-P7b, 62-L01, 92-R11d. First: 33-P7a a.ts:2 "Expression produces a tuple type
that is too large to represent.".

+TS6053 (spurious), 4 cases: 40-C11a, 40-C11b, 40-C11c, 42-C5c. First: 40-C11a a.ts:1 "File '…' not found.".

+TS7008 (spurious), 4 cases: 10-SC3, 30-P19, 32-C16, 34-C11. First: 10-SC3 j.js:7 "Member '…' implicitly has an '…'
type.".

+TS7024 (spurious), 4 cases: 07-C7, 73-R13b, 75-R18, 99-V4. First: 07-C7 a.ts:2 "Function implicitly has return type
'…' because it does not have a return type a".

+TS7026 (spurious), 4 cases: 43-C4b, 43-C5a, 93-P04c, 93-P04d. First: 43-C4b a.tsx:1 "JSX element implicitly has type
'…' because no interface '…' exists.".

+TS7034 (spurious), 4 cases: 21-P1, 30-P11, 39-C12a, 99-M1. First: 21-P1 a.ts:3 "Variable '…' implicitly has type '…'
in some locations where its type cannot be ".

+TS8024 (spurious), 4 cases: 16-C15b, 33-P4f, 77-R12a, 77-R12c. First: 16-C15b b.js:1 "JSDoc '…' tag has name '…', but
there is no parameter with that name.".

+TS2314 (spurious), 3 cases: 56-L01, 56-L20b, 99-V4. First: 56-L01 a.js:9 "Generic type '…' requires 1 type
argument(s).".

-TS2317 (missing), 3 cases: 01-C4c, 11-C7, 57-L13b. First: 01-C4c a.ts:11 "Global type '…' must have 1 type
parameter(s).".

-TS2341 (missing), 3 cases: 51-L20, 53-L04, 60-L19b. First: 51-L20 a.ts:4 "Property '…' is private and only accessible
within class '…'.".

+TS2349 (spurious), 3 cases: 39-C4d, 54-L10, 73-R03a. First: 39-C4d a.mts:2 "This expression is not callable.".

-TS2353 (missing), 3 cases: 15-C4c, 62-L07a, 76-R05b. First: 15-C4c a.ts:1 "Object literal may only specify known
properties, and '…' does not exist in type".

+TS2354 (spurious), 3 cases: 52-L01, 54-L01, 61-L08. First: 52-L01 a.ts:2 "This syntax requires an imported helper but
module '…' cannot be found.".

-TS2355 (missing), 3 cases: 02-C7, 21-C15, 31-SC1. First: 02-C7 a.ts:2 "A function whose declared type is neither '…',
'…', nor '…' must return a value.".

-TS2367 (missing), 3 cases: 99-V4, checks-of-a-value-assigned-on-a-loop-back-edge,
loop-back-edge-errors-of-two-variables-in-functions-that-other-files-call. First: 99-V4
getAssignedType__05_for_of_operand_comparison_in_loop.ts:9 "This comparison appears to be unintentional because the
types '…' and '…' have n".

-TS2383 (missing), 3 cases: 02-C12, 28-C33b, 28-C33i. First: 02-C12 a.ts:2 "Overload signatures must all be exported or
non-exported.".

-TS2432 (missing), 3 cases: 03-SC2, 30-C8a, 87-R17b. First: 03-SC2 b.ts:1 "In an enum with multiple declarations, only
one declaration can omit an initiali".

+TS2503 (spurious), 3 cases: 80-R18b, 90-R10a, 90-R10b. First: 80-R18b b.ts:4 "Cannot find namespace '…'.".

-TS2526 (missing), 3 cases: 33-C4a, 33-C4b, 33-C4c. First: 33-C4a a.ts:2 "A '…' type is available only in a non-static
member of a class or interface.".

+TS2537 (spurious), 3 cases: 19-C8, 76-R20, 78-R20d. First: 19-C8 a.ts:2 "Type '…' has no matching index signature for
type '…'.".

-TS2538 (missing), 3 cases: 53-L04, 57-L03, 87-R08. First: 53-L04 a.ts:4 "Type '…' cannot be used as an index type.".

-TS2552 (missing), 3 cases: 01-C15, 31-SC11, 92-R03c. First: 01-C15 :0 "Cannot find name '…'. Did you mean '…'?".

+TS2558 (spurious), 3 cases: 43-C4b, 93-P04c, 93-P04d. First: 43-C4b a.tsx:1 "Expected 0 type arguments, but got 1.".

+TS2571 (spurious), 3 cases: 06-C8, 22-C12, 22-C8. First: 06-C8 a.ts:3 "Object is of type '…'.".

+TS2589 (spurious), 3 cases: 26-P2, 95-Z1, 99-V4. First: 26-P2 a.ts:4 "Type instantiation is excessively deep and
possibly infinite.".

-TS2611 (missing), 3 cases: 03-C10, 03-C2a, 03-C2c. First: 03-C10 a.ts:3 "'…' is defined as a property in class '…',
but is overridden here in '…' as an a".

+TS2615 (spurious), 3 cases: 11-C15, 93-R01d, 99-V4. First: 11-C15 a.ts:1 "Type of property '…' circularly references
itself in mapped type '…'.".

-TS2651 (missing), 3 cases: 42-C3, 52-L15b, 99-V4. First: 42-C3 a.ts:1 "A member initializer in a enum declaration
cannot reference members declared aft".

-TS2683 (missing), 3 cases: 07-C5, 32-C2, 99-V4. First: 07-C5 a.ts:3 "'…' implicitly has type '…' because it does not
have a type annotation.".

-TS2714 (missing), 3 cases: 03-C17a, 03-C17b, 80-R04. First: 03-C17a a.ts:1 "The expression of an export assignment
must be an identifier or qualified name i".

-TS2717 (missing), 3 cases: 02-C15, 61-L15b, 99-V4. First: 02-C15 a.ts:1 "Subsequent property declarations must have
the same type.  Property '…' must be ".

-TS2726 (missing), 3 cases: 38-C3a, 38-C3b, 40-C5. First: 38-C3a a.ts:1 "Cannot find lib definition for '…'.".

+TS2749 (spurious), 3 cases: 30-P7a, 33-C6d, 33-C6e. First: 30-P7a a.ts:2 "'…' refers to a value, but is being used as
a type here. Did you mean '…'?".

-TS2749 (missing), 3 cases: 31-SC10, 92-R02h, 99-E1. First: 31-SC10 a.ts:1 "'…' refers to a value, but is being used as
a type here. Did you mean '…'?".

-TS2842 (missing), 3 cases: 28-P10b, 28-P11, 52-L01. First: 28-P10b a.ts:1 "'…' is an unused renaming of '…'. Did you
intend to use it as a type annotation?".

-TS2859 (missing), 3 cases: 17-SC1, 59-L01a, 87-R01b. First: 17-SC1 a.ts:5 "Excessive complexity comparing types '…'
and '…'.".

-TS2882 (missing), 3 cases: 35-C2a, 35-C2b, 35-C5a. First: 35-C2a a.ts:1 "Cannot find module or type declarations for
side-effect import of '…'.".

+TS2882 (spurious), 3 cases: 35-C9a, 35-C9b, 35-C9c. First: 35-C9a a.ts:1 "Cannot find module or type declarations for
side-effect import of '…'.".

+TS6046 (spurious), 3 cases: 37-C11a, 38-C4a, 38-SC5. First: 37-C11a tsconfig.json:1 "Argument for '…' option must be:
'…', '…', '…'.".

+TS7005 (spurious), 3 cases: 21-P1, 30-P11, 99-M1. First: 21-P1 a.ts:3 "Variable '…' implicitly has an '…' type.".

-TS7024 (missing), 3 cases: 75-R18c, 89-R14, 99-V4. First: 75-R18c a.ts:2 "Function implicitly has return type '…'
because it does not have a return type a".

+TS7033 (spurious), 3 cases: 11-C11, 59-L18, 80-R09b. First: 11-C11 a.ts:1 "Property '…' implicitly has type '…',
because its get accessor lacks a return ty".

-TS8029 (missing), 3 cases: 16-C15b, 77-R12a, 77-R12c. First: 16-C15b b.js:3 "JSDoc '…' tag has name '…', but there is
no parameter with that name. It would m".

-TS18012 (missing), 3 cases: 30-C7a, 30-C7b, 30-C7c. First: 30-C7a a.ts:1 "'…' is a reserved word.".

+TS18016 (spurious), 3 cases: 28-C24c, 71-R12a, 80-R01c. First: 28-C24c a.ts:1 "Private identifiers are not allowed
outside class bodies.".

+TS18048 (spurious), 3 cases: 21-C5b, 55-L01, 58-L05. First: 21-C5b a.ts:2 "'…' is possibly '…'.".

-TS18048 (missing), 3 cases: 28-P1, 32-C5b, 99-V4. First: 28-P1 a.ts:2 "'…' is possibly '…'.".

-TS2301 (missing), 2 cases: 77-R12c, 77-R12d. First: 77-R12c a.js:2 "Initializer of instance member variable '…' cannot
reference identifier '…' decl".

+TS2309 (spurious), 2 cases: 03-SC6, 95-J2. First: 03-SC6 a.d.ts:1 "An export assignment cannot be used in a module
with other exported elements.".

-TS2314 (missing), 2 cases: 56-L01, 99-V4. First: 56-L01 a.js:3 "Generic type '…' requires 1 type argument(s).".

-TS2316 (missing), 2 cases: 01-C4a, 53-L09b. First: 01-C4a a.ts:2 "Global type '…' must be a class or interface type.".

-TS2323 (missing), 2 cases: 08-C17, 58-L08b. First: 08-C17 a.ts:2 "Cannot redeclare exported variable '…'.".

+TS2343 (spurious), 2 cases: 34-C16, 90-R17c. First: 34-C16 a.ts:3 "This syntax requires an imported helper named '…'
which does not exist in '…'. C".

-TS2343 (missing), 2 cases: 40-C8b, 90-R17c. First: 40-C8b a.ts:2 "This syntax requires an imported helper named '…'
which does not exist in '…'. C".

+TS2347 (spurious), 2 cases: 63-L09, 99-V4. First: 63-L09 b.ts:2 "Untyped function calls may not accept type
arguments.".

-TS2351 (missing), 2 cases: 10-C8b, checks-of-a-value-assigned-on-a-loop-back-edge. First: 10-C8b a.ts:3 "This
expression is not constructable.".

+TS2351 (spurious), 2 cases: 73-R03a, 99-V4. First: 73-R03a a.ts:4 "This expression is not constructable.".

-TS2352 (missing), 2 cases: 18-C6a, 31-SC2. First: 18-C6a a.ts:2 "Conversion of type '…' to type '…' may be a mistake
because neither type suffici".

+TS2355 (spurious), 2 cases: 31-SC7, 58-L05. First: 31-SC7 a.ts:3 "A function whose declared type is neither '…', '…',
nor '…' must return a value.".

+TS2364 (spurious), 2 cases: 03-C22, 07-C17. First: 03-C22 a.ts:2 "The left-hand side of an assignment expression must
be a variable or a property ".

-TS2372 (missing), 2 cases: 15-C3c, 34-C2c. First: 15-C3c a.tsx:2 "Parameter '…' cannot reference itself.".

-TS2384 (missing), 2 cases: 02-C12, 28-C33a. First: 02-C12 a.ts:1 "Overload signatures must all be ambient or
non-ambient.".

-TS2391 (missing), 2 cases: 28-C19b, 43-C16d. First: 28-C19b a.ts:1 "Function implementation is missing or not
immediately following the declaration.".

-TS2403 (missing), 2 cases: 09-P2, 77-R17b. First: 09-P2 a.ts:2 "Subsequent variable declarations must have the same
type.  Variable '…' must be ".

-TS2405 (missing), 2 cases: 03-C22, 07-C17. First: 03-C22 a.ts:2 "The left-hand side of a '…' statement must be of type
'…' or '…'.".

-TS2412 (missing), 2 cases: 09-C3, 27-P2. First: 09-C3 a.ts:2 "Type '…' is not assignable to type '…' with '…'.
Consider adding '…' to the type".

-TS2430 (missing), 2 cases: 90-R09, 99-V4. First: 90-R09 a.ts:2 "Interface '…' incorrectly extends interface '…'.".

-TS2449 (missing), 2 cases: 01-C1, 90-R10b. First: 01-C1 a.ts:4 "Class '…' used before its declaration.".

-TS2450 (missing), 2 cases: 01-C1, 90-R10b. First: 01-C1 a.ts:2 "Enum '…' used before its declaration.".

+TS2456 (spurious), 2 cases: 12-P2, 99-V4. First: 12-P2 a.ts:1 "Type alias '…' circularly references itself.".

+TS2457 (spurious), 2 cases: 57-L02f, 61-L14. First: 57-L02f a.ts:3 "Type alias name cannot be '…'.".

+TS2464 (spurious), 2 cases: 33-C4b, 33-C4c. First: 33-C4b a.ts:2 "A computed property name must be of type '…', '…',
'…', or '…'.".

+TS2490 (spurious), 2 cases: 04-C9b, 99-V4. First: 04-C9b a.ts:4 "The type returned by the '…' method of an iterator
must have a '…' property.".

-TS2490 (missing), 2 cases: 99-V4, 99-V7. First: 99-V4 checkIteratedTypeOrElementType__06_for_of_then_spread.ts:7 "The
type returned by the '…' method of an iterator must have a '…' property.".

+TS2491 (spurious), 2 cases: 03-C22, 07-C17. First: 03-C22 a.ts:2 "The left-hand side of a '…' statement cannot be a
destructuring pattern.".

+TS2506 (spurious), 2 cases: 62-L13a, 99-V4. First: 62-L13a a.ts:5 "'…' is referenced directly or indirectly in its
own base expression.".

-TS2506 (missing), 2 cases: 89-R14, 99-V4. First: 89-R14 a.ts:5 "'…' is referenced directly or indirectly in its own
base expression.".

+TS2538 (spurious), 2 cases: 21-P1, 53-L04b. First: 21-P1 a.ts:3 "Type '…' cannot be used as an index type.".

+TS2542 (spurious), 2 cases: 05-C1b, 99-I5. First: 05-C1b a.ts:2 "Index signature in type '…' only permits reading.".

-TS2590 (missing), 2 cases: 14-P4, 99-V4. First: 14-P4 a.ts:6 "Expression produces a union type that is too complex to
represent.".

-TS2635 (missing), 2 cases: 06-C6, 51-L12. First: 06-C6 a.ts:2 "Type '…' has no signatures for which the type argument
list is applicable.".

-TS2662 (missing), 2 cases: 30-C17b, 51-L05. First: 30-C17b a.ts:1 "Cannot find name '…'. Did you mean the static
member '…'?".

+TS2678 (spurious), 2 cases: 21-C20a, 21-C20b. First: 21-C20a a.ts:4 "Type '…' is not comparable to type '…'.".

+TS2693 (spurious), 2 cases: 99-E1, value-named-like-a-type-parameter-and-a-member-of-the-class. First: 99-E1 m61.ts:2
"'…' only refers to a type, but is being used as a value here.".

+TS2694 (spurious), 2 cases: 33-P9a, 75-R02. First: 33-P9a a.js:1 "Namespace '…' has no exported member '…'.".

-TS2695 (missing), 2 cases: 07-C3, 32-C4. First: 07-C3 a.ts:3 "Left side of comma operator is unused and has no side
effects.".

-TS2697 (missing), 2 cases: 01-P3, 11-C6a. First: 01-P3 a.ts:15 "An async function or method must return a '…'. Make
sure you have a declaration ".

-TS2702 (missing), 2 cases: 90-R10a, 90-R10b. First: 90-R10a a.ts:2 "'…' only refers to a type, but is being used as a
namespace here.".

-TS2704 (missing), 2 cases: 53-L18, 99-V1. First: 53-L18 a.ts:8 "The operand of a '…' operator cannot be a read-only
property.".

+TS2715 (spurious), 2 cases: 06-C13, 25-C11. First: 06-C13 a.ts:1 "Abstract property '…' in class '…' cannot be
accessed in the constructor.".

-TS2719 (missing), 2 cases: 23-P3, 99-A1. First: 23-P3 a.ts:3 "Type '…' is not assignable to type '…'. Two different
types with this name exist".

+TS2746 (spurious), 2 cases: 27-C17, 43-C12b. First: 27-C17 a.tsx:3 "This JSX tag'…'children'…'number', but multiple
children were provided.".

-TS2746 (missing), 2 cases: 27-C2, 57-L13a. First: 27-C2 a.tsx:4 "This JSX tag'…'children'…'number', but multiple
children were provided.".

-TS2775 (missing), 2 cases: 60-L16b, 99-V4. First: 60-L16b a.ts:7 "Assertions require every name in the call target to
be declared with an explicit".

-TS2784 (missing), 2 cases: 24-C15, 63-L03b. First: 24-C15 a.ts:1 "'…' and '…' accessors cannot declare '…'
parameters.".

-TS2801 (missing), 2 cases: 71-R04b, checks-of-a-value-assigned-on-a-loop-back-edge. First: 71-R04b a.tsx:7 "This
condition will always return true since this '…' is always defined.".

-TS2835 (missing), 2 cases: 59-L09, 61-L05c. First: 59-L09 b.mts:1 "Relative import paths need explicit file extensions
in ECMAScript imports when '".

+TS2842 (spurious), 2 cases: 99-U1, 99-U2. First: 99-U1 abstract__own.ts:2 "'…' is an unused renaming of '…'. Did you
intend to use it as a type annotation?".

-TS2866 (missing), 2 cases: 34-C2d, 90-R10a. First: 34-C2d a.tsx:1 "Import '…' conflicts with global value used in this
file, so must be declared wi".

-TS2877 (missing), 2 cases: 08-P2, 39-C11c. First: 08-P2 a.ts:1 "This import uses a '…' extension to resolve to an
input TypeScript file, but wil".

-TS5010 (missing), 2 cases: 37-C16, 37-C4a. First: 37-C16 :0 "File specification cannot end in a recursive directory
wildcard ('…'): '…'.".

+TS5067 (spurious), 2 cases: 39-C17a, 39-C17b. First: 39-C17a tsconfig.json:1 "Invalid value for '…'. '…' is not a
valid identifier or qualified-name.".

-TS5114 (missing), 2 cases: 12-C7c, types-of-parameters-that-get-no-argument. First: 12-C7c a.ts:2 "Instantiations of
type '…' appear infinitely circular.".

-TS6142 (missing), 2 cases: 38-C5c, 38-P1b. First: 38-C5c ref/index.d.ts:1 "Module '…' was resolved to '…', but '…' is
not set.".

-TS6196 (missing), 2 cases: 34-C1, 88-R05a. First: 34-C1 a.ts:1 "'…' is declared but never used.".

-TS6504 (missing), 2 cases: 40-C7a, 40-C7c. First: 40-C7a a.ts:1 "File '…' is a JavaScript file. Did you mean to enable
the '…' option?".

-TS7005 (missing), 2 cases: 79-R11, 99-V4. First: 79-R11 j.js:3 "Variable '…' implicitly has an '…' type.".

-TS7016 (missing), 2 cases: 38-C5b, 41-P3b. First: 38-C5b ref/out/index.d.ts:1 "Could not find a declaration file for
module '…'. '…' implicitly has an '…' type".

+TS7016 (spurious), 2 cases: 40-C8b, 86-R08. First: 40-C8b a.ts:2 "Could not find a declaration file for module '…'.
'…' implicitly has an '…' type".

+TS7027 (spurious), 2 cases: 21-C20b, 31-SC12. First: 21-C20b a.ts:5 "Unreachable code detected.".

+TS7031 (spurious), 2 cases: 51-L19b, 78-R20b. First: 51-L19b a.ts:1 "Binding element '…' implicitly has an '…'
type.".

+TS8038 (spurious), 2 cases: 57-L01, 57-L02a. First: 57-L01 a.ts:2 "Decorators may not appear after '…' or '…' if they
also appear before '…'.".

+TS18033 (spurious), 2 cases: 52-L15b, 99-V4. First: 52-L15b a.ts:2 "Type '…' is not assignable to type '…' as
required for computed enum member valu".

+TS0 (spurious), 1 case: 56-L06. First: 56-L06 lt2.tsx:1 "Expected function.".

-TS2206 (missing), 1 case: 28-C6c. First: 28-C6c b.js:1 "The '…' modifier cannot be used on a named import when '…' is
used on its import".

-TS2308 (missing), 1 case: 09-P1. First: 09-P1 m.ts:3 "Module "…" has already exported a member named '…'. Consider
explicitly re-expor".

+TS2310 (spurious), 1 case: 99-V4. First: 99-V4
getBaseConstructorTypeOfClass__07_class_in_loop_extends_call_on_loop_variable.ts:6 "Type '…' recursively references
itself as a base type.".

+TS2318 (spurious), 1 case: 01-C15. First: 01-C15 :0 "Cannot find global type '…'.".

-TS2320 (missing), 1 case: 99-V4. First: 99-V4
resolveStructuredTypeMembers__05_interface_extends_mapped_type_over_own_keys.ts:5 "Interface '…' cannot simultaneously
extend types '…' and '…'.".

+TS2323 (spurious), 1 case: 03-SC3. First: 03-SC3 a.ts:3 "Cannot redeclare exported variable '…'.".

-TS2338 (missing), 1 case: 04-C15. First: 04-C15 a.ts:2 "'…' property access is permitted only in a constructor, member
function, or memb".

-TS2349 (missing), 1 case: checks-of-a-value-assigned-on-a-loop-back-edge. First:
checks-of-a-value-assigned-on-a-loop-back-edge a5.ts:8 "This expression is not callable.".

-TS2354 (missing), 1 case: 61-L08. First: 61-L08 a.ts:4 "This syntax requires an imported helper but module '…' cannot
be found.".

-TS2358 (missing), 1 case: checks-of-a-value-assigned-on-a-loop-back-edge. First:
checks-of-a-value-assigned-on-a-loop-back-edge a4.ts:21 "The left-hand side of an '…' expression must be of type '…',
an object type or a".

+TS2363 (spurious), 1 case: 43-X6a. First: 43-X6a a.ts:2 "The right-hand side of an arithmetic operation must be of
type '…', '…', '…' or ".

-TS2365 (missing), 1 case: checks-of-a-value-assigned-on-a-loop-back-edge. First:
checks-of-a-value-assigned-on-a-loop-back-edge a2.ts:19 "Operator '…' cannot be applied to types '…' and '…'.".

-TS2366 (missing), 1 case: 21-C20a. First: 21-C20a a.ts:1 "Function lacks ending return statement and return type does
not include '…'.".

+TS2366 (spurious), 1 case: 99-V4. First: 99-V4
checkAccessorDeclaration__04_getter_annotated_with_tuple_of_own_property.ts:4 "Function lacks ending return statement
and return type does not include '…'.".

+TS2367 (spurious), 1 case: 99-V4. First: 99-V4 getDeclaredTypeOfEnum__02_values_first_computed_for_another_enum.ts:16
"This comparison appears to be unintentional because the types '…' and '…' have n".

-TS2370 (missing), 1 case: 15-C15. First: 15-C15 a.ts:1 "A rest parameter must be of an array type.".

+TS2372 (spurious), 1 case: 01-C7. First: 01-C7 a.ts:2 "Parameter '…' cannot reference itself.".

-TS2378 (missing), 1 case: 31-SC12. First: 31-SC12 a.ts:1 "A '…' accessor must return a value.".

-TS2389 (missing), 1 case: 02-C20a. First: 02-C20a a.ts:2 "Function implementation name must be '…'.".

+TS2391 (spurious), 1 case: 28-C33e. First: 28-C33e a.ts:1 "Function implementation is missing or not immediately
following the declaration.".

-TS2394 (missing), 1 case: 02-C8. First: 02-C8 a.ts:1 "This overload signature is not compatible with its
implementation signature.".

-TS2395 (missing), 1 case: 04-P3. First: 04-P3 b.js:1 "Individual declarations in merged declaration '…' must be all
exported or all lo".

+TS2403 (spurious), 1 case: 83-R01c. First: 83-R01c k02.ts:1 "Subsequent variable declarations must have the same
type.  Variable '…' must be ".

+TS2404 (spurious), 1 case: 28-C25b. First: 28-C25b a.ts:1 "The left-hand side of a '…' statement cannot use a type
annotation.".

-TS2411 (missing), 1 case: 03-SC1. First: 03-SC1 a.ts:2 "Property '…' of type '…' is not assignable to '…' index type
'…'.".

+TS2411 (spurious), 1 case: 71-R15c. First: 71-R15c a.ts:5 "Property '…' of type '…' is not assignable to '…' index
type '…'.".

+TS2412 (spurious), 1 case: 17-P4. First: 17-P4 a.ts:2 "Type '…' is not assignable to type '…' with '…'. Consider
adding '…' to the type".

+TS2413 (spurious), 1 case: 71-R15c. First: 71-R15c a.ts:7 "'…' index type '…' is not assignable to '…' index type
'…'.".

-TS2415 (missing), 1 case: 99-V1. First: 99-V1 private_class__literal_keys.ts:227 "Class '…' incorrectly extends base
class '…'.".

-TS2416 (missing), 1 case: 17-SC2. First: 17-SC2 a.ts:5 "Property '…' in type '…' is not assignable to the same
property in base type '…'".

-TS2417 (missing), 1 case: 71-R15b. First: 71-R15b a.ts:3 "Class static side '…' incorrectly extends base class static
side '…'.".

+TS2427 (spurious), 1 case: 61-L14. First: 61-L14 b.ts:1 "Interface name cannot be '…'.".

-TS2427 (missing), 1 case: 96-SOLID1. First: 96-SOLID1 b.ts:8 "Interface name cannot be '…'.".

-TS2428 (missing), 1 case: 62-L13b. First: 62-L13b :0 "All declarations of '…' must have identical type parameters.".

-TS2435 (missing), 1 case: 33-C14b. First: 33-C14b j.js:2 "Ambient modules cannot be nested in other modules or
namespaces.".

-TS2445 (missing), 1 case: 60-L19b. First: 60-L19b a.ts:7 "Property '…' is protected and only accessible within class
'…' and its subclasse".

+TS2445 (spurious), 1 case: 99-V1. First: 99-V1 protected_writable_class__plus_readonly.ts:203 "Property '…' is
protected and only accessible within class '…' and its subclasse".

+TS2448 (spurious), 1 case: 28-C24b. First: 28-C24b a.ts:1 "Block-scoped variable '…' used before its declaration.".

-TS2457 (missing), 1 case: 96-SOLID1. First: 96-SOLID1 b.ts:9 "Type alias name cannot be '…'.".

+TS2459 (spurious), 1 case: 71-R07c. First: 71-R07c c.ts:1 "Module '…' declares '…' locally, but it is not exported.".

-TS2460 (missing), 1 case: 71-R07c. First: 71-R07c c.ts:1 "Module '…' declares '…' locally, but it is exported as
'…'.".

-TS2464 (missing), 1 case: 28-C28. First: 28-C28 a.ts:1 "A computed property name must be of type '…', '…', '…', or
'…'.".

-TS2473 (missing), 1 case: 30-C8b. First: 30-C8b a.ts:2 "Enum declarations must all be const or non-const.".

-TS2481 (missing), 1 case: 03-C7. First: 03-C7 a.ts:1 "Cannot initialize outer scoped variable '…' in the same scope as
block scoped de".

+TS2492 (spurious), 1 case: 03-C19. First: 03-C19 a.ts:1 "Cannot redeclare identifier '…' in catch clause.".

-TS2504 (missing), 1 case: 57-L13c. First: 57-L13c a.ts:5 "Type '…' must have a '…' method that returns an async
iterator.".

+TS2507 (spurious), 1 case: 99-V4. First: 99-V4
getBaseConstructorTypeOfClass__01_function_merged_with_self_extending_class.ts:4 "Type '…' is not a constructor
function type.".

-TS2507 (missing), 1 case: 99-V4. First: 99-V4
resolveAnonymousTypeMembers__03_static_index_signature_reads_derived_static_base_first.ts:7 "Type '…' is not a
constructor function type.".

-TS2509 (missing), 1 case: 99-V1. First: 99-V1 as_const__alias_minus_readonly.ts:164 "Base constructor return type '…'
is not an object type or intersection of object".

-TS2511 (missing), 1 case: 80-R08. First: 80-R08 a.ts:5 "Cannot create an instance of an abstract class.".

-TS2512 (missing), 1 case: 28-C33h. First: 28-C33h a.ts:1 "Overload signatures must all be abstract or non-abstract.".

+TS2512 (spurious), 1 case: 28-C33i. First: 28-C33i a.ts:1 "Overload signatures must all be abstract or
non-abstract.".

-TS2515 (missing), 1 case: 03-C1a. First: 03-C1a a.ts:3 "Non-abstract class '…' does not implement inherited abstract
member m from class".

+TS2524 (spurious), 1 case: 78-R07. First: 78-R07 a.ts:1 "'…' expressions cannot be used in a parameter initializer.".

-TS2528 (missing), 1 case: 51-L06b. First: 51-L06b a.ts:5 "A module cannot have multiple default exports.".

+TS2532 (spurious), 1 case: 21-C24. First: 21-C24 a.ts:2 "Object is possibly '…'.".

-TS2532 (missing), 1 case: 55-L12d. First: 55-L12d a.ts:4 "Object is possibly '…'.".

-TS2536 (missing), 1 case: 99-V1. First: 99-V1 protected_writable_class__plus_readonly.ts:225 "Type '…' cannot be used
to index type '…'.".

-TS2537 (missing), 1 case: 99-V4. First: 99-V4 getTypeFromTypeNode__05_interface_index_signature_reads_own_index.ts:4
"Type '…' has no matching index signature for type '…'.".

+TS2545 (spurious), 1 case: 10-C16. First: 10-C16 a.ts:2 "A mixin class must have a constructor with a single rest
parameter of type '…'.".

-TS2559 (missing), 1 case: 99-V4. First: 99-V4 lateBindMember__02_expando_key_in_loop_own_assignment.ts:9 "Type '…' has
no properties in common with type '…'.".

+TS2564 (spurious), 1 case: 31-SC7. First: 31-SC7 a.ts:6 "Property '…' has no initializer and is not definitely
assigned in the constructo".

-TS2564 (missing), 1 case: 60-L05. First: 60-L05 a.ts:1 "Property '…' has no initializer and is not definitely assigned
in the constructo".

+TS2565 (spurious), 1 case: 79-R11. First: 79-R11 j.js:4 "Property '…' is used before being assigned.".

-TS2567 (missing), 1 case: 54-L02b. First: 54-L02b a.ts:1 "Enum declarations can only merge with namespace or other
enum declarations.".

+TS2577 (spurious), 1 case: 11-C2d. First: 11-C2d a.ts:1 "Return type annotation circularly references itself.".

-TS2577 (missing), 1 case: 99-V4. First: 99-V4 getReturnTypeOfSignature__03_union_signature_cycle.ts:3 "Return type
annotation circularly references itself.".

-TS2578 (missing), 1 case: 39-C12b. First: 39-C12b a.ts:1 "Unused '…' directive.".

-TS2584 (missing), 1 case: 38-C3a. First: 38-C3a a.ts:2 "Cannot find name '…'. Do you need to change your target
library? Try changing th".

+TS2584 (spurious), 1 case: 38-C3c. First: 38-C3c a.ts:2 "Cannot find name '…'. Do you need to change your target
library? Try changing th".

+TS2590 (spurious), 1 case: 17-C17. First: 17-C17 a.ts:6 "Expression produces a union type that is too complex to
represent.".

-TS2608 (missing), 1 case: 27-C15b. First: 27-C15b a.tsx:1 "The global type '…' may not have more than one property.".

-TS2612 (missing), 1 case: 03-C10. First: 03-C10 a.ts:4 "Property '…' will overwrite the base property in '…'. If this
is intentional, ad".

+TS2612 (spurious), 1 case: 03-SC7. First: 03-SC7 a.ts:2 "Property '…' will overwrite the base property in '…'. If
this is intentional, ad".

-TS2615 (missing), 1 case: 99-I2. First: 99-I2 a.ts:7 "Type of property '…' circularly references itself in mapped type
'…'.".

+TS2649 (spurious), 1 case: 54-L02b. First: 54-L02b a.ts:1 "Cannot augment module '…' with value exports because it
resolves to a non-module".

-TS2654 (missing), 1 case: 59-L19b. First: 59-L19b a.ts:4 "Non-abstract class '…' is missing implementations for the
following members of '".

+TS2662 (spurious), 1 case: 01-C8. First: 01-C8 a.ts:2 "Cannot find name '…'. Did you mean the static member '…'?".

-TS2663 (missing), 1 case: value-named-like-a-type-parameter-and-a-member-of-the-class. First:
value-named-like-a-type-parameter-and-a-member-of-the-class a.ts:21 "Cannot find name '…'. Did you mean the instance
member '…'?".

+TS2664 (spurious), 1 case: 33-C14b. First: 33-C14b j.js:2 "Invalid module name in augmentation, module '…' cannot be
found.".

-TS2668 (missing), 1 case: 30-C5b. First: 30-C5b a.ts:1 "'…' modifier cannot be applied to ambient modules and module
augmentations since".

-TS2669 (missing), 1 case: 33-C14c. First: 33-C14c j.js:2 "Augmentations for the global scope can only be directly
nested in external modul".

-TS2671 (missing), 1 case: 33-C16b. First: 33-C16b c.ts:2 "Cannot augment module '…' because it resolves to a
non-module entity.".

-TS2676 (missing), 1 case: 02-C13c. First: 02-C13c a.ts:1 "Accessors must both be abstract or non-abstract.".

-TS2689 (missing), 1 case: 06-C16. First: 06-C16 a.ts:2 "Cannot extend an interface '…'. Did you mean '…'?".

-TS2694 (missing), 1 case: 08-C4. First: 08-C4 a.ts:2 "Namespace '…' has no exported member '…'.".

-TS2699 (missing), 1 case: 02-C14a. First: 02-C14a a.ts:1 "Static property '…' conflicts with built-in property '…' of
constructor function".

-TS2700 (missing), 1 case: 99-V1. First: 99-V1 private_class__literal_keys.ts:93 "Rest types may only be created from
object types.".

+TS2705 (spurious), 1 case: 87-R20a. First: 87-R20a a.ts:1 "An async function or method in ES5 requires the '…'
constructor.  Make sure you ".

+TS2708 (spurious), 1 case: 33-C15. First: 33-C15 a.ts:2 "Cannot use namespace '…' as a value.".

-TS2713 (missing), 1 case: 80-R18b. First: 80-R18b b.ts:4 "Cannot access '…' because '…' is a type, but not a
namespace. Did you mean to re".

+TS2716 (spurious), 1 case: 99-V4. First: 99-V4
getDefaultFromTypeParameter__06_two_interfaces_default_to_each_other.ts:3 "Type parameter '…' has a circular
default.".

+TS2717 (spurious), 1 case: 76-R05c. First: 76-R05c a.ts:4 "Subsequent property declarations must have the same type.
Property '…' must be ".

-TS2724 (missing), 1 case: 86-R16. First: 86-R16 use.ts:1 "'…' has no exported member named '…'. Did you mean '…'?".

-TS2731 (missing), 1 case: 05-C4. First: 05-C4 a.ts:3 "Implicit conversion of a '…' to a '…' will fail at runtime.
Consider wrapping th".

+TS2739 (spurious), 1 case: 10-SC1. First: 10-SC1 a.ts:6 "Type '…' is missing the following properties from type '…':
k, p, q".

+TS2744 (spurious), 1 case: 04-C11. First: 04-C11 a.ts:1 "Type parameter defaults can only reference previously
declared type parameters.".

+TS2745 (spurious), 1 case: 57-L13a. First: 57-L13a a.tsx:7 "This JSX tag'…'children'…'number' which requires multiple
children, but only a s".

+TS2748 (spurious), 1 case: 04-C14. First: 04-C14 a.ts:2 "Cannot access ambient const enums when '…' is enabled.".

-TS2748 (missing), 1 case: 99-B4. First: 99-B4 a.ts:4 "Cannot access ambient const enums when '…' is enabled.".

-TS2767 (missing), 1 case: 04-C2a. First: 04-C2a a.ts:2 "The '…' property of an iterator must be a method.".

+TS2767 (spurious), 1 case: 93-R19a. First: 93-R19a a.ts:3 "The '…' property of an iterator must be a method.".

-TS2769 (missing), 1 case: 13-P4b. First: 13-P4b a.ts:2 "No overload matches this call.".

+TS2775 (spurious), 1 case: 60-L16a. First: 60-L16a a.ts:5 "Assertions require every name in the call target to be
declared with an explicit".

+TS2776 (spurious), 1 case: 31-SC1. First: 31-SC1 a.ts:6 "Assertions require the call target to be an identifier or
qualified name.".

+TS2786 (spurious), 1 case: 27-C11. First: 27-C11 a.tsx:3 "'…' cannot be used as a JSX component.".

+TS2790 (spurious), 1 case: 53-L18. First: 53-L18 a.ts:8 "The operand of a '…' operator must be optional.".

-TS2790 (missing), 1 case: 99-V1. First: 99-V1 getter__partial.ts:14 "The operand of a '…' operator must be optional.".

-TS2794 (missing), 1 case: 58-L05. First: 58-L05 a.ts:6 "Expected 1 arguments, but got 0. Did you forget to include '…'
in your type argu".

-TS2799 (missing), 1 case: 33-C13. First: 33-C13 a.ts:3 "Type produces a tuple type that is too large to represent.".

-TS2808 (missing), 1 case: 02-C13a. First: 02-C13a a.ts:1 "A get accessor must be at least as accessible as the
setter".

-TS2813 (missing), 1 case: 30-P17. First: 30-P17 f.js:1 "Class declaration cannot implement overload list for '…'.".

-TS2814 (missing), 1 case: 30-P17. First: 30-P17 f.js:2 "Function with bodies can only merge with classes that are
ambient.".

+TS2815 (spurious), 1 case: 77-R12d. First: 77-R12d a.ts:1 "'…' cannot be referenced in property initializers or class
static initialization".

+TS2819 (spurious), 1 case: 61-L14. First: 61-L14 a.ts:1 "Namespace name cannot be '…'.".

-TS2834 (missing), 1 case: 59-L09. First: 59-L09 sub/a.mts:3 "Relative import paths need explicit file extensions in
ECMAScript imports when '".

+TS2834 (spurious), 1 case: 86-R11c. First: 86-R11c node_modules/@typescript/lib-dom/index.d.ts:1 "Relative import
paths need explicit file extensions in ECMAScript imports when '".

+TS2846 (spurious), 1 case: 59-L06. First: 59-L06 w*d/a.ts:2 "A declaration file cannot be imported without '…'. Did
you mean to import an imp".

+TS2859 (spurious), 1 case: 87-R01b. First: 87-R01b a.ts:6 "Excessive complexity comparing types '…' and '…'.".

+TS2874 (spurious), 1 case: 01-P5. First: 01-P5 a.tsx:3 "This JSX tag requires '…' to be in scope, but it could not be
found.".

+TS2883 (spurious), 1 case: 59-L10. First: 59-L10 a.ts:2 "The inferred type of '…' cannot be named without a reference
to '…' from '…'. Th".

-TS2883 (missing), 1 case: inferred-type-is-a-module-in-a-nested-node-modules. First:
inferred-type-is-a-module-in-a-nested-node-modules a.ts:4 "The inferred type of '…' cannot be named without a
reference to '…' from '…'. Th".

+TS4105 (spurious), 1 case: 99-V1. First: 99-V1 protected_writable_class__plus_readonly.ts:225 "Private or protected
member '…' cannot be accessed on a type parameter.".

-TS5023 (missing), 1 case: 41-C8b. First: 41-C8b base.json:1 "Unknown compiler option '…'.".

-TS5024 (missing), 1 case: 37-C6. First: 37-C6 tsconfig.json:1 "Compiler option '…' requires a value of type string or
Array.".

+TS5024 (spurious), 1 case: 37-C7. First: 37-C7 tsconfig.json:1 "Compiler option '…' requires a value of type Array.".

-TS5055 (missing), 1 case: 39-C6b. First: 39-C6b :0 "Cannot write file '…' because it would overwrite input file.".

-TS5056 (missing), 1 case: 39-C6a. First: 39-C6a :0 "Cannot write file '…' because it would be overwritten by multiple
input files.".

-TS5061 (missing), 1 case: 30-C5a. First: 30-C5a a.ts:1 "Pattern '…' can have at most one '…' character.".

-TS5065 (missing), 1 case: 37-C4b. First: 37-C4b :0 "File specification cannot contain a parent directory ('…') that
appears after a ".

-TS5097 (missing), 1 case: 42-C11. First: 42-C11 a.ts:1 "An import path can only end with a '…' extension when '…' is
enabled.".

+TS5114 (spurious), 1 case: types-of-parameters-that-get-no-argument. First: types-of-parameters-that-get-no-argument
argument_11.ts:7 "Instantiations of type '…' appear infinitely circular.".

-TS6053 (missing), 1 case: 37-C3c. First: 37-C3c tsconfig.json:1 "File '…' not found.".

-TS6137 (missing), 1 case: 61-L05b. First: 61-L05b a.tsx:1 "Cannot import type declaration files. Consider importing
'…' instead of '…'.".

-TS6192 (missing), 1 case: 02-C5. First: 02-C5 a.ts:1 "All imports in import declaration are unused.".

+TS6198 (spurious), 1 case: 83-R10a. First: 83-R10a a.ts:11 "All destructured elements are unused.".

-TS6234 (missing), 1 case: 54-L10. First: 54-L10 a.ts:2 "This expression is not callable because it is a '…' accessor.
Did you mean to us".

-TS6263 (missing), 1 case: 08-C8. First: 08-C8 a.mts:1 "Module '…' was resolved to '…', but '…' is not set.".

+TS6307 (spurious), 1 case: 38-C5a. First: 38-C5a ref/index.ts:1 "File '…' is not listed within the file list of
project '…'. Projects must list a".

-TS6307 (missing), 1 case: 86-R11b. First: 86-R11b :0 "File '…' is not listed within the file list of project '…'.
Projects must list a".

-TS7010 (missing), 1 case: 06-P8. First: 06-P8 a.ts:1 "'…', which lacks return-type annotation, implicitly has an '…'
return type.".

-TS7011 (missing), 1 case: 99-V4. First: 99-V4 getTypeFromBindingPattern__04_called_arrow_in_default_reads_sibling.ts:3
"Function expression, which lacks return-type annotation, implicitly has an '…' r".

-TS7015 (missing), 1 case: 05-C6. First: 05-C6 a.ts:3 "Element implicitly has an '…' type because index expression is
not of type '…'.".

-TS7018 (missing), 1 case: 54-L20b. First: 54-L20b a.ts:11 "Object literal'…'payload'…'any' type.".

-TS7026 (missing), 1 case: 27-P4b. First: 27-P4b a.tsx:2 "JSX element implicitly has type '…' because no interface '…'
exists.".

-TS7027 (missing), 1 case: 21-C14b. First: 21-C14b a.ts:2 "Unreachable code detected.".

-TS7032 (missing), 1 case: 33-C17b. First: 33-C17b a.ts:1 "Property '…' implicitly has type '…', because its set
accessor lacks a parameter".

-TS7034 (missing), 1 case: 79-R11. First: 79-R11 j.js:3 "Variable '…' implicitly has type '…' in some locations where
its type cannot be ".

-TS7036 (missing), 1 case: 05-C7. First: 05-C7 a.ts:1 "Dynamic import'…'string'…'number'.".

-TS8009 (missing), 1 case: 37-C7. First: 37-C7 tsconfig.json:1 "The '…' modifier can only be used in TypeScript
files.".

-TS8012 (missing), 1 case: 84-R01. First: 84-R01 b.js:2 "Parameter modifiers can only be used in TypeScript files.".

+TS8017 (spurious), 1 case: 61-L17b. First: 61-L17b a.js:1 "Signature declarations can only be used in TypeScript
files.".

-TS8017 (missing), 1 case: 80-R09b. First: 80-R09b a.js:1 "Signature declarations can only be used in TypeScript
files.".

-TS8026 (missing), 1 case: 56-L01. First: 56-L01 a.js:9 "Expected A<T> type arguments; provide these with an '…' tag.".

+TS8026 (spurious), 1 case: 56-L20b. First: 56-L20b a.js:10 "Expected A<T> type arguments; provide these with an '…'
tag.".

+TS8029 (spurious), 1 case: 16-C15a. First: 16-C15a b.js:4 "JSDoc '…' tag has name '…', but there is no parameter with
that name. It would m".

+TS17008 (spurious), 1 case: 56-L05b. First: 56-L05b a.tsx:1 "JSX element '…' has no corresponding closing tag.".

+TS17011 (spurious), 1 case: 04-C15. First: 04-C15 a.ts:2 "'…' must be called before accessing a property of '…' in
the constructor of a de".

-TS18002 (missing), 1 case: 37-C6. First: 37-C6 tsconfig.json:1 "The '…' list in config file '…' is empty.".

-TS18003 (missing), 1 case: 37-C14. First: 37-C14 :0 "No inputs were found in config file '…'. Specified '…' paths were
'…' and '…' pa".

-TS18004 (missing), 1 case: 44-C3. First: 44-C3 a.ts:2 "No value exists in scope for the shorthand property '…'. Either
declare one or p".

+TS18012 (spurious), 1 case: 80-R01c. First: 80-R01c a.ts:1 "'…' is a reserved word.".

-TS18016 (missing), 1 case: 56-L19. First: 56-L19 a.ts:3 "Private identifiers are not allowed outside class bodies.".

+TS18041 (spurious), 1 case: 28-C26f. First: 28-C26f a.ts:1 "A '…' statement cannot be used inside a class static
block.".

-TS18046 (missing), 1 case: 31-SC1. First: 31-SC1 a.ts:3 "'…' is of type '…'.".

-TS18047 (missing), 1 case: 54-L20b. First: 54-L20b a.ts:3 "'…' is possibly '…'.".
