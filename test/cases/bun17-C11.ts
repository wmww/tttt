// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A { static #p = 1; static a: number = 1; }
declare function f<T>(x: { a: T; zzz: string; prototype: any }): T;
f(A);
const sentinel: number = "";
