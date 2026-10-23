// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const s = (Symbol)();
const t: typeof s = Symbol();
const sentinel: number = "";
