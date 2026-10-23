// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function c3<T extends T["a"]>(t: T) { const n: {} = t; }
const sentinel: number = "";
