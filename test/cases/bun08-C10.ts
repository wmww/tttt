// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k = "k";
interface B3 { k: number; readonly [k]: number }
const sentinel: number = "";
