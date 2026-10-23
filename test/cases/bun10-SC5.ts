// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare function isT<T>(x: T | undefined): x is T;
declare function wrap<A, B extends A>(f: (a: A) => a is B): (a: A) => a is B;
const g = wrap(isT);
declare const v: "a" | undefined;
if (g(v)) { v.length; }
declare let n: never; n = g;
const sentinel: number = "";
// @filename: b.ts
export {};
declare function mk<A, B extends A>(): (a: A) => a is B;
const f: <T>(x: T | undefined) => x is T = mk();
const sentinel: number = "";
