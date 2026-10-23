// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare global { interface SymbolConstructor { readonly observable: symbol } }
({})[Symbol.observable];
const sentinel: number = "";
