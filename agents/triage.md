# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-09-27 (triage session, todo entry 10 of the second window). Everything that was here was fixed
(TS1338/TS1312, the ambient jump error, TS1156 spans, TS2374 after parse errors, `F.prototype = {}` in TS, the
`undefined` suggestion, TS2318 IterableIterator, TS18041) or went to `bugs/type_core.md` (variance annotations,
non-widening undefined, `infer X extends C` scope), `bugs/display.md` (object literal numeric-string keys),
`bugs/missing_checks.md` (TS2815), plan 19 (exactOptional's missing type and `in` narrowing, padded symbols), plan 26
"Leftovers collected by triage" (JSDoc attachment, printing through export=), plan 27 leftovers (infer lookahead).

- `shows_alias()` (ty_display.c) hides type aliases declared in a block/function body when printing for a place. tsc's
  isDeclarationVisible also makes non-exported aliases inside namespaces invisible in some cases; unverified.
- `export default;` gives TS1434 + "Cannot find name 'export'" + TS1005 "'export' expected" where tsc says only TS1109
  Expression expected at the `;` (the modifier loop takes `default` now for any following char; something later
  still rejects the statement). Found at plan 35 stage 1.
- `baseline_test_traversal.c` `port_item_add_alias`: the `import type`/`export type` declared-type rule checks the
  resolved symbol for class/interface/enum/alias only; tsc's getDeclaredTypeOfSymbol also takes type parameters and
  enum members. Unverified whether any test reaches that.
