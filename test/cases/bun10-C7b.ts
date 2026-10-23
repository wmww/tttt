// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { m() { return m(); } }
declare const m: C["m"];
const sentinel: number = "";
