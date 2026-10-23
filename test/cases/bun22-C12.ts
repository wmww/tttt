// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T extends unknown[]>(x: { [K in keyof T]: { v: T[K] } }): T;
function g<U extends { v: number }[]>(v: [...U]) { f(v)[0].foo; }
const sentinel: number = "";
