// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function g4<T extends U[K], U extends T, K extends keyof U>(t: T, u: U) { const a: U[K] = t; const b: T = u; }
const sentinel: number = "";
