# Plan 55: tsgo's pseudochecker (syntax-based declaration types) as one module

## What tsgo does
tsgo 7.1 types a declaration "from its syntax" before falling back to the checker's type, wherever a type is printed
with an enclosing declaration (types baselines, declaration emit, hover; not messages, which have none):

- `internal/pseudochecker` (~1100 lines: `lookup.go`, `type.go`): `GetTypeOfDeclaration` (variables, parameters,
  properties, property assignments, export assignments, expandos), `GetTypeOfAccessor`, `GetReturnTypeOfSignature`
  (annotation, else the single `return` expression), `typeFromExpression` (literals, `as` / `<T>` assertions, `as
  const`, object and array literals, function expressions and arrows, template literals, unary `-`/`+`, `undefined`,
  parenthesized). The result is a `PseudoType` tree: `Direct` (an annotation node), `Inferred` (give up, with
  `ErrorNodes` for isolatedDeclarations), `MaybeConstLocation` (const/regular variants, picked later by the checker's
  `isConstContext`), `ObjectLiteral` (members incl. accessor pairs and methods), `Tuple`, `SingleCallSignature`,
  keyword and literal kinds, `Union`.
- `checker/nodebuilderimpl.go` `serializeTypeForDeclaration` (line ~2300) and `serializeReturnTypeForSignature` (~2096):
  compute the pseudo type, `pseudoTypeEquivalentToType` (`checker/pseudotypenodebuilder.go:362`: the type it stands
  for equals the checker's type, ignoring the undefined of an optional declaration, regular vs fresh literals, unions
  compared structurally), then `pseudoTypeToNode` builds the node from syntax: reused annotation nodes, literal nodes
  cloned (quotes kept), object literals as type literals with `get`/`set` members, functions with their parameter
  declarations. Not equivalent: the checker's type is printed. `ReportInferenceFallback` turns `Inferred` into the
  TS9007-TS9039 errors under isolatedDeclarations.
- Observable in the types baselines: `const a = { b: { get foo(): string {...}, set foo(v: string) {...} } }` prints
  `{ b: { get foo(): string; set foo(value: string); }; ... }` on `a`'s line but `{ foo: string }` on the expression's
  own line (no declaration there); `{ r: ['w'] } as const` prints `readonly ['w']` for the variable; a property
  `foo: () => x` prints its function from syntax, so the function type isn't "visited" and recursion goes one level
  deeper (`{ foo: () => { foo: () => any; } }`).

## Where tttt has pieces of it today (three partial ports)
1. `src/ty/ty_display.c`: `single_return_expr`/`single_return_assertion`/`single_return_quoted_literal`/
   `single_return_node`/`single_return_keyword`/`single_return_function` (return types), `TyDisplay.syntactic_function`
   (function expressions printed with their parameter declarations), the member loop's `literal_initializer` and
   `initializer_function` (module variables seen as members), `primitive_literal_keyword` (`p? = 0`), the mapped
   `a?: T` optional rule (`display_structure`'s member printing), annotation reuse (`display_annotated`, `annotation_is`).
2. `src/ty/expr.c` object literal building: `PropDecl.asserted` (`a: x as T` reuses T) and the synthesized
   single-quoted literal type node under a syntactic `as const` (`ExprCtx.syntactic_const`).
3. `src/ty/declaration_diagnostics.c`: TS 6.0's expressionToTypeNode walk (`type_of_expression`, `type_of_object`,
   `type_of_array`, `type_of_function_expression`, `return_of_signature`, `type_of_assertion`) — complete enough for
   the TS9xxx errors but it only reports, it produces no type.

Each grew separately against TS 6.0 baselines; tsgo applies the one rule everywhere.

## Stages
1. **Extract** (no behaviour change): a `src/ty/pseudo.c` producing a small `Pseudo` tree (arena) from the AST, the
   shape of tsgo's `PseudoType` (Direct / Inferred{error nodes} / MaybeConst{const, regular} / ObjectLiteral / Tuple /
   SingleCallSignature / keywords / literals / union). Port it from `pseudochecker/lookup.go` rather than from
   declaration_diagnostics.c; then make declaration_diagnostics.c report from its `Inferred` error nodes. Validate:
   suite unchanged, the drizzle/declaration-diagnostic corpora unchanged (`TTTT_DECLARATION_DIAGNOSTICS=1`).
2. **Equivalence and printing**: `pseudo_equivalent(Pseudo, Ty, optional)` (pseudoTypeEquivalentToType, incl.
   MaybeConst through the checker's const context: the ExprCtx `is_const`/`syntactic_const` of the literal) and
   `display_pseudo()` in ty_display.c (pseudoTypeToNode: Direct = display_node, literals with their quotes,
   ObjectLiteral as `{ a: ...; get b(): T; set b(v: T); m(): R; }`, Tuple, SingleCallSignature = the function's
   declarations). Call it from the places tsgo calls serializeTypeForDeclaration: a variable's/parameter's/property's
   own type line in the types baseline (TyDisplay.at = the declaration name), members of anonymous object types whose
   property declarations have syntax (needs the property assignment's value reachable from the Prop: today only
   `asserted` is kept; add the value Expr to PropDeclRare), return types.
3. **Delete the slices** from item 1/2 above that the module now covers, one at a time with the suite as the check.

## Known traps
- Messages never use it (tsgo's typeToString has no enclosing declaration): keep `d.enclosing`/`reuses_nodes` gating.
- `isContextuallyTyped` (pseudochecker/lookup.go:715): a function or literal under a call, `satisfies` or an annotated
  parent is Inferred; tttt's `FunctionImpl.syntactically_contextual` is the same idea for functions only.
- Instantiated signatures: tsgo compares through `ctx.mapper`; tttt's `TyDisplay.mapped_by` exists, but reusing
  parameter annotations with it regressed 94 tests (commit ede5beab's message): do that separately.

## Validation and expected gains
- Suite types components: accessor pairs in object literals (7 tests: circularObjectLiteralAccessors,
  duplicateIdentifierChecks, recursiveConditionalTypes, objectLiteralShorthandPropertiesErrorFromNotUsingIdentifier,
  objectLiteralErrors, fixSignatureCaching, jsDeclarationsFunctionLikeClasses2), the deeper function recursion (5:
  genericCallOnMemberReturningClosedOverObject, thisInPropertyBoundDeclarations, newTarget.es5/es6,
  noImplicitThisBigThis), `as const` tuples of variables, and the isolatedDeclarations tests failing errors
  (isolatedDeclarationErrorsExpressions, isolatedDeclarationsAddUndefined, ...NonIdentifierAssignmentInference: 5).
  Roughly 15-20 tests, more in declaration emit correctness than in counts.
- `scripts/expecttypes.py` must not regress (68619/68640 at the time of writing), `scripts/oracles.py --quick`
  corpora unchanged, drizzle's declaration diagnostics unchanged or closer to tsgo.
