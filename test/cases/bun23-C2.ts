// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type M1 = { [K in (keyof { a?: 1 })]: 1 };
const m1: M1 = {};
const sentinel: number = "";
