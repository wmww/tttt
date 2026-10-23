// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "foo";
class C2 { [k]; constructor() { this[k] = 1; } }
const sentinel: number = "";
