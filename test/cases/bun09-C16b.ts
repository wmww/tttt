// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function c15<T extends [...T]>(t: T) { const n: never = t; }
const sentinel: number = "";
