// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I1 { static prototype: number }
type L1 = { static prototype: number };
const sentinel: number = "";
