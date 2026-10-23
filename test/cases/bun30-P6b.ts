// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export const o = { a() {}, get a() { return 1 }, set a(v: number) {} };
o.a = 3;
const sentinel: number = "";
