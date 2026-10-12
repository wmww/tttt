# Plan 55: tsgo's pseudochecker (syntax-based declaration types) as one module

## What tsgo does
tsgo 7.1 types a declaration "from its syntax" before falling back to the checker's type, wherever a type is printed
with an enclosing declaration (types baseline, declaration emit, hover, messages printed from their expression):

- `internal/pseudochecker` (`lookup.go`, `type.go`): `GetTypeOfDeclaration`, `GetTypeOfAccessor`,
  `GetReturnTypeOfSignature`, `typeFromExpression`. The result is a `PseudoType` tree.
- `checker/nodebuilderimpl.go` `serializeTypeForDeclaration` (~2253) and `serializeReturnTypeForSignature` (~2096):
  compute the pseudo type, `pseudoTypeEquivalentToType` (`checker/pseudotypenodebuilder.go`), then `pseudoTypeToNode`.
  Callers: `addPropertyToElementList` (every property of an anonymous object type), `symbolToParameterDeclaration`
  (every parameter of a printed signature), `signatureToSignatureDeclarationHelper` (every return type).
  `ReportInferenceFallback` (only with isolatedDeclarations, `transformers/declarations/diagnostics.go`
  `createGetIsolatedDeclarationErrors`) turns the pseudo type's error nodes and equivalence failures into TS9007-TS9039.

## Done (2026-10-12, todo entry 7)
- **`src/ty/pseudo.c`/`.h`**: the builder, a port of `lookup.go` function by function (the `Pseudo` tree in the
  checker's arena, memoized per node in the node links, `KEY_PSEUDO`). tttt has no parents, so `isContextuallyTyped` and
  `IsInConstContext` come from the resolve walk: `ObjectExprItem.pseudo_contextual`/`pseudo_const`,
  `FlowBuilder.const_context` (`as T`/`satisfies` now count as contextual, like tsgo), `FunctionImpl.syntactically_contextual`
  for functions. Const-ness at print time is syntactic too (`Pseudo.in_const`): tsgo's printer asks the checker's
  isConstContext, whose contextual type is post-inference (never a const type variable), so it is syntax in practice.
- **`ty_display.c`**: `pseudo_type` (pseudoTypeToType), `pseudo_equivalent` (pseudoTypeEquivalentToType; an
  instantiated signature's own type parameters count as fresh, `pseudo_enter_signature`), `display_pseudo`
  (pseudoTypeToNode; names through `display_member_name`, tsgo's reuseName; numeric literals print their value, strings
  as written). Called, pseudo first and the old slices as the fallback, from: `display_structure`'s property loop
  (object literal property assignments and accessors through `PropDecl.item`, kept by widened/spread copies; a module's
  variables as members), `display_signature_inner`'s parameters and return type (printed through the signature's mapper,
  tsgo's ctx.mapper). Not in messages yet (`!d.approx`), nor inside reused nodes (`!d.reused`).
- Suite 13318 -> 13327 (accessor pairs, `this`-returning literals, deeper function recursion, `as const` tuples);
  corpora, `$ExpectType` (68631) unchanged.

## Remaining
1. ~~Stage 1b~~ done (2026-10-12, todo entry 8): `declaration_diagnostics.c` serializes each emitted declaration like
   serializeTypeForDeclaration / serializeReturnTypeForSignature (pseudo type, `ty_pseudo_equivalent()` with a
   reporter, pseudoTypeToNode's reports), `report_fallback()` picks the diagnostic by node kind like
   createGetIsolatedDeclarationErrors, the printer reports where tsgo's typeToTypeNode does (`DeclTracker.report_fallback`,
   `TyDisplay.suppress_fallback`). TS 6.0's builder is deleted. Checked against tsgo with `--declaration
   --isolatedDeclarations` flipped on 400 clean source sweep projects (missing/spurious TS9xxx: old 4944/7352, now 714/1;
   487 of the missing are in projects where tttt has unrelated type errors, so the CLI drops declaration diagnostics) and
   zod/xstate/nest (exact). Leftovers: properties past a truncation (monkeytype contracts' TS7056), mapped chains
   knip's schema/configuration.ts (42: spreads of zod shapes `...x.shape` in huge printed types), react-navigation routers'
   TS9039 for `State` (8). The probe scripts are easy to rebuild: tsgo `-p P --noEmit --declaration
   --isolatedDeclarations --tsBuildInfoFile /tmp/x` against `tttt -p P --declaration --isolatedDeclarations`, TS9xxx only.
2. **Stage 2b: messages.** A message printed from its expression (`reuse_nodes`, `d.approx`) uses the pseudo type in
   tsgo too. tsgo counts approximateLength for each reused node by its source range including leading trivia
   (`tryReuseExistingNodeHelper`: Direct annotations, literals, reused names, type parameters) and nothing for the
   keywords and structure pseudoTypeToNode makes; Inferred parts count as typeToTypeNode does. tttt's `display_node`
   counts text + 1. Port that counting into `display_pseudo`, drop the `!d.approx` gates, then the message-only slices
   (`TyDisplay.literal_value`, `pseudo_object_literal()`/`pseudo_object_length()`, the "`return x as T` from nowhere"
   branch of display_signature_inner). Check with `scripts/errtextdiff.py` (truncation buckets).
3. **Stage 3: delete the slices** the pseudo path now precedes, one at a time with the suite as the check (after 2b,
   since messages still use them): in `display_signature_inner` `single_return_node`/`single_return_function`/
   `single_return_keyword`/`single_return_assertion`/`single_return_quoted_literal`, `TyDisplay.syntactic_function` and
   the parameter branches (`asserted`, `primitive_literal_keyword`, the initializer function); in `display_structure`'s
   property loop `literal_initializer`, `initializer_function`, `syntactic_getter`; in `src/ty/expr.c`
   `check_object_literal`'s `asserted` for `a: x as T` and the synthesized single-quoted literal node (PropDecl.asserted
   then only carries annotations of spread/mapped/reverse-mapped properties: give those a pseudo Direct instead).
4. **Builder gaps**: class property declarations are built (`pseudo_of_field()`) but only declaration emit uses them
   (not printed members of a class's structural type), expandos (`typeFromExpandoProperty`), JSDoc (`FullSignature`, `@type` on properties is
   only partly there), a variable's own contextual place (`pseudo_of_variable` takes `PseudoPlace{0}`: a variable inside
   a callback is contextually typed to tsgo), binding elements (NoResult).

## Known traps
- `isContextuallyTyped` stops at nothing (a variable inside a callback's body is contextual).
- Instantiated signatures: tsgo's fresh type parameters (handled by `pseudo_enter_signature`); reused nodes print
  through the mapper (`mapped_by`), equivalence doesn't apply it.
- Not pseudo: genericCallOnMemberReturningClosedOverObject's expression lines (an object literal instantiated inside its
  own generic method's signature is a new instantiation per level in tttt, tsgo's outer type parameters exclude the
  method's), the `this` type of an object literal method being the literal's widened type itself (tsgo's
  getWidenedType(checkExpressionCached(literal)) is another type, so it prints one level deeper; the pseudo return type
  hides this where the method's return is printed from syntax).
