// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { private a = 1; b = 2; }
class D { a = 1; private b = 2; }
declare const c: C, d: D;
export const { ...a: x } = c;
export const { ...a: b } = d;
export const sentinel: number = "";
