// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class D { x = 1 }
class D2 extends D { static s = 1 }
declare function f<T>(a: D | T, b: T): T;
f(D, D2);
const sentinel: number = "";
