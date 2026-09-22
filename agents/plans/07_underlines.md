# Plan 07: the `^^^` underlines of the types baseline (tsc's node reuse)

## Why
`TTTT_IGNORE_UNDERLINES=1 ./release/baseline_tests` compares the types component without the underline lines. On
2026-09-21 that turned **1445 more tests fully passing (1917 -> ~3360; types component 3905 -> 6334)**. Nothing
else in the project is that big. It's a harness-fidelity item, not typechecking, but while it stands the types
component can't tell us whether the *type strings* are right for half the suite, so `failstats.py` over-reports
everything else too.

## What the underline is
`thirdparty/tsc/src/harness/typeWriter.ts` (`createSyntheticNodeUnderliningPrinter`): the type is converted to a
*type node* (`checker.typeToTypeNode`) and printed; every character that comes from a **fully synthetic** node is
`^`, every character from a node that was **reused from the source** (`factory.cloneNode` of a parse tree node,
`getParseTreeNode` finds the original) is a space. Examples (`tscbaseline.sh` on a probe):

```
>f : (x: number) => 1 | 2
>  : ^ ^^      ^^^^^^^^^^        parameter name `x` and its annotation `number` are reused; the rest is synthetic
>foo : { (foo: string, bar: number): any; (foo: string): any; }
>    : ^^^   ^^      ^^   ^^      ^^^^^^^^^   ^^      ^^^^^^^^^
>A : () => (B: any) => C           `function A(): (public B) => C`: the return annotation is reused node by node,
>  : ^^^^^^    ^^^      `public` is dropped, `: any` is synthetic (no annotation), `C`, `(`, `)`, `=>` are reused
```

What gets reused (checker.ts + `src/compiler/expressionToTypeNode.ts`, the "syntactic node builder"):
- `symbolToParameterDeclaration` -> `parameterToParameterDeclarationName`: a parameter's name is *always* a clone
  of the declaration's name (identifier or binding pattern with initializers elided). `...` and `?` are synthetic.
- `serializeTypeForDeclaration` -> `syntacticNodeBuilder.serializeTypeOfDeclaration` -> `typeFromParameter` /
  `typeFromVariable` / `typeFromProperty`: an existing type annotation is reused through
  `serializeTypeAnnotationOfDeclaration` -> `serializeExistingTypeNode` -> `tryReuseExistingTypeNode`, which walks
  the annotation (`visitExistingNodeTreeSymbolsWorker`) and bails (whole annotation becomes synthetic) when a
  name in it isn't accessible from the enclosing declaration, `this` types in the wrong place, `typeof` of
  something unnameable, etc. Nested nodes inside a reused annotation can be individually synthetic (the `: any`
  above; JSDoc types get rewritten). The checker side also requires `typeNodeIsEquivalentToType` in some paths
  (`typeToTypeNodeHelperWithPossibleReusableTypeNode`: only when `getTypeFromTypeNode(node) === type`).
- `serializeReturnTypeForSignature`: an explicit return annotation is reused the same way; inferred returns are
  synthetic. `typeFromSingleReturnExpression`: a function whose body is a single `return <expr>` can reuse... check.
- Without an annotation, `typeFromExpression` can build a type node from an *initializer expression* (literals,
  `as const`, arrays, object literals, arrow functions): the resulting nodes are synthetic but literal nodes may
  be cloned. Measure whether this matters (variable declarations show `>x : number` with `^^^^^^`, so probably not
  for the common cases; it matters for `(x = 1) => ...` parameter defaults).
- Property names in object types are printed through `symbolToNode`... check whether they are ever reused
  (the `foo` in `{ foo: string }` for an interface reference printed structurally).
- `this` parameters, type parameters (`reuseTypeParameters`: `<T extends X>` in a printed signature is reused
  from the declaration; constraint and default nodes too), tuple element names, mapped types in aliases.

## Design
1. **Measurement first.** Dump the types output of the ~1445 tests that pass under `TTTT_IGNORE_UNDERLINES=1`
   (`./release/baseline_tests --dump-dir` or the existing `-d`), diff the underline lines only, and classify by
   the printed type's shape: signature parameters (expected to be nearly all of it), return annotations, type
   parameters, other. Write the numbers into this plan before designing further.
2. **`TyDisplay` gets an underline sink.** `ty_display()` already prints from the `Ty`; add an optional
   `cstr* underline` to `TyDisplay` that every write appends to (`^` by default), and a *reuse mode* that appends
   spaces. `baseline_test_case.c: add_type_rendered` uses it instead of `cstr_resize(... '^')`.
3. **Signatures know their declaration.** `Signature` (src/ty/ty.h) has (or gets) a pointer to its `FunctionImpl`
   / parameter list. When printing a parameter of a signature that has a declaration: print the name in reuse mode
   (source text of the identifier or binding pattern, initializers elided, whitespace normalised the way tsc's
   printer does), then `?`/`...` synthetic, then the annotation: if the declaration has one and it is reusable,
   print the annotation's *source text* (normalised: tsc's printer re-prints the reused node, so `Array< string >`
   comes out `Array<string>`, comments removed) in reuse mode; else print the `Ty` synthetically. Same for the
   return annotation and type parameters. Instantiated signatures (a mapper) print the instantiated types
   synthetically only where tsc does: check what tsc does for `compareFn?: (a: T, b: T) => number` reached
   through `number[]` (the plan-05 probe showed it prints `x?: T | undefined`-style spaces there; the reused
   node text is kept even though `T` is instantiated: verify on a probe).
4. **The reusability rule.** Start with: annotation exists and `ty_lower(annotation) == printed type` (interned
   `Ty` pointers, so identity is cheap) and every name in it is accessible from the enclosing scope (we have
   `TyDisplay.enclosing` and the accessibility walk that prints names). Then measure again and chase the biggest
   remaining shape. Do not port `visitExistingNodeTreeSymbolsWorker` wholesale; it is 300 lines of JSDoc and
   edge cases, port what the numbers ask for.
5. Note in `hacks.md` what is approximated.

## Validation
- `./fasttest.sh --summary` and `./passcount.py`: the types component should close most of the 3905 -> 6334 gap.
  Report how much of the gap is closed in the commit message and `measurements.md`.
- `TTTT_IGNORE_UNDERLINES=1` still works as a measurement of what remains; keep it.
- `./realworld.py` and `./bench.sh` unchanged (the sink is only used by the harness).
