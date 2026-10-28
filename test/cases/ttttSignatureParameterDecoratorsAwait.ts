// @target: esnext
// @noImplicitAny: false
// An interface's signatures are in the await context they are in: `@await` there is a missing identifier (tsc's
// parseDecoratorExpression), a static block's too. And `(` then `@` starts no function type (skipParameterStart): a
// parenthesized type, TS1110
export {};
declare const dec: any;
async function f() { interface I { m(@await y: any): void; (@await y: any): void } }
class K { static { interface I { m(@await y: any): void } } }
interface J { m(@await y: any): void }
function g() { interface I { m(a: (@dec y: any) => void): void } }
