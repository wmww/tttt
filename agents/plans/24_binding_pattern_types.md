# Plan 24: the type implied by a binding pattern

## Why
tsc gives a destructuring pattern a type of its own (getTypeFromBindingPattern): `{ x, y = 0 }` implies
`{ x: any; y?: number }`, `[a, b = ""]` implies `[any, string?]`. That type is used (1) as the declared type of an
unannotated destructured parameter, (2) as the contextual type of the pattern's initializer, merged into an object
literal initializer (checkObjectLiteral's `contextualTypeHasPattern`: properties the literal lacks come from the pattern
as optional ones), and (3) for defaults that replace `undefined`. Plan 21 stage 7 did part of (2): a non-empty pattern
is the contextual type of its initializer (`TYF_BINDING_PATTERN`, array patterns make tuples), destructuring
assignment targets are tuples.

Probe (2026-09-26, `// @strict: true`), each line a tttt bug:
```ts
function f({ x, y = 0 } = { x: 0 }) { return x + y; } // tsc: ({ x, y }?: { x: number; y?: number | undefined; }) => number
f({ x: 1, y: 1 });                                     // tttt: spurious TS2353 ('y' not in '{ x: number; }'), y: any
const g = ({ q = 1 }) => q;                            // tttt: spurious TS7031 on q (tsc: parameter type { q?: number })
const h = ([m, n = "s"]) => m;                         // tttt: spurious TS7031 on n
function k({ p, r } = { p: 1 }) { return p; }          // tsc: TS2339 'r' does not exist on '{ p: number; }' (tttt: none)
let [a = 1, c] = [undefined, "s"];                     // tsc: a: number (a default replaces an undefined element); tttt any
```
The TS7031 ones matter most: `strict` projects write unannotated callbacks with destructured defaults all the time,
and every one is a spurious error. Other leftovers from triage.md: object rest (`{ a, ...rest }`) is the whole object
instead of tsc's getRestType (omit the named properties); computed keys in patterns miss their symbol lines (probe
`test/probes/plan08/binding_pattern_display.ts`). Tests: destructuringWithLiteralInitializers,
literalTypes2 (f6), and whatever `./failstats.py --code TS7031` / `--code TS2353` list with patterns in them.

## Approach
- One function that lowers a pattern to its implied type (tsc's getTypeFromBindingPattern /
  getTypeFromObjectBindingPattern / getTypeFromArrayBindingPattern, `includePatternInType`, `reportErrors`), memoised
  per pattern node. Elements without defaults are `any` (implicit any is reported by the existing TS7031 logic only
  when tsc does: check `reportImplicitAny` conditions), defaults contribute their widened literal type and make the
  property optional.
- Use it as the declared type of unannotated destructured parameters (tsc's getWidenedTypeForVariableLikeDeclaration
  -> getTypeFromBindingPattern when there's no initializer or contextual type).
- In the object literal check, when the contextual type came from a pattern, add the pattern's properties the literal
  lacks as optional (tsc: `patternWithComputedProperties`, `ObjectFlags.ObjectLiteralPatternWithComputedProperties`),
  and report TS2525 ("Initializer provides no value for this binding element and the binding element has no default
  value") for pattern properties without default that the literal lacks... compare with tsc on `k` above: it says
  TS2339 there, so read checkObjectLiteral / checkVariableLikeDeclaration for which one fires when.
- Array element defaults replacing `undefined` (`[a = 1] = [undefined]`): tsc's getBindingElementTypeFromParentType
  with `getTypeWithFacts(type, TypeFacts.NEUndefined)` when there's an initializer.
- `ty_binding_assigned_type` (hacks.md) is the current per-element code path; it's not memoised per binding and
  handles arrays only. Fold that in if it gets in the way.

## Validation
- The probe above matches tsc (`FAST=1 ./filetest.sh probe.ts -d`); it is `test/probes/plan24/binding_pattern_types.ts`.
- `./failstats.py --code TS7031` and `--code TS2353`: fewer spurious; `./fasttest.sh --summary` no losses.
- Corpora unchanged or better (`./realworld.py`, `--corpus type-fest`, `--corpus valibot`).
