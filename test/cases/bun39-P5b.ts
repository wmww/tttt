// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
class C { m() { return g(); } }
const g = new C().m;
const sentinel: number = "";
