// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "k";
class A1 { k = 2; [k] = 1; }
const sentinel: number = "";
