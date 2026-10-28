// @strict: true
// A function expression's first check resolves its parameters' defaults (tsc's checkSignatureDeclaration at the end of
// contextuallyCheckFunctionExpressionOrObjectLiteralMethod): `(p = f()) => {}` checked while f's return type is being
// inferred is circular there, TS7022 on p (bun 99-V4 getReturnTypeOfSignature__08)
declare function done(cb: () => void): string;
declare const cond: boolean;
const x = f();
function f() {
  if (cond) return String(x);
  return done((p = f()) => {});
}
const probe: never = f();
export const sentinel: number = ""
