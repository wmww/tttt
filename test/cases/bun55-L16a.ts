// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function mk<U>(u: U, x: { k: 1 }) { return { u, ...x }; }
const a = mk(1, { k: 1 });
const b = mk("", { k: 1 });
declare function f<T>(p: typeof b | T, q: T): T;
declare const c: { u: number; k: 1; more: 1 };
f(a, c);
f(a, { z: 1 });
// without the spread both agree
function mk2<U>(u: U) { return { u }; }
const a2 = mk2(1);
const b2 = mk2("");
declare function f2<T>(p: typeof b2 | T, q: T): T;
declare const c2: { u: number; more: 1 };
f2(a2, c2);
const sentinel: number = "";
