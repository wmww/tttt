# Plan 49: destructured references, widening and object-literal-ness

Status: mostly done before the plan was written. Agent D's 8e175224 landed the core; agent C's for-of `const`
fix (with this plan) closed the last flagflip instance. What's left is a short audit (stage 3) and a few
divergences that are only theoretical so far. Read this before touching `ty_destructured_narrowed_type()`,
`binding_element_of()`, `ty_is_object_literal_without_spread()` or `widen_object_literal()`.

## tsc's mechanism (checker.ts @ 4d4f005c)

1. **The declared type of a variable is widened, its initial type isn't.** `getWidenedTypeForVariableLikeDeclaration`
   → `widenTypeForVariableLikeDeclaration` → `getWidenedType` (objects and, without strictNullChecks, null/undefined).
   But a pattern's *parent* type (`getTypeForBindingElementParent` → `getTypeForVariableLikeDeclaration`, or
   `checkRightHandSideOfForOf` for a for-of) only gets `widenTypeInferredFromInitializer` (literal widening, none
   for `const`): object literals in it stay fresh/regular object literals. Nested patterns' defaults likewise.
2. **References to destructured constants are re-read from that unwidened parent.** `getNarrowedTypeOfSymbol`:
   for a binding element without initializer or `...`, in a pattern of ≥ 2 elements, whose root is a `const`
   variable (for-of/for-in `const` included: `isConstantVariable` is about `NodeFlags.Constant`) or a never-assigned
   parameter, if the parent type is a union, the reference's type is `getBindingElementTypeFromParentType(element,
   flow-narrowed parent)`. That is the *unwidened* element, **even when the flow narrows nothing**. So
   `const { a, b } = c ? f() : { a: null, b: [] }; a` is `null | X` at the reference, not the widened declared
   `any | X`, and `{ ...a }` later reports TS7018 on what is still widening.
3. **Widening drops object-literal-ness.** `getWidenedTypeOfObjectLiteral` makes a new anonymous type keeping only
   `JSLiteral | NonInferrableType`: no `ObjectFlags.ObjectLiteral`. `getRegularTypeOfObjectLiteral` keeps it. So in
   `createUnionOrIntersectionProperty` a union member lacking the property reads as `undefined` (WritePartial) only
   if it's an unwidened literal; a widened one makes the property ReadPartial (TS2339 `x.p`).
   `e2 = c ? get().end : { mode: undefined }; e2.element` → TS2339; the same through a 2-element destructuring
   (`const { start, end } = ...; end.element`) → no error, by 2.
4. **ContainsWideningType propagates only from own properties** (`checkObjectLiteral` collects
   `ObjectFlags.PropagatingFlags` of its own members; spread members don't add theirs), so a spread literal isn't
   reported (TS7018) through the literal it's spread into, though it still widens (ContainsObjectOrArrayLiteral).
   `reportWideningErrorsInType` only reports a property whose valueDeclaration's parent is that literal, and its
   `errorReported = …` assignment lets the last widening property decide whether TS7005 follows too.
5. **Element names for narrowing** (`tryGetNameFromEntityNameExpression`): a const key names a property only through
   an annotation or an initializer; for-of/for-in variables have neither. `isMatchingReference` of `o[k]` with
   identifier keys asks `isConstantVariable(getResolvedSymbol(k))`: an import's alias and an exported variable's
   local (ExportValue) symbol don't qualify.

## What tttt does (state after 8e175224 and this plan's commit)

| tsc | tttt | status |
|---|---|---|
| 1 parent unwidened | `binding_assigned_type()` / `binding_element_of()` (lower.c); nested pattern defaults only literal-widened | done (8e175224) |
| 2 re-read from parent | `ty_destructured_narrowed_type()` (flow.c) returns `ty_binding_element_type_from()` = `binding_element_of()` without widening, also when nothing narrowed | done (8e175224) |
| 2 for-of `const` | `for_each_loop_add_names()` didn't set `is_const`: no destructured re-read, no aliased conditions, no `o[k]` narrowing for for-of consts | fixed here |
| 3 widened ≠ literal | `ty_is_object_literal_without_spread()` requires `ty_is_unwidened_object_literal()` (fresh or regular) | done (8e175224) |
| 4 spread flags | `TYX_CONTAINS_WIDENING` (report) vs `TYX_WIDENS` (widen), `ObjectLiteralProp.own_merged`, `declared_in_literal()` | done (agent C, ddafcce1-era) |
| 5 names | `access_name()` refuses for-in/of keys (this commit), `access_key_symbol()` refuses aliases/exports (c4891b64) | done |

### Why agent C's first attempt broke nuia (+51 TS18048 webpack, ~25 effect/excalidraw)

It made the narrowed path return the element unwidened but kept the old `ty_binding_element_type_from()`, which read
the element with `ty_access_reading()`. Under noUncheckedIndexedAccess that adds `| undefined` to every array
element (`const [[a, b, c]] = m` with `m: number[][]`), where tsc's `getBindingElementTypeFromParentType` (and tttt's
declared-type path) uses the iterated element/indexed access of the destructuring rules and gets `number`. The
declared path was right and the reference path, newly reached for every un-narrowed const destructuring, wasn't.
8e175224 fixed this properly by making both paths share `binding_element_of()`. **Rule for future changes: the
declared type and the reference type of a binding element must come from the same element function, differing
only in widening.**

## Probes (in /tmp/c-probes8 when written; recreate from these sources)

- `const { start, end } = c ? get() : { end: { mode: undefined } }; end.element;` plus
  `const e2 = c ? get().end : { mode: undefined }; e2.element;` (strict false, BS a 3-member union with
  `element?: undefined` in two): tsc errors only on `e2.element`.
- `for (const { name, rp } of [{ name: 'a', rp: null }, { name: 'b', rp: [] }]) { const s = { v: 1, rp }; }`
  (strict, strictNullChecks false): TS7018 on `rp` in `s`, the reference is `undefined[] | ...`, unwidened.
- `const [{ flag } = {}] = options;` with `options: [{ flag: 'a' | 'b' }?]`: no error (nested default unwidened).
- `const [[a, b, c]] = m;` with `m: number[][]` under noUncheckedIndexedAccess (and a JS/JSDoc variant): `a` is
  `number | undefined` iff tsc says so; compare the declared and the reference type in the types baseline.
- `{ y: null, ...{ a: null } }` in a `const` (strictNullChecks false): TS7018 on y *and* TS7005.
- `for (const k in o) { for (const r in o[k].res) {} }` (vscode userDataSyncStoreService.ts:512): no TS7022.

## Stages

1. (Done) the table above. Validation: the probes, the suite (14805 at the time), corpora (vscode 1, rest
   unchanged), flagflip all cells: vscode nosnc/sloppy/nonia/nosft/nuia 6/0 → 5/0 missing/spurious; non-vscode
   cells unchanged (17/6/1).
2. Audit the other users of `Symbol.is_const` now that for-of/for-in consts have it (closure narrowing of captured
   consts, `isConstantReference`, TS2588 assignment to a const, `abstract_literals`). The suite and corpora
   didn't move, but grep `is_const` and write a probe per use with a for-of `const`. Also `using`/`await using` in
   for-of (`variable_statement_is_const()` includes them; tsc's NodeFlags.Constant covers `using` too).
3. Parameters: tsc's re-read also applies to destructured parameters never assigned (`rootDeclaration.kind ===
   Parameter`), with the parent the parameter's (possibly contextual) type. `ty_destructured_narrowed_type()`
   handles `parameter`; check a contextually typed arrow `({ a, b }) => ...` against a union contextual type and a
   default-valued parameter, under nosnc (unwidened null) and eopt.
4. Display: the reference types are now unwidened where tsc's are; run `./displaydiff.py` after a full
   `./failstats.py` and look for `we-any`/widening buckets on destructured references, and `./expecttypes.py`.
5. Known remainder (not this mechanism, triage): vscode/nonia resultCodeEditorView.ts TS2339 x5 (`m.left` of a
   generator `join` with TLeft inferred unknown; tsc reports at every use after `if (x === active)`, tttt only at
   the last), and stateToProgressAdapter.test.ts:1956 (the last vscode diagnostic).

## Risks

- Performance: the destructured re-read runs a flow query per reference to a const element of a union parent; it
  already did before when narrowing applied. Watch `./instrcount.sh` on vscode/effect (many destructured consts in
  loops now that for-of consts qualify).
- Circularity: re-reading the parent type from a reference inside the initializer's own closures (for-of bodies
  referencing the iterated expression) can re-enter; `ty_binding_type()` has a resolving stack. The vscode
  userDataSyncStoreService TS7022 found during this work was such a cycle (through `access_name()` typing a for-in
  key); expect similar ones and fix them at the place tsc doesn't ask for a type.
