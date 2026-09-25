# Plan 21: display fidelity (types and symbols baselines)

## Why
On 2026-09-24 (after plan 15 session 1, suite 5471) **2621 tests pass their errors but fail types** (291 of them JS)
and **1827 pass errors but fail symbols** (199 JS). That's the biggest pool in the suite, and what the types baseline
prints is also what users read in error messages. No single mechanism is huge, but four of them are architectural
(they need something tttt doesn't have), and those are most of it. Don't fit more rules into `ty_display.c`: build the
missing pieces.

Measure with `./displaydiff.py` (after a full `./failstats.py`): `--symbols` for the symbols component, `-k KIND
--names` for the tests one kind alone fails. Its "tests fixed by one kind" table is the ranking metric (tests whose
every mismatching line has that kind), JS tests counted apart (JS/JSDoc semantics are their own plan).

## Where the failures are (2026-09-24, TS tests fixed by one kind alone; lines/tests with the kind anywhere)
Types (2621 tests):
| kind | alone | anywhere (lines / tests) | mechanism |
|---|---|---|---|
| underlines/order only | 263 | 280 tests | mostly reuse marking (`^^^` where tsc reuses a part), a few file/blank-line layout issues (files with only `/// <reference>` lines, blank lines) |
| line-extra | 154 | 874 / 278 | `>import : any` for the `import` of `import("m")` (104 tests!), `> : true` for an empty `for(;;)` condition (31 tests), arbitrary namespace identifiers |
| our-typeof | 97 | 279 / 186 | tsc prints the *declared* type for identifiers in `export = X` and on the right of `import x = A.B` (checker.ts getTypeOfNode, isInRightSideOfImportOrExportAssignment); we print `typeof X`. Also mixins |
| tsc-typeof | 87 (+42 with class-expression) | 2455 / 293 | annotation reuse: tsc reprints a written `typeof a` (in parameter/return annotations of signatures); `typeof f` for the type of a function with expando members / `new.target`; `typeof C` for class expressions |
| line-missing | 76 | 976 / 242 | `>global : typeof global`, import attribute keys (`type`), `>true : true` in some statements, lines of other file kinds |
| overloads | 55 | 578 / 101 | overload lists printed as one signature (a type-core gap more than display: check each) |
| T_1-rename | 54 | 363 / 95 | tsc's GenerateNamesForShadowedTypeParams (`<A>(...) ... <A_1>(...)`) |
| import()-qualification | 43 (+36 tsc-import()) | 632 / 81 | symbol accessibility: we print `import("m").A` where an alias names it (`Widgets.A`), and the other way round |
| union-order | 39 | 529 / 87 | type ids created in a different order than tsc's; case by case |
| other | 599 | 5467 / 1130 | heterogeneous: many are type-core bugs, not display (inference, index keys of computed names, `P<1>` vs `P<"1">`); display ones inside: alias/import renames (`typeof a` vs `typeof A`, `m1_im2_private.c1`), `ReadonlyArray<T>`, `(() => S)`, `keyof Thing`, `Omit<O, "p">` (all annotation reuse), method-vs-property form of members, `"new"` quoting |
| we-any | 96 (+48 JS) | 3628 / 466 | we print `any`: unresolved imports, JS, missing features. Not display; leave to the plans that own the causes |
Symbols (1827 tests):
| kind | alone | mechanism |
|---|---|---|
| name | 470 | symbol qualification: tsc's `symbolToString(symbol, node.parent)` qualifies only as far as needed from the use site (getAccessibleSymbolChain); we print the full container chain (`Editor.ListFactory.RemoveEntry` for `ListFactory.RemoveEntry`), or not enough (`p9` for `ExpandoMerge.p9`) |
| line-missing | 253 (+87 JS) | symbol lines we don't produce: `this` (its class), `arguments`, `global`, `__index` for element access through an index signature, destructuring property names, some import forms |
| decls-differ / decls-missing | 157 + 109 | declaration lists: positions (tsc's node start includes modifiers like `declare`; a binding element with a property name starts at the property name), merged declarations (method overloads, interface merges, `T` of merged declarations: tttt has one symbol per declaration there) |
| underlines/order only | 99 | order of lines |

## Stages (in order; each is a commit with numbers, `./fasttest.sh -s` before committing)

### Stage 0: traversal fixes (cheap, measured first)
In `src/test/baseline_test_traversal.c`: no line for the `import` keyword of `import("m")` calls; no line for an absent
`for` condition (the parser synthesizes `true`: mark it synthetic, or skip spans that are empty); `>global` for `declare
global`; import attribute keys; whatever else the `line-extra`/`line-missing` samples show that is traversal, not
types (`python3` over `build/failstats/dump` like displaydiff's `lines()` pairing, grouped by the shape of the LHS).
Expect ~100-150 types components.

### Stage 1: declared types on the right of `import x = A.B` and in `export = X`
tsc's getTypeOfNode: those identifiers are not expressions; they print `getDeclaredTypeOfSymbol` (a class's instance
type, an interface) unless that is the error type, then the value type. Port that in the traversal. The `our-typeof`
bucket; check the mixin cases there separately (they're class expressions, stage 4).

### Stage 2: symbol accessibility (both baselines), the biggest one
tsc prints symbols and names in types relative to a location: `symbolToString(symbol, node.parent)` in the symbols
writer, `typeToTypeNode(type, node.parent, ...)` in the types writer. The qualification is getAccessibleSymbolChain:
the shortest chain of containers (or an alias that names the symbol) accessible from the location, with the meaning
of the position. `out_symbol_name()` in `ty/ty_display.c` is a simplified version that knows file and namespace
scopes only (the traversal tracks those), and the symbols baseline prints full chains.
1. The traversal needs the scope at each node. Scopes of blocks and functions aren't kept (`documentation.md`,
   "Symbols, tables, scopes"). Decide: keep them (a `Scope*` on each function/block, or on each identifier the resolve
   walk visits), or record them only in the test build (a side table in `baseline_test_traversal`, filled by a second
   resolve-like walk). Resolution from a location must follow tsc's rules (locals before exports, meaning, aliases).
   Whatever is chosen must not cost the CLI: it doesn't print relative names (error messages are fully qualified).
2. Port getAccessibleSymbolChain + the qualification rules of symbolToString/typeToString (`SymbolFormatFlags`,
   `UseOnlyExternalAliasing`, `import("m")` only when nothing accessible names the module, `globalThis.` for a
   shadowed global, `typeof` chains). Use it for the symbols baseline and in `ty_display.c`.
3. Measure `name` (symbols), `import()-qualification`/`tsc-import()` and the alias renames in `other` (types).

### Stage 3: annotation reuse by reprinting the node
tsc's node builder reuses a declaration's type annotation (serializeTypeForDeclaration / tryReuseExistingTypeNode)
when the annotation's type is the type being printed and every name in it is accessible from the print location
(visitExistingNodeTreeSymbols), and then prints *the node*: `Array<string>`, `ReadonlyArray<T>`, `typeof a`, `(() =>
S)`, `keyof Thing`, `Omit<O, "p">` stay as written. tttt decides reuse (`reuses()`, `annotation_accessible()`) but
prints the `Ty` and only marks the underline (hacks.md "Types baseline underlines").
1. A TypeNode printer with tsc's emitter layout (`{ a: string; }`, `(x: T) => U`, parentheses as written, qualified
   names, type arguments, tuples with names and `?`/`...`, template literal types, conditional, mapped, indexed
   access, `typeof x` with type arguments, `import("m").A`, literal types, `unique symbol`, `this`, predicates).
   The legacy `display.c` prints TypeNodes too; check whether it can be the base or should go.
2. Print reused annotations with it, in the places tsc serializes declarations: signature parameters and return
   types (the biggest: `tsc-typeof`), variable/property annotations, type parameter constraints. Keep the reuse
   decision tsc's: the node's type must equal the printed type (instantiated signatures don't reuse, see hacks.md).
3. Accessibility of the annotation's names becomes stage 2's resolution (all names, `typeof x` too, type parameters
   in scope), replacing `annotation_accessible()`'s first-name check.
4. Measure `tsc-typeof`, `Array<>-reuse`, the underline-only bucket, and the reuse cases in `other`.

### Stage 4: class expressions and member symbols (triage.md "Class expressions and member symbols")
A class expression's constructor type prints `{ new (): ; prototype: ; }` (tsc `typeof C`, `typeof (Anonymous
class)`), its instance type prints empty, messages say `class ''`. tsc names the class symbol after where it is
(`const C = class {}` -> `C`, a property assignment -> the property name, else `(Anonymous class)`). Also the symbols
baseline's qualification of parameter properties (`P.q`) and enum members referenced in initializers. The
`class-expression` bucket (86 tests, 42 alone with tsc-typeof).

### Stage 5: `T_1` renaming
tsc's GenerateNamesForShadowedTypeParams (the types writer passes it): a type parameter whose name is already taken in
the printed type (overload lists, nested signatures, `createMCache : <T_1>() => MCache<T_1>` inside a scope that has a
`T`) is printed `T_1`, `T_2`. Port the rule (the node builder's typeParameterNamesByText / scope walk). 54 alone.

### Stage 6: symbols declarations and missing symbol lines
Declaration positions (modifiers are part of the node: `declare function` starts at `declare`; binding elements),
merged declaration lists (overloads of methods, merged interfaces' members and type parameters: one symbol in tsc),
and the symbol lines we don't print (`this` -> its class, `arguments`, `global`, `__index`). Sample the buckets first;
some of it may be binder work (merged type parameters of interfaces are separate symbols in tttt, hacks.md "Missing
checks").

### Stage 7 (decide after stage 3): union origins
tsc keeps the union a type was written with (`origin`) through narrowing and filtering, so `FooBase[]` prints where we
print `string | false` (spreadBooleanRespectsFreshness, literalFreshnessPropagationOnNarrowing, a distributed
intersection's origin `LeftHandSideExpression & BindableStaticNameExpression`, mappedTypeAsClauses `P & ("a" |
"b")`). A type-core feature (unions carry an origin), not a printer one: measure how many tests the samples of
`other` show for it before committing to it.

### Not in this plan
`we-any` (type-core and JS gaps, owned elsewhere), `overloads` and most of `other` (check each sample: many are
inference or relation bugs that the types baseline shows first; triage them as such), union order (keep fixing the
cheap cases where a type is created in the wrong order, don't design for it).

## Validation
- After each stage, `./displaydiff.py` (and `--symbols`) shows the stage's buckets shrinking by most of their "alone"
  count, and `./fasttest.sh --summary` gains that many types/symbols components with nothing lost in errors (display
  is in messages too: check TS2322 & co. messages didn't change for the worse, `./resultdiff.py --lost`).
- Stage 2 and 3 must not change the CLI's cost: `./bench.sh` typecheck times within noise.
- Plan done when the architectural buckets (stages 2-5) are below ~20 tests alone each; what's left of `other`
  goes to triage.md with samples.

## Folded in from triage.md (the probes behind the stages)
Display fidelity (types baseline):
tsc *reuses written annotations* when it
  prints (`Comparator["operator"]` where we print the resolved `"<" | "<=" | "="`, `p: E.x` as `E.x` not `E`,
  `ReturnType<typeof mk>["a"]`, `type R = ReturnType<typeof f>` resolved, optional parameters as `x?: T | undefined`
  under strict, `get(k): T | undefined` without strictNullChecks where we drop the `| undefined` (re-probed); probes in
  `test/probes/plan12/` and `returntype_index.ts`); union *origins* (the aliases a union was written with survive
  narrowing/filtering: `FooBase[]` where we print `string | false`, spreadBooleanRespectsFreshness,
  literalFreshnessPropagationOnNarrowing; a distributed intersection's origin, `LeftHandSideExpression &
  BindableStaticNameExpression`; mappedTypeAsClauses `P & ("a" | "b")`); T_1-style renaming of clashing type
  parameter names (overload lists `{ <A>(...): ...; <A_1>(...): ... }`, `createMCache : <T_1>() => MCache<T_1>` inside
  `function per<T>`: probes plan11/memoizeOne.ts, nestedReturnContext.ts; the second overload of functions sharing a
  type parameter name); union member order (tsc's is type id order and literals are made when the node is first
  resolved; we lower annotations eagerly: `y: "p" | "q" | 1` as `1 | "p" | "q"` in stringLiteralsWithEqualityChecks03;
  worth a failstats pass over diffs that differ only by order); `type Phantom<T> = {}` instantiations all print
  `Phantom<T>` (re-probed: instantiating a node that mentions no type parameter returns the node's type, alias
  arguments and all); string-named enum members print `E.b c` (re-probed, tsc `(typeof E)["b c"]`); `class C extends
  Base<V[]>`'s expression prints `typeof Base` (tsc the instance `Base<V[]>`); interface method overloads print per
  declaration (tsc the merged `{ (...): number; (): number; }` with both declarations, probe
  `plan08/contextual_overloads.ts`); symbols of mapped-type properties (tsc `Symbol(flags, Decl(...))`, we
  `Symbol(A.flags)`) and of union properties (tsc lists every member's declaration, we `Symbol(flags)`: probe
  `plan08/mutable_union.ts`); `[]` in `outputs || []` under strict is `undefined[]` where tsc says `never[]`; TS2694
  names namespaces inside an import type by bare name where tsc qualifies; a structurally printed type has no size
  limit (`typeof globalThis` was 23 MB of error text and deadlocked the harness; tsc truncates at 160 characters by
  default, `noErrorTruncation`). Most of these are one mechanism each; insights should decide whether annotation reuse
  and origins get a plan.

Class expressions and member symbols: a class expression prints as `''` in messages (tsc `(Anonymous class)`, or `C`
for `const C = class {}`), its instance type prints *empty* in the types baseline (`c : `) and its constructor as `{
new (): ; prototype: ; }` (tsc `typeof C`); the mixin tests show the same (`class ''` in TS2341/TS2445). Member symbols
aren't qualified in the symbols baseline for parameter properties (`Symbol(q)` for `Symbol(P.q)`), class expression
members and enum members referenced in initializers (`Symbol(A)` for `Symbol(SyntaxKind.A)`).
