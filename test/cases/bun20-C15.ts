// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A { a = 1 } class C { c = 3; static s = 1 }
function o2(x: typeof C | A) { if (x.constructor === C) { const _: null = x; } }
const sentinel: number = "";
