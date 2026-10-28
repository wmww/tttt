// @noEmit: true
// An object literal's (a type's, a class's) computed property name missing its expression is TS1109 and the member goes on (tsc's
// parseComputedPropertyName), not a member that fails and the list's TS1136 at each token (mutation seed 20261106,
// declarationEmitShadowingInferNotRenamed: `{ any[]) => infer C }`)
let o = { a [] ) };
let q = { []: 1 };
let r = { [] };
let s = { [ , b: 1 };
type T = { [ , b]: 1 };
class C { [ , b]: 1 }
