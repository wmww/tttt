// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(x: { [K in (keyof T & "a") | (keyof T & "b")]: T[K] }): T;
declare const s: { a: 1; c: 1 };
f(s).c;
const sentinel: number = "";
