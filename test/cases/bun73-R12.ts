// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function t0<T>(): T;
declare function box<T>(): { v: T };
declare function prom<T>(): Promise<T>;
declare function dflt<T = { a: number }>(): T;
let n: number, u: unknown;
declare let d: number | undefined;
declare const o: { p: number };
({ a: n } = t0());
({ n } = t0());
({ a: u } = t0());
({ a: n = 1 } = t0());
({ a: { b: n } } = t0());
({ v: n } = box());
({ v: { w: n } } = box());
async function f() { ({ a: n } = await prom()); }
// no error in either: an array literal is a tuple (one identity), a default for every type parameter skips the test,
// and a left side that needs flow analysis is cached by getTypeOfExpression
[n] = t0();
({ a: n } = dflt());
({ a: o.p } = t0());
({ a: n = d! } = t0());
const sentinel: number = "";
