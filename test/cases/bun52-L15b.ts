// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum E1 { A = E1["z"] } namespace E1 { export const z = 1; }
enum E2 { A = E2["z"] } namespace E2 { export function z() {} }
const sentinel: number = "";
