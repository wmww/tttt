// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare var Symbol: SymbolConstructor;
const s = Symbol(); const k: number = s;
const sentinel: number = "";
