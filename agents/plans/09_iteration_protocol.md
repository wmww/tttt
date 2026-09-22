# Plan 09: unique symbols, the iteration protocol, generators

## Why
The iteration protocol is the largest single root cause left in the real corpus (plan 08 "Progress"), and a whole
category of the suite. Nothing in tttt knows that `Set<T>`, `Map<K, V>`, `MapIterator<T>` or `Iterable<T>` produce
values, because the only thing that says so is a member named `[Symbol.iterator]`, and tttt drops those members.

Measured 2026-09-22 (corpus 203 diagnostics, suite 3714):
- Corpus: TS2495 25 (for-of over `Set`/`Map`/`Iterable<T>`/`MapIterator`), TS2461 10 (array destructuring of an
  iterable, mostly `arrayFrom(map.entries(), ([k, v]) => ...)`), TS18046 21 + TS2769 3 + TS2365 1 (`unknown` element
  types from the above), TS7022 ~8 and TS7006 `key` in builder.ts (`arrayFrom(x.keys()).sort(...)`, the `unknown[]`
  makes the chain circular), and a share of the ~60 TS2345/TS2322 that mention `unknown`. Expect ~100 of the 203 to
  go. Re-run `./realworld.py` for the current list: `grep -E "TS2495|TS2461|TS18046|unknown" /tmp/realworld.txt`.
- Suite: `./failstats.py --code TS2495` 58 tests, `--code TS2461` 36; by name, `symbol*`/`uniqueSymbol*` tests pass
  fully 10 of 119, `for-of*`/`iterable*`/`iterator*` 16 of 127, `generator*`/`yield*` 11 of 134
  (conformance/es6/Symbols, es6/for-ofStatements, es6/spread, es6/destructuring, es6/yieldExpressions, generators).

Probes already written (they fail today, `FAST=1 ./filetest.sh test/probes/plan09/X.ts -d`): `iter_basic.ts`
(for-of, destructuring, spread, `arrayFrom(m.keys())` over `Set`/`Map`), `unique_symbols.ts` (`Symbol()` consts,
`unique symbol` annotations, symbol-keyed class/object members, `keyof`, element access, for-of over a class with a
`[Symbol.iterator]()` method).

## Where things stand
- `unique symbol` parses as plain `symbol` (`parse_unique_symbol_type`). `Symbol.iterator` is a property of
  `SymbolConstructor` (lib.es2015.iterable.d.ts: `readonly iterator: unique symbol`), so it's `symbol`.
- Member keys are `Atom`s (`Prop.name`); a computed key gets its atom from `ty_literal_name()` (ty/access.c), which
  only knows string/number literals. Symbol-keyed members are dropped at the three places that build members:
  `builder_add_interface_member` (TODO in ty/members.c), `builder_add_class_items` ("computed member names only if the
  item has a `name_string`"), `check_object` (TODO in ty/expr.c). Relations therefore ignore them (`{ length: unknown }`
  is a subtype of `Iterable<any>`, triage).
- Consumers of "what does iterating X give": `for_each_loop_statement_evaluate_var_type` + the TS2495 check
  (statement.c), `ty_binding_assigned_type` (array patterns, ty/lower.c) + TS2461 (`binding_typecheck`, binding.c),
  `check_array` and `EXPR_SPREAD` (ty/expr.c), spread arguments in calls (ty/call.c), array assignment patterns
  (`[a, b] = x`). All go through `ty_extends_array` + `ty_access(number)` and give `any` otherwise.
- Generators: `ty_function_return_type` returns `any` for `function*`, `yield` is `any` (ty/function.c, ty/expr.c).
- `type BuiltinIteratorReturn = intrinsic` (lib.es2015.iterable.d.ts): `intrinsic` is a global `any`
  (`GLOBAL_TYPE("intrinsic", type_any)` in program.c), which also covers `Uppercase<S>` and friends. tsc makes it
  `undefined` under `strictBuiltinIteratorReturn` (part of `strict`, already in `CompilerOptions`), else `any`.
- `for await (... of ...)` doesn't parse (triage "Remaining syntax gaps").

## Stage 1: unique symbol types and symbol-keyed members
The foundation, and worth doing well: everything after it is ordinary inference and relations once the members exist.

1. **A unique symbol `Ty`.** tsc's UniqueESSymbolType: one type per *declaration* (`const s = Symbol()`, `declare const
   s: unique symbol`, `static readonly k: unique symbol`, the interface property `readonly iterator: unique symbol`).
   Suggested shape: `TY_LITERAL` with a new flag `TYF_UNIQUE_ES_SYMBOL` (bit 23 is free), interned in the literal
   table by declaration identity. Class and interface members aren't binder `Symbol`s (plan 04 stage 1), so the
   identity is the declaration's AST node (the `InterfaceMember`, class item or `Binding`); the lib AST is shared
   between test programs and checkers, so node pointers are stable keys. It carries a display name (`sym`,
   `Symbol.iterator`, `C.key`) and an escaped name atom like tsc's `__@iterator@<id>`, which is what becomes
   `Prop.name`. Two merged declarations of `SymbolConstructor` must agree: the property is declared once, in one
   file, so that's automatic, but check the lib overlay (`binder_redirect`) doesn't clone the node.
2. **Where unique symbols come from.** `unique symbol` type nodes (the parser needs a `TypeNode` for it; TS1335 and
   friends for misplaced ones can wait); `Symbol()`/`Symbol("x")` as the initializer of a `const` or of a
   `static readonly` class property (tsc: `isValidESSymbolDeclaration` + a call to the global `Symbol`,
   `getESSymbolLikeTypeForNode`); widening to `symbol` in mutable locations (`let x = Symbol()` is `symbol`,
   `getWidenedUniqueESSymbolType`). A unique symbol is assignable to `symbol` and to nothing else unique.
3. **Keys.** `ty_literal_name()` returns the unique symbol's atom, which makes interface members, element access
   (`o[sym]`, `s[Symbol.iterator]`), mapped type keys and binding keys work in one place. Then class items and object
   literals: compute the key type when there's no `name_string` instead of dropping the member. `Prop` gets the key
   type (say `Prop.name_type`, null for plain names) for display and `keyof` (`keyof typeof o` includes
   `typeof sym`). Mapped types over `keyof T` must keep symbol keys (`Partial<C>` keeps `[Symbol.iterator]`).
4. **Display.** Types baseline prints a unique symbol as `unique symbol` (tsc's `AllowUniqueESSymbolType`), error
   messages as `typeof sym` / `typeof Symbol.iterator`. Members print as `[sym]: string`, `[Symbol.iterator]():
   ...`. Symbols baseline: `Symbol(C[Symbol.iterator], Decl(...))`, `Symbol([sym], Decl(...))` for object literal
   members, `Symbol(SymbolConstructor.iterator, ...)` for `Symbol.iterator`. The probe shows all of these.
5. **Checks that become possible**: TS1166/TS1168/TS1169/TS1170 (computed names that aren't literal or unique symbol
   types) lose their `symbol`/`any` escape hatch in `interface_member_typecheck` (hacks.md).

Check before moving on: `unique_symbols.ts` passes or its remaining diff is explained; `./fasttest.sh --summary`
doesn't lose tests (it will gain some in es6/Symbols); the corpus count hasn't *risen* (relations now see
`[Symbol.iterator]` members, which may expose other bugs: that's expected, look at each new one).

## Stage 2: iteration types and their consumers
1. **One function**, tsc's `getIterationTypesOfIterable` (checker.ts ~45047): yield / return / next types of a type
   for a use (for-of, spread, destructuring, `yield*`, later async), cached per type id in the checker. Fast path:
   a reference to one of the global iterable/iterator interfaces (`Iterable`, `IterableIterator`, `IteratorObject`,
   `Generator`, ... by declared-type identity, `getIterationTypesOfIterableFast`); slow path: `[Symbol.iterator]`
   property -> call signatures -> return type -> `next` method -> return type -> `IteratorResult` split by
   `done` (`getIterationTypesOfIteratorResult`: the `value` of the members with `done: false` is the yield type).
   Unions iterate member-wise. Arrays and strings keep their direct path (element type / `string`), like tsc's
   `getIteratedTypeOrElementType`. Write it the way tsc observably behaves, not as a transliteration; the fast path
   is also the perf path (the corpus iterates a lot).
2. **Target gating.** Below ES2015 without `downlevelIteration`, only arrays and strings iterate: TS2495 (for-of) and
   TS2461 (destructuring) stay correct there, TS2802 for an iterable that would work with `downlevelIteration`.
   From ES2015 on the error is TS2488 ("Type must have a '[Symbol.iterator]()' method that returns an iterator").
   Look at what tsc reports in the for-of* and iterableArrayPattern* baselines rather than guessing.
3. **Consumers**, each replacing an `any` fallback: for-of variable types (statement.c), array binding patterns and
   rest elements (`ty_binding_assigned_type`: an iterable pattern element is the yield type, a rest element is
   `T[]`), array assignment patterns, array literal spread (`[...s]` is `string[]`; tuple contexts), call argument
   spread (`f(...s)`: tsc treats a non-array iterable spread as `...T[]`), `Array.from`/`arrayFrom` (should already
   work once inference sees `[Symbol.iterator]` members: a string-named stand-in, `arrayFrom<T>(it: { iter():
   Itor<T> })` with a `MySet<string>` whose `iter()` returns a derived iterator interface, already infers `string`
   through `iter().next()`. If the real one doesn't, fix it in `infer_from`, not here).
4. **`BuiltinIteratorReturn`**: resolve the `intrinsic` keyword per alias name like tsc
   (`BuiltinIteratorReturn` -> `undefined` with `strictBuiltinIteratorReturn`, else `any`). `Uppercase` & co. can
   stay `any` (a separate item) but must not break.

Check: `iter_basic.ts` passes; the corpus TS2495/TS2461/TS18046 buckets are gone or each remaining one is explained;
suite for-of*/iterable* tests move.

## Stage 3: generators (and async iteration if there's room)
1. `function*` return types: inferred `Generator<Y, R, N>` from `yield` operands, `yield*` delegations (via stage 2)
   and returns (tsc `getReturnTypeFromBody` + `createGeneratorType`), `IterableIterator`/`Iterable`/`Generator`
   annotations checked (`getIterationTypesOfGeneratorFunctionReturnType`). `yield` evaluates to the next type from
   the contextual/declared return type (`checkYieldExpression`). TS7055/TS7057 implicit-any generator diagnostics.
2. Optional, only if stage 1-2 left time: `for await` (parser first: it's in triage's syntax gaps), `AsyncIterable`
   through `[Symbol.asyncIterator]`, async generators. The corpus has none of these; stop at the suite's es2018
   tests if it gets long, and write down what's left.

## Risks
- Relations and inference suddenly see a new member on every `Array`, `Map`, `Set`, `String`... (`[Symbol.iterator]`
  and, with es2015.symbol.wellknown, `[Symbol.toStringTag]`, `[Symbol.unscopables]`...). Correct, but it changes
  which structural comparisons succeed: watch `./resultdiff.py --lost` after stage 1 and read a sample of what moved.
- Perf: every structural relation between two lib collection types now compares one more method. The fast path in
  stage 2 is what keeps for-of cheap. Run `./bench.sh` after stage 2.
- Printing: `[Symbol.iterator]` members show up in expanded object types in messages and in the types baseline,
  often in places where tsc truncates or reuses a name. Don't chase every display diff in this plan; list them.

## Validation
- Per stage: the probes above, `./fasttest.sh --summary` (no losses that aren't explained in the commit),
  `./realworld.py` (count down, no new code appearing that isn't explained).
- Plan done: `test/probes/plan09/*` pass, the corpus has no TS2495/TS2461 and no `unknown` diagnostic that comes
  from iteration, `./bench.sh` numbers in `measurements.md`. Update plan 08's "Progress" list and `hacks.md`
  (remove the entries this plan fixes: `interface_member_typecheck` TS1166, `for_each_loop_statement_evaluate_var_type`,
  generators, `ty_access` unique symbol keys, `check_object` computed keys, relations ignoring symbol-keyed members).
