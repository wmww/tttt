# Plan 14: syntax gaps (`using`, import attributes, import/export forms)

## Why
`./failstats.py --syntax` (2026-09-22): 1256 tests have a syntax error tsc doesn't report, and then typecheck on a
recovered AST, so they usually fail all three components and cascade into the spurious TS2304 (4811 in 1098 tests),
TS1005 (3214), TS1128 (2332) buckets. The top shapes are a handful of *missing features*, not recovery fidelity:
- **`using` / `await using` declarations: ~330 tests** (`using x = ...` 216 + object literal initializers 33 + `await
  using` 18 + in `for`/`for of`/`for await` ~30 + misc). The parser has no `using` at all (not even a keyword).
- **Import attributes / assertions: ~65** (`import './0' with { type: "json" }`, `assert { ... }`, in import types
  `import("pkg", { with: { "resolution-mode": "import" } })`, `export ... from ... with`).
- **Import/export forms: ~35**: `export { x as "" }` / arbitrary module namespace identifiers (15), `export * as
  default from` (7), `import d, * as ns from` (11), `export import d, { } from` (5), `import { default as b }`.
- Smaller: `continue L;`/`break L;` label targets (13, some are *validation* errors tsc gives: TS1107/TS1115/TS1116),
  shebang line (7), `new.target` (6), `satisfies` after `export default`/`as const` (9), `@dec ["computed"]` class
  fields and `static declare` (16), `async #m()`, `accessor` fields (autoAccessor*), `*() {}` nameless generator
  members, `yield`/`await` as identifiers in parameter defaults (36: tsc parses them, then reports semantic errors).
The corpus has none of these (it parses), so this is suite work; it's cheap per test and unblocks typechecking of
whole test files, so the payoff is large. JSX (~100 tests with `<div>` shapes) is **not** in scope: it's a feature
plan of its own (parse + JSX namespace types + intrinsic elements).

## Progress
- **`using` / `await using`: done** (4221 -> 4535 full; tests with a spurious syntax error 1242 -> 916). Parser
  (`using_declaration_follows`, tsc's lookahead; `for (await using of x)` is an empty list), `VAR_INIT_USING`/
  `VAR_INIT_AWAIT_USING` bind as const, checks TS2850/2851 (`ty_check_using_initializer`), TS1492, TS1493/1494,
  TS1491/1495 (modifiers, in the parser), TS2852/2853/2854/1309, TS18054, and TS1156 for every block-scoped
  declaration in an `if`/loop body. Left in the `using` tests: TS2339/TS2488 on destructuring `null` (general, not
  `using`), TS2448/TS2354/TS7018, TS1432 (top-level `for await`, not implemented for any loop).
- **Import attributes/assertions: done** (+4 full, +45 tests with gained components). `ImportAttributes`
  (`import_attributes.h`) on import/export statements and import types; tsc's checkImportAttributes
  (`port_statement_typecheck`: TS2821/2823/2880/2836/2856/2822/2857/1454) and getResolutionModeOverride
  (TS1453/1455/1456/1463/1464), and `resolution-mode` of type-only imports and import types now picks the resolution
  conditions (`ModuleRef.resolution_override`). Not done: the attributes' type against the global `ImportAttributes`,
  error recovery of malformed import types (nodeModulesImport*DeclarationEmitErrors), TS2343 (tslib helpers).
- New facility: `error_ctx_grammar_error_d` (tsc's grammarErrorOnNode: nothing in a file with parse errors,
  `SourceFile.has_parse_errors`). Only the checks added in this plan use it so far (hacks.md).
- **Import/export forms and small shapes: done** (4539 -> 4575): `import d, * as ns` (the namespace import is an
  item), string specifier names (`port_item_name()` strips the quotes for table keys), `export * as default`,
  `export import x from` (TS1191), shebang lines, tsc's pairwise modifier order rules (`static async` was TS1029),
  break/continue targets (TS1107 across function/static block boundaries, TS1115/1116 by a label stack in
  `BlockTypecheckCtx`).
- Next (`./failstats.py --syntax`): `@dec ["computed"]` fields and `static declare` (16), `new.target` (6),
  `export default x satisfies T` (5), `async #m()`, `*() {}` members, `yield`/`await` in parameter defaults (36).

## How
Parser work driven by the list, largest shape first, like plan 08's loop:
1. `./failstats.py` then `./failstats.py --syntax -n 60`; pick the top shape, read 2-3 of its tests' tsc baselines.
2. Parse it the way tsc's parser does (`thirdparty/tsc/src/compiler/parser.ts`: `parseVariableDeclarationList` with
   `NodeFlags.Using/AwaitUsing`, `isUsingDeclaration` lookahead: `using` is an identifier unless followed by an
   identifier on the same line; `parseImportAttributes`). Add the AST node/flag, bind it (a `using` binding is a
   block-scoped const), and give it the minimal checker semantics the baselines need:
   - `using`: the initializer must be `Disposable | null | undefined` (TS2850), `await using` `AsyncDisposable |
     Disposable | null | undefined` (TS2851); TS1492/TS2853/TS2854 placement errors (not at top level of a script, not
     in `for in`, etc.); `thirdparty/lib/esnext.disposable.d.ts` has the types. Downlevel emit
     helpers are not our business, but TS2318 "Cannot find global type 'Disposable'" when the lib lacks it is.
   - Import attributes: parse and store; TS2821/TS2823/TS1324 by `module` option, `resolution-mode` feeding module
     resolution (hacks.md "Module resolution" lists it as missing, ~20 tests).
3. `./fasttest.sh --summary`: parse fixes expose checker gaps in the newly typechecked files. That's expected; record
   honest losses (a test that used to pass its errors component by accident, e.g. both tsc and tttt erroring) in the
   commit message, not by special-casing.
4. One commit per shape with the numbers.

## Risks
- `using` as an identifier (`using(x)`, `using = 1`, `var using`) must keep parsing: tsc's lookahead is exact, copy it.
- Recovery shapes (`''`, `'};'`, `'{'` ~100 tests) are recovery fidelity, not features: leave them unless one shape
  is clearly a single parser rule.

## Validation
- `./failstats.py --syntax`: the `using`/attributes/import-export shapes gone from the top of the list; spurious TS2304/
  TS1005/TS1128 totals down; `./fasttest.sh --summary` gains with explained losses; `./realworld.py` unchanged (parse
  time in `./bench.sh` not worse).
- Named tests: usingDeclarations.1, awaitUsingDeclarations.1, importAttributes1, importAssertion1,
  arbitraryModuleNamespaceIdentifiers_exportEmpty, exportAsNamespace4, es6ImportDefaultBindingFollowedWithNamespaceBinding.
