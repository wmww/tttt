// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function t<T extends unknown[]>(a: `a${[...T]}`) { const x1: number = a; }
const sentinel: number = "";
