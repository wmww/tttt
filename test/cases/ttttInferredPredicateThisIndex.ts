// @strict: true
// An inferred type predicate's parameter index counts a `this` parameter (tsc's checkIfExpressionRefinesAnyParameter
// indexes fn.Parameters()): `isStr(this: void, x)` is about its second argument, which a call doesn't have, so the
// call narrows nothing (bun's 11-C12)
declare const a: string | number;
function isStr(this: void, x: string | number) { return typeof x === "string"; }
const t: never = isStr;
if (isStr(a)) { a.toUpperCase(); }
function isNum(x: string | number) { return typeof x === "number"; }
if (isNum(a)) { a.toFixed(); }
